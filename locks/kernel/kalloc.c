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

struct kmem{
  struct spinlock lock; 
  struct run *freelist; 
} kmem;

//add a structure for each CPU
struct kmem mem_cpus[NCPU];

void
kinit()
{
  //init all the locks from all CPUs
for(int i=0; i< NCPU; i++ ) {  
  initlock(&mem_cpus[i].lock, "kmem");
  }
  freerange(end, (void*)PHYSTOP);

}

void
freerange(void *pa_start, void *pa_end)
{
  //turn interrupts off
push_off(); 

  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
  //turn interrupts on
 pop_off();
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  //turn  off interrupts
push_off();
//get the cpu which called kfree 
int id = cpuid();  

  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  //lock the curr cpu
  acquire(&mem_cpus[id].lock); 
  //add the curr mem block to freelist at the begining of list 
  r->next = mem_cpus[id].freelist;  
  mem_cpus[id].freelist = r;
  release(&mem_cpus[id].lock);
  
  //turn on interrupts
  pop_off();  
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  // interrupts off
 push_off();  
 //get the cpu id which called kalloc 
 int id = cpuid();  
 
  struct run *r;
  
  //acquire lock for cpu given by id
  acquire(&mem_cpus[id].lock); 
  //get free list for cpu id
  r = mem_cpus[id].freelist;  
  if(r) 
    mem_cpus[id].freelist = r->next;   ///move the pointer to next
  release(&mem_cpus[id].lock);    //release the lock - id

/*if the current cpu's list is not having free mem block, then "steal" part of the other CPU's free list */
if(!r)  /// if the freelist returns null
{
    ///check all other cpu's freelist
    for(int i=0; i< NCPU; i++){
     if(i == id) continue;
     //acquire the lock for ith cpu
    acquire(&mem_cpus[i].lock); 
    r = mem_cpus[i].freelist; 
   
    if(r) 
        mem_cpus[i].freelist = r->next;
     release(&mem_cpus[i].lock); 
     if(r) 
        break;
        
      //if not found freelist in curr cpu, release the lock and check next cpu
    }
}
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
    
  pop_off();   ///interrupts on
  return (void*)r;
}
