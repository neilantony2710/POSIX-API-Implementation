#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define TOTAL_THREADS 128
#define BATCH_SIZE 16
#define NUM_BATCHES (TOTAL_THREADS / BATCH_SIZE)

// Counter to track how many threads completed
volatile int completed_count = 0;
volatile int batch_completed[NUM_BATCHES] = {0};

// Thread function that does some work
void* thread_func(void* arg) {
    int thread_num = *(int*)arg;
    int batch_num = thread_num / BATCH_SIZE;

    // Do some work that takes time (allow preemption)
    int sum = 0;
    for (int i = 0; i < 10000; i++) {
        sum += i;
        // Periodically yield CPU to allow scheduler to run
        if (i % 1000 == 0) {
            // Just burn some cycles
            for (volatile int j = 0; j < 100; j++);
        }
    }

    // Increment counters
    completed_count++;
    batch_completed[batch_num]++;

    printf("Thread %d (batch %d) completed (sum=%d, batch_count=%d, total=%d)\n",
           thread_num, batch_num, sum,
           batch_completed[batch_num], completed_count);

    return (void*)(long)thread_num;
}

int main() {
    pthread_t threads[TOTAL_THREADS];
    int thread_ids[TOTAL_THREADS];

    printf("Creating %d threads in %d batches of %d...\n",
           TOTAL_THREADS, NUM_BATCHES, BATCH_SIZE);

    // Create threads in batches
    for (int batch = 0; batch < NUM_BATCHES; batch++) {
        printf("\n=== Creating batch %d (threads %d-%d) ===\n",
               batch, batch * BATCH_SIZE, (batch + 1) * BATCH_SIZE - 1);

        // Create one batch of threads
        for (int i = 0; i < BATCH_SIZE; i++) {
            int thread_idx = batch * BATCH_SIZE + i;
            thread_ids[thread_idx] = thread_idx;

            int result = pthread_create(&threads[thread_idx], NULL,
                                       thread_func, &thread_ids[thread_idx]);

            if (result != 0) {
                printf("ERROR: Failed to create thread %d (result=%d)\n",
                       thread_idx, result);
                exit(1);
            }
        }

        printf("Batch %d created successfully. Waiting briefly...\n", batch);

        // Small delay between batches to allow some threads to start running
        // This simulates a more realistic scenario where threads are created
        // over time rather than all at once
        usleep(10000);  // 10ms delay
    }

    printf("\n=== All %d threads created ===\n", TOTAL_THREADS);
    printf("Main thread waiting for all threads to complete...\n");

    // Print summary of batches
    printf("\nBatch completion status:\n");
    for (int i = 0; i < NUM_BATCHES; i++) {
        printf("  Batch %d: %d/%d threads completed\n",
               i, batch_completed[i], BATCH_SIZE);
    }
    printf("Total: %d/%d threads completed\n", completed_count, TOTAL_THREADS);

    // Exit main thread, let other threads continue
    pthread_exit(NULL);

    // Should never reach here
    return 0;
}
