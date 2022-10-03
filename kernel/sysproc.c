#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "file.h"
#include "pstat.h"

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

// Lottery scheduler 
// first syscall implemented
uint64
sys_settickets(void)
{
  // TBD
  int tickets;

  argint(0, &tickets);

  if (tickets < 1)
    return -1;

  myproc()->tickets = tickets;
  return 0; // worked
}


uint64
sys_getpinfo(void)
{

  uint64 pstat;
  argaddr(0, &pstat);
  //struct pstat * ps = (struct pstat *) &pstat;

  return pinfo(pstat);
}

void
sys_mmap(void)
{
  int length, prot, flags, fd, offset;
  void * addr;
  struct file * f;
  // addr y offset podemos asumir q son 0.

  argaddr(0, addr);
  argint(1, &length);
  argint(2, &prot);
  argint(3, &flags);
  
  if(argfd(4, &fd, f) < 0)
  {
    addr = 0xFFFFFFFFFFFFFFFF;
    return;
  }
    
  if(prot << 2 != 0 || prot == 0)
  {
    addr = 0xFFFFFFFFFFFFFFFF;
    return;
  }

  if(flags != 0x004  && flags != 0x005)
  {
    addr = 0xFFFFFFFFFFFFFFFF;
    return;
  }

  if(length > f->ip->size ||length < 0)
  {
    addr = 0xFFFFFFFFFFFFFFFF;
    return;
  }

  addr = myproc()->sz;
  myproc()->sz += length;

}


void
sys_munmap(void)
{

}
