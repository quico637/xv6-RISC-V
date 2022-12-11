#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

void allocPhysicalVMA(struct vma *vma, struct proc *p, uint64 addr, int prot)
{
  // leo y cargo la pagina

  // coger un MP fisico
  char *phy_addr = kalloc();
  if (phy_addr == 0)
  {
    printf("usertrap(): No physical pages available. pid=%d\n", p->pid);
    setkilled(p);
  }

  // para que no vea cosas de procesos anteriores.
  memset(phy_addr, 0, PGSIZE);

  ilock(vma->ip);
  if (readi(vma->ip, 0, (uint64)phy_addr, PGROUNDDOWN(addr - vma->addr) + vma->offset, PGSIZE) < 0)
  {
    printf("readi(): failed. pid=%d\n", p->pid);
    setkilled(p);
  }
  iunlock(vma->ip);

  if (mappages(p->pagetable, PGROUNDDOWN(addr), PGSIZE, (uint64)phy_addr, prot) < 0)
  {
    kfree(phy_addr);
    printf("usertrap(): Could not map physical to virtual address, pid=%d\n", p->pid);
    setkilled(p);
  }
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void usertrap(void)
{
  int which_dev = 0;

  if ((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();

  // save user program counter.
  p->trapframe->epc = r_sepc();

  if (r_scause() == 8)
  {
    // system call

    if (killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  }
  else if (r_scause() == 12)
  {
    uint64 addr = r_stval();
    if (addr >= p->text.addr && addr < (p->text.addr + p->text.size))
    {
      int prot = PTE_R | PTE_X;
      allocPhysicalVMA(&(p->text), p, addr, prot | PTE_U);
    }
  }
  else if (r_scause() == 13 || r_scause() == 15)
  {
    // load / store page fault

    // direccion que dio el fallo.
    uint64 addr = r_stval();
    int solved = 0;
    int cow = 0;

    uint64 phy = walkaddr(p->pagetable, addr);
    if (phy && r_scause() == 15)
    {
      for (int i = 0; i < PER_PROCESS_VMAS && !solved; i++)
      {
        if (p->vmas[i] == 0)
          continue;

        if (addr >= p->vmas[i]->addr && addr < (p->vmas[i]->addr + p->vmas[i]->size))
        {
          cow = 1;
          break;
        }
      }
      if (cow)
      {
        int ref = getref((void *)phy);
        if (ref > 1)
        {
          char *new_phy = kalloc();
          if (new_phy == 0)
          {
            printf("usertrap(): No physical pages available. pid=%d\n", p->pid);
            setkilled(p);
          }
          memcpy(new_phy, (void *)phy, PGSIZE);
          uint64 *pte = walk(p->pagetable, addr, 0);
          *pte = *pte & ~(PTE_V);
          if (mappages(p->pagetable, PGROUNDDOWN(addr), PGSIZE, (uint64)new_phy, PTE_FLAGS(*pte) | PTE_W) < 0)
          {
            kfree(new_phy);
            printf("usertrap(): Could not map physical to virtual address, pid=%d\n", p->pid);
            setkilled(p);
          }
          decref((void *)phy);
        }
        if (ref == 1)
        {
          uint64 *pte = walk(p->pagetable, addr, 0);
          *pte = *pte | PTE_W;
        }
        solved = 1;
      }
    }
    else if (addr >= p->data.addr && addr < (p->data.addr + p->data.size))
    {
      int prot = PTE_R | PTE_W;
      allocPhysicalVMA(&(p->data), p, addr, prot | PTE_U);
      solved = 1;
    }
    else
    {
      for (int i = 0; i < PER_PROCESS_VMAS && !solved; i++)
      {
        if (p->vmas[i] == 0)
          continue;
        
        if (addr >= p->vmas[i]->addr && addr < (p->vmas[i]->addr + p->vmas[i]->size))
        {
          int prot;
          switch (p->vmas[i]->prot)
          {
          case (PROT_READ):
            prot = PTE_R;
            break;
          case (PROT_WRITE):
            prot = PTE_W;
            break;
          case (PROT_RW):
            prot = PTE_R | PTE_W;
            break;
          default:
            prot = 0;
          }

          allocPhysicalVMA(p->vmas[i], p, addr, prot | PTE_U);

          solved = 1;
        }
      }
    }

    // fallo
    if (!solved)
    {
      printf("usertrap(): Wrong memory address. Not your business. pid=%d\n", p->pid);
      setkilled(p);
    }
  }
  else if ((which_dev = devintr()) != 0)
  {
    // ok
  }
  else
  {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if (killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if (which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void usertrapret(void)
{
  struct proc *p = myproc();
  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp(); // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.

  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to userret in trampoline.S at the top of memory, which
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  struct proc *p = myproc();

  if ((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if (intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if (r_scause() == 12)
  {
    uint64 addr = r_stval();
    if (addr >= p->text.addr && addr < (p->text.addr + p->text.size))
    {
      int prot = PTE_R | PTE_X;
      allocPhysicalVMA(&(p->text), p, addr, prot | PTE_U);
    }
  }
  else if (r_scause() == 13 || r_scause() == 15)
  {
    // load / store page fault

    // direccion que dio el fallo.
    uint64 addr = r_stval();
    int solved = 0;
    int cow = 0;

    uint64 phy = walkaddr(p->pagetable, addr);
    if (phy && r_scause() == 15)
    {
      for (int i = 0; i < PER_PROCESS_VMAS && !solved; i++)
      {
        if (p->vmas[i] == 0)
          continue;

        if (addr >= p->vmas[i]->addr && addr < (p->vmas[i]->addr + p->vmas[i]->size))
        {
          cow = 1;
          break;
        }
      }
      if (cow)
      {
        int ref = getref((void *)phy);
        if (ref > 1)
        {
          char *new_phy = kalloc();
          if (new_phy == 0)
          {
            printf("usertrap(): No physical pages available. pid=%d\n", p->pid);
            setkilled(p);
          }
          memcpy(new_phy, (void *)phy, PGSIZE);
          uint64 *pte = walk(p->pagetable, addr, 0);
          *pte = *pte & ~(PTE_V);
          if (mappages(p->pagetable, PGROUNDDOWN(addr), PGSIZE, (uint64)new_phy, PTE_FLAGS(*pte) | PTE_W) < 0)
          {
            kfree(new_phy);
            printf("usertrap(): Could not map physical to virtual address, pid=%d\n", p->pid);
            setkilled(p);
          }
          decref((void *)phy);
        }
        if (ref == 1)
        {
          uint64 *pte = walk(p->pagetable, addr, 0);
          *pte = *pte | PTE_W;
        }
        solved = 1;
      }
    }
    else if (addr >= p->data.addr && addr < (p->data.addr + p->data.size))
    {
      int prot = PTE_R | PTE_W;
      allocPhysicalVMA(&(p->data), p, addr, prot | PTE_U);
    }
    else
    {
      for (int i = 0; i < PER_PROCESS_VMAS && !solved; i++)
      {
        if (p->vmas[i] == 0)
          continue;
        
        if (addr >= p->vmas[i]->addr && addr < (p->vmas[i]->addr + p->vmas[i]->size))
        {
          int prot;
          switch (p->vmas[i]->prot)
          {
          case (PROT_READ):
            prot = PTE_R;
            break;
          case (PROT_WRITE):
            prot = PTE_W;
            break;
          case (PROT_RW):
            prot = PTE_R | PTE_W;
            break;
          default:
            prot = 0;
          }

          allocPhysicalVMA(p->vmas[i], p, addr, prot | PTE_U);

          solved = 1;
        }
      }
    }

    // fallo
    if (!solved)
    {
      printf("usertrap(): Wrong memory address. Not your business. pid=%d\n", p->pid);
      setkilled(p);
    }
  }

  if ((which_dev = devintr()) == 0)
  {
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }
  // give up the CPU if this is a timer interrupt.
  if (which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int devintr()
{
  uint64 scause = r_scause();

  if ((scause & 0x8000000000000000L) &&
      (scause & 0xff) == 9)
  {
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if (irq == UART0_IRQ)
    {
      uartintr();
    }
    else if (irq == VIRTIO0_IRQ)
    {
      virtio_disk_intr();
    }
    else if (irq)
    {
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if (irq)
      plic_complete(irq);

    return 1;
  }
  else if (scause == 0x8000000000000001L)
  {
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if (cpuid() == 0)
    {
      clockintr();
    }

    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  }
  else
  {
    return 0;
  }
}
