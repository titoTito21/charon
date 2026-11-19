/* Charon synthesizer modules: x86-64 SysV stack switch. GPL-2.0-or-later.
 * void synth_coro_switch(void** save_sp, void* load_sp)
 * Saves the callee-saved registers, MXCSR and the x87 control word on the
 * current stack, stores the stack pointer to *save_sp and resumes the stack
 * load_sp saved the same way (or prepared by synth_coro_create). */
.text
.globl synth_coro_switch
.type synth_coro_switch,@function
synth_coro_switch:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $8,%rsp
    stmxcsr (%rsp)
    fnstcw 4(%rsp)
    movq %rsp,(%rdi)
    movq %rsi,%rsp
    ldmxcsr (%rsp)
    fldcw 4(%rsp)
    addq $8,%rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.size synth_coro_switch,.-synth_coro_switch

/* First activation: rbx = entry, r12 = argument (popped above). The stack
 * is 16-byte aligned here, so the call leaves the callee correctly aligned. */
.globl synth_coro_start
.type synth_coro_start,@function
synth_coro_start:
    movq %r12,%rdi
    callq *%rbx
    ud2
.size synth_coro_start,.-synth_coro_start
.section .note.GNU-stack,"",@progbits
