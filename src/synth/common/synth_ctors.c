/* Charon synthesizer modules: explicit static construction. GPL-2.0-or-later.
 * No C runtime runs .init_array in firmware. A module with C++ static
 * objects (RHVoice, libc++) calls synth_run_constructors() once from its
 * entry point, on the engine stack, before any other engine code. The
 * bounds are defined by the module linker script. Destructors never run:
 * modules stay resident until the firmware hands over to an OS. */
typedef void (*constructor)(void);
extern constructor __init_array_start[];
extern constructor __init_array_end[];

void synth_run_constructors(void);
void synth_run_constructors(void)
{
    for (constructor* p = __init_array_start; p < __init_array_end; ++p)
        (*p)();
}
