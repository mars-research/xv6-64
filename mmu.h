// This file contains definitions for the
// x86-64 memory management unit (MMU).

// Rflags register
#define FL_IF           0x00000200      // Interrupt Enable

// Control Register flags
#define CR0_PE          0x00000001      // Protection Enable
#define CR0_WP          0x00010000      // Write Protect
#define CR0_PG          0x80000000      // Paging

#define CR4_PSE         0x00000010      // Page size extension
#define CR4_PAE         0x00000020      // Physical Address Extension

// IA32_EFER (Extended Feature Enable Register) MSR (Model-Specific Register)
#define EFER_MSR        0xC0000080
#define EFER_MSR_LME    0x00000100      // IA-32e Mode Enable

// various segment selectors.
#define SEG_KCODE 1  // kernel code
#define SEG_KDATA 2  // kernel data+stack
#define SEG_UCODE 3  // user code
#define SEG_UDATA 4  // user data+stack
#define SEG_TSS   5  // this process's task state

// cpu->gdt[NSEGS] holds the above segments.
#define NSEGS     6

#ifndef __ASSEMBLER__
// Segment Descriptor
// In IA_32e mode, only type, s, dpl, p, and l are effective
struct segdesc {
    uint lim_15_0 : 16;  // Low bits of segment limit
    uint base_15_0 : 16; // Low bits of segment base address
    uint base_23_16 : 8; // Middle bits of segment base address
    uint type : 4;       // Segment type (see STS_ constants)
    uint s : 1;          // 0 = system, 1 = application
    uint dpl : 2;        // Descriptor Privilege Level
    uint p : 1;          // Present
    uint lim_19_16 : 4;  // High bits of segment limit
    uint avl : 1;        // Unused (available for software use)
    uint l : 1;          // 64-bit code segment
    uint db : 1;         // 0 = 16-bit segment, 1 = 32-bit segment, 0 in IA-32e mode
    uint g : 1;          // Granularity: limit scaled by 4K when set
    uint base_31_24 : 8; // High bits of segment base address
};

// Normal segment
#define SEG64(type, base, lim, dpl, code) (struct segdesc)    \
{ ((lim) >> 12) & 0xffff, (uint)(base) & 0xffff,              \
  ((uint)(base) >> 16) & 0xff, type, 1, dpl, 1,               \
  (uint)(lim) >> 28, 0, code, 0, 1, (uint)(base) >> 24 }
#endif

#define DPL_USER    0x3     // User DPL

// Application segment type bits
#define STA_X       0x8     // Executable segment
#define STA_W       0x2     // Writeable (non-executable segments)
#define STA_R       0x2     // Readable (executable segments)

// System segment type bits
#define STS_T64A    0x9     // Available 64-bit TSS
#define STS_IG64    0xE     // 64-bit Interrupt Gate
#define STS_TG64    0xF     // 64-bit Trap Gate

// A virtual address 'la' has a six-part structure as follows:
//
// +---------16----------+--------9-------+-------9--------+--------9-------+-------9--------+---------12----------+
// |                     |    Page Map    | Page Directory |                |                |                     |
// |       Ignored       |     Level 4    |  Pointer Table | Page Directory |   Page Table   | Offset within Page  |
// |                     |     Index      |     Index      |      Index     |      Index     |                     |
// +---------------------+----------------+----------------+----------------+----------------+---------------------+
//                        \-- PML4X(va) -/ \-- PDPTX(va) -/ \--- PDX(va) --/ \--- PTX(va) --/

// page map level 4 index
#define PML4X(va)       (((uint64)(va) >> PML4SHIFT) & 0x1FF)

// page directory pointer table index
#define PDPTX(va)       (((uint64)(va) >> PDPTSHIFT) & 0x1FF)

// page directory index
#define PDX(va)         (((uint64)(va) >> PDXSHIFT) & 0x1FF)

// page table index
#define PTX(va)         (((uint64)(va) >> PTXSHIFT) & 0x1FF)

// construct virtual address from indexes and offset
#define PGADDR(l4, dpt, d, t, o) ((uint64)(((l4)  & 0x1FF) << PML4SHIFT |\
                                           ((dpt) & 0x1FF) << PDPTSHIFT |\
                                           ((d)   & 0x1FF) << PDXSHIFT  |\
                                           ((t)   & 0x1FF) << PTXSHIFT  |\
                                           ((o)   & 0xFFF)))

// Page directory and page table constants.
#define NPML4ENTRIES    512     // # page map level 4 entries
#define NPDPTENTRIES    512     // # page directory pointer table entries
#define NPDENTRIES      512     // # directory entries per page directory
#define NPTENTRIES      512     // # PTEs per page table
#define PGSIZE          4096    // bytes mapped by a page

#define PTXSHIFT        12      // offset of PTX in a linear address
#define PDXSHIFT        21      // offset of PDX in a linear address
#define PDPTSHIFT       30      // offset of PDPT in a linear address
#define PML4SHIFT       39      // offset of PML4 in a linear address

#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))

// Page table/directory entry flags.
#define PTE_P           0x001   // Present
#define PTE_W           0x002   // Writeable
#define PTE_U           0x004   // User
#define PTE_PS          0x080   // Page Size

// Address in page table or page directory entry
#define PTE_ADDR(pte)   ((uint64)(pte) & ~0xFFF)
#define PTE_FLAGS(pte)  ((uint64)(pte) &  0xFFF)

#ifndef __ASSEMBLER__
typedef uint64 pte_t;
typedef uint64 pde_t;
typedef uint64 pdpte_t;
typedef uint64 pml4e_t;

// Task state segment format
struct taskstate {
    uint link;         // Old ts selector
    uint esp0;         // Stack pointers and segment selectors
    ushort ss0;        //   after an increase in privilege level
    ushort padding1;
    uint *esp1;
    ushort ss1;
    ushort padding2;
    uint *esp2;
    ushort ss2;
    ushort padding3;
    void *cr3;         // Page directory base
    uint *eip;         // Saved state from last task switch
    uint eflags;
    uint eax;          // More saved state (registers)
    uint ecx;
    uint edx;
    uint ebx;
    uint *esp;
    uint *ebp;
    uint esi;
    uint edi;
    ushort es;         // Even more saved state (segment selectors)
    ushort padding4;
    ushort cs;
    ushort padding5;
    ushort ss;
    ushort padding6;
    ushort ds;
    ushort padding7;
    ushort fs;
    ushort padding8;
    ushort gs;
    ushort padding9;
    ushort ldt;
    ushort padding10;
    ushort t;          // Trap on task switch
    ushort iomb;       // I/O map base address
};

// Gate descriptors for interrupts and traps
struct gatedesc {
    uint off_15_0 : 16;   // low 16 bits of offset in segment
    uint cs : 16;         // code segment selector
    uint ist : 3;         // interrupt stack table
    uint args : 2;        // # args, 0 for interrupt/trap gates
    uint rsv1 : 3;        // reserved(should be zero I guess)
    uint type : 4;        // type(STS_{IG32,TG32})
    uint s : 1;           // must be 0 (system)
    uint dpl : 2;         // descriptor(meaning new) privilege level
    uint p : 1;           // Present
    uint off_31_16 : 16;  // bits 16-31 of offset in segment
    uint off_63_32;       // high bits of offset in segment
    uint rsv2;            // reserved(should be zero I guess)
};

// Set up a normal interrupt/trap gate descriptor.
// - istrap: 1 for a trap (= exception) gate, 0 for an interrupt gate.
//   interrupt gate clears FL_IF, trap gate leaves FL_IF alone
// - sel: Code segment selector for interrupt/trap handler
// - off: Offset in code segment for interrupt/trap handler
// - dpl: Descriptor Privilege Level -
//        the privilege level required for software to invoke
//        this interrupt/trap gate explicitly using an int instruction.
#define SETGATE(gate, istrap, sel, off, d)                \
{                                                         \
  (gate).off_15_0 = (uint)((off) & 0xffff);               \
  (gate).cs = (sel);                                      \
  (gate).args = 0;                                        \
  (gate).rsv1 = 0;                                        \
  (gate).type = (istrap) ? STS_TG64 : STS_IG64;           \
  (gate).s = 0;                                           \
  (gate).dpl = (d);                                       \
  (gate).p = 1;                                           \
  (gate).off_31_16 = (uint)(((off) >> 16) & 0xffff);      \
  (gate).off_63_32 = (uint)((off) >> 32);                 \
  (gate).rsv2 = 0;                                        \
}

#endif
