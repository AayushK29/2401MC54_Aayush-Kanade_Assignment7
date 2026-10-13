#include "types.h"
#include "riscv.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

#define NSEM 10

struct semaphore {
  int value;
  int allocated;
};

static struct {
  struct spinlock lock;
  struct semaphore sem[NSEM];
} semtable;

void
semaphoreinit(void)
{
  initlock(&semtable.lock, "semtable");

  for(int i = 0; i < NSEM; i++){
    semtable.sem[i].value = 0;
    semtable.sem[i].allocated = 0;
  }
}

int
sem_init(int id, int value)
{
  if(id < 0 || id >= NSEM || value < 0)
    return -1;

  acquire(&semtable.lock);

  semtable.sem[id].value = value;
  semtable.sem[id].allocated = 1;

  release(&semtable.lock);
  return 0;
}

int
sem_wait(int id)
{
  struct proc *p = myproc();

  if(id < 0 || id >= NSEM)
    return -1;

  acquire(&semtable.lock);

  if(!semtable.sem[id].allocated){
    release(&semtable.lock);
    return -1;
  }

  while(semtable.sem[id].value == 0){
  sem_sleep(&semtable.sem[id], &semtable.lock);

  if(p->killed){
    release(&semtable.lock);
    return -1;
  }
}

  semtable.sem[id].value--;

  release(&semtable.lock);
  return 0;
}

int
sem_signal(int id)
{
  if(id < 0 || id >= NSEM)
    return -1;

  acquire(&semtable.lock);

  if(!semtable.sem[id].allocated){
    release(&semtable.lock);
    return -1;
  }

  semtable.sem[id].value++;

 
  wakeup(&semtable.sem[id]);
  return 0;
}
