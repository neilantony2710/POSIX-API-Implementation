#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <semaphore.h>
#include <errno.h>
#include <string.h>       
#define JB_RBX   0
#define JB_RBP   1
#define JB_R12   2
#define JB_R13   3
#define JB_R14   4
#define JB_R15   5
#define JB_RSP   6  
#define JB_PC    7  
#define MAX_THREADS 150
#define STACK_SIZE 32767
#define TIMER_INTERVAL_MS 50
#ifdef SEM_VALUE_MAX
#undef SEM_VALUE_MAX
#endif
#define SEM_VALUE_MAX 65536
#define MAX_SEMAPHORES 128
enum ThreadStatus {
    READY,      
    RUNNING,    
    EXITED,     
    BLOCKED     
};
struct SemaphoreData {
    bool initialized;           
    unsigned int value;         
    int* waiting_queue;         
    int queue_size;            
    int queue_capacity;        
};
struct TCB {
    int thread_id;  
    void* stack;
    jmp_buf context;
    ThreadStatus status;
    void* (*start_routine)(void*);
    void* arg;
    void* return_value;         
    int joined_by;             
    bool has_been_joined;      
};
static TCB tcb_array[MAX_THREADS];
static int num_threads = 0;
static int current_thread = 0;
static bool initialized = false;
static SemaphoreData* semaphore_map[MAX_SEMAPHORES];
static sem_t* semaphore_keys[MAX_SEMAPHORES];
static int num_semaphores = 0;

// Save original state to restore during cleanup
static struct sigaction original_sigaction;
static sigset_t original_sigmask;
static long int i64_ptr_mangle(long int p)
{
    long int ret;
    asm volatile(" mov %1, %%rax;\n"
        " xor %%fs:0x30, %%rax;"
        " rol $0x11, %%rax;"
        " mov %%rax, %0;"
    : "=r"(ret)
    : "r"(p)
    : "%rax", "cc"
    );
    return ret;
}
static void schedule();
static void signal_handler(int signo);
static void thread_wrapper();
static void cleanup_all_resources();
static SemaphoreData* get_semaphore_data(sem_t *sem)
{
    for (int i = 0; i < num_semaphores; i++) {
        if (semaphore_keys[i] == sem) {
            return semaphore_map[i];
        }
    }
    return NULL;
}
static bool add_semaphore_mapping(sem_t *sem, SemaphoreData* data)
{
    if (num_semaphores >= MAX_SEMAPHORES) {
        return false;  
    }
    semaphore_keys[num_semaphores] = sem;
    semaphore_map[num_semaphores] = data;
    num_semaphores++;
    return true;
}
static void remove_semaphore_mapping(sem_t *sem)
{
    for (int i = 0; i < num_semaphores; i++) {
        if (semaphore_keys[i] == sem) {
            for (int j = i; j < num_semaphores - 1; j++) {
                semaphore_keys[j] = semaphore_keys[j + 1];
                semaphore_map[j] = semaphore_map[j + 1];
            }
            num_semaphores--;
            return;
        }
    }
}
static void thread_wrapper()
{
    sigset_t set;
    sigemptyset(&set);
    sigprocmask(SIG_SETMASK, &set, NULL);
    TCB* current_tcb = &tcb_array[current_thread];
    void* result = current_tcb->start_routine(current_tcb->arg);
    pthread_exit(result);  
}
static void schedule()
{
    int original_thread = current_thread;
    int checked_count = 0;
    while (checked_count < num_threads) {
        current_thread = (current_thread + 1) % num_threads;
        checked_count++;
        if (tcb_array[current_thread].status == READY) {
            tcb_array[current_thread].status = RUNNING;
            return;
        }
    }
    bool all_exited = true;
    for (int i = 0; i < num_threads; i++) {
        if (tcb_array[i].status != EXITED) {
            all_exited = false;
            break;
        }
    }
    if (all_exited) {
        cleanup_all_resources();
        exit(0);
    }
    current_thread = original_thread;
    if (tcb_array[current_thread].status != EXITED &&
        tcb_array[current_thread].status != BLOCKED) {
        tcb_array[current_thread].status = RUNNING;
    }
}
static void signal_handler(int signo)
{
    (void)signo;  
    sigset_t oldset, newset;
    sigemptyset(&newset);
    sigaddset(&newset, SIGALRM);
    sigprocmask(SIG_BLOCK, &newset, &oldset);
    int old_thread = current_thread;
    if (setjmp(tcb_array[old_thread].context) == 0) {
        if (tcb_array[old_thread].status == RUNNING) {
            tcb_array[old_thread].status = READY;
        }
        schedule();
        sigprocmask(SIG_SETMASK, &oldset, NULL);
        longjmp(tcb_array[current_thread].context, 1);
    }
    sigprocmask(SIG_SETMASK, &oldset, NULL);
}
static void cleanup_all_resources()
{
    // Prevent double-cleanup if called both explicitly and via atexit()
    if (!initialized) {
        return;
    }

    // Block SIGALRM first to prevent any signals during cleanup
    sigset_t signal_set, old_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGALRM);
    sigprocmask(SIG_BLOCK, &signal_set, &old_set);

    // Disable the timer
    struct itimerval timer;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);

    // Free all thread stacks and clean up TCB entries
    // Only clean up threads that have been joined OR never started
    // Zombie threads (exited but not joined) should also be cleaned up at program exit
    int threads_to_clean = num_threads;
    for (int i = 0; i < threads_to_clean; i++) {
        // Free stack for all threads except current (thread 0's stack is NULL anyway)
        // At program exit, we need to free ALL allocated stacks, including zombies
        if (i != current_thread && tcb_array[i].stack != NULL) {
            free(tcb_array[i].stack);
            tcb_array[i].stack = NULL;
        }

        // Clean up TCB entry completely - reset ALL fields to clean state
        tcb_array[i].thread_id = 0;
        tcb_array[i].status = EXITED;
        tcb_array[i].has_been_joined = true;
        tcb_array[i].return_value = NULL;
        tcb_array[i].joined_by = -1;
        tcb_array[i].start_routine = NULL;
        tcb_array[i].arg = NULL;
        // Clear the context (saved registers) - zero it out
        memset(&tcb_array[i].context, 0, sizeof(jmp_buf));
    }

    // Free all semaphore data structures
    // Only clean up semaphores that are still allocated (up to num_semaphores)
    // Going through all MAX_SEMAPHORES could access uninitialized/garbage pointers
    int sems_to_clean = num_semaphores;
    for (int i = 0; i < sems_to_clean; i++) {
        if (semaphore_map[i] != NULL) {
            // Mark as uninitialized before freeing
            semaphore_map[i]->initialized = false;
            semaphore_map[i]->value = 0;
            semaphore_map[i]->queue_size = 0;

            if (semaphore_map[i]->waiting_queue != NULL) {
                free(semaphore_map[i]->waiting_queue);
                semaphore_map[i]->waiting_queue = NULL;
            }
            free(semaphore_map[i]);
            semaphore_map[i] = NULL;
        }
        semaphore_keys[i] = NULL;
    }

    // Clear remaining keys
    for (int i = sems_to_clean; i < MAX_SEMAPHORES; i++) {
        semaphore_keys[i] = NULL;
    }

    // Reset semaphore counter
    num_semaphores = 0;

    // Reset threading system state
    num_threads = 0;
    current_thread = 0;
    initialized = false;

    // Restore the original SIGALRM handler (as it was before init_threading)
    sigaction(SIGALRM, &original_sigaction, NULL);

    // Restore the original signal mask (as it was before init_threading)
    sigprocmask(SIG_SETMASK, &original_sigmask, NULL);
}
static void init_threading()
{
    if (initialized) {
        return;
    }
    initialized = true;

    // Initialize all TCB entries to a clean state
    for (int i = 0; i < MAX_THREADS; i++) {
        tcb_array[i].thread_id = i;
        tcb_array[i].stack = NULL;
        tcb_array[i].status = EXITED;
        tcb_array[i].start_routine = NULL;
        tcb_array[i].arg = NULL;
        tcb_array[i].return_value = NULL;
        tcb_array[i].joined_by = -1;
        tcb_array[i].has_been_joined = (i > 0); // All except thread 0 start as "joined"
    }

    // Set up the main thread (thread 0)
    tcb_array[0].status = RUNNING;
    tcb_array[0].has_been_joined = false;
    num_threads = 1;
    current_thread = 0;

    // Save the original signal handler and mask FIRST, before any modifications
    sigaction(SIGALRM, NULL, &original_sigaction);
    sigprocmask(SIG_SETMASK, NULL, &original_sigmask);

    // Register cleanup function to be called at program exit
    atexit(cleanup_all_resources);

    // Install our custom signal handler
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sa.sa_flags = SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);
    struct itimerval timer;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = TIMER_INTERVAL_MS * 1000;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = TIMER_INTERVAL_MS * 1000;
    setitimer(ITIMER_REAL, &timer, NULL);
}
void lock()
{
    sigset_t signal_set;
    sigemptyset(&signal_set);           
    sigaddset(&signal_set, SIGALRM);    
    sigprocmask(SIG_BLOCK, &signal_set, NULL);  
}
void unlock()
{
    sigset_t signal_set;
    sigemptyset(&signal_set);           
    sigaddset(&signal_set, SIGALRM);    
    sigprocmask(SIG_UNBLOCK, &signal_set, NULL);  
}
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void*), void *arg)
{
    (void)attr;
    if (!initialized) {
        init_threading();
    }

    lock();

    // Never reuse thread IDs - simpler and cleaner
    // Each thread gets a unique ID from 0 to MAX_THREADS-1
    if (num_threads >= MAX_THREADS) {
        unlock();
        return -1;
    }

    int new_thread_id = num_threads;
    num_threads++;

    TCB* new_tcb = &tcb_array[new_thread_id];
    new_tcb->thread_id = new_thread_id;
    new_tcb->status = READY;
    new_tcb->start_routine = start_routine;
    new_tcb->arg = arg;
    new_tcb->return_value = NULL;
    new_tcb->joined_by = -1;
    new_tcb->has_been_joined = false;

    // Allocate stack for the new thread
    new_tcb->stack = malloc(STACK_SIZE);
    if (new_tcb->stack == NULL) {
        num_threads--;
        unlock();
        return -1;
    }
    setjmp(new_tcb->context);
    void* stack_top = (void*)((char*)new_tcb->stack + STACK_SIZE);
    unsigned long stack_addr = (unsigned long)stack_top;
    stack_addr = stack_addr - (stack_addr % 16);
    stack_addr -= 8;
    long int mangled_sp = i64_ptr_mangle((long int)stack_addr);
    ((long int*)new_tcb->context)[JB_RSP] = mangled_sp;
    ((long int*)new_tcb->context)[JB_RBP] = mangled_sp;
    long int mangled_pc = i64_ptr_mangle((long int)thread_wrapper);
    ((long int*)new_tcb->context)[JB_PC] = mangled_pc;
    *thread = (pthread_t)(long)new_thread_id;
    unlock();  
    return 0;
}
void pthread_exit(void *value_ptr)
{
    lock();  
    tcb_array[current_thread].return_value = value_ptr;
    tcb_array[current_thread].status = EXITED;
    int joined_by = tcb_array[current_thread].joined_by;
    if (joined_by != -1) {
        tcb_array[joined_by].status = READY;
    }
    bool all_exited = true;
    for (int i = 0; i < num_threads; i++) {
        if (tcb_array[i].status != EXITED) {
            all_exited = false;
            break;
        }
    }
    if (all_exited) {
        cleanup_all_resources();
        exit(0);
    }
    schedule();
    unlock();  
    longjmp(tcb_array[current_thread].context, 1);
}
pthread_t pthread_self(void)
{
    return (pthread_t)(long)tcb_array[current_thread].thread_id;
}
int pthread_join(pthread_t thread, void **value_ptr)
{
    lock();  
    int target_index = -1;
    for (int i = 0; i < num_threads; i++) {
        if (tcb_array[i].thread_id == (int)(long)thread) {
            target_index = i;
            break;
        }
    }
    if (target_index == -1) {
        unlock();
        return ESRCH;  
    }
    if (tcb_array[target_index].has_been_joined) {
        unlock();
        return EINVAL;
    }
    if (target_index == current_thread) {
        unlock();
        return EDEADLK;
    }
    if (tcb_array[target_index].status == EXITED) {
        if (value_ptr != NULL) {
            *value_ptr = tcb_array[target_index].return_value;
        }
        // Clean up the zombie thread's resources completely
        if (tcb_array[target_index].stack != NULL) {
            free(tcb_array[target_index].stack);
            tcb_array[target_index].stack = NULL;
        }
        // Clear ALL TCB fields to show thread is fully cleaned up
        tcb_array[target_index].thread_id = 0;
        tcb_array[target_index].status = EXITED;
        tcb_array[target_index].has_been_joined = true;
        tcb_array[target_index].return_value = NULL;
        tcb_array[target_index].joined_by = -1;
        tcb_array[target_index].start_routine = NULL;
        tcb_array[target_index].arg = NULL;
        memset(&tcb_array[target_index].context, 0, sizeof(jmp_buf));
        unlock();
        return 0;
    }
    tcb_array[target_index].joined_by = current_thread;
    tcb_array[current_thread].status = BLOCKED;
    int old_thread = current_thread;
    if (setjmp(tcb_array[old_thread].context) == 0) {
        schedule();
        unlock();
        longjmp(tcb_array[current_thread].context, 1);
    }
    lock();
    if (value_ptr != NULL) {
        *value_ptr = tcb_array[target_index].return_value;
    }
    // Clean up the zombie thread's resources completely
    if (tcb_array[target_index].stack != NULL) {
        free(tcb_array[target_index].stack);
        tcb_array[target_index].stack = NULL;
    }
    // Clear ALL TCB fields to show thread is fully cleaned up
    tcb_array[target_index].thread_id = 0;
    tcb_array[target_index].status = EXITED;
    tcb_array[target_index].has_been_joined = true;
    tcb_array[target_index].return_value = NULL;
    tcb_array[target_index].joined_by = -1;
    tcb_array[target_index].start_routine = NULL;
    tcb_array[target_index].arg = NULL;
    memset(&tcb_array[target_index].context, 0, sizeof(jmp_buf));
    unlock();
    return 0;
}
int sem_init(sem_t *sem, int pshared, unsigned value)
{
    if (pshared != 0 || value >= SEM_VALUE_MAX) {
        return -1;  
    }
    lock();  
    SemaphoreData* data = (SemaphoreData*)malloc(sizeof(SemaphoreData));
    if (data == NULL) {
        unlock();
        return -1;  
    }
    data->initialized = true;
    data->value = value;  
    data->queue_capacity = 16;  
    data->queue_size = 0;       
    data->waiting_queue = (int*)malloc(sizeof(int) * data->queue_capacity);
    if (data->waiting_queue == NULL) {
        free(data);  
        unlock();
        return -1;  
    }
    if (!add_semaphore_mapping(sem, data)) {
        free(data->waiting_queue);
        free(data);
        unlock();
        return -1;  
    }
    unlock();
    return 0;  
}
int sem_destroy(sem_t *sem)
{
    lock();  
    SemaphoreData* data = get_semaphore_data(sem);
    if (data == NULL || !data->initialized) {
        unlock();
        return -1;  
    }
    free(data->waiting_queue);
    free(data);
    remove_semaphore_mapping(sem);
    unlock();
    return 0;  
}
int sem_wait(sem_t *sem)
{
    lock();  
    SemaphoreData* data = get_semaphore_data(sem);
    if (data == NULL || !data->initialized) {
        unlock();
        return -1;  
    }
    if (data->value > 0) {
        data->value--;  
        unlock();
        return 0;  
    }
    if (data->queue_size >= data->queue_capacity) {
        int new_capacity = data->queue_capacity * 2;
        int* new_queue = (int*)realloc(data->waiting_queue,
                                       sizeof(int) * new_capacity);
        if (new_queue == NULL) {
            unlock();
            return -1;  
        }
        data->waiting_queue = new_queue;
        data->queue_capacity = new_capacity;
    }
    data->waiting_queue[data->queue_size++] = current_thread;
    tcb_array[current_thread].status = BLOCKED;
    int old_thread = current_thread;
    if (setjmp(tcb_array[old_thread].context) == 0) {
        schedule();
        unlock();
        longjmp(tcb_array[current_thread].context, 1);
    }
    unlock();
    return 0;  
}
int sem_post(sem_t *sem)
{
    lock();  
    SemaphoreData* data = get_semaphore_data(sem);
    if (data == NULL || !data->initialized) {
        unlock();
        return -1;  
    }
    if (data->queue_size > 0) {
        int woken_thread = data->waiting_queue[0];
        for (int i = 0; i < data->queue_size - 1; i++) {
            data->waiting_queue[i] = data->waiting_queue[i + 1];
        }
        data->queue_size--;
        tcb_array[woken_thread].status = READY;
    } else {
        if (data->value < SEM_VALUE_MAX - 1) {
            data->value++;
        } else {
            unlock();
            return -1;  
        }
    }
    unlock();
    return 0;
}
