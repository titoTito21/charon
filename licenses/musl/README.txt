Freestanding x86-64 math dependency for HAK live TTS
==================================================

musl 1.2.5 sources are vendored unchanged from the official release archive.
provenance.json records the release URL, archive hash and every source hash.
musl/COPYRIGHT and all per-file notices are retained. This is not a replacement
speech engine, a completed eSpeak/TVox port, or a prerecorded speech mechanism.

Exports: sin, cos, exp, fabs, floor, pow, sqrt, scalbn, ldexp, log, fmod,
copysign, tanh, expm1, fmax, fmin, hypot, nearbyint, rint and labs, plus the
original x86-64 floating-point environment helpers. The source
closure includes original range reduction, polynomial tables, subnormal and
floating-point exception handling. No custom approximations or math stubs.

Build: sh src/tts/runtime/math/build.sh NEW_OUTPUT_DIRECTORY
Test:  sh src/tts/runtime/math/test.sh BUILT_OUTPUT_DIRECTORY

build.sh uses only upstream headers and generated upstream alltypes.h, with
-nostdinc and -ffreestanding. It verifies zero undefined symbols in the linked
object before creating libhak-math.a. There are no OS/libc/libm imports or heap,
file, thread or audio calls. The C ABI is x86-64 SysV within the runtime; EFI
entry points must still use their declared EFIAPI calling convention.

test.sh prefixes every symbol of the SAME object and compares it against the
host's independent libm. The hosted test harness is not part of the firmware
library. It checks signed zeros and exceptional values exactly by class/sign,
and bounded ULP discrepancies for transcendental operations. The current test
uses round-to-nearest, not a claim of exhaustive rounding-mode verification.
nearbyint is additionally checked in all four rounding modes, including ties,
signed zero and preservation/suppression of FE_INEXACT as specified. labs excludes
LONG_MIN, whose absolute value is unrepresentable under the C contract.
The TVox PCM limiter dependency is covered by 40002 signed tanh samples from
the knee through saturation and exact copysign magnitude/sign-bit preservation,
including NaN payloads and signed zeros. expm1 is tanh's original dependency.
Floating-point exception flags may be raised by the original implementation;
errno reporting is not provided by this musl math subset.

Exception handling: musl's original x86-64 fenv.s is retained unchanged under
musl/. EFI cannot use its two leaf functions' SysV red-zone scratch slots.
prepare-fenv.py produces fenv-no-red-zone.s with an explicit 8-byte stack
reservation/restoration for feclearexcept and feraiseexcept only. Floating-point
instructions are unchanged; fenv-adaptation.json records hashes and changes.
build.sh links that assembly together with original nearbyint/rint, never fake
exception functions. All symbols remain prefixed in the hosted differential
object so its environment implementation cannot override host libm's version.

Do not enable fast-math, flush-to-zero, unsafe contraction or change floating
point representation when integrating. A synthesis backend must preserve its
floating-point environment and pass actual DSP/audio tests separately.
