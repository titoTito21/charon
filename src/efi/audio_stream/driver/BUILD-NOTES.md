# AudioDxe streaming extension: source acquisition

The pinned baseline and extended driver both compile successfully with MSVC
14.51 and NASM 2.16.03. Actual audio-device tests remain separate; successful
compilation and simulated DMA tests do not prove audible playback.

Resolved inputs (download hashes retained in
`work/charon-audio-driver-r347/sources.json` and companion dependency manifests):

- OpenCorePkg 1.0.8: `6fe4d15cb66596d99d8cfedce69d8fc6dad71886`.
- ocbuild: `020b14fa68a9825642e0c0fb643c2963bd679ae3`.
- AUDK: `dfd5516702b4911f1fc1919a429d637b456df913`.
- Brotli: `e230f474b87134e8c6c85b630084c612057f253e`.
- MipiSysT: `370b5944c046bab043dd8b133727b2135af7747a`.
- libfdt: `cfff805481bdea27f900c32698171286542b8d3c`.

The original extracted sources remain under `work/charon-audio-driver-r347`.
The actual build copy is `work/ha`, shortened because NMAKE rejected generated
paths under the archive's full commit name. No junction or system directory
modification was required. `prepare-driver-dsc.py` retains upstream library,
PCD and compiler mappings but selects only AudioDxe, omits the full firmware
FDF, and removes unrelated Network/Ext4 include fragments. This avoids pulling
unrelated Redfish packages solely through the platform's component list.

`build-host-baseline.cmd work/charon-audio-driver-r347` sets the explicit MSVC
VS2022-prefix path, HOST_ARCH=X64, Win64 BaseTools path, Python3.15 and NASM.
Host BaseTools alone suppresses MSVC14.51 warnings 4366 (packed HII GUID address
passed to CopyGuid) and 4319 (UINT32 firmware alignment mask widening). Driver
warning policy is unchanged. No remote bootstrap is executed.

Apply the extension to the build copy with:

```text
python3.15 outputs/nvda-haiku/src/efi/audio_stream/driver/apply-driver-extension.py work/charon-audio-driver-r347/opencore/OpenCorePkg-6fe4d15cb66596d99d8cfedce69d8fc6dad71886 work/ha
```

The script checks every patch anchor and records original file hashes in
PATCH-BASE-SHA256.json. Original license notices are preserved. Added queue and
controller code is in this directory. HakAudioRingTests.c passed 100000
simulated cursor-interval checks for FIFO, backpressure, wrap and padding drain.
It is a queue-accounting test, not a hardware audio test.

Revision 1 uses actual HDA LPIB progress, a temporary 8192-byte DMA ring with
two 4096-byte blocks, and a 2048-frame copy-in queue. The old 512 KiB DMA layout
is restored on successful Stop. Calls are serialized against the polling event
at TPL_NOTIFY. Poll gaps over 20 ms or no observed progress for 200 ms fault
instead of inventing consumption. The application must yield frequently enough
for firmware timers. The 128-frame final pipeline allowance derives from the
original driver's 512-byte padding; physical-controller drain latency still
requires verification. Restrict the initial format to 48 kHz stereo PCM16.

The current binary is unsigned build output; signing and normal firmware
LoadImage verification remain required. See BUILD-RESULT.json for its hash.

The existing binary provenance identifies OpenCorePkg release 1.0.8. Its actual
driver module is `Staging/AudioDxe/AudioDxe.inf`, not `Platform/AudioDxe`.
The module consumes MdePkg, UefiCpuPkg and OpenCorePkg plus OpenCore's
OcBootManagementLib, OcDeviceMiscLib, OcDevicePathLib, OcFlexArrayLib,
OcHdaDevicesLib, OcMp3Lib, OcStringLib and OcWaveLib. Preserve the module's
MIT notices and the licenses of all dependent libraries.

Official acquisition endpoints:

- https://api.github.com/repos/acidanthera/OpenCorePkg/git/ref/tags/1.0.8
- https://codeload.github.com/acidanthera/OpenCorePkg/tar.gz/refs/tags/1.0.8
- https://raw.githubusercontent.com/acidanthera/OpenCorePkg/1.0.8/Staging/AudioDxe/AudioDxe.inf
- https://raw.githubusercontent.com/acidanthera/OpenCorePkg/1.0.8/build_oc.tool
- https://api.github.com/repos/acidanthera/ocbuild/commits/master
- https://raw.githubusercontent.com/acidanthera/ocbuild/master/efibuild.sh
- https://api.github.com/repos/acidanthera/audk/commits/master

OpenCore's tagged build_oc.tool dynamically downloads ocbuild/master/efibuild.sh
(line 380). That script clones acidanthera/audk master as UDK (line 372) and
initializes its submodules (line 64). Consequently, the release tag alone does
not pin the build. Resolve and record full commits and archive hashes for all
three repositories and the AUDK submodules; inspect the pinned files rather
than execute an unpinned remote bootstrap. If historical binary reproduction
is needed, release-time dependency commits require separate evidence.

Supported upstream Windows build path uses Visual Studio and EDK2 BaseTools
with nmake, Python, NASM and IASL. Host MSVC 14.51 is installed at
`C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231`;
the corresponding vcvars64.bat exists. NASM was acquired separately; IASL was
not needed by this driver-only build. Plain `python` resolves to an unusable
Windows alias here; `python3.15` successfully built and ran these BaseTools.

After constructing a pinned workspace, building BaseTools and running its
edksetup, a narrow module build should take this form (toolchain tag must match
the acquired AUDK tools_def and installed compiler):

```text
build -a X64 -b RELEASE -t <verified-MSVC-tag> -p OpenCorePkg/OpenCorePkg.dsc -m OpenCorePkg/Staging/AudioDxe/AudioDxe.inf
```

First compile the unchanged driver. Only then add a separately negotiated
continuous-stream protocol, preserving AudioIo revision 4 behavior. Required
controller changes and the bounded DMA/mixer ownership contract are described
in `../README.md`. A protocol declaration alone does not implement streaming.
The rebuilt extended image must pass normal Secure Boot signing/authentication;
no Haiku system changes or firmware verification bypass are involved.

## r406: binding and output preparation for modern machines

`HakHdaCompat.h`, `HakHdaCompat.c` (controller) and `HakHdaCodecCompat.c`
(codec) are copied beside the stream extension; `apply-driver-extension.py`
adds their call sites to `HdaController.c`, `HdaCodec.c` and
`HdaCodecAudioIo.c` (every anchor is checked). What they do and what was and
was not verified: `../AUDIO-HARDWARE.txt`. Build as before, with the tools of
`work/charon-audio-driver-r347`:

```text
python3.15 apply-driver-extension.py work/charon-audio-driver-r347/opencore/OpenCorePkg-6fe4d15cb66596d99d8cfedce69d8fc6dad71886 work/ha
cmd /c build-host-baseline.cmd work\charon-audio-driver-r347   (set HAK_AUDIO_TRACE=1 and delete the Hak*.obj files for the trace variant)
```

Results: `work/charon-audio-driver-r406/AudioDxe-r406.efi` (SHA-256
12525748c4040c908f30414070b4345c0750106942af2f39ce758a36770f0a20) and
`AudioDxe-r406-trace.efi`; logs `work/build-audiodxe-r406.log` and
`work/build-audiodxe-r406trace.log`. A rebuild differs in two bytes (build
time). `BUILD-RESULT.json` still describes the r347 binary. Not tested on
hardware.

## r407: no shared controller

`HakHdaControllerMayShare()` (HakHdaCompat.c) was first edited into the build
copy by hand; since r408 `apply-driver-extension.py` carries that edit, so the
script again reproduces the build copy from the pinned tree.

## r408: codec families and machine quirks (GPL-2.0-or-later)

New files: `HakHdaQuirks.h`, `HakHdaQuirkMatch.h`, `HakHdaQuirks.c` and the
generated `HakHdaQuirkTable.h`. They hold logic and data ported from the Linux
kernel v7.3 (files under GPL-2.0-or-later only), so a driver built with them
is GPL-2.0-or-later as a whole. What they do, the sources, the safety rules
and the off-switch (`\EFI\HAK\audio-noquirks`, or `/DHAK_HDA_NO_QUIRKS`):
`../../AUDIO-CODECS.txt`.

Regenerate the table (kernel tree: sparse checkout of torvalds/linux at the
tag, kept in `work/linux-hda-v7.3/src`; `-I` because the tree is downloaded
text):

```text
python3.15 -I tools/gen-charon-hda-quirks.py work/linux-hda-v7.3/src src/efi/audio_stream/driver/HakHdaQuirkTable.h --tag v7.3 --commit c03c1a14507dfaf8b856c2608f9ac216021fc222
```

Call sites added by `apply-driver-extension.py` (anchors checked):
`HakHdaQuirkProbe` before `HdaCodecParsePorts` in `HdaCodec.c`,
`HakHdaQuirkFinish` after the output paths in `HdaCodecAudioIo.c`, the four
files, `BaseLib` and `gEfiSimpleFileSystemProtocolGuid` in `AudioDxe.inf`.

Build as before, inside the workspace lock `work/ha/.charon-build.lock`
(two edk2 builds at once in one workspace corrupt each other): in PowerShell
remove the environment variable `NoDefaultCurrentDirectoryInExePath`, put the
Visual Studio Installer directory on `PATH`, delete the stale `.obj` files of
the changed sources, apply the extension, then from `work`:

```text
cmd /c charon-src-r395\src\efi\audio_stream\driver\build-host-baseline.cmd charon-audio-driver-r347
```

Result: `work/charon-audio-driver-r408/AudioDxe-r408.efi` (208896 bytes,
SHA-256 910ada692e0cf72ceed7d2f6c3809b25b51fe3f5db17205f8dc155d89a84726b),
log beside it. Checked in one QEMU run (emulated HD Audio, sound measured);
not tested on hardware.

## r409: the poll clock starts when the stream runs (candidate, see tests/charon-r409-audio-stall-results.txt)

`HakWrite` took `LastTick` before `HdaControllerSetStreamState(TRUE)`. VMware
creates its host sound stream inside that register write, on the processor's
thread: 23..48 ms on an idle host (measured with a trace build, 25 starts),
all of it counted against `HAK_RING_MAX_POLL_NS` (70 ms). The cursor (LPIB)
read right after the call was 0 in every measured start. r409 reads the cursor
when the call returns and, if it is 0, takes `LastTick`/`LastProgressTick`
there; a cursor that has moved keeps the clock taken before the call. Later
polls and both limits are unchanged.

Trace builds (`HAK_AUDIO_TRACE=1`) also print "HAK start run ns", "HAK start
cursor" and gap/read time of the first three polls, once after the third poll
or at a fault. Test builds only: `-DHAK_AUDIO_STARTDELAY_MS=n` (a Stall before
the RUN write, a host that needs that long) and `-DHAK_AUDIO_OLDSTART=1` (the
clock as up to r408), passed in the environment variable `CL`.

The EDK2 build does not notice a changed `_CL_`/`CL`: delete
`work/ha/Build/HakAudioDriver/RELEASE_VS2022/X64/OpenCorePkg/Staging/AudioDxe`
before switching between a trace and a normal build. From a shell that sets
`NoDefaultCurrentDirectoryInExePath`, clear that variable first (the script
calls `edksetup.bat` from the current directory).

Result: `work/charon-audio-driver-r409/AudioDxe-r409.efi` (208896 bytes; two
builds of the same source differ in two bytes), logs beside it. Checked in
VMware and QEMU/OVMF; not tested on hardware.
