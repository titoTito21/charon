/* Replaces musl's arch/x86_64/pthread_arch.h for Charon synthesizer modules.
 * Firmware gives no thread pointer (%fs is undefined in UEFI), so the
 * "current thread" is a static block initialised by synth_rt_init(). */
extern unsigned char __synth_thread_block[];

static inline uintptr_t __get_tp()
{
	return (uintptr_t)__synth_thread_block;
}

#define MC_PC gregs[REG_RIP]
