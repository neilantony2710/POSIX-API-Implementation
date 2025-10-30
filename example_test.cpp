#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <pthread.h>
#include <cassert>
#include <cstring>

#define PASS 1
#define FAILED 0

static int data_seg_var = 1;  //data segment should be shared

void* print_helloworld(void* arr){
	int* ptr = (int*) arr;
	pthread_t b = pthread_self();
	printf("hello world\n");
	data_seg_var += 1;
	ptr[0] = 1;
	ptr[1] = 1;
	pthread_exit(NULL);
}

void*  print_helloworld_after(void* arr){
	int* ptr = (int*) arr;
	while (ptr[0] == 0) { // heap data should be shared
		sleep(1);
	}
	assert(data_seg_var==2);
	printf("hello world %d time\n",data_seg_var);
	ptr[1] = 2;
	pthread_exit(NULL);
}

int test(){
	int* arr = (int*)malloc(2*sizeof(int));
	memset(arr,0,sizeof(int)*2);
	pthread_t pid1 = 0;
	pthread_t pid2 = 0;
	pthread_create(&pid1,NULL,print_helloworld_after,arr);
	pthread_create(&pid2,NULL,print_helloworld,arr);
	assert(pid1 != pid2);
    int counter = 0;
	while (arr[1] != 2) {
		sleep(1);
        counter += 1;
        if(counter >= 10){
            return FAILED;
        }
	}; // wait for threads to complete
	free(arr);
	return PASS;
}

int main() {
	int score = test();

	if (score) {
		printf("PASS\n");
	} else{
		printf("FAILED\n");
	}
	return 0;
}
