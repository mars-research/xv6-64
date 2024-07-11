# The xv6 kernel starts executing in this file. This file is linked with
# the kernel C code, so it can refer to kernel symbols such as main().
# The boot block (bootasm.S and bootmain.c) jumps to entry below.
        
# Multiboot header, for multiboot boot loaders like GNU Grub.
# http://www.gnu.org/software/grub/manual/multiboot/multiboot.html
#
# Using GRUB 2, you can boot xv6 from a file stored in a
# Linux file system by copying kernel or kernelmemfs to /boot
# and then adding this menu entry:
#
# menuentry "xv6" {
# 	insmod ext2
# 	set root='(hd0,msdos1)'
# 	set kernel='/boot/kernel'
# 	echo "Loading ${kernel}..."
# 	multiboot ${kernel} ${kernel}
# 	boot
# }

#include "asm.h"
#include "memlayout.h"
#include "mmu.h"
#include "param.h"

.code32
# Multiboot header.  Data to direct multiboot loader.
.p2align 2
.text
.globl multiboot_header
multiboot_header:
  #define magic 0x1badb002
  #define flags 0
  .long magic
  .long flags
  .long (-magic-flags)

# By convention, the _start symbol specifies the ELF entry point.
# Since we haven't set up virtual memory yet, our entry point is
# the physical address of 'entry'.
.globl _start
_start = V2P_WO(entry)

# Entering xv6 on boot processor, with paging off, in protected mode.
.globl entry
entry:
  # Enable physical-address extensions (PAE).
  movl    %cr4, %eax
  orl     $(CR4_PAE), %eax
  movl    %eax, %cr4

  # Complete the initialization of the Page Map Level 4 table (PML4) with:
  #   PML4[0]               = @identitymap  | PTE_P | PTE_W
  #   PML4[PML4X(KERNBASE)] = @kernmap      | PTE_P | PTE_W
  movl    $(V2P_WO(entrypml4)), %eax

  movl    $(V2P_WO(identitymap)), %ebx
  orl     $(PTE_P|PTE_W), %ebx
  movl    $0, %ecx
  movl    %ebx, (%eax,%ecx,8) # equivalent to movl %ebx, (%eax)

  movl    $(V2P_WO(kernmap)), %ebx
  orl     $(PTE_P|PTE_W), %ebx
  movl    $(PML4X_WO(KERNBASE)), %ecx
  movl    %ebx, (%eax,%ecx,8)

  # Load CR3 with the physical address of PML4.
  movl    %eax, %cr3

  # Enable IA-32e mode by setting IA32_EFER.LME = 1.
  movl    $EFER_MSR, %ecx
  rdmsr
  orl     $EFER_MSR_LME, %eax
  wrmsr

  # Enable paging. This causes the processor to set the IA32_EFER.LMA bit to 1.
  movl    %cr0, %eax
  orl     $CR0_PG, %eax
  movl    %eax, %cr0

  # We are now in the 32-bit compatibility submode of IA-32e mode.
  # To complete the transition to 64-bit submode, we have to load a gdt with the 64-bit flag set
  # (in the code segment), and then use a far jump to reload %cs and %rip.
  lgdt    (V2P_WO(gdt64desc))
  ljmp    $(SEG_KCODE<<3), $(V2P_WO(start64))



.p2align 4
.code64 # Tell assembler to generate 64-bit code now.
start64:
  # Jump to main(), and switch to executing at
  # high addresses. The indirect call is needed because
  # the assembler produces a PC-relative instruction
  # for a direct jump.
  movabs  $(stack + KSTACKSIZE), %rsp
  movabs  $main, %rax
  jmp     *%rax


# Bootstrap GDT64
.data
.comm stack, KSTACKSIZE

.p2align 4                                # force 16 byte alignment
gdt64:
  SEG_NULLASM                             # null seg
  SEG64_ASM(STA_X|STA_R, SEG64_CODE)      # code seg
  SEG64_ASM(STA_W, SEG64_OTHER)           # data seg

gdt64desc:
  .word   (gdt64desc - gdt64 - 1)         # sizeof(gdt64) - 1
  .quad   V2P_WO(gdt64)                   # address gdt64