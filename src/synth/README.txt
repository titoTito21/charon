Charon loadable synthesizer modules (r393)
=========================================

ABI: src/efi/CharonSynthModule.h (fixed). Build, stage and test on Windows:

  python tools/build-charon-synths.py NEW_BUILD_DIR --stage NEW_STAGE_DIR --test

Result: NEW_STAGE_DIR/EFI/HAK/synths/{espeak,rhvoice}.tts, .manifest, the
data packs espeak/espeak-ng-data.chsp and rhvoice/rhvoice.chsp, LICENCES.txt,
licences/, and NEW_STAGE_DIR/test-wav (WAVs + host-test-results.json).
Copy EFI/HAK/synths to Charon's disk; for test machines
CHARON_SYNTHS=NEW_STAGE_DIR work/charon-vm.py prepare ... does it.

Layout
  common/   freestanding runtime shared by the modules
    synth_rt.h        runtime API (platform, VFS, coroutines, diagnostics)
    synth_efi.cpp     efi_main of a boot service driver (subsystem 11, ABI 2):
                      data folder from LoadOptions, files from the module's
                      own volume, only the data index is checked; protocol on
                      a NEW handle; the firmware keeps the driver resident.
    synth_malloc.c    malloc family (size classes over AllocatePool
                      EfiBootServicesData, budget 62 MiB)
    synth_posix.c     OS side of musl 1.2.5: read-only fopen over the VFS,
                      discarding stdout/stderr, opendir/stat, C/C.UTF-8
                      locale only, no-op locks and pthread mutexes,
                      abort()/exit() fail the engine coroutine, no syscalls
    synth-musl-arch/  pthread_arch.h: static thread block (no %fs in UEFI)
    synth_pack.c      CHSP packs: LZMA (LZMA SDK LzmaDec, public domain),
                      reversible float32 byte-plane ranges, CRC-32 check
    synth_coro*.{c,S} coroutines on private 512 KiB/1 MiB stacks; Render
                      resumes the engine inside its own audio callback
    synth_ctors.c     explicit .init_array run (rhvoice only; no firmware
                      or loader runs constructors)
  espeak/   espeak_module.cpp  (eSpeak NG unmodified, synchronous mode)
  rhvoice/  rhvoice_module.cpp (RHVoice C API), patches/ (see README there)
  host/     Windows test programs: tts-host-loader.cpp loads the shipped PE
            file, emulates the boot services the module uses, drives
            Begin/Render/Cancel and writes WAVs; espeak-reference.c and
            rhvoice-reference.cpp build the same engine sources hosted (MSVC
            runtime) for a sample-exact comparison; espeak-compile-data.c
            compiles eSpeak NG data with the library's own compilers.

ABI 2: Prepare(voice) loads one voice: eSpeak mounts espeak-ng-data.chsp
(phoneme tables, once) and espeak-pl.chsp or espeak-en.chsp; RHVoice mounts
rhvoice-pl.chsp (Polish + Natan) or rhvoice-en.chsp (English + Slt) and
creates one RHVoice engine per voice. Both voices stay prepared if they fit;
RHVoice releases the other voice only when preparing runs out of memory.
Begin on an unprepared voice returns -3. Prepare/Begin/Render/Cancel are
ms_abi (CHARON_SYNTH_API).

Text switches: ESC 'p'/'e' select the pl/en voice (if prepared) for the following text;
ESC 'v' (+4 bytes), ESC 'f' (+6) and ESC 'i' (+2) are skipped.

Rate/pitch/volume: eSpeak 175 wpm x rate (80..450), pitch 50 x pitch,
volume 100 x volume; RHVoice relative_rate/pitch/volume.

Failure reporting: efi_main returns EFI_NOT_FOUND (data pack missing),
EFI_VOLUME_CORRUPTED (pack corrupt), EFI_UNSUPPORTED (voices unusable),
EFI_OUT_OF_RESOURCES, EFI_LOAD_ERROR (engine failure); the reason text is in
the module's synth_diagnostic[] (and the engine's last stderr bytes in
synth_stderr_tail[]); nothing is ever printed.
