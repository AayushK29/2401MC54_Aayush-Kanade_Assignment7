#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define NTHREADS 3
#define ITERATIONS 100000
#define SEM_MUTEX 0

int counter = 0;
char stacks[NTHREADS][PGSIZE];
int thread_args[NTHREADS];

void
worker(void *arg)
{
  int id = *(int *)arg;

  for(int i = 0; i < ITERATIONS; i++){
    sem_wait(SEM_MUTEX);
    counter++;
    sem_signal(SEM_MUTEX);
  }

  printf("Thread %d finished\n", id);
  exit(0);
}

int
main(void)
{
  printf("Starting synchronization test...\n");

  if(sem_init(SEM_MUTEX, 1) < 0){
    printf("Semaphore initialization failed\n");
    exit(1);
  }

  for(int i = 0; i < NTHREADS; i++){
    thread_args[i] = i + 1;

    int pid = clone(worker, &thread_args[i], stacks[i]);

    if(pid < 0){
      printf("clone failed for thread %d\n", i + 1);
      exit(1);
    }
  }

  printf("Created %d threads.\n", NTHREADS);

  for(int i = 0; i < NTHREADS; i++){
    if(join() < 0){
      printf("join failed\n");
      exit(1);
    }
  }

  printf("Final counter: %d\n", counter);

  if(counter == NTHREADS * ITERATIONS)
    printf("Synchronization test PASSED\n");
  else
    printf("Synchronization test FAILED\n");

  exit(0);
}

