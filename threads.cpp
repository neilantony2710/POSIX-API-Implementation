#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

#define JB_RBX   0
#define JB_RBP   1
#define JB_R12   2
#define JB_R13   3
#define JB_R14   4
#define JB_R15   5
#define JB_RSP   6  // Stack pointer
#define JB_PC    7  // Program counter

#define MAX_THREADS 150
#define STACK_SIZE 32767
#define TIMER_INTERVAL_MS 50

enum ThreadStatus {
    READY,
    RUNNING,
    EXITED
};

struct TCB {
    int thread_id;  // Use int for thread ID (pthread_t type varies by platform)
    void* stack;
    jmp_buf context;
    ThreadStatus status;
    void* (*start_routine)(void*);
    void* arg;
};

static TCB tcb_array[MAX_THREADS];
static int num_threads = 0;
static int current_thread = 0;
static bool initialized = false;

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

static void thread_wrapper()
{
    // Unblock all signals - new threads start with signals blocked
    // because setjmp was called with signals blocked in pthread_create
    sigset_t set;
    sigemptyset(&set);
    sigprocmask(SIG_SETMASK, &set, nullptr);

    TCB* current_tcb = &tcb_array[current_thread];
    void* result = current_tcb->start_routine(current_tcb->arg);
    pthread_exit(result);
}

static void schedule()
{
    // Note: Signals are already blocked by caller (signal_handler or pthread_exit)
    // so we don't need additional blocking here

    int original_thread = current_thread;
    int checked_count = 0;

    while (checked_count < num_threads) {
        current_thread = (current_thread + 1) % num_threads;
        checked_count++;

        if (tcb_array[current_thread].status == READY) {
            // Found a ready thread
            tcb_array[current_thread].status = RUNNING;
            return;
        }
    }

    // We've checked all threads and none are ready
    // Check if all threads have exited
    bool all_exited = true;
    for (int i = 0; i < num_threads; i++) {
        if (tcb_array[i].status != EXITED) {
            all_exited = false;
            break;
        }
    }

    if (all_exited) {
        exit(0);
    }

    // If not all exited but we can't find a ready thread, restore to original thread
    // But only if original thread is not exited
    current_thread = original_thread;
    if (tcb_array[current_thread].status != EXITED) {
        tcb_array[current_thread].status = RUNNING;
    }
}

static void signal_handler(int signo)
{
    (void)signo;  // Unused parameter

    // Block signals before saving context to prevent corruption
    sigset_t oldset, newset;
    sigemptyset(&newset);
    sigaddset(&newset, SIGALRM);
    sigprocmask(SIG_BLOCK, &newset, &oldset);

    // Save current thread's context
    int old_thread = current_thread;

    if (setjmp(tcb_array[old_thread].context) == 0) {
        if (tcb_array[old_thread].status == RUNNING) {
            tcb_array[old_thread].status = READY;
        }
        schedule();

        // Restore signal mask before switching
        sigprocmask(SIG_SETMASK, &oldset, nullptr);
        longjmp(tcb_array[current_thread].context, 1);
    }
    // When we return here via longjmp, restore signals
    sigprocmask(SIG_SETMASK, &oldset, nullptr);
}

static void init_threading()
{
    if (initialized) {
        return;
    }

    initialized = true;

    tcb_array[0].thread_id = 0;
    tcb_array[0].stack = nullptr;  // Main thread uses its original stack
    tcb_array[0].status = RUNNING;
    tcb_array[0].start_routine = nullptr;
    tcb_array[0].arg = nullptr;

    num_threads = 1;
    current_thread = 0;

    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sa.sa_flags = SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, nullptr);

    struct itimerval timer;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = TIMER_INTERVAL_MS * 1000;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = TIMER_INTERVAL_MS * 1000;
    setitimer(ITIMER_REAL, &timer, nullptr);
}

extern "C" {

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void*), void *arg)
{
    (void)attr;

    if (!initialized) {
        init_threading();
    }

    // Check if we've reached the maximum number of threads
    if (num_threads >= MAX_THREADS) {
        return -1;  // Error: too many threads
    }

    // Allocate new thread ID before blocking signals
    int new_thread_id = num_threads;

    // Block all signals during TCB modification
    sigset_t oldset, newset;
    sigfillset(&newset);  // Fill set with all signals
    sigprocmask(SIG_BLOCK, &newset, &oldset);

    // Increment num_threads while signals are blocked
    num_threads++;

    // Initialize TCB
    TCB* new_tcb = &tcb_array[new_thread_id];
    new_tcb->thread_id = new_thread_id;
    new_tcb->status = READY;
    new_tcb->start_routine = start_routine;
    new_tcb->arg = arg;

    // Allocate stack
    new_tcb->stack = malloc(STACK_SIZE);
    if (new_tcb->stack == nullptr) {
        num_threads--;
        sigprocmask(SIG_SETMASK, &oldset, nullptr);
        return -1;  // memory allocation failed
    }

    // Save a clean context - this will be the return point for the new thread
    setjmp(new_tcb->context);

    // Align down to 16-byte boundary
    void* stack_top = (void*)((char*)new_tcb->stack + STACK_SIZE);
    unsigned long stack_addr = (unsigned long)stack_top;
    stack_addr = stack_addr - (stack_addr % 16);
    stack_addr -= 8;

    long int mangled_sp = i64_ptr_mangle((long int)stack_addr);
    ((long int*)new_tcb->context)[JB_RSP] = mangled_sp;

    // Set RBP to same as RSP for proper stack frame
    ((long int*)new_tcb->context)[JB_RBP] = mangled_sp;

    long int mangled_pc = i64_ptr_mangle((long int)thread_wrapper);
    ((long int*)new_tcb->context)[JB_PC] = mangled_pc;

    *thread = (pthread_t)(long)new_thread_id;

    // Restore signals
    sigprocmask(SIG_SETMASK, &oldset, nullptr);

    return 0;  
}

void pthread_exit(void *value_ptr)
{
    (void)value_ptr;

    sigset_t oldset, newset;
    sigfillset(&newset);  // Fill set with all signals
    sigprocmask(SIG_BLOCK, &newset, &oldset);

    // Mark thread as exited
    // Note: We DON'T free the stack here because free() is not signal-safe
    // and could be interrupted by SIGALRM, corrupting malloc's internal state
    tcb_array[current_thread].status = EXITED;

    // Check if this is the last thread
    bool all_exited = true;
    for (int i = 0; i < num_threads; i++) {
        if (tcb_array[i].status != EXITED) {
            all_exited = false;
            break;
        }
    }

    if (all_exited) {
        // All threads exited - safe to exit process
        // The OS will reclaim all memory including stacks
        exit(0);
    }

    // Find next thread to run
    schedule();

    // Restore signals before jumping to new thread
    sigprocmask(SIG_SETMASK, &oldset, nullptr);
    longjmp(tcb_array[current_thread].context, 1);

    // Should never reach here
}

pthread_t pthread_self(void)
{
    return (pthread_t)(long)tcb_array[current_thread].thread_id;
}

}  
