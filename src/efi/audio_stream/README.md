# Continuous pre-OS audio: integration requirement

Status (r347 development): an additive continuous-output implementation now
exists in `driver/`, with its build provenance and limitations recorded in
`driver/BUILD-NOTES.md` and `driver/BUILD-RESULT.json`. The driver has compiled
successfully and its queue model has passed tests; actual EFI playback remains
an outstanding integration gate. The audit below describes the original
AudioIo revision 4 interface, which by itself cannot provide bounded live
speech replacement while background music continues.

## Verified existing behavior

Local evidence under `work/efi-audio-r336/`:

- `AudioIo.h:238` onward: StartPlaybackAsync takes a finite buffer and a final
  completion callback at TPL_NOTIFY. There is no append, refill, consumed-frame
  counter or cancellation of just one mixed source.
- `HdaCodecAudioIo.c:840-886`: StartPlaybackAsync forwards that finite buffer
  to the controller's StartStream.
- `HdaControllerHdaIo.c:456-500`: StartStream retains the input pointer and
  copies its contents into private DMA buffers; modifying a source buffer
  cannot replace samples already copied to DMA.
- `HdaController.c:128-149`: after total DMA consumption exceeds the finite
  source length plus padding, the driver idles and aborts the stream before
  invoking the final callback. Its comment explicitly leaves continuous stream
  operation as a TODO. Restarting from this callback is not seamless playback.
- `HdaController.c:155-190`: block-completion polling refills the next DMA block
  from the finite source. No application refill hook is exposed.
- `HdaController.h:94-104`: two DMA descriptors share a 512 KiB buffer, giving
  256 KiB blocks. At 48 kHz stereo PCM16 each block represents about 1.37 s.
  A continuous extension must also reduce/reconfigure the buffering; exposing
  the existing refill operation alone would not provide responsive speech.
- `HdaControllerHdaIo.c:570-589`: StopStream cancels its timer, stops hardware,
  then releases source/callback references. Failure before those steps complete
  must retain the provider image and its buffers.

Elapsed wall time is not a replacement for the hardware DMA cursor. Neither
repeated playAsync calls nor an oversized mutable buffer can prove safe refill,
continuous music or a bounded interruption delay.

## Required driver extension

Keep AudioIo revision 4 intact for existing consumers. Add a separate versioned
stream protocol to the HDA controller/codec driver, with validated size and
capabilities and an explicitly negotiated PCM format. It needs:

1. Continuous DMA operation with bounded, small descriptors and actual cursor
   accounting, including the existing LPIB fallback. Size/alignment and poll
   cadence must be verified against real controllers, not just QEMU.
2. A bounded queue of owned PCM blocks, explicit accepted/consumed frame counts,
   and nonblocking enqueue/backpressure. Applications may release a source block
   only when the driver has copied it or explicitly returned ownership.
3. Defined underrun behavior: output silence without replaying stale speech;
   report underruns and device errors. Track DMA wrap, poll delays and missed
   descriptor completions without guessing from wall time.
4. Stop with a quiescence guarantee: no further DMA/source/callback access after
   success. Retain memory on failure. Avoid running synthesis, allocation or file
   I/O from TPL_NOTIFY; the normal application loop prepares PCM in advance.

One shared mixer feeds that output. Music and speech have independent positions
and generations. A new utterance cancels the old TVox stream and discards its
unmixed samples, while music keeps advancing. Already committed mixed samples
cannot safely be unmixed: bound their queue duration and state this maximum
interruption latency. Do not stop/reset the hardware or rewind music to cancel
speech. Convert the 22050 Hz TVox stream into the negotiated output rate using
persistent resampling state; mixing and clipping must not introduce overload.

## Source and build inputs

`work/efi-audio-r336/provenance.json` identifies the installed binary as:

- Repository: `https://github.com/acidanthera/OpenCorePkg`, release/tag `1.0.8`.
- Release archive: `OpenCore-1.0.8-RELEASE.zip` from that release.
- Archive SHA-256:
  `2011e8b7216ecb2645d97ea710df965947edd406846e92050b9bc4190be6d27b`.
- AudioDxe SHA-256:
  `bda19da04ac380cde3e1a2c0e09a3edef21fde876cb2018f1214c0a0efc53418`.

The local files are inspection snapshots, not a complete reproducible EDK2
driver build tree. Obtain the exact OpenCorePkg 1.0.8 source, its AudioDxe INF
and containing DSC/build configuration, referenced libraries/protocol headers,
matching EDK2/dependency revisions and supported build toolchain. Preserve
upstream licenses. Pin the resolved source commits and build inputs before
modification; a filename or matching release label alone is not source/binary
reproducibility evidence. Build and sign the extended driver through the
existing HAK signing path; standard firmware LoadImage authentication remains
mandatory. No Haiku source or binary modification is involved.

## Acceptance evidence still needed

Test native queue ownership, wrap/underrun, cancellation generation and stop
failures first. Then the single QEMU coordinator must capture actual audio:
continuous music with repeated live-text speech replacements and bounded
latency, no old speech after the committed-queue bound, no melody restart,
no clipping and safe handoff. Repeat on supported physical hardware before
claiming firmware-wide compatibility. Secure Boot must still verify the rebuilt
driver. No prerecorded utterance fallback satisfies these tests.
