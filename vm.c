#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // defined by kernel.ld
pml4e_t *kpml4;  // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG64(STA_X|STA_R, 0, 0xffffffff, 0, 1);
  c->gdt[SEG_KDATA] = SEG64(STA_W, 0, 0xffffffff, 0, 0);
  c->gdt[SEG_UCODE] = SEG64(STA_X|STA_R, 0, 0xffffffff, DPL_USER, 1);
  c->gdt[SEG_UDATA] = SEG64(STA_W, 0, 0xffffffff, DPL_USER, 0);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table of the specified level
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpglevel(uint64 *pgdir, const void *va, int alloc, int level, int *failure_level)
{
  uint64 *entry;
  uint64 *next_pgdir;
  
  switch (level){
    case 4:
      entry = &pgdir[PML4X(va)];
      break;
    case 3:
      entry = &pgdir[PDPTX(va)];
      break;
    case 2:
      entry = &pgdir[PDX(va)];
      break;
    case 1:
      return &pgdir[PTX(va)];
    default:
      panic("walkpglevel");
  }

  if(*entry & PTE_P){
    next_pgdir = (uint64*)P2V(PTE_ADDR(*entry));
  } else {
    if(!alloc || (next_pgdir = (uint64*)kalloc()) == 0){
      if (failure_level)
        *failure_level = level;
      return 0;
    }
    // Make sure all those PTE_P bits are zero.
    memset(next_pgdir, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *entry = V2P(next_pgdir) | PTE_P | PTE_W | PTE_U;
  }
  return walkpglevel(next_pgdir, va, alloc, level - 1, failure_level);
}

// Return the address of the PTE in page table pml4
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpml4(pml4e_t *pml4, const void *va, int alloc)
{
  return walkpglevel(pml4, va, alloc, 4, (int*)0);
}

// Return the address of the PTE in page table pml4
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages. 
// On failure (return value equals 0), failure_level is set to the level 
// in which the table was not present.
static pte_t *
walkpml4_withinfo(pml4e_t *pml4, const void *va, int alloc, int *failure_level)
{
  return walkpglevel(pml4, va, alloc, 4, failure_level);
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pml4e_t *pml4, void *va, uint size, uint64 pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint64)va);
  last = (char*)PGROUNDDOWN(((uint64)va) + size - 1);
  for(;;){
    if((pte = walkpml4(pml4, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Make sure the ACPI table at address pa is in the page table. 
// If it is not, add it so it can be accessed.
// Return the virtual address of the ACPI table referring to the physical address pa.
void *
acpitable(uint64 pa)
{
  uint64 *va;
  pte_t *pte;

  va = P2V(pa);
  if ((pte = walkpml4(kpml4, va, 1)) == 0)
    return 0;
  if (! *pte & PTE_P)
    *pte = pa | PTE_P;
  
  return va;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpml4). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint64 phys_start;
  uint64 phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVBASE,  DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pml4e_t*
setupkvm(void)
{
  pml4e_t *pml4;
  struct kmap *k;

  if((pml4 = (pml4e_t*)kalloc()) == 0)
    return 0;
  memset(pml4, 0, PGSIZE);
  if (P2V(PHYSTOP) > DEV_P2V(DEVSPACE))
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pml4, k->virt, k->phys_end - k->phys_start,
                (uint64)k->phys_start, k->perm) < 0) {
      freevm(pml4);
      return 0;
    }
  return pml4;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpml4 = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpml4));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pml4 == 0)
    panic("switchuvm: no pml4");

  pushcli();
  struct tssdesc *tss = (struct tssdesc *)&mycpu()->gdt[SEG_TSS];
  *tss = TSS64(STS_T64A, &mycpu()->ts, sizeof(mycpu()->ts)-1, 0);
  mycpu()->ts.rsp0_31_0 = (uint)((uint64)p->kstack + KSTACKSIZE);
  mycpu()->ts.rsp0_63_32 = (uint)(((uint64)p->kstack + KSTACKSIZE) >> 32);
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pml4));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pml4.
// sz must be less than a page.
void
inituvm(pml4e_t *pml4, char *init, uint64 sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pml4, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pml4.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pml4e_t *pml4, char *addr, struct inode *ip, uint offset, uint64 sz)
{
  uint i, n;
  uint64 pa;
  pte_t *pte;

  if((uint64) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpml4(pml4, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pml4e_t *pml4, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pml4, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pml4, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pml4, newsz, oldsz);
      kfree(mem);
      return 0;
    }
  }
  return newsz;
}

static inline uint64
skiptables(uint64 va, int walkpml4_failure_level)
{
  switch (walkpml4_failure_level)
  {
    case 4:
      if (PML4X(va) == 0x1FF)
        return va + PGSIZE;
      va = PGADDR(PML4X(va) + 1, 0, 0, 0, 0);
      break;
    case 3:
      if (PDPTX(va) == 0x1FF)
        return skiptables(va, walkpml4_failure_level + 1);
      va = PGADDR(PML4X(va), PDPTX(va) + 1, 0, 0, 0);
      break;
    case 2:
      if (PDX(va) == 0x1FF)
        return skiptables(va, walkpml4_failure_level + 1);
      va = PGADDR(PML4X(va), PDPTX(va), PDX(va) + 1, 0, 0);
      break;
  }
  return va;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pml4e_t *pml4, uint64 oldsz, uint64 newsz)
{
  pte_t *pte;
  uint64 a, pa;
  int walkpml4_failure_level;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpml4_withinfo(pml4, (char*)a, 0, &walkpml4_failure_level);
    if(!pte){ // Skip the page table
      a = skiptables(a, walkpml4_failure_level) - PGSIZE;
    } else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

// Free page tables
static void freepgdir(uint64 *pgdir, int level) {
  if (level <= 1)
    goto finish;

  for (int i = 0; i < NPTENTRIES; i++) {
    if ((pgdir[i] & PTE_P)) {
      uint64 *next_table = (uint64*)P2V(PTE_ADDR(pgdir[i]));
      freepgdir(next_table, level - 1);
    }
  }

finish:
  kfree((char *)pgdir);
}

// Free page tables and all the physical memory pages
// in the user part.
void
freevm(pml4e_t *pml4)
{
  if(pml4 == 0)
    panic("freevm: no pml4");
  deallocuvm(pml4, KERNBASE, 0);
  freepgdir(pml4, 4);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pml4e_t *pml4, char *uva)
{
  pte_t *pte;

  pte = walkpml4(pml4, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pml4e_t*
copyuvm(pml4e_t *pml4, uint64 sz)
{
  pde_t *d;
  pte_t *pte;
  uint64 pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpml4(pml4, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {
      kfree(mem);
      goto bad;
    }
  }
  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pml4e_t *pml4, char *uva)
{
  pte_t *pte;

  pte = walkpml4(pml4, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pml4.
// Most useful when pml4 is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pml4e_t *pml4, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint64 n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint64)PGROUNDDOWN(va);
    pa0 = uva2ka(pml4, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

