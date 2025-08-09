#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void startothers(void);
static void mpmain(void)  __attribute__((noreturn));
extern pml4e_t *kpml4;
extern char end[]; // first address after kernel loaded from ELF file

// Bootstrap processor starts running C code here.
// Allocate a real stack and switch to it, first
// doing some setup required for memory allocator to work.
int
main(void)
{
  void *kinit1_end = (1024*1024*1024 > PHYSTOP) ? P2V(PHYSTOP) 
                                                : P2V(1024*1024*1024);
  kinit1(end, kinit1_end); // phys page allocator
  kvmalloc();      // kernel page table
  mpinit();        // detect other processors
  lapicinit();     // interrupt controller
  seginit();       // segment descriptors
  picinit();       // disable pic
  ioapicinit();    // another interrupt controller
  consoleinit();   // console hardware
  uartinit();      // serial port
  pinit();         // process table
  tvinit();        // trap vectors
  binit();         // buffer cache
  fileinit();      // file table
  ideinit();       // disk 
  startothers();   // start other processors
  kinit2(kinit1_end, P2V(PHYSTOP)); // must come after startothers()
  userinit();      // first user process
  mpmain();
}

// Other CPUs jump here from entryother.S.
static void
mpenter(void)
{
  switchkvm();
  seginit();
  lapicinit();
  mpmain();
}

// Common CPU setup code.
static void
mpmain(void)
{
  cprintf("cpu%d: starting %d\n", cpuid(), cpuid());
  idtinit();       // load idt register
  xchg(&(mycpu()->started), 1); // tell startothers() we're up
  scheduler();     // start running processes
}

extern pml4e_t entrypml4[NPML4ENTRIES];

// Start the non-boot (AP) processors.
static void
startothers(void)
{
  extern uchar _binary_entryother_start[], _binary_entryother_size[];
  uchar *code;
  struct cpu *c;
  char *stack;

  // Write entry code to unused memory at 0x7000.
  // The linker has placed the image of entryother.S in
  // _binary_entryother_start.
  code = P2V(0x7000);
  memmove(code, _binary_entryother_start, (uint64)_binary_entryother_size);

  for(c = cpus; c < cpus+ncpu; c++){
    if(c == mycpu())  // We've started already.
      continue;

    // Tell entryother.S what stack to use, where to enter, and what
    // pml4 to use. We cannot use kpml4 yet, because the AP processor
    // is running in low  memory, so we use entrypml4 for the APs too.
    stack = kalloc();
    *(void**)(code-8) = stack + KSTACKSIZE;
    *(void(**)(void))(code-16) = mpenter;
    *(uint*)(code-20) = V2P(entrypml4);

    lapicstartap(c->apicid, V2P(code));

    // wait for cpu to finish mpmain()
    while(c->started == 0)
      ;
  }
}

// The boot page table used in entry.S and entryother.S.
// Page directories (and page tables) must start on page boundaries,
// hence the __aligned__ attribute.
// PTE_PS in a page directory entry enables 1Gbyte pages.
__attribute__((__aligned__(PGSIZE)))
pdpte_t identitymap[NPDPTENTRIES] = {
  // Map VA's [0, 1GB) to PA's [0, 1GB)
  [0] = (0) | PTE_P | PTE_W | PTE_PS,
};

__attribute__((__aligned__(PGSIZE)))
pdpte_t kernmap[NPDPTENTRIES] = {
  // Map VA's [KERNBASE, KERNBASE+1GB) to PA's [0, 1GB)
  [PDPTX(KERNBASE)] = (0) | PTE_P | PTE_W | PTE_PS,
};

__attribute__((__aligned__(PGSIZE)))
pml4e_t entrypml4[NPML4ENTRIES] = {
  // Flags below should be added with "|" and not with "+",
  // however, "|" seems to complex for the link editor,
  // so the compiler refuses to compile the code.
  // The use of "+" is valid since PTE_* are only single bits.
  [0] = V2P(identitymap) + PTE_P + PTE_W,
  [PML4X(KERNBASE)] = V2P(kernmap) + PTE_P + PTE_W,
};

