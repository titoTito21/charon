/* Charon synthesizer modules: coroutines on private stacks. GPL-2.0-or-later.
 * One coroutine runs at a time; nesting is not supported (not needed). */
#include <stdlib.h>
#include <string.h>
#include "synth_rt.h"

void synth_coro_switch(void** save_sp, void* load_sp);
void synth_coro_start(void);

enum { CORO_READY, CORO_RUNNING, CORO_SUSPENDED, CORO_DONE, CORO_FAILED };

struct synth_coro {
    void* sp;
    void* caller_sp;
    void* stack;
    synth_coro_entry entry;
    void* argument;
    int state;
};

static synth_coro* current;

static void trampoline(void* argument)
{
    synth_coro* c = argument;
    c->entry(c->argument);
    c->state = CORO_DONE;
    current = NULL;
    synth_coro_switch(&c->sp, c->caller_sp);
    __builtin_trap();
}

synth_coro* synth_coro_create(size_t stack_bytes, synth_coro_entry entry, void* argument)
{
    synth_coro* c = calloc(1, sizeof *c);
    if (!c) return NULL;
    stack_bytes = (stack_bytes + 15) & ~(size_t)15;
    c->stack = malloc(stack_bytes);
    if (!c->stack) { free(c); return NULL; }
    c->entry = entry;
    c->argument = argument;
    uintptr_t top = ((uintptr_t)c->stack + stack_bytes) & ~(uintptr_t)15;
    uint64_t* sp = (uint64_t*)(top - 64);
    uint32_t controls[2];
    __asm__ volatile("stmxcsr %0" : "=m"(controls[0]));
    __asm__ volatile("fnstcw %0" : "=m"(*(uint16_t*)&controls[1]));
    controls[1] &= 0xffff;
    memcpy(&sp[0], controls, 8);
    sp[1] = 0;                         /* r15 */
    sp[2] = 0;                         /* r14 */
    sp[3] = 0;                         /* r13 */
    sp[4] = (uint64_t)(uintptr_t)c;    /* r12: argument of trampoline */
    sp[5] = (uint64_t)(uintptr_t)trampoline; /* rbx: entry */
    sp[6] = 0;                         /* rbp */
    sp[7] = (uint64_t)(uintptr_t)synth_coro_start;
    c->sp = sp;
    c->state = CORO_READY;
    return c;
}

int synth_coro_resume(synth_coro* c)
{
    if (!c || current) return -1;
    if (c->state == CORO_DONE) return 0;
    if (c->state == CORO_FAILED) return -1;
    current = c;
    c->state = CORO_RUNNING;
    synth_coro_switch(&c->caller_sp, c->sp);
    current = NULL;
    if (c->state == CORO_RUNNING) c->state = CORO_SUSPENDED;
    return c->state == CORO_DONE ? 0 : c->state == CORO_FAILED ? -1 : 1;
}

void synth_coro_yield(void)
{
    synth_coro* c = current;
    if (!c) return;
    current = NULL;
    synth_coro_switch(&c->sp, c->caller_sp);
}

int synth_coro_active(void) { return current != NULL; }

void synth_coro_fail(void)
{
    synth_coro* c = current;
    if (!c) {
        /* Outside any coroutine nothing can be unwound safely. */
        for (;;) __asm__ volatile("hlt");
    }
    {
        char text[sizeof synth_diagnostic];
        const char prefix[] = "engine aborted after: ";
        size_t n = sizeof prefix - 1, m = strlen(synth_diagnostic);
        if (n + m >= sizeof text) m = sizeof text - n - 1;
        memcpy(text, prefix, n);
        memcpy(text + n, synth_diagnostic, m);
        text[n + m] = 0;
        synth_diagnose(text);
    }
    c->state = CORO_FAILED;
    current = NULL;
    for (;;) synth_coro_switch(&c->sp, c->caller_sp);
}

void synth_coro_destroy(synth_coro* c)
{
    if (!c) return;
    free(c->stack);
    free(c);
}
