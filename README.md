# User-Space Threading Library

A POSIX-compliant threading library implementation in user space for Linux, featuring preemptive multithreading, synchronization primitives, and comprehensive resource management.

## Features

- **Preemptive Threading**: Round-robin scheduling with 50ms time slices
- **Thread Management**: Create, exit, join, and identify threads
- **Synchronization**: Locks, semaphores (binary and counting), and thread joining
- **Resource Cleanup**: Automatic cleanup on program exit with proper signal handler restoration

## Implementation Details

- **Architecture**: User-space implementation using `setjmp`/`longjmp` for context switching
- **Scheduling**: Periodic SIGALRM-based preemptive scheduler
- **Stack Management**: 32,767-byte stack allocation per thread
- **Thread Limit**: Maximum 150 concurrent threads per process
- **Semaphore Limit**: Maximum 128 semaphores per process

## API Reference

### Thread Functions

```c
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void*), void *arg);
```
Creates a new thread executing `start_routine` with argument `arg`. Returns 0 on success, -1 on failure.

```c
void pthread_exit(void *value_ptr);
```
Terminates the calling thread with return value `value_ptr`. Automatically called when thread function returns.

```c
pthread_t pthread_self(void);
```
Returns the thread ID of the calling thread.

```c
int pthread_join(pthread_t thread, void **value_ptr);
```
Suspends execution until target thread terminates. Retrieves return value if `value_ptr` is non-NULL. Returns 0 on success, or error code (ESRCH, EINVAL, EDEADLK).

### Synchronization Functions

```c
void lock(void);
void unlock(void);
```
Disable/enable thread preemption for critical sections. Must be paired correctly.

```c
int sem_init(sem_t *sem, int pshared, unsigned value);
int sem_destroy(sem_t *sem);
int sem_wait(sem_t *sem);
int sem_post(sem_t *sem);
```
POSIX-compliant semaphore operations. `pshared` must be 0. `value` must be less than 65,536.

## Usage

### Compilation

Compile the library:
```bash
g++ -c -o threads.o threads.cpp
```

Link with your application:
```bash
g++ -o myapp myapp.cpp threads.o
```

### Example: Basic Threading

```c
#include <pthread.h>
#include <stdio.h>

void* worker(void* arg) {
    int id = *(int*)arg;
    printf("Thread %d running\n", id);
    return (void*)(long)(id * 100);
}

int main() {
    pthread_t t1, t2;
    int id1 = 1, id2 = 2;

    pthread_create(&t1, NULL, worker, &id1);
    pthread_create(&t2, NULL, worker, &id2);

    void *ret1, *ret2;
    pthread_join(t1, &ret1);
    pthread_join(t2, &ret2);

    printf("Thread 1 returned: %ld\n", (long)ret1);
    printf("Thread 2 returned: %ld\n", (long)ret2);

    return 0;
}
```

### Example: Mutual Exclusion with Semaphores

```c
#include <pthread.h>
#include <semaphore.h>

sem_t mutex;
int shared_counter = 0;

void* increment(void* arg) {
    for (int i = 0; i < 1000; i++) {
        sem_wait(&mutex);
        shared_counter++;
        sem_post(&mutex);
    }
    return NULL;
}

int main() {
    sem_init(&mutex, 0, 1);  // Binary semaphore

    pthread_t threads[4];
    for (int i = 0; i < 4; i++) {
        pthread_create(&threads[i], NULL, increment, NULL);
    }

    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("Final counter: %d\n", shared_counter);
    sem_destroy(&mutex);
    return 0;
}
```

### Example: Producer-Consumer Pattern

```c
#include <pthread.h>
#include <semaphore.h>

sem_t empty, full;
int buffer = 0;

void* producer(void* arg) {
    for (int i = 0; i < 5; i++) {
        sem_wait(&empty);
        buffer = i + 100;
        printf("Produced: %d\n", buffer);
        sem_post(&full);
    }
    return NULL;
}

void* consumer(void* arg) {
    for (int i = 0; i < 5; i++) {
        sem_wait(&full);
        printf("Consumed: %d\n", buffer);
        sem_post(&empty);
    }
    return NULL;
}

int main() {
    sem_init(&empty, 0, 1);
    sem_init(&full, 0, 0);

    pthread_t prod, cons;
    pthread_create(&prod, NULL, producer, NULL);
    pthread_create(&cons, NULL, consumer, NULL);

    pthread_join(prod, NULL);
    pthread_join(cons, NULL);

    sem_destroy(&empty);
    sem_destroy(&full);
    return 0;
}
```

## Thread Lifecycle

1. **Created** → Thread allocated and initialized
2. **Ready** → Eligible for scheduling
3. **Running** → Currently executing
4. **Blocked** → Waiting on join or semaphore
5. **Exited** → Terminated, awaiting join (zombie state)
6. **Cleaned** → Joined and resources freed

## Important Notes

- Main thread (thread 0) uses the original program stack; all other threads use malloc'd stacks
- Threads must be explicitly joined to reclaim resources; unjoined threads become zombies
- All resources are automatically cleaned up on program exit via `atexit()` handler
- Signal handlers and masks are restored to their original state on cleanup
- `lock()`/`unlock()` calls must be properly nested; behavior is undefined otherwise
- Maximum semaphore value is 65,535

## Error Handling

- `pthread_create`: Returns -1 if maximum threads reached or memory allocation fails
- `pthread_join`: Returns ESRCH if thread doesn't exist, EINVAL if already joined, EDEADLK if self-join attempted
- Semaphore functions: Return -1 on error (invalid parameters, uninitialized semaphore, etc.)

## Architecture Notes

This implementation uses:
- **Context Switching**: `setjmp`/`longjmp` with pointer mangling for security
- **Preemption**: `setitimer()` with SIGALRM every 50ms
- **Scheduling**: Round-robin with fair time slicing
- **Stack Allocation**: 16-byte aligned stacks with proper initialization
- **Thread Control Blocks**: Statically allocated array for O(1) access
- **Zombie Thread Management**: Threads retain return values until joined
- **Comprehensive Cleanup**: Complete resource deallocation including signal handler restoration

## License

Based on an academic project for CS170 (F25, Prof. Kruegel) - Operating Systems
