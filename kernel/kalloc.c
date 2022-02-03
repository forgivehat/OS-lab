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

struct {
  struct spinlock lock;
  uint refCount[(PHYSTOP - KERNBASE) / PGSIZE];
} ref_list;

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
acquire_reflock()
{
  acquire(&ref_list.lock);
}

void
release_reflock()
{
  release(&ref_list.lock);
}

uint64
ipage(uint64 pa)
{
  return (pa - KERNBASE) / PGSIZE;
}

void
refcnt_incr_n(uint64 pa, int n)
{
  ref_list.refCount[ipage(pa)] += n;
}

uint
r_refcnt(uint64 pa) 
{
  return ref_list.refCount[ipage(pa)];
}

void
w_refcnt(uint64 pa,int n)
{
  ref_list.refCount[ipage(pa)] = n;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
    acquire_reflock();
    if(ref_list.refCount[ipage((uint64)pa)] > 1) {
      ref_list.refCount[ipage((uint64)pa)] -= 1;
      release_reflock();
      return;
    }

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  ref_list.refCount[ipage((uint64)pa)] = 0;

  release_reflock();
  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) 
     memset((char*)r, 5, PGSIZE); // fill with junk
  if (r)
  {
    acquire_reflock();
    refcnt_incr_n((uint64)r, 1);
    release_reflock();
  }

   return (void *)r;
}

void *
kalloc_freelock(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) 
     memset((char*)r, 5, PGSIZE); // fill with junk
  if (r)
  {
  refcnt_incr_n((uint64)r, 1);
  }

   return (void *)r;
}