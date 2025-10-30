#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define NUM_THREADS 128

// Counter to track how many threads completed
volatile int completed_count = 0;

// Simple thread function that increments a counter
void* thread_func(void* arg) {
    int thread_num = *(int*)arg;

    // Do some trivial work
    int sum = 0;
    for (int i = 0; i < 1000; i++) {
        sum += i;
    }

    // Increment completed counter
    completed_count++;

    printf("Thread %d completed (sum=%d, total_completed=%d)\n",
           thread_num, sum, completed_count);

    return (void*)(long)thread_num;
}

int main() {
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];

    printf("Creating %d threads all at once...\n", NUM_THREADS);

    // Create all 128 threads at once
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;

        int result = pthread_create(&threads[i], NULL, thread_func, &thread_ids[i]);

        if (result != 0) {
            printf("ERROR: Failed to create thread %d (result=%d)\n", i, result);
            exit(1);
        }
    }

    printf("All %d threads created successfully!\n", NUM_THREADS);
    printf("Main thread waiting for threads to complete...\n");

    // Wait for all threads to complete by calling pthread_exit
    // This allows other threads to run while main exits
    pthread_exit(NULL);

    // Should never reach here
    return 0;
}
