#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;


// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");

    uint64 va = KSTACK((int)(p - proc));

    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE,
           PTE_R | PTE_W);
  }
}


// initialize the proc table.
void
procinit(void)
{
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");

  for(p = proc; p < &proc[NPROC]; p++) {
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = KSTACK((int)(p - proc));

    p->isthread = 0;
    p->thread_parent = 0;
  }
}


// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}


// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];

  return c;
}


// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();

  struct cpu *c = mycpu();
  struct proc *p = c->proc;

  pop_off();

  return p;
}


int
allocpid()
{
  int pid;

  acquire(&pid_lock);

  pid = nextpid;
  nextpid = nextpid + 1;

  release(&pid_lock);

  return pid;
}


// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails,
// return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {

    acquire(&p->lock);

    if(p->state == UNUSED) {
      goto found;
    }
    else {
      release(&p->lock);
    }
  }

  return 0;

found:

  p->pid = allocpid();
  p->state = USED;

  p->isthread = 0;
  p->thread_parent = 0;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);

  if(p->pagetable == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));

  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}


// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void *)p->trapframe);

  p->trapframe = 0;

  if(p->pagetable) {
  if(p->isthread)
    uvmfree_thread(p->pagetable, p->sz);
  else
    proc_freepagetable(p->pagetable, p->sz);
}

  p->pagetable = 0;

  p->sz = 0;
  p->pid = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;

  p->isthread = 0;
  p->thread_parent = 0;

  p->state = UNUSED;
}


// Create a user page table for a given process,
// with no user memory, but with trampoline and
// trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();

  if(pagetable == 0)
    return 0;

  // map the trampoline code
  // at the highest user virtual address.
  if(mappages(pagetable,
              TRAMPOLINE,
              PGSIZE,
              (uint64)trampoline,
              PTE_R | PTE_X) < 0) {

    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page
  // just below the trampoline page.
  if(mappages(pagetable,
              TRAPFRAME,
              PGSIZE,
              (uint64)(p->trapframe),
              PTE_R | PTE_W) < 0) {

    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);

    return 0;
  }

  return pagetable;
}


// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);

  uvmfree(pagetable, sz);
}


// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();

  initproc = p;

  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}


// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;

  if(n > 0) {

    if(sz + n > TRAPFRAME)
      return -1;

    if((sz = uvmalloc(p->pagetable,
                      sz,
                      sz + n,
                      PTE_W)) == 0)
      return -1;

  }
  else if(n < 0) {

    sz = uvmdealloc(p->pagetable,
                    sz,
                    sz + n);
  }

  p->sz = sz;

  return 0;
}


// Create a new process, copying the parent.
// Sets up child kernel stack to return as if
// from fork() system call.
int
kfork(void)
{
  int i, pid;

  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0)
    return -1;

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable,
             np->pagetable,
             p->sz) < 0) {

    freeproc(np);
    release(&np->lock);

    return -1;
  }

  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in child.
  np->trapframe->a0 = 0;

  // Normal process, NOT a thread.
  np->isthread = 0;
  np->thread_parent = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);

  np->cwd = idup(p->cwd);

  safestrcpy(np->name,
             p->name,
             sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);

  np->parent = p;

  release(&wait_lock);

  acquire(&np->lock);

  np->state = RUNNABLE;

  release(&np->lock);

  return pid;
}


/*
 * ============================================================
 *                     THREAD IMPLEMENTATION
 * ============================================================
 *
 * clone()
 *
 * Creates a thread which:
 *
 * 1. Shares the parent's user address space.
 * 2. Has its own kernel stack.
 * 3. Has its own trapframe.
 * 4. Starts execution at fcn(arg).
 *
 * IMPORTANT:
 *
 * The thread gets a separate page table containing the same
 * user memory mappings. This is necessary in this RISC-V xv6
 * implementation because TRAPFRAME is mapped at a fixed
 * virtual address.
 *
 * The user memory itself is shared through the physical pages.
 *
 * ============================================================
 */

int
clone(void (*fcn)(void *), void *arg, void *stack)
{
  int i;
  int pid;

  struct proc *np;
  struct proc *p = myproc();

  if(fcn == 0)
    return -1;

  if(stack == 0)
    return -1;

  /*
   * Allocate process structure.
   *
   * allocproc() gives us:
   *
   * - PID
   * - kernel stack
   * - trapframe
   * - page table containing trampoline
   * - page table containing this thread's trapframe
   */
  if((np = allocproc()) == 0)
    return -1;


  /*
   * ----------------------------------------------------------
   * SHARE USER MEMORY
   * ----------------------------------------------------------
   *
   * We do NOT call uvmcopy().
   *
   * Instead, copy the parent's page-table entries into the
   * child's page table while keeping references to the same
   * physical pages.
   *
   * For a complete implementation, the helper below should
   * duplicate the user mappings with the same physical pages.
   */

  if(uvmshare(p->pagetable,
              np->pagetable,
              p->sz) < 0) {

    freeproc(np);
    release(&np->lock);

    return -1;
  }

  np->sz = p->sz;


  /*
   * ----------------------------------------------------------
   * COPY TRAPFRAME
   * ----------------------------------------------------------
   */

  *(np->trapframe) = *(p->trapframe);


  /*
   * ----------------------------------------------------------
   * START EXECUTION AT fcn(arg)
   * ----------------------------------------------------------
   *
   * The supplied stack points to the beginning of a
   * user-allocated page.
   *
   * Stack grows downward.
   */

  uint64 sp = (uint64)stack + PGSIZE;

  // Make sure stack is aligned.
  sp &= ~0xFULL;


  /*
   * Put argument on the stack.
   */
  sp -= sizeof(uint64);

  if(copyout(np->pagetable,
             np->sz,
             sp,
             (char *)&arg,
             sizeof(uint64)) < 0) {

    freeproc(np);
    release(&np->lock);

    return -1;
  }


  /*
   * Fake return address.
   *
   * If fcn ever returns, execution will jump to an invalid
   * address. User program should normally call exit().
   */
  uint64 fake_return = 0xffffffffffffffffULL;

  sp -= sizeof(uint64);

  if(copyout(np->pagetable,
             np->sz,
             sp,
             (char *)&fake_return,
             sizeof(uint64)) < 0) {

    freeproc(np);
    release(&np->lock);

    return -1;
  }


  /*
   * Set initial user registers.
   *
   * RISC-V calling convention:
   *
   * a0 = first argument
   *
   * So fcn(arg) receives arg through a0.
   */

  np->trapframe->epc = (uint64)fcn;
  np->trapframe->sp = sp;
  np->trapframe->a0 = (uint64)arg;


  /*
   * ----------------------------------------------------------
   * THREAD METADATA
   * ----------------------------------------------------------
   */

  np->isthread = 1;
  np->thread_parent = p;


  /*
   * Threads share the same open files and cwd.
   */
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);

  np->cwd = idup(p->cwd);

  safestrcpy(np->name,
             p->name,
             sizeof(p->name));


  pid = np->pid;


  /*
   * Set parent.
   */
  release(&np->lock);

  acquire(&wait_lock);

  np->parent = p;

  release(&wait_lock);


  /*
   * Make thread runnable.
   */
  acquire(&np->lock);

  np->state = RUNNABLE;

  release(&np->lock);


  return pid;
}


/*
 * ============================================================
 *                         JOIN
 * ============================================================
 *
 * Wait for one thread created by clone() to exit.
 *
 * join() is similar to wait(), except it looks specifically
 * for thread children.
 *
 * ============================================================
 */

int
join(void)
{
  struct proc *p = myproc();
  struct proc *pp;

  int have_threads;
  int pid;


  acquire(&wait_lock);

  for(;;) {

    have_threads = 0;


    for(pp = proc;
        pp < &proc[NPROC];
        pp++) {

      /*
       * Only consider threads belonging to this process.
       */
      if(pp->parent != p ||
         pp->isthread == 0)
        continue;


      acquire(&pp->lock);

      have_threads = 1;


      if(pp->state == ZOMBIE) {

        pid = pp->pid;


        /*
         * Remove from parent's child list.
         */
        pp->parent = 0;
        pp->thread_parent = 0;


        /*
         * A thread has its own trapframe and kernel
         * page table in this implementation.
         *
         * Therefore freeproc() can safely release those
         * resources.
         */
        freeproc(pp);

        release(&pp->lock);
        release(&wait_lock);

        return pid;
      }


      release(&pp->lock);
    }


    /*
     * No threads exist.
     */
    if(!have_threads || killed(p)) {

      release(&wait_lock);

      return -1;
    }


    /*
     * Sleep until a child/thread exits.
     */
    sleep_prepare(p);

    release(&wait_lock);

    sleep();

    acquire(&wait_lock);
  }
}


// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc;
      pp < &proc[NPROC];
      pp++) {

    if(pp->parent == p) {

      pp->parent = initproc;

      wakeup(initproc);
    }
  }
}


// Exit the current process.
// Does not return.
void
kexit(int status)
{
  struct proc *p = myproc();


  if(p == initproc)
    panic("init exiting");


  /*
   * Close all open files.
   */
  for(int fd = 0;
      fd < NOFILE;
      fd++) {

    if(p->ofile[fd]) {

      struct file *f = p->ofile[fd];

      fileclose(f);

      p->ofile[fd] = 0;
    }
  }


  begin_op();

  iput(p->cwd);

  end_op();

  p->cwd = 0;


  acquire(&wait_lock);


  /*
   * Give children to init.
   */
  reparent(p);


  /*
   * Wake parent.
   *
   * This is important for both normal processes
   * and threads.
   */
  wakeup(p->parent);


  acquire(&p->lock);


  p->xstate = status;
  p->state = ZOMBIE;


  release(&wait_lock);


  /*
   * Jump into scheduler.
   */
  sched();

  panic("zombie exit");
}


// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
kwait(uint64 addr)
{
  struct proc *pp;

  int havekids;
  int pid;

  struct proc *p = myproc();


  acquire(&wait_lock);


  for(;;) {

    havekids = 0;


    for(pp = proc;
        pp < &proc[NPROC];
        pp++) {

      /*
       * IMPORTANT:
       *
       * wait() should NOT collect threads.
       *
       * Threads are collected by join().
       */
      if(pp->parent != p ||
         pp->isthread)
        continue;


      acquire(&pp->lock);


      havekids = 1;


      if(pp->state == ZOMBIE) {

        pid = pp->pid;


        if(addr != 0 &&
           copyout(p->pagetable,
                   p->sz,
                   addr,
                   (char *)&pp->xstate,
                   sizeof(pp->xstate)) < 0) {

          release(&pp->lock);
          release(&wait_lock);

          return -1;
        }


        pp->parent = 0;

        freeproc(pp);

        release(&pp->lock);
        release(&wait_lock);

        return pid;
      }


      release(&pp->lock);
    }


    if(!havekids || killed(p)) {

      release(&wait_lock);

      return -1;
    }


    sleep_prepare(p);

    release(&wait_lock);

    sleep();

    acquire(&wait_lock);
  }
}


// Per-CPU process scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;


  for(;;) {

    intr_on();
    intr_off();


    int found = 0;


    for(p = proc;
        p < &proc[NPROC];
        p++) {

      acquire(&p->lock);


      if(p->state == RUNNABLE) {

        /*
         * Switch to chosen process.
         */
        p->state = RUNNING;

        c->proc = p;

        swtch(&c->context,
              &p->context);


        /*
         * Don't re-enable interrupts on release.
         */
        mycpu()->intena = 0;


        /*
         * Process is done running for now.
         */
        c->proc = 0;

        found = 1;
      }


      release(&p->lock);
    }


    if(found == 0) {

      asm volatile("wfi");
    }
  }
}


// Switch to scheduler.
void
sched(void)
{
  int intena;

  struct proc *p = myproc();


  if(!holding(&p->lock))
    panic("sched p->lock");

  if(mycpu()->noff != 1)
    panic("sched locks");

  if(p->state == RUNNING)
    panic("sched RUNNING");

  if(intr_get())
    panic("sched interruptible");


  intena = mycpu()->intena;


  swtch(&p->context,
        &mycpu()->context);


  mycpu()->intena = intena;
}


// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();

  acquire(&p->lock);

  p->state = RUNNABLE;

  sched();

  release(&p->lock);
}


// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  extern char userret[];

  static int first = 1;

  struct proc *p = myproc();


  // Still holding p->lock from scheduler.
  release(&p->lock);


  if(__atomic_load_n(&first,
                     __ATOMIC_ACQUIRE)) {

    fsinit(ROOTDEV);


    __atomic_store_n(&first,
                     0,
                     __ATOMIC_RELEASE);


    p->trapframe->a0 =
      kexec("/init",
            (char *[]){"/init", 0});


    if(p->trapframe->a0 == -1)
      panic("exec");
  }


  prepare_return();


  uint64 satp =
    MAKE_SATP(p->pagetable);


  uint64 trampoline_userret =
    TRAMPOLINE +
    (userret - trampoline);


  ((void (*)(uint64))
   trampoline_userret)(satp);
}


// Register current process as waiting for wakeups on chan.
void
sleep_prepare(void *chan)
{
  struct proc *p = myproc();

  acquire(&p->lock);

  if(chan == 0)
    panic("sleep_prepare: zero chan");

  p->chan = chan;

  release(&p->lock);
}


// Put the thread to sleep.
void
sleep(void)
{
  struct proc *p = myproc();

  acquire(&p->lock);

  if(p->chan != 0) {

    p->state = SLEEPING;

    sched();
  }

  release(&p->lock);
}

// Atomically sleep on chan while releasing lk.
void
sem_sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(chan == 0)
    panic("sem_sleep: zero chan");

  if(lk == &p->lock)
    panic("sem_sleep: p->lock");

  acquire(&p->lock);

  release(lk);

  p->chan = chan;
  p->state = SLEEPING;

  sched();

  p->chan = 0;

  release(&p->lock);

  acquire(lk);
}
// Wake up all processes sleeping on channel chan.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc;
      p < &proc[NPROC];
      p++) {

    acquire(&p->lock);

    if(p->chan == chan) {

      p->chan = 0;

      if(p->state == SLEEPING)
        p->state = RUNNABLE;
    }

    release(&p->lock);
  }
}


// Kill the process with the given pid.
int
kkill(int pid)
{
  struct proc *p;

  for(p = proc;
      p < &proc[NPROC];
      p++) {

    acquire(&p->lock);

    if(p->pid == pid) {

      p->killed = 1;

      if(p->state == SLEEPING)
        p->state = RUNNABLE;

      release(&p->lock);

      return 0;
    }

    release(&p->lock);
  }

  return -1;
}


void
setkilled(struct proc *p)
{
  acquire(&p->lock);

  p->killed = 1;

  release(&p->lock);
}


int
killed(struct proc *p)
{
  int k;

  acquire(&p->lock);

  k = p->killed;

  release(&p->lock);

  return k;
}


// Copy to either a user address, or kernel address,
// depending on usr_dst.
int
either_copyout(int user_dst,
               uint64 dst,
               void *src,
               uint64 len)
{
  struct proc *p = myproc();

  if(user_dst) {

    return copyout(p->pagetable,
                   p->sz,
                   dst,
                   src,
                   len);
  }
  else {

    memmove((char *)dst,
            src,
            len);

    return 0;
  }
}


// Copy from either a user address, or kernel address,
// depending on usr_src.
int
either_copyin(void *dst,
              int user_src,
              uint64 src,
              uint64 len)
{
  struct proc *p = myproc();

  if(user_src) {

    return copyin(p->pagetable,
                  p->sz,
                  dst,
                  src,
                  len);
  }
  else {

    memmove(dst,
            (char *)src,
            len);

    return 0;
  }
}


// Print a process listing to console.
void
procdump(void)
{
  static char *states[] = {
    [UNUSED]   "unused",
    [USED]     "used",
    [SLEEPING] "sleep ",
    [RUNNABLE] "runble",
    [RUNNING]  "run   ",
    [ZOMBIE]   "zombie"
  };

  struct proc *p;
  char *state;


  printk("\n");


  for(p = proc;
      p < &proc[NPROC];
      p++) {

    if(p->state == UNUSED)
      continue;


    if(p->state >= 0 &&
       p->state < NELEM(states) &&
       states[p->state])

      state = states[p->state];

    else

      state = "???";


    printk("%d %s %s",
           p->pid,
           state,
           p->name);

    printk("\n");
  }
}
