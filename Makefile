# Change the compiler, SRC, and TEST if needed
CC=g++
SRC=threads.cpp
TEST=example_test.cpp

# Add "-g3" to CFLAGS to compile with debug level 3
CFLAGS= -Wall

EXEC=student_submission

all: $(TEST) thread_lib
	$(CC) $(CFLAGS) $(TEST) threads.o -o $(EXEC)

thread_lib: $(SRC)
	$(CC) $(CFLAGS) -c $(SRC) -o threads.o

# Custom test targets
test_all_at_once: test_all_at_once.c thread_lib
	gcc $(CFLAGS) -o test_all_at_once test_all_at_once.c threads.o

test_batches: test_batches.c thread_lib
	gcc $(CFLAGS) -o test_batches test_batches.c threads.o

run_all_at_once: test_all_at_once
	@echo "===================="
	@echo "Running: All threads at once test (128 threads)"
	@echo "===================="
	./test_all_at_once

run_batches: test_batches
	@echo "===================="
	@echo "Running: Batch creation test (128 threads in 8 batches)"
	@echo "===================="
	./test_batches

tests: test_all_at_once test_batches

run_tests: run_all_at_once run_batches

clean:
	rm -f $(EXEC) threads.o test_all_at_once test_batches
