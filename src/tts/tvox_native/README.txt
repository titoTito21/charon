TVox native PL frontend and calibration foundation (not a speech engine)

tvox_numeric.c is an exact translation of bundled Titan Voice g2p_pl.py's
number_to_words/_below1000/_form over uint64_t. It intentionally preserves
the source's use of miliard forms for all groups above millions, including
very large uint64 values. It does not invent a different Polish number policy.
Negative integers are outside this API; the original helper does not terminate
for negative input. Signed text normalization must handle sign before calling.

No operating-system calls, allocation, libc functions, filesystem, Python,
eSpeak, speech recordings or cached utterances are needed by this component.
The caller supplies output storage; required size includes the terminating NUL.
Insufficient storage leaves output untouched. Its stack scratch buffer has a
fixed 2048-byte bound: uint64 has at most seven three-digit groups, each using
at most four words from the original bounded tables (well below that bound).

generate_data.py extracts original numeric tables and the eight voice records
without importing or executing the synthesis engine. Calibration includes the
actual PL/EN default Klatt f0/gain at pitch 1 and clarity .5, not guessed voice
parameters. Other clarity settings and synthesis controls remain future work.
PROVENANCE.json records SHA256 of every source input. Regenerate after changing
bundled TVox; differential tests compare compiled native code to original
Python functions over all numbers 0..9999 and deterministic uint64/boundary
vectors, and compare all exposed voice/calibration fields.

Build a host shared library:
  cc -std=c11 -Wall -Wextra -Werror -fPIC -shared tvox_numeric.c -o libtvox_numeric.so
  python3 test_numeric.py ./libtvox_numeric.so
For EFI compile tvox_numeric.c into the chosen firmware build; no constructor
or runtime initialization is required. This is not proof of BIOS/UEFI speech.

upstream/tvox_dsp.c is a byte-for-byte copy of the actual TVox native DSP.
It is deliberately NOT wrapped as a finished TTS API. klatt_render consumes
already-generated numeric frames, LF tables and voice/source parameters;
the original API consumes prepared frames. The native PL frontend described
below now prepares those frames directly from text. The DSP still renders
a whole utterance per call; bounded/cancellable firmware operation requires
preserving its internal state across chunks without altering sample order.
Future firmware integration must prepare frames at runtime from arbitrary
input, render bounded chunks, cancel old work before new text, and stop audio
before handoff. Phoneme/formant tables are legitimate voice data; prerecorded
menu utterances are not a replacement for live TVox.

PL G2P update: tvox_pl.cpp now translates the complete original Polish word
phonemization, lexicon lookup, stress, proclitic/enclitic merges and phrase
stress. generate_pl.py freezes source tables, the whole bundled Polish
lexicon and Unicode lowercase mappings. test_pl.py compares the compiled
implementation to actual g2p_pl.py functions (including all lexicon entries,
unseen words, upper case, device names, numbers and contextual phrases).
Its low-level input is already-normalized words. The higher-level APIs below
now accept arbitrary valid UTF8 text. See tvox_pl.h for exact size/error bounds.
Unlike the numeric C component, this C++17 implementation currently requires
hosted string/vector allocation and exceptions. It is NOT an EFI driver;
firmware integration must provide or replace this runtime and bound frontend
work between keyboard polls. No whole-word audio or phoneme-result cache is
substituted for the original rules. WIKIPRON-NOTICE.txt preserves lexicon origin.
Build: c++ -std=c++17 -fPIC -shared tvox_pl.cpp -o libtvox_pl.so
       python3 test_pl.py ./libtvox_pl.so

Built-in text normalization update: tvox_textproc.inc translates PL/EN
textproc.normalise with original spelling/abbreviation/unit/century/ordinal/
number-range/decimal/token/punctuation rules. Unicode predicates and lowercase
data are generated from the recorded Python Unicode version, not ASCII guesses.
It preserves original failure outcomes (e.g. unsupported numeric tokens and
out-of-range English number groups). Only explicit PL/EN are supported; automatic
language-pack detection remains unimplemented and is never silently approximated.
tvox_normalise exposes this component. tvox_pl_text connects normalization,
all Polish G2P/stress/context rules and the original phoneme inventory filter,
returning the same phrases, word indexes and last-in-word markers as the original
engine.text_to_phones. This particular API returns no PCM; the acoustic and
DSP APIs below complete the synchronous PL reference rendering path.

Validation:
  python3 test_textproc.py ./libtvox_pl.so
  python3 test_pl_pipeline.py ./libtvox_pl.so
test_textproc compares actual original normalise on all units/abbreviations,
Unicode, numbers/dates, device names and random combinations. Pipeline tests
execute the actual original engine.text_to_phones function over original data.
generate_textproc.py records exact source/Unicode provenance. Generation-time
Python is not part of the native runtime. Generated source tables contain rules,
Unicode properties and lexicon data, not precomputed utterance results or audio.

Adam PL acoustic update: tvox_acoustic.inc translates original engine segment
construction, allophones, learned durations, boundary/formant interpolation,
Fujisaki pitch and microprosody, voice parameters and Klatt frame construction.
generate_acoustic.py freezes only original phoneme/learned voice constants.
tvox_pl_frames_ex returns 28 doubles per frame and actual first/last pause
lengths. It accepts arbitrary text, rate and pitch; voice is explicitly Adam PL
with original default Klatt settings. test_acoustic.py compares every frame
column to actual original source (324 varied cases, exact on tested host).

test_pl_pcm.py connects this real text frontend to tvox_dsp_render_pl and
compares output against original Python frontend plus original native DSP:
15 menu/device sentences at varying rates/pitches, exact PCM on tested host.
Example: python3 test_pl_pcm.py ./libtvox_pl.so ./libtvox_dsp_adapter.so
An optional third argument writes a local test WAV; it is never backend data.
This is a synchronous hosted native synthesis reference, not a working EFI
voice protocol. Caller sentence grouping must follow original stream_pcm;
arbitrary whole-paragraph rendering is not claimed equivalent to that stream.

Freestanding runtime update: build tvox_freestanding.cpp INSTEAD of tvox_pl.cpp
and link ../runtime/CharonPool.c. tvox_frontend_configure supplies a caller-owned
pool and cancellation callback. The conditional value containers use arena
slabs and have trivial destructors, checked at compile time including nested
segments/phrases. Recoverable allocation/validation/cancellation returns use
an explicit wrapper boundary, never fatal throw stubs. GCC uses built-in stack
jumps; hosted MSVC tests use the C jump runtime. Calls are single-owner and
synchronous. The algorithm/Unicode/voice data are shared with the hosted path.
test_freestanding.py injects OOM and cancellation, verifies failure atomicity,
zero allocations after return, and runs actual-source differential suites.
Firmware compilation/import checks and live integration remain separate proof.
Resumable preparation update: x86_64 tvox_frontend_begin_pl/pump/frames/cancel
preserves the entire original frontend activation on a private 1MiB arena
stack. Link tvox_context_x64.S for GCC SysV; firmware protocol entry thunks
must retain their own UEFI ABI. A separate MASM variant tests Windows x64.
Pump yields at work checkpoints, including container copies/scans and acoustic
frame loops. The budget counts checkpoints, not wall-clock time of caller
allocation/cancellation callbacks; callbacks must remain nonblocking. Input is
borrowed immutable text until completion. Ready frames remain borrowed and valid
until cancel, including throughout DSP rendering. Cancel discards an activation
whose owned values have only trivial destructors and releases all arena memory.
There is one serialized frontend instance. No utterance splitting changes
phrase prosody. test_job.py compares budget1/7/64/4096 output against original
TVox. test_job_native.c additionally checks real SysV and caller/worker FP state.
The native context switch/live firmware scheduling still need integration proof.

English update: generate_en.py preserves the complete original CMU dictionary,
finite context rules, composition/inflection and function-word stress. The
CMUDICT_LICENSE.txt and source hashes accompany the data. test_en.py compares
131074 dictionary/unseen/composed/Unicode inputs to the actual original code.
Shared acoustic functions now select the original language table, durations,
allophones/aspiration, learned targets, stress and pitch model. EN Adam uses
118.5Hz calibrated base, its own source EQ shelves and gain, never PL audio
under a new name. New EN APIs are additive; original PL entry points remain.
test_en_acoustic.py compares 327 frame cases exactly on the tested host;
test_en_pcm.py compares actual text->PCM and streaming budgets1/17/480/4096.
test_en_job.py tests the same resumable/cancellable pipeline. Both language
paths are integrated in ../runtime/CharonLiveVoice with explicit ownership,
separate preparation/DSP/sink work budgets, latest-speech cancellation retaining
cues/music, and a hardware drain callback required for completion.

Remaining: automatic language-pack selection,
integrated firmware preparation/DSP/audio scheduling and cancellation,
and complete original voice/options support beyond the explicit Adam PL path.
Whole-utterance calls cannot honestly implement a bounded firmware Pump.
An eSpeak fallback is a separate explicitly identified engine.

Licensing (r385): the author relicensed TVox in Charon MIT-0 (COPYING). The
eSpeak NG-derived 'clear' timbre stays GPL-3.0-or-later in upstream/tvox_dsp.c,
which Charon no longer builds: generate_dsp_stream.py writes tvox_dsp_mit.c and
the stream core without it (PCM of Charon's voices unchanged, same hash). The source inputs remain in dependencies/tvox. Future
dictionary integration must preserve CMUDICT_LICENSE.txt and the Polish
WikiPron lexicon's CC BY-SA attribution. No optional recorded databases or
optional IBM-derived style tables are imported by this foundation.
