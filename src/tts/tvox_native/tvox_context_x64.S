/* MIT-0. GCC x86_64 SysV context switch, including SSE/FPU state.
 * Invoke from ordinary SysV C++; EFI protocol entry thunks keep their ms_abi.
 * This routine never calls firmware, the scheduler, or another language runtime.
 */
.text
.globl tvox_context_swap
.type tvox_context_swap,@function
tvox_context_swap:
 movq %rsp,0(%rdi)
 movq %rbx,8(%rdi)
 movq %rbp,16(%rdi)
 movq %r12,24(%rdi)
 movq %r13,32(%rdi)
 movq %r14,40(%rdi)
 movq %r15,48(%rdi)
 movq %rdi,56(%rdi)
 movq %rsi,64(%rdi)
 movdqu %xmm6,80(%rdi)
 movdqu %xmm7,96(%rdi)
 movdqu %xmm8,112(%rdi)
 movdqu %xmm9,128(%rdi)
 movdqu %xmm10,144(%rdi)
 movdqu %xmm11,160(%rdi)
 movdqu %xmm12,176(%rdi)
 movdqu %xmm13,192(%rdi)
 movdqu %xmm14,208(%rdi)
 movdqu %xmm15,224(%rdi)
 stmxcsr 240(%rdi)
 fnstcw 244(%rdi)
 movq %rsi,%rax
 movq 0(%rax),%rsp
 movq 8(%rax),%rbx
 movq 16(%rax),%rbp
 movq 24(%rax),%r12
 movq 32(%rax),%r13
 movq 40(%rax),%r14
 movq 48(%rax),%r15
 movq 56(%rax),%rdi
 movq 64(%rax),%rsi
 movdqu 80(%rax),%xmm6
 movdqu 96(%rax),%xmm7
 movdqu 112(%rax),%xmm8
 movdqu 128(%rax),%xmm9
 movdqu 144(%rax),%xmm10
 movdqu 160(%rax),%xmm11
 movdqu 176(%rax),%xmm12
 movdqu 192(%rax),%xmm13
 movdqu 208(%rax),%xmm14
 movdqu 224(%rax),%xmm15
 ldmxcsr 240(%rax)
 fldcw 244(%rax)
 ret
.size tvox_context_swap,.-tvox_context_swap
.globl tvox_context_controls
.type tvox_context_controls,@function
tvox_context_controls:
 stmxcsr 240(%rdi)
 fnstcw 244(%rdi)
 ret
.size tvox_context_controls,.-tvox_context_controls
.section .note.GNU-stack,"",@progbits
