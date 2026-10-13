// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

static int page_index(void *pa);
struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

#define NPAGES (PHYSTOP / PGSIZE)

struct {
  struct spinlock lock;
  int count[NPAGES];
} page_refs;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&page_refs.lock, "page_refs");

  for(int i = 0; i < NPAGES; i++)
    page_refs.count[i] = 0;

  freerange(end, (void *)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;

  p = (char *)PGROUNDUP((uint64)pa_start);

  for(; p + PGSIZE <= (char *)pa_end; p += PGSIZE) {
    int idx = (uint64)p / PGSIZE;

    acquire(&page_refs.lock);
    page_refs.count[idx] = 1;
    release(&page_refs.lock);

    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)

void
kfree(void *pa)
{
  struct run *r;
  int idx;
  int count;

  if(((uint64)pa % PGSIZE) != 0 ||
     (char *)pa < end ||
     (uint64)pa >= PHYSTOP)
    panic("kfree");

  idx = page_index(pa);

  acquire(&page_refs.lock);
  count = page_refs.count[idx];

  if(count < 1) {
    release(&page_refs.lock);
    panic("kfree refcount");
  }

  page_refs.count[idx]--;

  if(page_refs.count[idx] > 0) {
    release(&page_refs.lock);
    return;
  }

  release(&page_refs.lock);

  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

void
page_incref(void *pa)
{
  int idx = page_index(pa);

  if(((uint64)pa % PGSIZE) != 0 || idx < 0 || idx >= NPAGES)
    panic("page_incref");

  acquire(&page_refs.lock);
  page_refs.count[idx]++;
  release(&page_refs.lock);
}

int
page_refcount(void *pa)
{
  int idx = page_index(pa);
  int count;

  if(((uint64)pa % PGSIZE) != 0 || idx < 0 || idx >= NPAGES)
    panic("page_refcount");

  acquire(&page_refs.lock);
  count = page_refs.count[idx];
  release(&page_refs.lock);

  return count;
}



// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int idx;

  acquire(&kmem.lock);
  r = kmem.freelist;

  if(r)
    kmem.freelist = r->next;

  release(&kmem.lock);

  if(r) {
    idx = page_index(r);

    acquire(&page_refs.lock);
    page_refs.count[idx] = 1;
    release(&page_refs.lock);

    memset((char *)r, 5, PGSIZE);
  }

  return (void *)r;
}

static int
page_index(void *pa)
{
  return (uint64)pa / PGSIZE;
}
