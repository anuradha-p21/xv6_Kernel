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

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

/**********************************/
//declare an array here for pageref.  --> will have as many elements as pages 
// and it will keep a count.
//uint pgref[(PHYSTOP - KERNBASE)/ PGSIZE ] = {0};
/*********************************/
uint pgref[(PHYSTOP - KERNBASE)/ PGSIZE ] = {0};
struct spinlock pgref_lock;



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

  //get the pageref , if ref is >1, reduce count
  // return , we do not delete it yet. 
  //int ref = pageref_get(...)
  

  int ref = pageref_get((uint64)pa);
  if(ref > 1){
    pageref_set((uint64)pa, ref-1);
    return;
  }

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

// Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);
  pageref_set((uint64)pa, 0);

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
/***************************************/
// in this if we should call pageref_set for copyout 
//pageref_set((unit64)r , 1); r is the freelist pointer
 /***************************************/ 
  if(r)  {
    memset((char*)r, 5, PGSIZE); // fill with junk
    pageref_set((uint64)r , 1); // set the ref count here.
  }
  
  return (void*)r;
}


/** pagreref_set **/
void pageref_set( uint64 pa, int n)
{
  acquire(&pgref_lock);
  pgref[(pa - KERNBASE)/PGSIZE] = n;
  release(&pgref_lock);
}

/** pageref_get **/
int pageref_get( uint64 pa)
{
  int ref_count;
  acquire(&pgref_lock);
  ref_count = pgref[(pa-KERNBASE)/ PGSIZE];
  release(&pgref_lock);
  return ref_count;
}

