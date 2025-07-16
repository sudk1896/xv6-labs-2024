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

int bitmap[32703];
int NPAGES = 32703;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  //for(int i=0;i<NPAGES;i++) bitmap[i] = 0;
  freerange((void*)end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  int cnt = 0;
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    bitmap[cnt] = 0;
    ++cnt; 
  }
  printf("Free pages: %d\n", cnt);
}

int get_bitmap_index(void* pa){
  uint64 end_addr = (uint64)pa;
  return ((PGROUNDUP(end_addr) - PGROUNDUP((uint64)end))/PGSIZE);
}

void
superfree(void *pa){
  if(((uint64)pa % SUPERPGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("superfree");
  
  acquire(&kmem.lock);  
  int st_index = get_bitmap_index(pa);
  for(int i=0;i<512;i++){
    bitmap[st_index+i]=0;
  }
  memset(pa, 1, SUPERPGSIZE);
  //printf("Superfree called\n");
  release(&kmem.lock);
}

// returns SUPERPGSIZE aligned superpage
void* superalloc(void){
  uint64 l = (uint64)SUPERPGROUNDUP((uint64)end);
  uint64 r = (uint64)SUPERPGROUNDUP((uint64)PHYSTOP);
  
  acquire(&kmem.lock); 
  uint64 st = l;
  int pg_index = -1;
  int f = 0;
  uint64 ret_addr = 0;
  for(;st <= SUPERPGROUNDUP(r - 512*PGSIZE); st += SUPERPGSIZE){
    int idx = get_bitmap_index((void*)st);
    int found = 1;
    for(int j = 0; j < 512; j++){
      if(!bitmap[idx + j]){
        continue;
      }else{
        found = 0;
	break;
      }
    }

    if(found){
      f = 1;
      pg_index = idx;
      ret_addr = (uint64)st;
      break;
    }
  }
  
  if (f){
   for(int i=0;i<512;i++){
      bitmap[pg_index+i]=1;
    }
    memset((void*)ret_addr, 1, SUPERPGSIZE);
    //printf("superalloc called\n");
  }

  release(&kmem.lock);
  return (void*)ret_addr;
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  acquire(&kmem.lock);
  memset(pa, 1, PGSIZE); 
  int idx = get_bitmap_index(pa);
  bitmap[idx] = 0;
  //printf("Deallocated index: %d\n", idx);
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  acquire(&kmem.lock);
  void* r = 0;
  for(int i = 0;i<NPAGES;i++){
    if (!bitmap[i]){
      bitmap[i] = 1;
      r = (void*)(PGROUNDUP((uint64)end + i*PGSIZE));
      //printf("allocated index: %d\n", i);
      break;
    }
  }
  release(&kmem.lock);
  return (void*)r;
}

int count_free_pages(){
  int cnt = 0;
  acquire(&kmem.lock);
  for(int i=0;i<NPAGES;i++){
    if (!bitmap[i]) ++cnt;
  }
  release(&kmem.lock);
  return cnt;
}
