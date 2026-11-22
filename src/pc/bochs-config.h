// CharonPC: Bochs 3.1 configuration, written by hand from config.h.in
// (Bochs's configure is never run). The build copies this file to config.h.
// Target: x86-64 UEFI application, musl 1.2.5 headers, clang 23, no plugins.
// LGPL-2.1-or-later (as Bochs's config.h.in it derives from).
#ifndef _BX_CONFIG_H_
#define _BX_CONFIG_H_ 1

#define BX_EXIT(x)  SIM->quit_sim (x)

#define BX_DEBUG_LINUX 0
#define HAVE_LIBREADLINE 0
#define HAVE_READLINE_HISTORY_H 0
/* No HAVE_LOCALE_H: main.cc tests it with #ifdef. */
#define BX_TIMER_DEBUG 0
#define BX_SUPPORT_A20 1
#define BX_SHOW_IPS         0
#define MSVC_TARGET 0

#define BX_DMA_FLOPPY_IO 1
#define BX_DEFAULT_MEM_MEGS 32
// A Pentium Pro / II / III / 4 class machine (Windows 95 ... XP), 32-bit.
#define BX_CPU_LEVEL 6
#define BX_SUPPORT_X86_64 0
#define BX_PHY_ADDRESS_LONG 0
#define BX_HAVE_SLEEP 1
#define BX_HAVE_MSLEEP 0
#define BX_HAVE_USLEEP 1
#define BX_HAVE_NANOSLEEP 0
#define BX_HAVE_ABORT 1
#define BX_HAVE_SOCKLEN_T 1
#define BX_HAVE_SOCKADDR_IN_SIN_LEN 0
#define BX_HAVE_GETTIMEOFDAY 1
#define BX_HAVE_REALTIME_USEC (BX_HAVE_GETTIMEOFDAY)
#define BX_HAVE_MKSTEMP 0
#define BX_HAVE_SYS_MMAN_H 1   // mmap() exists and always fails: Bochs falls back to file reads
#define BX_HAVE_XPM_H 0
#define BX_HAVE_XRANDR_H 0
#define BX_HAVE_MKTIME 1
#define BX_HAVE_TMPFILE64 0
#define BX_HAVE_FSEEK64 0
#define BX_HAVE_FSEEKO64 0
#define BX_HAVE_NET_IF_H 0
#define BX_HAVE___BUILTIN_BSWAP32 1
#define BX_HAVE___BUILTIN_BSWAP64 1
#define BX_HAVE_SSIZE_T 1
#define BX_USE_IDLE_HACK 0
#define BX_MIN_IPS 1000000
#define BX_SMP_QUANTUM_MIN  1
#define BX_SMP_QUANTUM_MAX 32

#define BX_USE_CPU_SMF 1
#define BX_USE_MEM_SMF 1
#define BX_USE_HD_SMF       1
#define BX_USE_BIOS_SMF     1
#define BX_USE_CMOS_SMF     1
#define BX_USE_DMA_SMF      1
#define BX_USE_FD_SMF       1
#define BX_USE_KEY_SMF      1
#define BX_USE_PAR_SMF      1
#define BX_USE_PIC_SMF      1
#define BX_USE_PIT_SMF      1
#define BX_USE_SER_SMF      1
#define BX_USE_UM_SMF       1
#define BX_USE_VGA_SMF      1
#define BX_USE_SB16_SMF     1
#define BX_USE_ES1370_SMF   1
#define BX_USE_DEV_SMF      1
#define BX_USE_PCI_SMF      1
#define BX_USE_P2I_SMF      1
#define BX_USE_PIDE_SMF     1
#define BX_USE_PCIDEV_SMF   1
#define BX_USE_USB_UHCI_SMF 1
#define BX_USE_USB_OHCI_SMF 1
#define BX_USE_USB_EHCI_SMF 1
#define BX_USE_USB_XHCI_SMF 1
#define BX_USE_PCIPNIC_SMF  1
#define BX_USE_EFI_SMF      1
#define BX_USE_GAMEPORT_SMF 1
#define BX_USE_CIRRUS_SMF   1
#define BX_USE_GEFORCE_SMF  1
#define BX_USE_BUSM_SMF     1
#define BX_USE_ACPI_SMF     1

#define BX_PLUGINS  0
#define BX_HAVE_LTDL 0
#define BX_HAVE_DLFCN_H 0

#define BX_USE_RAW_SERIAL 0
#define BX_LARGE_RAMFILE 0
#define BX_MAX_ATA_CHANNEL 4

#define BX_DBG_MAX_VIR_BPOINTS 16
#define BX_DBG_MAX_LIN_BPOINTS 16
#define BX_DBG_MAX_PHY_BPOINTS 16
#define BX_DBG_MAX_WATCHPONTS  16
#define BX_MAX_PATH     256
#define BX_INFILE_DEPTH  10
#define BX_INCLUDE_CMD   "source"
#define BX_DBG_EXTENSIONS 0

// Display: the Charon GUI (src/pc/pc_gui.cc) is the only one.
#define BX_WITH_X11 0
#define BX_WITH_WIN32 0
#define BX_WITH_MACOS 0
#define BX_WITH_CARBON 0
#define BX_WITH_NOGUI 0
#define BX_WITH_TERM 0
#define BX_WITH_RFB 0
#define BX_WITH_VNCSRV 0
#define BX_WITH_AMIGAOS 0
#define BX_WITH_SDL 0
#define BX_WITH_SDL2 0
#define BX_WITH_WX 0
#define BX_WITH_CHARON 1
#define BX_USE_TEXTCONFIG 1
#define BX_USE_GUI_CONSOLE 0
#define BX_USE_WIN32CONFIG 0
#define WX_MSW_UNICODE 0
#define WX_GDK_VERSION 0
#define BX_HAVE_GTK_VERSION 0

#ifndef CDECL
#define CDECL
#endif
#define BOCHSAPI
#define BOCHSAPI_CYGONLY
#define BOCHSAPI_MSVCONLY

#define BX_DEFAULT_CONFIG_INTERFACE "textconfig"
#define BX_DEFAULT_DISPLAY_LIBRARY "charon"

#define WORDS_BIGENDIAN 0
#define SIZEOF_UNSIGNED_CHAR      1
#define SIZEOF_UNSIGNED_SHORT     2
#define SIZEOF_UNSIGNED_INT       4
#define SIZEOF_UNSIGNED_LONG      8
#define SIZEOF_UNSIGNED_LONG_LONG 8
#define SIZEOF_INT_P              8
#define BX_64BIT_CONSTANTS_USE_LL 1
#define BX_CONST64(x)  (x##LL)

typedef unsigned char Bit8u;
typedef   signed char Bit8s;
typedef unsigned short Bit16u;
typedef   signed short Bit16s;
typedef unsigned int Bit32u;
typedef   signed int Bit32s;
typedef unsigned long Bit64u;
typedef   signed long Bit64s;

#define GET32L(val64) ((Bit32u)(((Bit64u)(val64)) & 0xFFFFFFFF))
#define GET32H(val64) ((Bit32u)(((Bit64u)(val64)) >> 32))
#define GET64_FROM_HI32_LO32(hi, lo) (Bit64u(lo) | (Bit64u(hi) << 32))

typedef Bit32u bx_address;
typedef bx_address bx_lin_address;
typedef Bit32u bx_phy_address;
#define BX_PHY_ADDRESS_WIDTH 32

#define BX_MAX_BIT64U ( (Bit64u) -1           )
#define BX_MIN_BIT64U ( 0                     )
#define BX_MAX_BIT64S ( ((Bit64u) -1) >> 1    )
#define BX_MIN_BIT64S ( (Bit64s)(1ULL << 63)  )
#define BX_MAX_BIT32U ( (Bit32u) -1           )
#define BX_MIN_BIT32U ( 0                     )
#define BX_MAX_BIT32S ( ((Bit32u) -1) >> 1    )
#define BX_MIN_BIT32S ( (Bit32s)(1U << 31)    )
#define BX_MAX_BIT16U ( (Bit16u) -1           )
#define BX_MIN_BIT16U ( 0                     )
#define BX_MAX_BIT16S ( ((Bit16u) -1) >> 1    )
#define BX_MIN_BIT16S ( (Bit16s)-(((Bit16u) -1) >> 1) - 1)
#define BX_MAX_BIT8U  ( (Bit8u) -1            )
#define BX_MIN_BIT8U  ( 0                     )
#define BX_MAX_BIT8S  ( ((Bit8u) -1) >> 1     )
#define BX_MIN_BIT8S  ( (Bit8s)-(((Bit8u) -1) >> 1) - 1)

typedef Bit64u bx_ptr_equiv_t;
#define bx_ptr_t void *
#define BX_LITTLE_ENDIAN

#define BX_GUI_SIGHANDLER 0
#define HAVE_SIGACTION 0

#define BX_CPP_INLINE inline
#define BX_CPP_AlignN(n) __attribute__ ((aligned (n)))
#define BX_CPP_AttrPrintf(formatArg, firstArg) __attribute__ ((format (printf, formatArg, firstArg)))
#define BX_CPP_AttrNoReturn() __attribute__ ((noreturn))
#ifndef likely
#define likely(x)      __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x)    __builtin_expect(!!(x), 0)
#endif

#define BX_GDBSTUB      0
#define BX_DEBUGGER     0
#define BX_DEBUGGER_GUI 0
#define BX_INSTRUMENTATION    0
#define BX_NO_LOGGING 0
#define BX_ASSERT_ENABLE 0
#define BX_ENABLE_STATISTICS 0
#define BX_SUPPORT_ALIGNMENT_CHECK 1
#define BX_SUPPORT_FPU 1
#define BX_SUPPORT_3DNOW 0
#define BX_SUPPORT_PKEYS 0
#define BX_SUPPORT_CET 0
#define BX_SUPPORT_UINTR 0
#define BX_SUPPORT_FRED 0
#define BX_SUPPORT_MONITOR_MWAIT 0
#define BX_SUPPORT_PERFMON 0
#define BX_SUPPORT_MEMTYPE 0
#define BX_SUPPORT_SVM 0
#define BX_SUPPORT_VMX 0
#define BX_SUPPORT_AVX 0
#define BX_SUPPORT_EVEX 0
#define BX_SUPPORT_AMX 0
#define BX_SUPPORT_REPEAT_SPEEDUPS 1
#define BX_SUPPORT_HANDLERS_CHAINING_SPEEDUPS 1
#define BX_ENABLE_TRACE_LINKING 1
#define BX_CPU_VENDOR_INTEL 1
#define BX_CPUID_VENDOR_LEN 12
#define BX_CPUID_BRAND_LEN  48
#define BX_CONFIGURE_MSRS 0

#define BX_SUPPORT_SMP         0
#define BX_BOOTSTRAP_PROCESSOR 0
#define BX_MAX_SMP_THREADS_SUPPORTED 0xfe
#define BX_SUPPORT_APIC 1

#define BX_HAVE_GETENV 1
#define BX_HAVE_SETENV 0
#define BX_HAVE_SELECT 0
#define BX_HAVE_SNPRINTF 1
#define BX_HAVE_VSNPRINTF 1
#define BX_HAVE_STRTOULL 1
#define BX_HAVE_STRTOUQ 0
#define BX_HAVE_STRDUP 1
#define BX_HAVE_STRREV 0
#define BX_HAVE_STRICMP 0
#define BX_HAVE_STRCASECMP 1
#define BX_HAVE_COLOR_SET 0
#define BX_HAVE_MVHLINE 0
#define BX_HAVE_MVVLINE 0
#define BX_NO_ATTRIBUTES 0
#define GCC_ATTRIBUTE __attribute__
#define BX_FAST_FUNC_CALL 0
#define BX_CPP_AttrRegparmN(X)
#define BX_HAVE_SET 0
#define BX_HAVE_SET_H 0
#define BX_HAVE_MAP 0
#define BX_HAVE_MAP_H 0
#define BX_X86_DEBUGGER 0

// i440FX PCI chipset with PIIX IDE and ACPI (Windows 98 SE / ME / XP).
#define BX_SUPPORT_PCI 1
#define BX_SUPPORT_PCIDEV 0
#define BX_SUPPORT_CLGD54XX 0
#define BX_SUPPORT_GEFORCE 0
#define BX_SUPPORT_VOODOO 0
#define BX_SUPPORT_USB_UHCI 0
#define BX_SUPPORT_USB_OHCI 0
#define BX_SUPPORT_USB_EHCI 0
#define BX_SUPPORT_USB_XHCI 0
#define BX_SUPPORT_PCIUSB 0
#define BX_USB_DEBUGGER 0
#define BX_SUPPORT_BUSMOUSE 0
// ATAPI CD-ROM backed by an ISO image file (installation media).
#define BX_SUPPORT_CDROM 1
#define LOWLEVEL_CDAUDIO  0
#define LOWLEVEL_CDROM cdrom_base_c
// Stage 1: no network or sound card yet (src/pc/PLAN.txt).
#define BX_SUPPORT_NE2K 0
#define BX_SUPPORT_PCIPNIC 0
#define BX_SUPPORT_E1000 0
#define BX_NETWORKING 0
#define BX_NETMOD_FBSD    0
#define BX_NETMOD_LINUX   0
#define BX_NETMOD_WIN32   0
#define BX_NETMOD_TAP     0
#define BX_NETMOD_TUNTAP  0
#define BX_NETMOD_VDE     0
#define BX_NETMOD_SLIRP   0
#define BX_NETMOD_SOCKET  0
#define BX_HAVE_LIBSLIRP  0
#define BX_SUPPORT_SB16     1   /* r407: with the sound driver "charon" (src/pc/pc_sound.cc) */
#define BX_SUPPORT_ES1370   0
#define BX_SUPPORT_GAMEPORT 0
#define BX_SUPPORT_SOUNDLOW 1
#define BX_SOUND_LOWLEVEL_NAME "dummy"   /* the default; sound=sb16 selects "charon" */
#define BX_HAVE_LIBSAMPLERATE 0
#define BX_HAVE_SOXR_LSR 0
#define BX_HAVE_SOUND_ALSA  0
#define BX_HAVE_SOUND_OSS   0
#define BX_HAVE_SOUND_OSX   0
#define BX_HAVE_SOUND_PULSE 0
#define BX_HAVE_SOUND_SDL   0
#define BX_HAVE_SOUND_WIN   0
#define BX_SUPPORT_IODEBUG 0

#define BX_FLOPPY0_NAME "Floppy Disk 0"
#define BX_FLOPPY1_NAME "Floppy Disk 1"

#endif  // _BX_CONFIG_H
