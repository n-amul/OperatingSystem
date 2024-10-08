#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "wmap.h"

struct file
{
  enum
  {
    FD_NONE,
    FD_PIPE,
    FD_INODE
  } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe;
  struct inode *ip;
  uint off;
};

struct
{
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);
pte_t *walkpgdir(pde_t *pgdir, const void *va, int alloc);

int isRegionFree(pde_t *pgdir, void *va, uint size)
{
  char *start = (char *)va;
  char *end = start + size;
  pte_t *pte;

  for (char *a = start; a < end; a += PGSIZE)
  {
    pte = walkpgdir(pgdir, a, 0);
    if (pte && (*pte & PTE_P))
    {
      // Page is present, region is not free.
      return 0;
    }
  }
  // No page is present in the region, hence it's free.
  return 1;
}

void *findFreeRegion(uint *pgdir, uint start, uint end, int length, uint *last_end_addr) {
    void *addr = (void *)start;
    if (last_end_addr) {
        addr = (void *)(*last_end_addr + PGSIZE);
    }

    for (; addr < (void *)end; addr += PGSIZE) {
        if (isRegionFree(pgdir, addr, length)) {
            return addr;
        }
    }
    return (void *)-1;
}


int isValidFreeRegion(uint *pgdir, void *addr, int length)
{
  if ((uint)addr >= VMA_START && (uint)addr + length <= VMA_END)
  {
    return isRegionFree(pgdir, addr, length);
  }
  return 0;
}

int findFreeVmaSlot(struct proc *curproc)
{
  for (int i = 0; i < NVMA; i++)
  {
    if (curproc->vmas[i].addr == 0)
    {
      return i;
    }
  }
  return -1;
}

int allocateAndMapPages(struct proc *curproc, void *addr, int alloc_length, int flags, int fd)
{
  char *mem;
  uint a;

  for (a = (uint)addr; a < (uint)addr + alloc_length; a += PGSIZE)
  {
    // Allocate a physical page
    mem = kalloc();
    if (!mem)
    {
      // Allocation failed, free previously allocated pages
      deallocuvm(curproc->pgdir, (uint)addr, a);
      return -1; // Return -1 on failure
    }
    // Clear allocated memory to zero
    memset(mem, 0, PGSIZE);
    // Map the virtual address to the physical page
    if (mappages(curproc->pgdir, (void *)a, PGSIZE, V2P(mem), PTE_W | PTE_U) < 0)
    {
      // Mapping failed, free the allocated page and previously allocated pages
      kfree(mem);
      deallocuvm(curproc->pgdir, (uint)addr, a + PGSIZE);
      return -1; // Return -1 on failure
    }
  }

  return 0; // Return 0 on success
}

void updateVma(struct proc *curproc, int vma_index, void *addr, int length, int prot, int flags, int fd, int offset)
{
  struct VMA *vma = &curproc->vmas[vma_index];
  vma->addr = addr;
  vma->end = (void *)((uint)addr + length);
  vma->prot = prot;
  vma->flags = flags;
  vma->fd = fd;
  vma->offset = offset;
  vma->valid = 1;
}
int wmap(uint addr, int length, int flags) {
    struct proc *curproc = myproc();
    static uint last_end_addr = 0;

    if (length <= 0 || (flags & (MAP_SHARED | MAP_PRIVATE)) == 0 ||
        (flags & (MAP_SHARED | MAP_PRIVATE)) == (MAP_SHARED | MAP_PRIVATE)) {
        return -1;
    }

    int alloc_length = PGROUNDUP(length);
    void *alloc_addr;

    if (flags & MAP_FIXED) {
        if ((uint)addr < VMA_START || (uint)addr + alloc_length > VMA_END ||
            !isRegionFree(curproc->pgdir, (void *)addr, alloc_length)) {
            return -1;
        }
        alloc_addr = (void *)addr;
    } else {
        alloc_addr = findFreeRegion(curproc->pgdir, VMA_START, VMA_END, alloc_length, &last_end_addr);
        if (alloc_addr == (void *)-1) {
            return -1;
        }
    }

    int vma_index = findFreeVmaSlot(curproc);
    if (vma_index == -1) {
        return -1;
    }

    if (flags & MAP_POPULATE) {
        if (allocateAndMapPages(curproc, alloc_addr, alloc_length, flags, -1) != 0) {
            return -1;
        }
    }

    updateVma(curproc, vma_index, alloc_addr, alloc_length, 0, flags, -1, 0);
    last_end_addr = (uint)alloc_addr + alloc_length - PGSIZE;
    return (int)alloc_addr;
}

int wunmap(uint addr) {
    struct proc *curproc = myproc();
    struct VMA *vma = 0;

    // Ensure addr is page-aligned
    if (addr % PGSIZE != 0) {
        return -1;
    }

    // Find the VMA corresponding to addr
    for (int i = 0; i < NVMA; i++) {
        if (curproc->vmas[i].valid && (uint)curproc->vmas[i].addr == addr) {
            vma = &curproc->vmas[i];
            break;
        }
    }

    // If no such VMA is found, return -1
    if (!vma) {
        return -1;
    }

    // Handle MAP_SHARED and inherited from the parent
    if ((vma->flags & MAP_SHARED) && curproc->parent) {
        struct proc *parent = curproc->parent;
        for (int i = 0; i < NVMA; i++) {
            if (parent->vmas[i].valid && (uint)parent->vmas[i].addr == addr) {
                vma->valid = 0; // Just invalidate the VMA, do not free physical pages
                return 0;
            }
        }
    }

    // Deallocate the memory and invalidate the VMA
    if (vma->flags & MAP_POPULATE) {
        deallocuvm(curproc->pgdir, (uint)vma->end, (uint)vma->addr);
    } else {
        // If MAP_POPULATE is not set, manually clear the page table entries
        for (uint a = (uint)vma->addr; a < (uint)vma->end; a += PGSIZE) {
            pte_t *pte = walkpgdir(curproc->pgdir, (void *)a, 0);
            if (pte && (*pte & PTE_P)) {
                *pte = 0; // Clear the page table entry
            }
        }
    }

    // Invalidate the VMA
    vma->valid = 0;

    return 0;
}


int getpgdirinfo(struct pgdirinfo *pdinfo) {
  struct proc *p = myproc();
  pde_t *pgdir = p->pgdir;
  int count = 0;

  for (int i = 0; i < NPDENTRIES && count < MAX_UPAGE_INFO; i++) {
    if (pgdir[i] & PTE_P) {
      pte_t *pgtab = (pte_t *)P2V(PTE_ADDR(pgdir[i]));
      for (int j = 0; j < NPTENTRIES && count < MAX_UPAGE_INFO; j++) {
        if (pgtab[j] & PTE_P) {
          if (pgtab[j] & PTE_U) { // Check if it's a user page
            pdinfo->va[count] = (i << PDXSHIFT) | (j << PTXSHIFT);
            pdinfo->pa[count] = PTE_ADDR(pgtab[j]);
            count++;
          }
        }
      }
    }
  }

  pdinfo->n_upages = count;

  return 0;
}
int getwmapinfo(struct wmapinfo *wminfo) {
  struct proc *p = myproc();
  int count = 0;

  for (int i = 0; i < NVMA && count < MAX_WMAP_INFO; i++) {
    if (p->vmas[i].valid) {
      uint length = ((uint)p->vmas[i].end - (uint)p->vmas[i].addr + PGSIZE - 1) & ~(PGSIZE - 1);
      wminfo->addr[count] = (uint)p->vmas[i].addr;
      wminfo->length[count] = length;
      int loaded_pages = 0;
      for (uint va = (uint)p->vmas[i].addr; va < (uint)p->vmas[i].addr + length; va += PGSIZE) {
        pte_t *pte = walkpgdir(p->pgdir, (void *)va, 0);
        if (pte && (*pte & PTE_P)) {
          loaded_pages++;
        }
      }
      wminfo->n_loaded_pages[count] = loaded_pages;
      count++;
    }
  }

  wminfo->total_mmaps = count;

  return 0;
}



void pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int cpuid()
{
  return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu *
mycpu(void)
{
  int apicid, i;

  if (readeflags() & FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i)
  {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc *
myproc(void)
{
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

// PAGEBREAK: 32
//  Look in the process table for an UNUSED proc.
//  If found, change state to EMBRYO and initialize
//  state required to run in the kernel.
//  Otherwise return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if ((p->kstack = kalloc()) == 0)
  {
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe *)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint *)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context *)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

// PAGEBREAK: 32
//  Set up first user process.
void userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if ((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0; // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if (n > 0)
  {
    if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if (n < 0)
  {
    if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

void clean_vma(struct proc *p)
{
  int i;
  for (i = 0; i < 32; i++)
  {
    if (p->vmas[i].valid)
    {
      if (p->vmas[i].pf)
      {
        fileclose(p->vmas[i].pf);
        p->vmas[i].pf = 0;
      }
      p->vmas[i].valid = 0;
    }
  }
}
// Function to copy a single VMA from the parent to the child process.
// Returns 0 on success, -1 on failure.
int copy_vma(struct proc *child, struct VMA *source_vma, int index)
{
  struct VMA *dest_vma = &child->vmas[index];
  // Check if VMA is valid
  if (!source_vma->valid)
  {
    return -1;
  }
  // Copy VMA struct
  *dest_vma = *source_vma;
  // check if there's a file also need to copy
  if (source_vma->pf)
  {
    dest_vma->pf = filedup(source_vma->pf);
  }
  // Set the VMA as valid.
  dest_vma->valid = 1;
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy process state from proc.
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)
  {
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;
  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  // copy VMAs
  for (i = 0; i < 32; i++)
  {
    if (curproc->vmas[i].valid)
    {
      if (copy_vma(np, &curproc->vmas[i], i) != 0)
      {
        // handle error
        kfree(np->kstack);
        np->kstack = 0;
        np->state = UNUSED;
        clean_vma(np);
        return -1;
      }
    }
  }

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if (curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd])
    {
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent == curproc)
    {
      p->parent = initproc;
      if (p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Deallocate all VMAs
  clean_vma(curproc);

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;
      if (p->state == ZOMBIE)
      {
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); // DOC: wait-sleep
  }
}

// PAGEBREAK: 42
//  Per-CPU process scheduler.
//  Each CPU calls scheduler() after setting itself up.
//  Scheduler never returns.  It loops, doing:
//   - choose a process to run
//   - swtch to start running that process
//   - eventually that process transfers control
//       via swtch back to the scheduler.
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  for (;;)
  {
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&ptable.lock))
    panic("sched ptable.lock");
  if (mycpu()->ncli != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (readeflags() & FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  acquire(&ptable.lock); // DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first)
  {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if (p == 0)
    panic("sleep");

  if (lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if (lk != &ptable.lock)
  {                        // DOC: sleeplock0
    acquire(&ptable.lock); // DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if (lk != &ptable.lock)
  { // DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

// PAGEBREAK!
//  Wake up all processes sleeping on chan.
//  The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->killed = 1;
      // Wake process from sleep if necessary.
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

// PAGEBREAK: 36
//  Print a process listing to console.  For debugging.
//  Runs when user types ^P on console.
//  No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [EMBRYO] "embryo",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if (p->state == SLEEPING)
    {
      getcallerpcs((uint *)p->context->ebp + 2, pc);
      for (i = 0; i < 10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}