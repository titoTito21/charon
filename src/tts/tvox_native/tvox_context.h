/* MIT-0. x86_64 stack context; no OS or firmware calls. */
#pragma once
#include <stdint.h>
#include <stddef.h>
struct alignas(16) TVoxContext {
    uintptr_t rsp,rbx,rbp,r12,r13,r14,r15,rdi,rsi,pad;
    unsigned char xmm[10][16];
    uint32_t mxcsr;
    uint16_t x87control;
};
static_assert(offsetof(TVoxContext,xmm)==80 && offsetof(TVoxContext,mxcsr)==240,"assembly ABI");
extern "C" void tvox_context_swap(TVoxContext* from,const TVoxContext* to);
extern "C" void tvox_context_controls(TVoxContext* target);
