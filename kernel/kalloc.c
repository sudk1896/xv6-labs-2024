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

struct freelist{
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct freelist cpu_freelists[NCPU];

void
kinit()
{ 
  freerange(end, (void*)PHYSTOP);
  cpu_freelists[0].lock = kmem.lock;
  for(int i=1;i<NCPU;i++){
   initlock(&cpu_freelists[i].lock, "kmem");
   cpu_freelists[i].freelist = 0; 
  }
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  push_off();
  int cur_cpu = cpuid();
  pop_off();
  //printf("freeing block on %d\n", cur_cpu);
  acquire(&cpu_freelists[cur_cpu].lock);
  r->next = cpu_freelists[cur_cpu].freelist;
  cpu_freelists[cur_cpu].freelist = r;
  release(&cpu_freelists[cur_cpu].lock);
}

void* steal(int cur_cpu){
   struct run* r = 0;
  // steal a free page for the current CPU
  for(int i = 0;i < NCPU;i++){
    if(i != cur_cpu){
      acquire(&cpu_freelists[i].lock);
      r = cpu_freelists[i].freelist;
      if(r){
        cpu_freelists[i].freelist = r->next;
	release(&cpu_freelists[i].lock);
        //printf("Got a page from cpu %d\n", cur_cpu);	
	return r;
      }
     release(&cpu_freelists[i].lock);
    }
  }

  return 0;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  push_off();
  int cur_cpu = cpuid();
  pop_off();
  //printf("Running kalloc on cpu %d\n", cur_cpu);
  acquire(&cpu_freelists[cur_cpu].lock);
  r = cpu_freelists[cur_cpu].freelist;
  if(r)
    cpu_freelists[cur_cpu].freelist = r->next;
  else{
    r = (struct run*)steal(cur_cpu);
  }
  release(&cpu_freelists[cur_cpu].lock);
  //pop_off();
  //pop_off();
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
