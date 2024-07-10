#include "mmu.h"

  # vectors.S sends all traps here.
.globl alltraps
alltraps:
  # Build trap frame.
  pushq %r15
  pushq %r14
  pushq %r13
  pushq %r12
  pushq %r11
  pushq %r10
  pushq %r9
  pushq %r8
  pushq %rdi
  pushq %rsi
  pushq %rbp
  pushq %rdx
  pushq %rcx
  pushq %rbx
  pushq %rax
  
  # Call trap(tf), where tf=%esp
  movq %rsp, %rdi
  call trap

  # Return falls through to trapret...
.globl trapret
trapret:
  popq %rax
  popq %rbx
  popq %rcx
  popq %rdx
  popq %rbp
  popq %rsi
  popq %rdi
  popq %r8
  popq %r9
  popq %r10
  popq %r11
  popq %r12
  popq %r13
  popq %r14
  popq %r15
  addq $16, %rsp  # trapno and errcode
  iretq
