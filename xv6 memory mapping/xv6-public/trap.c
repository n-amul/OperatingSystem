#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
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

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[]; // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);
pte_t *walkpgdir(pde_t *pgdir, const void *va, int alloc);

void tvinit(void)
{
  int i;

  for (i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE << 3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE << 3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void idtinit(void)
{
  lidt(idt, sizeof(idt));
}

// Helper function to handle segmentation faults
void handle_segmentation_fault(struct proc *p)
{
  cprintf("Segmentation Fault\n");
  p->killed = 1;
}

void update_vma_structure(struct VMA *next, struct VMA *v_grow, int addr)
{
  v_grow->next = addr;
  next->addr = v_grow->addr + PGSIZE;
  next->end = v_grow->end + PGSIZE;
  next->prot = v_grow->prot;
  next->flags = v_grow->flags;
  next->fd = v_grow->fd;
  next->offset = v_grow->offset + PGSIZE;
  next->valid = 1;
  next->pf = v_grow->pf;
}
// load a page from the disk into the new memory space,
// ensuring the process's memory reflects the contents of the file it's mapped to.
void copy_page_from_disk(struct proc *process, char *memory, struct VMA *next)
{
  struct file *f = process->ofile[next->fd];
  ilock(f->ip);
  readi(f->ip, memory, PGSIZE, PGSIZE); // copy a page of the file from the disk
  iunlock(f->ip);
}

// PAGEBREAK: 41
void trap(struct trapframe *tf)
{
  if (tf->trapno == T_SYSCALL)
  {
    if (myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if (myproc()->killed)
      exit();
    return;
  }

  switch (tf->trapno)
  {
  case T_IRQ0 + IRQ_TIMER:
    if (cpuid() == 0)
    {
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE + 1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;
  case T_PGFLT: { // Page Fault handler
    struct proc *p = myproc();
    uint addr = rcr2(); // Get the faulting address

    // Check if the faulting address is within the VMA region
    if (addr < VMA_START || addr >= VMA_END) {
        handle_segmentation_fault(p);
        break;
    }

    struct VMA *vma = 0;
    for (int i = 0; i < NVMA; i++) {
        if (p->vmas[i].valid && (uint)p->vmas[i].addr <= addr && addr < (uint)p->vmas[i].end) {
            vma = &p->vmas[i];
            break;
        }
    }

    if (!vma) {
        handle_segmentation_fault(p);
        break;
    }

    // Allocate physical memory and map it to the faulting address
    char *mem = kalloc();
    if (!mem) {
        handle_segmentation_fault(p);
        break;
    }
    memset(mem, 0, PGSIZE);
    if (mappages(p->pgdir, (char *)PGROUNDDOWN(addr), PGSIZE, V2P(mem), PTE_W | PTE_U) < 0) {
        kfree(mem);
        handle_segmentation_fault(p);
        break;
    }

    if (!(vma->flags & MAP_ANONYMOUS)) {
        copy_page_from_disk(p, mem, vma);
    }
    break;
  }

  // PAGEBREAK: 13
  default:
    // need to allocate more space
    /*mmap_growsup();*/
    if (myproc() == 0 || (tf->cs & 3) == 0)
    {
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if (myproc() && myproc()->killed && (tf->cs & 3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if (myproc() && myproc()->state == RUNNING &&
      tf->trapno == T_IRQ0 + IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if (myproc() && myproc()->killed && (tf->cs & 3) == DPL_USER)
    exit();
}