#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096

char stack[PGSIZE];

void
thread_func(void *arg)
{
  int id = *(int *)arg;
  printf("Thread started: %d\n", id);
  exit(0);
}

int
main(void)
{
  int arg = 42;

  int pid = clone(thread_func, &arg, stack);

  if(pid < 0){
    printf("clone failed\n");
    exit(1);
  }

  printf("Created thread: %d\n", pid);

  int joined = join();

  if(joined < 0){
    printf("join failed\n");
    exit(1);
  }

  printf("Joined thread: %d\n", joined);
  exit(0);
}
