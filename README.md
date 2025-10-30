# User-Level Threading Library

A lightweight, POSIX-compliant user-level threading library implemented in C++ that provides preemptive multitasking with round-robin scheduling. This project demonstrates low-level systems programming concepts including context switching, signal handling, memory management, and x86-64 assembly integration.

## Features

- **POSIX pthread API**: Implements `pthread_create`, `pthread_exit`, and `pthread_self`
- **Preemptive Scheduling**: Round-robin time-slicing with 50ms intervals using `SIGALRM`
- **High Thread Count**: Supports up to 128 concurrent threads
- **Custom Context Switching**: Manual implementation using `setjmp`/`longjmp` with x86-64 register manipulation
- **Signal-Safe Scheduling**: Protected critical sections prevent race conditions
- **Efficient Memory Management**: Custom stack allocation (32KB per thread) with proper alignment

## Technical Highlights

### Low-Level Implementation
- **Stack Management**: Manual stack allocation and alignment for x86-64 ABI compliance
- **Register Manipulation**: Direct modification of RSP, RBP, and RIP registers
- **Pointer Mangling**: Security feature to protect saved contexts from exploitation
- **Signal Handling**: Uses `SA_NODEFER` flag with proper signal masking for atomicity

### Concurrency & Synchronization
- **Round-Robin Scheduler**: Fair time-slicing across all ready threads
- **Thread Control Blocks (TCB)**: Maintains thread state, stack pointer, and context
- **Atomic Operations**: Signal blocking during critical sections to prevent corruption
- **Edge Case Handling**: Robust handling of batch thread creation, rapid exits, and high thread counts

## Project Structure

```
.
├── threads.cpp          # Main threading library implementation
├── test_all_at_once.c   # Stress test: 128 threads created simultaneously
├── test_batches.c       # Stress test: 128 threads created in batches
├── example_test.cpp     # Sample test
├── Makefile             # Build system
└── README.md            # This file
```

## Compilation

### Requirements
- **Compiler**: GCC or G++ with C++11 support
- **Platform**: Linux x86-64 (tested on Ubuntu/Debian-based systems)
- **Libraries**: Standard C library (libc)

### Build the Library

```bash
# Compile the threading library
make thread_lib
```

This creates `threads.o` which can be linked with your application.

### Build Test Programs

```bash
# Build all test programs
make tests

# Or build individual tests
make test_all_at_once
make test_batches
```

## Usage

### Basic Example

```c
#include <pthread.h>
#include <stdio.h>

void* thread_function(void* arg) {
    int id = *(int*)arg;
    printf("Hello from thread %d!\n", id);
    return NULL;
}

int main() {
    pthread_t thread;
    int thread_id = 1;

    // Create a new thread
    pthread_create(&thread, NULL, thread_function, &thread_id);

    // Main thread exits, letting worker thread finish
    pthread_exit(NULL);
    return 0;
}
```

### Compile Your Application

```bash
# Step 1: Compile the threading library
g++ -Wall -c threads.cpp -o threads.o

# Step 2: Compile your application and link with the library
gcc -Wall -o my_program my_program.c threads.o
```

### Run Your Application

```bash
./my_program
```

## Running Tests

### Test 1: All Threads at Once (128 threads)
Tests maximum thread capacity with simultaneous creation:

```bash
make run_all_at_once
```

**Expected output**: All 128 threads create successfully, execute, and exit cleanly.

### Test 2: Batch Creation (128 threads in 8 batches)
Tests interleaved thread creation and execution:

```bash
make run_batches
```

**Expected output**: Threads are created in batches of 16, with visible scheduling across batches.

### Run All Tests

```bash
make run_tests
```

## API Reference

### `pthread_create`
```c
int pthread_create(pthread_t *thread,
                   const pthread_attr_t *attr,
                   void *(*start_routine)(void*),
                   void *arg);
```
Creates a new thread executing `start_routine(arg)`.
- **Returns**: 0 on success, -1 on error (max threads reached or allocation failure)

### `pthread_exit`
```c
void pthread_exit(void *value_ptr);
```
Terminates the calling thread. When the last thread exits, the process terminates.

### `pthread_self`
```c
pthread_t pthread_self(void);
```
Returns the thread ID of the calling thread.

## Implementation Details

### Thread States
- **READY**: Thread is ready to run but not currently executing
- **RUNNING**: Thread is currently executing
- **EXITED**: Thread has terminated

### Scheduling Algorithm
Round-robin with 50ms time quantum:
1. Timer fires every 50ms via `SIGALRM`
2. Signal handler saves current thread context
3. Scheduler selects next READY thread
4. Context switch via `longjmp` to selected thread

### Context Switching
1. **Save Context**: `setjmp` captures all registers (RBX, RBP, R12-R15, RSP, RIP)
2. **Modify Context**: Manually set RSP (stack pointer) and RIP (program counter)
3. **Restore Context**: `longjmp` restores registers and jumps to new thread

### Signal Safety
Critical sections use `sigprocmask` to block `SIGALRM`:
- Thread creation/destruction
- Scheduler operations
- TCB modifications

## Known Limitations

- **Maximum Threads**: 128 (defined by `MAX_THREADS`)
- **Stack Size**: Fixed 32KB per thread (defined by `STACK_SIZE`)
- **Memory Leak**: Thread stacks are not freed (small leak, OS reclaims on exit)
- **Platform**: x86-64 Linux only (uses platform-specific `jmp_buf` layout)
- **No Synchronization**: Mutexes and semaphores not included in this version

## Debugging Tips

### Common Issues

**Segmentation Fault on Thread Creation:**
- Check that you haven't exceeded 128 threads
- Verify stack alignment (should be 16-byte aligned)

**Threads Not Scheduling:**
- Ensure timer is initialized (`init_threading` called on first `pthread_create`)
- Check that signals are not blocked in user code

**Infinite Loops:**
- Verify all threads eventually call `pthread_exit` or return from start routine
- Check for deadlocks if multiple threads share resources

### Debugging with GDB

```bash
# Compile with debug symbols
g++ -g -Wall -c threads.cpp -o threads.o
gcc -g -Wall -o my_program my_program.c threads.o

# Run with GDB
gdb ./my_program

# Set breakpoints
(gdb) break pthread_create
(gdb) break schedule
(gdb) run
```

## Performance Characteristics

- **Context Switch Overhead**: ~50µs per switch (depending on hardware)
- **Thread Creation**: ~100µs per thread (dominated by `malloc`)
- **Memory Footprint**: 32KB per thread + TCB overhead (~128 bytes)
- **Scheduler Overhead**: O(n) where n is number of threads (linear scan)

## Educational Value

This project demonstrates:
- ✅ Low-level systems programming in C/C++
- ✅ Understanding of OS thread scheduling
- ✅ Signal handling and asynchronous events
- ✅ Memory management and stack manipulation
- ✅ x86-64 calling conventions and ABI
- ✅ Race condition prevention and atomicity
- ✅ Debugging complex concurrency bugs

## License

This project was created for educational purposes as part of CS170 (Operating Systems). Feel free to use it for learning, but please cite if used in academic work.

## Author

Neil Antony

## Acknowledgments

- Project specification from CS170 coursework
- POSIX pthread documentation
- x86-64 System V ABI documentation

