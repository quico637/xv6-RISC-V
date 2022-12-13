#include "types.h"
#include "param.h"
#include "fcntl.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "pstat.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

// GLOBAL
struct vma vmas[NVMAS];

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
void proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    char *pa = kalloc();
    if (pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc and VMA table.
void procinit(void)
{
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = KSTACK((int)(p - proc));
  }

  struct vma *vma;
  for (vma = vmas; vma < &vmas[NVMAS]; vma++)
  {
    initlock(&vma->lock, "vma");
    vma->used = 0;
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int allocpid()
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
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == UNUSED)
    {
      goto found;
    }
    else
    {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  p->nmp = TRAPFRAME;

  // Allocate a trapframe page.
  if ((p->trapframe = (struct trapframe *)kalloc()) == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0)
  {
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
  if (p->trapframe)
    kfree((void *)p->trapframe);
  p->trapframe = 0;
  if (p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if (pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if (mappages(pagetable, TRAMPOLINE, PGSIZE,
               (uint64)trampoline, PTE_R | PTE_X) < 0)
  {
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if (mappages(pagetable, TRAPFRAME, PGSIZE,
               (uint64)(p->trapframe), PTE_R | PTE_W) < 0)
  {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
    0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
    0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
    0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
    0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
    0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
    0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};

// Set up first user process.
void userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;     // user program counter
  p->trapframe->sp = PGSIZE; // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");
  p->tickets = 1; /* DEFAULT PRIORITY */
  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0)
  {
    if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0)
    {
      return -1;
    }
  }
  else if (n < 0)
  {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy user memory from parent to child.
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
  {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // child process inherits tickets from father
  np->tickets = p->tickets;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  // child process inherits father's VMAs
  for (i = 0; i < PER_PROCESS_VMAS; i++)
  {
    if (p->vmas[i])
    {
      for (int j = 0; j < NVMAS; j++)
      {
        acquire(&vmas[j].lock);
        if (!vmas[j].used)
        {
          np->vmas[i] = &vmas[j];
          vmas[j].used = 1;
          vmas[j].mfile = p->vmas[i]->mfile;
          vmas[j].prot = p->vmas[i]->prot;
          vmas[j].flags = p->vmas[i]->flags;
          vmas[j].size = p->vmas[i]->size;
          vmas[j].offset = p->vmas[i]->offset;
          vmas[j].mfile->ref++;
          np->nmp -= PGROUNDUP(vmas[j].size);
          vmas[j].addr = np->nmp;
          for (int k = 0; k < PGROUNDUP(p->vmas[i]->size); k += PGSIZE)
          {

            uint64 phy = walkaddr(p->pagetable, p->vmas[i]->addr + k);
            if (phy)
            {
              int prot = 0;
              switch (p->vmas[i]->prot)
              {
              case (PROT_READ):
                prot = PTE_R;
                break;
              case (PROT_WRITE):
                if (p->vmas[i]->flags != MAP_PRIVATE)
                  prot = PTE_W;
                break;
              case (PROT_RW):
                if (p->vmas[i]->flags != MAP_PRIVATE)
                  prot = PTE_R | PTE_W;
                else
                  prot = PTE_R;
                break;
              default:
                prot = 0;
              }
              if (mappages(np->pagetable, PGROUNDDOWN(vmas[j].addr + k), PGSIZE, phy, prot | PTE_U) < 0)
              {
                printf("fork(): Could not map physical to virtual address, pid=%d\n", np->pid);
                setkilled(np);
              }
              if (p->vmas[i]->flags == MAP_PRIVATE)
              {
                pte_t *entry = walk(p->pagetable, p->vmas[i]->addr + k, 0);
                *entry = PA2PTE(phy) | prot | PTE_V | PTE_U;
              }
              incref((void*)phy);
            }
          }
          release(&vmas[j].lock);
          break;
        }
        release(&vmas[j].lock);
      }
    }
  }
  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p)
{
  struct proc *pp;

  for (pp = proc; pp < &proc[NPROC]; pp++)
  {
    if (pp->parent == p)
    {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void exit(int status)
{
  struct proc *p = myproc();

  if (p == initproc)
    panic("init exiting");

  // Close all open files.
  for (int fd = 0; fd < NOFILE; fd++)
  {
    if (p->ofile[fd])
    {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  // dealloc all VMAS
  for (int i = 0; i < PER_PROCESS_VMAS; i++)
  {
    if (p->vmas[i])
      deallocvma(p->vmas[i]->addr, p->vmas[i]->size);
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (pp = proc; pp < &proc[NPROC]; pp++)
    {
      if (pp->parent == p)
      {
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if (pp->state == ZOMBIE)
        {
          // Found one.
          pid = pp->pid;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                   sizeof(pp->xstate)) < 0)
          {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || killed(p))
    {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock); // DOC: wait-sleep
  }
}


// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){

    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();
    int total_tickets = 0;
    int total_ticks = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        total_tickets += p->tickets;
        total_ticks += p->ticks;
      }
      release(&p->lock);
    }

    if(total_tickets < 1)
    {
      continue;
    }

    int seed = total_tickets + total_ticks;
    int random = randomrange(seed, 1, total_tickets);


    for(p = proc; p < &proc[NPROC]; p++) {    
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        if (random <= p->tickets) {
          // HAS BEEN SELECTED
          p->state = RUNNING;
          p->ticks++; /* ASSUMING 1 CLOCK TICK PER QUANTUM */
          c->proc = p;
          swtch(&c->context, &p->context);

          // Process is done running for now.
          // It should have changed its p->state before coming back.
          c->proc = 0;

          /* SOLAMENTE PUEDES VOLVER AQUI CUANDO HAYAS VUELTO DE UN CAMBIO DE CONTEXTO */ 
          release(&p->lock);
          break;
        }
        random -= p->tickets;
      }
      release(&p->lock);
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first)
  {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock); // DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void wakeup(void *chan)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p != myproc())
    {
      acquire(&p->lock);
      if (p->state == SLEEPING && p->chan == chan)
      {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kill(int pid)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->pid == pid)
    {
      p->killed = 1;
      if (p->state == SLEEPING)
      {
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int killed(struct proc *p)
{
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if (user_dst)
  {
    return copyout(p->pagetable, dst, src, len);
  }
  else
  {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if (user_src)
  {
    return copyin(p->pagetable, dst, src, len);
  }
  else
  {
    memmove(dst, (char *)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [USED] "used",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  struct proc *p;
  char *state;

  printf("\n");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

int pinfo(uint64 ps)
{
  struct pstat st;
  for (int i = 0; i < NPROC; i++)
  {
    st.tickets[i] = (&proc[i])->tickets;
    st.pid[i] = (&proc[i])->pid;
    if ((&proc[i])->state == UNUSED)
      st.inuse[i] = 0;
    else
      st.inuse[i] = 1;
    st.ticks[i] = (&proc[i])->ticks;
  }

  if (copyout(myproc()->pagetable, ps, (char *)&st, sizeof(st)) < 0)
    return -1;
  return 0;
}

uint64
allocvma(int length, int prot, int flags, struct file *f, int fd, int offset)
{
  struct proc *p = myproc();
  for (int i = 0; i < PER_PROCESS_VMAS; i++)
  {
    if (!p->vmas[i])
    {
      for (int j = 0; j < NVMAS; j++)
      {
        acquire(&vmas[j].lock);
        if (!vmas[j].used)
        {
          p->vmas[i] = &vmas[j];
          vmas[j].used = 1;
          vmas[j].mfile = f;
          vmas[j].prot = prot;
          vmas[j].flags = flags;
          vmas[j].size = PGROUNDUP(length);
          vmas[j].offset = 0;
          vmas[j].fd = fd;
          vmas[j].mfile->ref++;
          p->nmp -= PGROUNDUP(length);
          vmas[j].addr = p->nmp;
          release(&vmas[j].lock);
          return vmas[j].addr;
        }
        release(&vmas[j].lock);
      }
      return (uint64)MAP_FAILED;
    }
  }
  return (uint64)MAP_FAILED;
}

int deallocvma(uint64 addr, int size)
{
  struct proc *p = myproc();
  for (int i = 0; i < PER_PROCESS_VMAS; i++)
  {
    if (p->vmas[i] && p->vmas[i]->used && (addr >= p->vmas[i]->addr) && (addr < p->vmas[i]->addr + p->vmas[i]->size))
    {
      // Unmap complete VMA
      int complete = 0;
      int new_offset = p->vmas[i]->offset;
      if (addr == p->vmas[i]->addr && size == p->vmas[i]->size)
      {
        p->vmas[i]->used = 0;
        // fileclose cierra la ultima referencia del fichero correctamente
        complete = 1;
      }
      // Unmap first part of VMA
      else if (addr == p->vmas[i]->addr && size < p->vmas[i]->size)
      {
        p->vmas[i]->addr += size;
        p->vmas[i]->size -= size;
        new_offset = p->vmas[i]->offset + size;
      }
      // Unmap last part of the VMA
      else if (addr > p->vmas[i]->addr && (addr + size == p->vmas[i]->addr + p->vmas[i]->size))
      {
        p->vmas[i]->size -= size;
      }
      else
        return -1;
      if (p->vmas[i]->flags == MAP_SHARED)
      {
        int max = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE;
        int j = 0;
        while (j < size)
        {
          int n1 = size - j;
          if (n1 > max)
            n1 = max;

          if (!(walkaddr(p->pagetable, addr + j)))
          {
            j += PGSIZE;
            continue;
          }

          begin_op();
          ilock(p->vmas[i]->mfile->ip);
          int w = 0;
          while (w < PGSIZE)
          {
            int r = writei(p->vmas[i]->mfile->ip, 1, addr + j + w, p->vmas[i]->offset + j + w, n1);
            w += r;
            n1 = PGSIZE - w;
            if (n1 > max)
              n1 = max;
          }
          if (getref((void*)walkaddr(p->pagetable, addr+j)) == 1)
            uvmunmap(p->pagetable, addr + j, 1, 1);
          else
          {
            decref((void*)walkaddr(p->pagetable, addr+j));
            uvmunmap(p->pagetable, addr + j, 1, 0);
          }
          iunlock(p->vmas[i]->mfile->ip);
          end_op();
          j += w;
        }
      }
      else
      {
        for (int j = 0; j < size / PGSIZE; j++)
        {
          uint64 phy_addr = walkaddr(p->pagetable, addr+j*PGSIZE);
          if (phy_addr)
          {
            if (getref((void*)phy_addr) == 1)
              uvmunmap(p->pagetable, addr + j * PGSIZE, 1, 1);
            else
            {
              decref((void*)phy_addr);
              uvmunmap(p->pagetable, addr + j * PGSIZE, 1, 0);
            }
          }
        }
      }
      p->vmas[i]->offset = new_offset;

      if (complete)
      {
        if (p->vmas[i]->mfile->ref == 1)
        {
          fileclose(p->vmas[i]->mfile);
          p->ofile[p->vmas[i]->fd] = 0;
        }
        else
          p->vmas[i]->mfile->ref--;

        p->vmas[i] = 0;
      }

      uint64 min_vma = TRAPFRAME;
      for (int v = 0; v < PER_PROCESS_VMAS; v++)
      {
        if (p->vmas[v] && p->vmas[v]->addr < min_vma)
          min_vma = p->vmas[v]->addr;
      }
      p->nmp = min_vma;
      return 0;
    }
  }
  return -1;
}
