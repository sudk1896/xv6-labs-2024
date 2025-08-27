#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_mmap(void){
  int len, prot, flags, fd;
  argint(1, &len);
  argint(2, &prot);
  argint(3, &flags);
  argint(4, &fd);
  struct proc* cur = myproc();
  if(is_mmap_allowed(cur->ofile[fd], prot, flags) == 0){
    printf("mmap not allowed\n");
    return 0xffffffffffffffff;
  }
  for(int i = 0; i < 16; i++){
     if(cur->vma[i].allocated == 0){
       cur->vma[i].len = len;
       cur->vma[i].prot = prot;
       cur->vma[i].flags = flags;
       cur->vma[i].f = filedup(cur->ofile[fd]);
       cur->vma[i].allocated = 1;
       cur->vma[i].start = (void*)mmap_inc(len);
       printf("start 0x%lx end 0x%lx\n", (uint64)cur->vma[i].start, (uint64)cur->vma[i].start + len);
       return (uint64)cur->vma[i].start;
     }
  }
  return 0xffffffffffffffff;
}

uint64
sys_munmap(void){
  uint64 addr;
  int len;
  argaddr(0, &addr);
  argint(1, &len);
  int vma_idx = check_vma(addr);
  if(vma_idx < 0) return -1;
  struct proc* cur = myproc();
  int ret = unmap_mmap(cur->pagetable, addr, len, &cur->vma[vma_idx]); 
  printf("munmap retVal %d\n", ret);
  if(ret==0 && cur->vma[vma_idx].len == 0){
    cur->vma[vma_idx].allocated = 0;
    cur->vma[vma_idx].offset = 0;
  }

  return ret;
}
