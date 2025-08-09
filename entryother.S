#include "asm.h"
#include "memlayout.h"
#include "mmu.h"
	
# Each non-boot CPU ("AP") is started up in response to a STARTUP
# IPI from the boot CPU.  Section B.4.2 of the Multi-Processor
# Specification says that the AP will start in real mode with CS:IP
# set to XY00:0000, where XY is an 8-bit value sent with the
# STARTUP. Thus this code must start at a 4096-byte boundary.
#
# Because this code sets DS to zero, it must sit
# at an address in the low 2^16 bytes.
#
# Startothers (in main.c) sends the STARTUPs one at a time.
# It copies this code (start) at 0x7000.  It puts the address of
# a newly allocated per-core stack in start-8,the address of the
# place to jump to (mpenter) in start-16, and the physical address
# of entrypml4 in start-20.
#
# This code combines elements of bootasm.S and entry.S.

.code16           
.globl start
start:
  cli            

  # Zero data segment registers DS, ES, and SS.
  xorw    %ax,%ax
  movw    %ax,%ds
  movw    %ax,%es
  movw    %ax,%ss

  # Switch from real to protected mode.  Use a bootstrap GDT that makes
  # virtual addresses map directly to physical addresses so that the
  # effective memory map doesn't change during the transition.
  lgdt    gdtdesc
  movl    %cr0, %eax
  orl     $CR0_PE, %eax
  movl    %eax, %cr0

  # Complete the transition to 32-bit protected mode by using a long jmp
  # to reload %cs and %eip.  The segment descriptors are set up with no
  # translation, so that the mapping is still the identity mapping.
  ljmpl    $(SEG_KCODE<<3), $(start32)

//PAGEBREAK!
.code32  # Tell assembler to generate 32-bit code now.
start32:
  # Set up the protected-mode data segment registers
  movw    $(SEG_KDATA<<3), %ax    # Our data segment selector
  movw    %ax, %ds                # -> DS: Data Segment
  movw    %ax, %es                # -> ES: Extra Segment
  movw    %ax, %ss                # -> SS: Stack Segment
  movw    $0, %ax                 # Zero segments not ready for use
  movw    %ax, %fs                # -> FS
  movw    %ax, %gs                # -> GS

  # Enable physical-address extensions (PAE).
  movl    %cr4, %eax
  orl     $(CR4_PAE), %eax
  movl    %eax, %cr4

  # Use entrypml4 as our initial page table
  movl    (start-20), %eax
  movl    %eax, %cr3

  # Enable IA-32e mode by setting IA32_EFER.LME = 1.
  movl    $EFER_MSR, %ecx
  rdmsr
  orl     $EFER_MSR_LME, %eax
  wrmsr

  # Enable paging. 
  # This causes the processor to set the IA32_EFER.LMA bit to 1.
  movl    %cr0, %eax
  orl     $CR0_PG, %eax
  movl    %eax, %cr0

  # We are now in the 32-bit compatibility submode of IA-32e mode.
  # To complete the transition to 64-bit submode, we have to load a gdt 
  # with the 64-bit flag set (in the code segment), and then use 
  # a far jump to reload %cs and %rip.
  lgdt    gdt64desc
  ljmp    $(SEG_KCODE<<3), $(start64)

  .p2align 4
  .code64 # Tell assembler to generate 64-bit code now.
  start64:
    # Switch to the stack allocated by startothers()
    movq    (start-8), %rsp
    # Call mpenter()
    call	 *(start-16)

  movw    $0x8a00, %ax
  movw    %ax, %dx
  outw    %ax, %dx
  movw    $0x8ae0, %ax
  outw    %ax, %dx
spin:
  jmp     spin

.p2align 2
gdt:
  SEG_NULLASM
  SEG_ASM(STA_X|STA_R, 0, 0xffffffff)
  SEG_ASM(STA_W, 0, 0xffffffff)


gdtdesc:
  .word   (gdtdesc - gdt - 1)
  .long   gdt

.p2align 4
gdt64:
  SEG_NULLASM
  SEG64_ASM(STA_X|STA_R, SEG64_CODE)
  SEG64_ASM(STA_W, SEG64_OTHER)

gdt64desc:
  .word   (gdt64desc - gdt64 - 1)
  .quad   gdt64

