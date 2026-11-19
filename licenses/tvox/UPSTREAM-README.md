# TitanVox (TVox)

A Titan TTS engine made of **Titan Voice**: eight voices (Adam, Edward, Andrzej, Benek, Barbara, Viktoria,
Julia, Inga), Polish and English, each in up to four **variants**, and any number of other languages for
Klatt through **language packs**.

| Variant | How the voice is made | Data |
|---|---|---|
| `klatt` | formant rules (Klatt cascade/parallel) in one of eleven **timbres** | none, always there |
| `parametric` | statistical models trained on recordings | `parameters_<voice>_<lang>.json.xz` |
| `compact` | recorded units, small database | `concat/compact_<voice>_<lang>.bin` |
| `concat` | recorded units, full database - **Polish only** | `concat/units_<voice>_pl.bin`, or a full `.seg` database in `~/titan_voice_data` or `TITAN_VOICE_DATA` |

The English concatenative variant was taken out (it joined its units badly): English text in the
`concat` variant speaks with the compact database.

### Two packages: the basic engine and the recorded voices

The recorded databases are not part of the engine (user 2026-11-03). They live in a **`concat`
subfolder** of a data folder - `lib/titan_voice/data/concat/` beside the engine's own data, or a
`concat` folder inside `TITAN_VOICE_DATA` or `~/titan_voice_data` (`concat.database_dirs()`, each
folder's `concat` first and then the folder itself, for an installation made before the split) - and
a voice and language with no database there has neither variant offered at all (`has_variant` asks
the disk through `concat.locate`): the **basic** engine is Klatt, pseudonatural and parametric,
and installing the **voices** package is dropping that folder in. A saved setting that asks for a
variant whose database is gone speaks Klatt rather than nothing.

```bash
# the basic engine, without the recorded voices (about 400 MB less)
python src/scripts/pack_addon.py "data/titantts engines/tvox" --kind tts_engine -o tvox.tcd \
    --exclude lib/titan_voice/data/concat
# the voices: the folder itself, to be unpacked into lib/titan_voice/data/ or ~/titan_voice_data/
7z a tvox.voices.7z "./data/titantts engines/tvox/lib/titan_voice/data/concat"
```

`python -m titan_voice.repack` and `repack --check` read the compact databases from that subfolder.

### The recorded audio is Opus (2026-11-03)

User: "improve the concat and compact voices without rebuilding the audio, compress the files if you
can, and keep them as responsive on long texts as on short ones". The audio of both database sizes is
now **Opus at 48 kb/s** (`lib/titan_voice/opus_audio.py`; libopus 1.4 through ctypes, the engine
carries `lib/libopus-0.dll`, Titan's own copy and the system library are looked for after it):

- **Measured on the narrators' own speech** (60 s of Adam and of Viktoria, PESQ-WB with mu-law itself
  as the reference, 4.64 being the ceiling): the shipped dpcm4 compact codec 4.45 / 4.25, Opus 48k
  **4.52 / 4.53**, Opus 40k 4.48 / 4.49, Opus 32k 4.39 / 4.41; STOI 0.995 at 48k. Lossless coding of
  mu-law bytes gains only 1.5:1 (zlib, zstd, LZMA measured), so the lossless originals of the compact
  databases (in the stand-alone Titan Voice project; `repack --from DIR`) were re-coded from mu-law to
  Opus - never from the dpcm4, whose hiss would have stayed: **21.6 -> 10.4 MB** a Polish compact
  database, 28.7 -> 13.8 MB an English one, and the dpcm4 noise is gone. Opus is not a waveform coder
  (12 dB waveform SNR against the source is its ordinary figure; the output's waveform SNR after a join
  is meaningless anyway, every crossfade moves the phase), so it is judged by PESQ, STOI and the
  long-term spectrum (within 1 dB per 1/3 octave), and by listening.
- **Nothing is decoded at load.** A compact voice holds 10 MB of packets and decodes the units a sentence
  uses as it assembles them (0.3 ms a unit, nothing for a unit that continues the last one); the full
  database reads its `.opus` through mmap. The samples are handed to libopus as if they were 24 kHz,
  so every sample position - segment cuts, PSOLA marks - is untouched and nothing is resampled.
- **A read from the middle is a seek**, and a seek has to settle: measured over 377 reads of a
  narrator's speech against the continuous decode, 12 packets of preroll put the worst read 44 dB off
  (under the mu-law source's own 37 dB noise), 8 packets 24 dB, 4 packets 10 dB. `PREROLL` is 12.
- **The full databases**: `python -m titan_voice.repack --full ~/titan_voice_data/units_adam_pl ...
  --jobs 8` writes `units_<voice>_<lang>.opus` beside the `.audio` (22 -> 5.5 kB/s: the eight voices'
  35 GB -> about 8.6 GB; about 100x real time, so a 100-hour voice is an hour). The `.audio` is read
  while it exists (`TV_PREFER_OPUS=1` reads the `.opus` instead, to compare); `--replace` removes it once
  the `.opus` has been read back and checked. `repack --codec dpcm4` still writes the old codec, and both
  old codecs are still read.

## Klatt's timbres

Settings -> speech -> **Klatt timbre**, offered only while the Klatt variant is chosen (Titan reads the
fields when its speech settings open, so it appears there the next time after choosing Klatt).

A timbre is a whole **style** - the sound, the sample rate, the rhythm and the melody of the synthesiser
it is named after (user 2026-10-31: "the eSpeak timbre does not sound like eSpeak and Eloquence does not
sound like Eloquence"). What stays Titan Voice's in every one of them is the **pronunciation** (text
normalisation, letter rules, stress, the language packs) and **who speaks** (the voice's sex, pitch,
range, rate). Each style is a module of `lib/titan_voice/styles/`, and every library it needs is inside
`lib/titan_voice/`:

| Timbre | What makes the sound | Measured against |
|---|---|---|
| **Default** (Domyslny) | Titan Voice's own Klatt, tuned against the narrators | - |
| **eSpeak** | **eSpeak NG itself** (`titan_voice/espeak`: libespeak-ng and only the data the phonemes and voices need), handed Titan Voice's phonemes in its `[[ ]]` notation; eSpeak's phoneme data and wavegen. Every voice speaks through an eSpeak variant of its OWN (`voices/!v/tvox_<voice>_<pl|en>`: no echo, the voice's vocal tract, bandwidths and breath - eSpeak's stock variants are other people, and `m2` has an echo), and eSpeak's speed is fitted to the same voice in the Default timbre (`styles/espeak_ng.fit`, stored in `levels.json`). **The melody is Titan Voice's** (2026-11-03): eSpeak is rendered as a monotone at the voice's median, reports where every phoneme landed, and Titan's Fujisaki contour for the same sentence is laid on pitch-synchronously (`styles/melody.py`, TD-PSOLA) - see "Titan Voice's melody on eSpeak" below | contour within 0.2-0.3 st of the Default timbre's after time warping (eSpeak's own melody was 0.5-0.6 st away); pace within ~1 % |
| **Klatt 1980**, **KLSYN88**, **SpeechPlayer** | eSpeak NG's Klatt synthesisers (`klatt 1` impulsive source, `klatt 2` KLGLOTT88 natural source, `klatt 6` NV Access's SpeechPlayer), fed, fitted and given Titan Voice's melody exactly as eSpeak. The first two are as hissy and thin in the bass as eSpeak's own `klatt` voices | as eSpeak |
| **Votrax SC-01** | the chip's analog path as MAME emulates it (`titan_voice/native/votrax`, BSD-3): glottal ladder wave, switched-capacitor filters, noise shaper, closure, 1/8-step register glides - without its phoneme ROM: Titan Voice's F1-F3 quantised to the chip's filter codes every 5 ms, Titan Voice's pitch; a voice's size is the chip's clock | Titan Voice's pitch and pace |
| **SAM (Commodore 64)** | the METHOD of SoftVoice's Software Automatic Mouth, written anew (`titan_voice/native/sam`; SAM's own code and tables carry no licence and are not used): three formant oscillators restarted at every glottal pulse, a 4-bit DAC, 1-bit noise | Titan Voice's pitch and pace |
| **Eloquence** | **IBM's Eloquence synthesiser** (clsyn) as OpenEVV transcribed it, at its own 11025 Hz (`titan_voice/native/openevv`); Titan Voice's formant frames converted to Eloquence's 62 parameters with Eloquence's voice quality (open quotient, tilt, aspiration under voicing, its bandwidths) and a piecewise-linear melody with Eloquence's deep statement fall | OpenEVV's own output: same frames -> same waveform (correlation 0.9989); long-term spectrum within 1.4 dB of OpenEVV's for the same English sentences |
| **DECtalk** | Titan Voice's C Klatt with no breath and no jitter, DECtalk's 11025 Hz band, MITalk's "hat" melody (flat baseline, sharp accent peaks) and DECtalk's spectrum as a fixed equaliser | Titan's DECtalk engine: Perfect Paul 1.5 dB, Beautiful Betty 3.9 dB mean 1/3-octave difference |
| **Speak & Spell (LPC)** | Titan Voice's speech analysed into 25 ms frames of ten reflection coefficients, quantised to Texas Instruments' own tables and played back by the chip's decoder in its fixed point (`titan_voice/native/lpc`, after MAME), 8 kHz | spectral envelope kept (correlation 0.8) |
| **Vocal tract (physical model)** | Pink Trombone's Kelly-Lochbaum waveguide with a nose (`titan_voice/native/tract`), played by Titan Voice: tongue, lips, velum, constrictions and voicing every 5 ms, the articulators moving at a finite speed; vowels from a measured grid of tube shapes (`data/tract_vowels.json`, `python -m titan_voice.styles.tract`); a voice's size is the rate the tube runs at | vowels within ~10-20 % of Titan Voice's F1/F2 |

The melodies of Eloquence and DECtalk are `styles/prosody.py` - a few targets joined by straight lines,
measured off each synthesiser's own output. A style whose back end is missing (the library not built)
speaks Klatt with the old voice-source-only version of that timbre and says so once.

A saved setting with the first names (`clear`, `warm`) is read as `espeak`, `default`.

### Measured per sound, against the narrator (2026-11-02)

Every timbre but the eSpeak ones was measured the same way, on the same sentences, with the engine's own
segmentation saying which 5 ms frame is which sound: the vowels' level, each sibilant class's level and its
1/3-octave shape, the stops' bursts, the nasals, the crest factor, isolated spikes, DC. The reference is the
narrator himself - the compact variant's recorded units, cut at `concat`'s own unit boundaries - and Klatt's
Default timbre, which already sits on him (vowels -18 dB, s -6, ś -4, sz -4, bursts -16, nasals -3 re the
vowels; natural -18, -6, -3, -4, -17, -2). What was wrong, and what answers it:

- **s, ś, sz are the narrator's shapes** (`phonemes.PL_OVERRIDE`, `par` = A2..A6, AB): Klatt's s had no
  shoulder under 5 kHz, its sz was centred on 2.5 kHz where the narrator's is a plateau from 2.5 to 6.3 kHz,
  its ś and sz carried a flat bypass that put a floor under 2 kHz the narrator has not got. Within 2-4 dB of
  his bands now in every third-octave from 1 to 8 kHz. Vosk: Default 13.8 -> 10.3 % of words wrong.
- **A band-limited machine gets back in level what its band takes** (`styles.band_compensate`): at 8-11 kHz
  s loses most of itself; the loss is read off the parallel amplitudes and given back in `af`, a resonance
  above the band is silenced (not clamped onto its edge - that stacked s and sz onto one 4.9 kHz peak), a
  sound with nothing under the band is given a shoulder at 0.85 x the edge. Per-machine trims where the
  machine's own structure favours one sibilant (`trim_sibilants`): Eloquence's amplitude saturates near 60
  and its hiss is weighted upwards, so its sz's F4/F5 are raised to the narrator's plateau; the SC-01's noise
  shaper puts ś brightest, s middle, sz darkest, whatever F3 says; the SP0256's noise frames are set by their
  cascade's PEAK gain, not its DC gain.
- **The parametric coders ring after the pulse, not around it** (`vocoders.min_phase`): LPC-10, MELP, the
  channel, formant, cepstral and WORLD vocoders applied their envelopes as bare magnitudes, which rings
  symmetrically round each pulse - spikes 8-10 x the rms once a period, crest 25-30 dB, the utterance 7-9 dB
  quieter once normalised. The phase vocoder advances each channel at its INSTANTANEOUS frequency (it
  realigned every channel every 1024 samples into one spike: crest 30 -> 16.5 dB). Every frame keeps the
  input frame's energy (`match_gain`); the harmonic coders' noise was +3.1 dB. At 8 kHz the harmonic coders
  analysed 128 ms at a time (a 1024-point window meant for 22 kHz): MBE 83 -> 57 %, Codec 2 97 -> 59 % of
  words wrong. The formant vocoder is analysed at 10 kHz as Holmes's were and carries fricatives through the
  frame's whole envelope (it had none: 95 % wrong). GSM weights its residual before picking the pulses; CELP's
  codebook is 512 as FS-1016's; ADPCM's and CVSD's step rules were chosen by segmental SNR (17.7 and 15.9 dB).
- **No style leaves DC** (`styles.dc_free`): the TI lattice sat at +0.022 of full scale, LPC-10's pulse train
  at +0.017 - a thump at every start and end.
- **A fit from Klatt's 1987 record is capped at +-6 dB** (`RECORD_CAP_DB`): the record's chain is in it as
  well as the machine - -19 dB at 2 kHz and -22 at 4 kHz for Klattalk - and laid on whole it took the words
  away (Klatt 3.04: 69 % wrong with it, 26 % without, 60 % capped at 10 dB; MITalk 26 %, Echo 80 -> 66 %, Holmes 53 -> 33 %). A tone
  measured on a machine's VOWELS is not laid over its hiss (`_hiss_kept`: a frame is a hiss when almost
  nothing of it is under 1 kHz, whatever the band). The Echo's and the Votrax's measured +18 dB vowel tone
  drives a small amplifier into clipping (`SOFT_CLIP`), as the cards did.
- **The vocal tract**: per-place turbulence gains, voiced fricatives at 0.4 of the voicing, the nasal's
  voicing at 0.45 (the murmur was 6 dB OVER the vowels), the release click 0.13 in the C (bursts 7 dB over
  Klatt's; `native/build.bat` rebuilt).

**Melodies, the way the machines made them (2026-11-02).** User: every timbre must INTONATE like the machine it
is named after, from the machine's code where there is code, otherwise from its output. A speech recogniser says
nothing about a melody, so melodies are judged by F0 tracks (YIN, `f0track.py`): the real machine and the timbre
speaking the SAME sentences, each track in semitones re its own median, DTW distance plus the descriptors (p90-p10
spread, first and last vowel, slope, final fall); where the machine cannot be made to speak our sentences, the
descriptors of its recording.

| Timbre | Melody | Where it comes from |
|---|---|---|
| SAM, Apple II | SAM's own rules, `sam_real.sam_melody`: the voice's PERIOD in ticks plus `TAB47492[stress + 1]` (a primary stress is 13 ticks shorter, i.e. higher), interpolated from the centre of one phoneme to the centre of the next, half of F1 taken off every frame, and `AddInflection`: over the last 30 frames the period grows by a tick a frame before a full stop (a fall) and shrinks before a question mark (a rise) | SAM's code (s-macke's C port of the 1982 disassembly: `render.c`, `RenderTabs.h`) |
| Amiga narrator.device, MacinTalk 1984 | `narrator.narrator_melody`: Hz round a floor of 110; the first primary stress of a phrase peaks at `rise*spread + 123 - pass*8` (125..165), later stresses step down by `(peak - floor) / stresses`, a stress climbs 0.4 of its height above 110 into its peak and drops 0.2 after, lead-in syllables sit at 110, the ones between glide down with a 5 Hz bump, a full stop ends on 75, a question mark on 154/128 of the highest peak | narrator.device 33.2's code, through bitplane's byte-exact narrator-ts port (`prosody.ts`); MacinTalk 1984 is the same authors' same rules |
| Klatt 3.04 | Klattalk's hat (Klatt 1983, MITalk's rules on his synthesiser): p90-p10 6.3 st, final fall 5.6, a level baseline, fitted | item 33 of Klatt's 1987 record (Acoustics Today's digitisation) |
| Holmes 1973 | rsynth's melody, `prosody.rsynth_contour`: ONE straight line from 1.1 x F0 down to 0.66 x F0 over the utterance, no accents | Nick Ing-Simmons' rsynth, `holmes.c` `rsynth_interpolate` - the text-to-speech round the Holmes-Mattingly-Shearme elements |
| DECtalk | DECtalk's own F0 commands, `dectalk.dectalk_melody`: a STEP of HR (Paul 18 Hz) at the first stress of a clause, an IMPULSE on every stressed vowel 1/4 of the way in whose height is `stress_level + phrase_position` in Hz*10 ((81+160)/10 = 24 Hz for the first accent, then 16, 14, 12...) times SR/32, 16 frames of 6.4 ms long, a GLIDE down of hat + fall at the last stress (55/2 Hz for a man before a full stop, 12 at a comma, 15 at a semicolon, 8 before a question mark), a glottal dip of 55 Hz over the last 8+8 frames, +17/+25 Hz at a comma, +35/+45 Hz at a question mark, a baseline from +BF/2 falling 0.1 Hz per frame to -BF/2, two-pole smoothing; Paul's and Betty's numbers from `p_us_vdf.c`; the two gains and the hat's decline fitted to Titan's own DECtalk engine by DTW (p90-p10 4.1 st against its 4.6, final fall 4.1 against 6.1) | the published DECtalk source (github.com/dectalk/dectalk: `ph_inton*.c`, `ph_drwt01.c`, `p_us_vdf.c`) and Titan's DECtalk engine on twelve English sentences |
| Eloquence | `slide` shape, re-fitted by DTW against OpenEVV speaking the same twelve English sentences (first 1.10, accent 1.05, decline 0.84, fall 0.52 over 320 ms) | OpenEVV's `evv.exe` (the C rebuild of IBM Embedded ViaVoice; its intonation rules are disassembled bytecode and cannot be read) |
| BeSTspeech | `slide` shape re-fitted by DTW against the real engine on the same twelve Polish sentences: it DOES fall at the end, 3.3 st over the last 300 ms, last vowel 2.6 st under the median, p90-p10 3.4 st (the earlier "no final fall" was wrong) | Titan's BeSTspeech engine |
| Apollo 2 | re-measured on Orpheus 2.10 speaking the same twelve Polish sentences through a stand-in audio library: a nearly LEVEL body (+-1 st), small accents, sliding 2-3 st down through the last word, and then a short RISE of about 3 st held over the last quarter second - in statements too; no final fall (the earlier "falling slowly through the whole last word" came from a bad capture); the Shape fitted by DTW | Dolphin Orpheus 2.10 on this machine (`a3s.dll` with the text in UTF-16) |
| Echo II | Titan's melody drawn 0.47 of the way to flat (`prosody.compress`): the Echo's p90-p10 2.7 st, slope -0.8 | item 29 of Klatt's record |
| Votrax Type-'n'-Talk | Titan's melody drawn 0.31 of the way to flat, then the SC-01's four inflection levels: p90-p10 1.8 st | item 28 of Klatt's record, the chip's two inflection bits |
| MITalk, OVE II, Infovox (GLOVE), Prose-2000, CallText, PAT | `hat` / `fall` / `slide` shapes fitted to the descriptors of each machine's recording: MITalk spread 5.4, last vowel -6.1, final fall 4.6; OVE 5.8 / -3.5 / 4.9; Infovox 4.7 / -2.8 / 3.8; Prose 5.8 / -1.7 / 2.8 (CallText the same family); PAT 8.5 st wide, falling 6 st/s (hand-painted) | items 30, 06, 31, 32, 03 of Klatt's record |
| Speak & Spell, TI-99, TMS5110 | Titan's melody flattened to 0.35 / 0.5 / 1.0 of the voice's range | recorded-word playback; not measured |
| SP0256-AL2 | one pitch | the chip's allophones carry one |
| MEA 8000, SSI-263 (Accent SA), MacinTalk 3 (Mac OS) | `peak` / `slide` shapes written, NOT measured - no recording of these machines is on this machine, and none could be had from the web (2026-11-03: the DoubleTalk and SSI-263 emulators on GitHub run proprietary firmware they do not ship, CPCWiki's MEA 8000 recording refuses every client that is not a browser, archive.org's "Fitter Happier" item is a disk image for an emulator with MacinTalk 2). What the web DID give: the real SSI-263's pitch register, measured on a Braille Lite's line out (tgeczy/ssi263-speech, `docs/measurements.md`) - F0 = 1953.125 / (32 - k) Hz, 32 pitches - so `ssi263` and `accent` sing on that staircase now (`ssi263.chip_pitch`) | the SSI-263's pitch grid: a real chip's measurement |
| eSpeak, Klatt 1980, KLSYN88, SpeechPlayer | **Titan Voice's own Fujisaki melody**, laid on eSpeak's monotone pitch-synchronously (`styles/melody.py`) - user 2026-11-03: "barwy espeakowe i speechPlayerowe mają mieć melodię Titan Voice" | Titan Voice |
| DoubleTalk (pseudonatural) | **Measured on RC Systems' own recordings** (2026-11-03): rcsys.com/demos.htm publishes five demos "created with the RC8650 chipset" with their texts printed beside them; demo1 (Genesis) and demo3 (the address, the recipe, the earnings) were rendered on the same texts. The unit speaks at 88 Hz, 6.4 st wide, between a low baseline and plateaus 3-4 st over the median held across a phrase, and a statement ENDS HIGH - both demos end 3.7 st over the median - where the data sheet says "the pitch drops at the end of the sentence" and Titan's contour fell 6 st (scratch `dt_compare.py`). So the timbre has a Shape of its own, grid-fitted by DTW to the demos (`doubletalk.SHAPE`, scratch `dt_fit.py`), and its tone and tempo are in `fitted.json`: 1/3-octave spectrum (200 Hz - 2.5 kHz, the demos being 11 kHz 8-bit) within 0.12 dB after the fit, 3.4 dB before (the unit is 4-5 dB darker under 1 kHz and 6-13 dB brighter at 1.8-2.3 kHz than the pseudonatural Klatt), tempo 1.08 | the maker's own demos |
| MacinTalk 3 (pseudonatural `macos`) | a `peak` Shape, written; Apple's Speech Synthesis Programming Guide says only that a comma rises, a full stop falls, parentheses reduce and quotes expand the range, a question rises unless rhetorical (`macintalk3.py` quotes it) | the maker's documentation, qualitative; no recording |
| SSI-263 (bare chip, `ssi263`) and Accent SA (`accent`) | **Measured on the real products** (2026-11-03, with the user's leave): tgeczy/ssi263-speech's emulation of the chip, driven by the Accent SA's own 8085 firmware and by a Braille Lite 2000's firmware (English, and Polish - the one SSI-263 product that spoke Polish), eight sentences each. The Braille Lite: 81 Hz (pitch code 8), nearly level (p90-p10 1.6 st), a final drop of 4 st, a question rising 3 st; its Polish firmware a MONOTONE (0.15 st) with one final drop of 5 st and no rise on a question - so `ssi263` has a `SHAPE_PL` that is exactly that. The Accent SA: 109 Hz (code 14), 3.9 st wide, last vowel -2.6 st, final fall 7 st, question +3.5. Both shapes grid-fitted by DTW (`ssi263` 1.6 / -0.3 / 2.6, DTW 0.25 st; `accent` 3.2 / -2.1 / 6.9, DTW 0.51 st against the written shape's 2.6 / -0.7 / 4.5), and both TONES and tempos in `fitted.json`: 1/3-octave spectra within 0.34 and 0.33 dB after the fit (3.3 and 4.1 dB before - both units lift 1-2 kHz by 11-15 dB and roll off 20 dB above 3 kHz), durations within 2 %. A plain YIN sat on these units' THIRD harmonic (390 Hz for a voice at 130): the pitch range is chosen by which expected pitch reads the most of the signal as periodic (scratch `ssi_compare.py`) | the real firmware on the emulated chip; Aicom's and Blazie's firmware is that project's to ship, measured here and never redistributed |
| the vocal tract, the pulses (Rosenberg, FOF, VOSIM), the pseudonaturals, the parametric coders | Titan Voice's own Fujisaki melody | a synthesiser with no text-to-speech rules of its own |

### Titan Voice's melody on eSpeak (2026-11-03)

eSpeak's phoneme input carries stress and no pitch; its intonation module decides every syllable's pitch, and the
API offers no way in (a mid-clause SSML `<prosody pitch>` does take, measured, but per word, and it doubled the
clause's length). So the four eSpeak-family timbres are made in three steps (`styles/espeak_ng.render`):

- **a monotone**: eSpeak at pitch range 0, its pitch parameter set so the monotone sits at the median of Titan's
  contour for the sentence (`_mono_pitch`: the monotone's height per parameter is measured once per voice, the
  parameter being relative to the variant's own pitch line);
- **the alignment**: eSpeak is initialised with phoneme events, so every phoneme comes back with its position in
  the audio; the tokens handed in (`to_espeak_tokens`, each knowing which Titan phones it came from) are aligned
  to those events by sequence alignment (`_align`: a pause event is free, a prefix match cheap), and each token's
  stretch of eSpeak time is laid over the frames of the Titan segments made from the same phones
  (`_target_track`);
- **TD-PSOLA** (`styles/melody.py`): the monotone is tracked (YIN, told what pitch to expect, since eSpeak's
  harmonic-rich periods pass the threshold at a quarter period too - a tenth of the frames came back at four
  times the pitch; read leniently, since the last vowel fades and judged unvoiced it kept the monotone while
  Titan fell), marked once per period on the same phase (the correlation of one period with the last mark's,
  not the highest sample - which jumped between peaks of nearly one height), and the periods repeated at Titan's
  rate under two-period Hann windows whose sum is divided out; the output is held to the input's peak.

Measured: identity (the source's own track as the target) within a lag of one period correlates 0.96-0.99;
the output follows the target within 0.05 st (median, p90 0.25); the four timbres' contours are 0.18-0.31 st
from the Default Klatt's after time warping, against 0.46-0.61 st for eSpeak's own melody; the renders are at
least as periodic as eSpeak's own (strict YIN, voiced hops: Polish Klatt 1980 6 % before, 26 % after). Whisper
(small, English, which it can read - its Polish on this family is near chance) hears every one better with
Titan's melody: eSpeak 11.8 -> 10.4 % of words wrong, Klatt 1980 1.4 -> 0.7, KLSYN88 4.9 -> 1.4, SpeechPlayer
6.9 -> 4.2. The four timbres' streaming gains in `levels.json` were re-measured on the new render.

**English in the eSpeak timbres is eSpeak's own American.** Titan's English pronunciation is CMU's, and written
for eSpeak with a bare consonant r after every vowel and no flap it was neither accent (user: "a mixture of
American and British"). eSpeak en-us's own transcriptions of a word list (`espeak_TextToPhonemes`) gave the
rules: a vowel + r in one syllable is one rhotic vowel (car `k'A@`, near `n'i@3`, where `w'e@`, door `d'o@`,
poor `p'U@`; before a vowel the r comes back, `h'i@3rIN`, `st'o@ri`, while A: and E keep a plain r, `s'A:ri`,
`m'Eri`), the linking r- before a vowel-initial word (`k'A@r- Iz`), the flap `t#` (water `w'O:t#3`), the
glottal stop with a syllabic n (button `b'V?n-`), the syllabic L (little `l'It#@L`), the happY vowel (very
`v'Eri`, but he `hi:`), the article `a#`, CMU's AO before ng/f/th as the CLOTH vowel `O2` and before g as the
LOT vowel (which en-us sounds exactly as `A:` - measured, as are `aa` = `a`), and "was" as `wVz` (CMU's first
pronunciation is W AA1 Z, "wahz", which nobody says; `g2p_en.SAID`, with "bought" and "caught", which the
dictionary carries with AA where thought, taught and fought carry AO). Words written as eSpeak writes them on
fifteen sentences: 46 of 89 before, 55 after; over twenty sentences aligned by sequence (135 word pairs): 80
identical, 22 apart only by eSpeak's reduced-vowel spellings (`@2`, `I2`, `a#`, `0` for `A:` - the same sounds,
measured), 6 only by where a stress mark sits. **Every remaining case, checked one by one** (2026-11-03):
"of the", "there are", "I am", "for the" are ONE token in eSpeak's own output (it joins a function word to
the next) and the same sounds in ours; "costs", "on", "orange" carry eSpeak's CLOTH vowel `O2` and "warm",
"born" its NORTH vowel `O@`, lexical sets CMU does not mark (ours `A:` and `o@`: American, and the
cot-caught and horse-hoarse mergers are American too); "and" eSpeak leaves full where ours is the reduced
function word; "mountain" ends `tIn` in its dictionary and `t@n` in CMU; "stop" is `st2'0p` (its cluster
marker and the LOT vowel that sounds exactly as `A:`); "tomorrow", "button", "kittens" differ only because the
probe split them at a stress mark or a glottal stop. Nothing British is left: no non-rhotic vowel, no
unflapped t between vowels, no BATH-broadening - the `aa` eSpeak writes for "dance" sounds exactly as `a`
(measured, the same formants).

**The parametric timbres are the intelligible ones.** Measured with Vosk (small-pl) on Adam and Barbara over
twelve sentences: the plain parametric voice loses 15 % of the words; channel 20, sinusoidal 25, HNM 26, phase
16, cepstral 24, WORLD 16 % stay; LPC-10 59, MELP 50, CELP 47, GSM 47, the formant vocoder 66, IMBE 62, Codec 2
60, ADPCM 62 and CVSD 77 % were removed (user: "niezrozumiałe barwy parametryczne usuń").

**Every Klatt and pseudonatural timbre, swept** (`tests`-free: six Polish and six English sentences, Adam, YIN
descriptors): all 59 render on their own back end at one level (rms 0.08, no clipping), the code-derived and
record-fitted melodies sit at their documented numbers (Echo 2.5 st spread against 2.7, Votrax 2.5/2.0 against
1.8, MITalk 4.6/5.7 against 5.4 with the last vowel -3.6/-4.4, GLOVE 3.9/4.8 against 4.7, Prose 4.5/5.7 against
5.8, BeSTspeech 2.8/4.1 against 3.4, Apollo ending +2.4/+2.9 with no fall, SP0256 0.7 st, the pseudonaturals
Titan's 5.4), and the SAM, Amiga and MacinTalk timbres speak at the voice's own height (102-136 Hz for Adam).

**And against the recordings themselves** (statements only - a question or an exclamation in the set dilutes
every final-fall figure): the timbres beside Klatt's 1987 record (the items Titan has from Acoustics Today) and
Titan's DECtalk capture, spread / last vowel / final fall in st - PAT 8.3 / -5.7 / 1.8 against ours 6.9 / -3.0
/ 2.7; OVE 5.8 / -3.5 / 4.9 against 5.5 / -3.1 / 2.7; Votrax 1.8 against 2.6 (its median 119 Hz for Adam, read
with the tracker told the voice's pitch - a plain YIN reads the SC-01's hard harmonics an octave up); Echo 2.7 /
-1.3 / 5.3 against 2.8 / -1.5 / 1.7; MITalk 5.3 / -6.6 / 4.6 against 5.6 / -5.0 / 4.2; Infovox 4.8 / -5.6 / 6.6
against 4.9 / -4.0 / 4.1; Prose 5.8 / -1.7 / 4.3 against 5.5 / -3.0 / 2.8; Klattalk 5.8 / -3.6 / 6.6 against
5.6 / -4.0 / 3.9. The records' final falls are a long reading's last sentence and read deeper than six short
ones; the spreads and last vowels are within 1-2 st everywhere but PAT's.

**Eloquence was re-fitted against OpenEVV itself** (`evv.exe` speaking the same six statements). The 2026-11-02
fit was made with a tracker that lost everything under ~70 Hz, so it was fitted to the part of the fall it could
see: measured with a tracker told to read the low end, OpenEVV's statements spread 4.7 st, end on a last vowel
6.5 st under the median and fall 9.2 st, where the shipped shape gave 2.9 / -4.9 / 6.1. The shape is now
`accent 1.25, decline 0.76, fall 0.38 over 420 ms` (4.0 / -6.3 / 9.0; `styles/eloquence.SHAPE`).

**The chip sibilants, re-tuned** (the two tests that were failing): the Votrax s had drifted to 3.1 kHz, over
ś's 2.95 - routing 3 puts it at 2.69, between ś (2.95, the brightest the SC-01 makes) and sz (2.40, the
darkest), 250 Hz from each, which is the whole of the chip's window. And the narrator himself, measured as the
SP0256 test measures (the hiss's centroid), has ś UNDER sz (4029 against 4419 Hz) - the Default Klatt wears his
sibilant shapes since 2026-11-02 (4953 / 5058), so the tests no longer ask for ś above sz; they ask for s the
brightest and the other two apart (SP0256: s 4154, ś 3784, sz 3691; anything that moves ś up on that chip
moves it over s).

### The pseudonatural variant is a concat-Klatt

User 2026-11-02: the pseudonatural timbres must not sound like Klatt - "ni to Klatt, ni to concat". So every one
of them (`styles/lively.build(natural=True)` + `styles/colour`):

- speaks with **the narrator's colour**: Klatt renders the sentence, and then every 5 ms frame's spectral
  envelope is moved 65 % of the way from Klatt's own toward the narrator's envelope for the same sound in
  the same context (the parametric variant's order-40 fits, `data/parameters_<voice>_<lang>.json.xz`), the
  shape only - the level stays the rules' - clamped to +-12 dB, never on a burst. Measured: the vowels' 1/3-octave
  spectrum against the narrator's recordings, Klatt 7.8 dB mean |difference|, pseudonatural 6.1 (the parametric
  variant itself 4.2); Vosk 19.0 -> 12.1 % of words wrong WITH the colour against without it;
- leans its **vowels a third of the way to the narrator's** connected-speech qualities (`NATURAL_VOWEL_MIX`;
  further and the recogniser lost the vowels: 12 -> 7 % without the mix), and moves inside each vowel the way
  his do (`NATURAL_TRAJ`: F1/F2 at 20 % and 80 % against the middle, measured on 21 a, 18 o, 14 e, 11 i);
- scatters its **durations** +-7 % round the rules', fixed per word and position (the same text is the same
  speech);
- and keeps the living voice (stress, onset breath, final creak) it had.

**MacinTalk 3 (Mac OS)** (`macos`, `styles/macintalk3.py`) joins the pseudonatural timbres: a clean pulse,
no breath, ringing formants (bandwidths x0.75), slow transitions (x1.4), its own sing-song melody, and only
35 % of the narrator's colour - a formant machine. Not measured against Apple's synthesiser.

## Languages

Polish and English work with every variant. Every other language is a **Klatt language pack**: one JSON
file in `lib/titan_voice/langs/` (or a folder in `TITAN_VOICE_LANGS`), written in Titan Voice's own sound
symbols - letter rules with contexts, the stress rule, vowel reduction, voicing, numbers, the NAME of
every letter, what tells the language apart. Shipped: Spanish, Czech, Russian, Ukrainian, Jamaican
Patois. `lib/titan_voice/langs/README.md` is the format; `python -m titan_voice.langs check` finds every
problem in a pack.

**A single letter is said by its name** in every language - "ka", "ku", "be", "кей" - never as a bare
consonant; a letter that is a word ("w domu", "I am") stays a word inside a sentence.

## Responsive, and nothing is cached

There is **no cache**: no utterance is kept, no file is written while speaking, so a sentence heard
before and one never heard cost the same. What makes that quick:

- **The whole Klatt rendering is one C call per sentence** (`klatt_render` in `lib/native/tvox_dsp.c` ->
  `lib/tvox_dsp.dll`): gains, resonators, the glottal source, the harmonic voicing, subnormal numbers
  flushed to zero. The Python loop it replaces gives the same samples (both draw from one xorshift64*
  generator), and is what runs without the library.
- The formant tracks and the pitch contour in numpy, the limiter and 16-bit conversion in C.
- Measured on this machine, texts never heard before: a label ("Zapisz plik") ~4-15 ms, a 5-second
  sentence 15-40 ms depending on timbre and voice (before: 13-18 ms and 70 ms with a cache, which only
  helped texts heard before).
- Speech is made in **worker processes** (`tvox_worker.py`): Titan's interpreter is never held. A text
  of several sentences is split and its sentences made **at the same time** on up to 4 workers.
- The two numbers a stream needs before its first sample - Klatt's base pitch and its stream gain per
  voice, language, timbre and clarity - are **data**: `lib/titan_voice/data/levels.json`, made by
  `python -m titan_voice.levels` (run it after changing voices, the Klatt sound or a language pack).
- The compact databases are Opus (see "The recorded audio is Opus"; they were 4-bit DPCM, and LZMA
  before that): nothing is decoded at load, a voice loads in ~0.3 s (the segment index), and only the
  units a sentence uses are decoded. Nothing is written to disk. The worker is started, with the voice
  loaded, the moment the engine is created, so the first thing Titan says does not wait for a process
  (2 s -> 31 ms). The extra workers of the pool are started in the background, and a text is split only
  across workers already running (creating a process takes ~0.6 s on Windows, longer than Klatt takes
  for a paragraph): a first paragraph 714 -> 120 ms.
- **A long sentence is as quick as a short one on the recorded variants** (2026-11-03). Measured in one
  process before: a label 17 ms, a 322-character sentence 296 ms on compact and 699 ms on concat - and
  Titan plays nothing until `generate()` has the whole text, so a long sentence was a wait. Three
  things: `psola()` - half of a compact sentence's time, a pure-Python grain loop - adds each grain as
  one numpy slice (`_psola_np`, the same samples to 1e-16); the compact Viterbi step indexes
  precomputed arrays instead of building them from lists per step, and the full database's preselect
  takes its candidate indices through numpy (the same path, tested); and the pool cuts a long sentence
  into **shares** (`Pool.shares`: a sentence of 60+ characters with phrase breaks, one share per 40 characters, up to
  one per process), each worker given the WHOLE sentence and `part=(k, n)` (`TitanVoice.stream`): it builds the
  sentence's segments and melody and renders only its contiguous share of the phrase pieces, so the
  shares joined in order are byte for byte the sentence made whole (tested on compact, concat and
  Klatt). After, in one process: the long sentence 169 ms compact (241 with Opus decoding) / 379 ms
  concat; the slowest of four shares 64 / 108 ms. Klatt's own sentences are never cut (it renders a
  sentence whole and is quick).

### How natural can the recorded voices get, and what holds them back (2026-11-03)

User: "the concatenative files are over 2 GB and the compacts are excerpts - can they not be made to
sound natural?" Measured on 30 Polish sentences (Adam), with the engine's own `select` watched:

- **The data is not the limit.** 88 % of a sentence's 5-phone sequences and 74 % of its 6-phone ones
  exist contiguously somewhere in Adam's 30 hours (Benek, 100 hours: 92 / 82 %) - yet the selection
  joined every 2.9 phones (median stretch 3, p90 5), and Benek's 100 hours every 3.3. The 25-minute
  compact joins every 1.9 phones. A wider beam (`K_CAND` 40 -> 120, `PRESELECT` 300 -> 900) changed
  nothing.
- **The first unit of a long recorded stretch was cut before the Viterbi could weigh it**: every column
  keeps its `K_CAND` best units by TARGET cost alone, and a stretch that fits the sentence as a whole
  rarely has the best-fitting first unit. `concat.long_matches` now looks the stretches up directly - an
  index of every 4-gram of the database's phones (`_NgramIndex`, sorted int64 keys, 0.3 s to build for
  Adam, in `preload`), each occurrence extended phone by phone while recording and target agree inside
  one utterance - and pins every unit of a stretch of 4+ phones into its column past the cut, with its
  ordinary target cost (`MATCH_N`, `MATCH_MIN`, `MATCH_MOST`, `MATCH_PINS`). Stretches of up to 17 phones
  then reach the lattice. **Off by default** (`TV_LONG_MATCH=1` turns it on): at the shipped weights it
  moves 2 % of the joins and makes every column's Viterbi step larger; it is the prerequisite for a
  join-base experiment, not a gain on its own.
- **And the Viterbi still cuts them** (joins per phone 0.323 -> 0.315): the target costs - the rule
  durations, the melody target, stress, phrase and word position, sentence type - outweigh the join cost
  over a long stretch from another sentence. That balance is the real lever, and it is a trade: raising
  the join base cost gives longer stretches and fewer joins (base 4: 0.267 joins per phone, mean stretch
  3.5; base 8: 0.254 / 3.6; base 16: 0.237 / 3.9) at larger pitch and timbre steps at the joins that
  remain (median pitch step 0.36 -> 0.42 -> 0.54 semitones, mel 6.5 -> 7.1 -> 7.3 dB) and a looser fit
  to the rule prosody. Without a listening judge here (UTMOS22 could not be run: its code comes from a
  hub repository this session may not execute; DNSMOS needs an onnxruntime that has no wheel for
  Python 3.15) the weights are left as they were, and **`TV_JOIN_BASE`** sets the base cost for an
  A/B by ear (`TV_LONG_MATCH=1 TV_JOIN_BASE=4`; samples `joinbase/<variant>_base<1|4|8>_<sentence>.wav`
  were written in the scratch folder of the session that made them).
- What the compact cannot do: 25 minutes of excerpts cover the diphones and little more, and no cost can
  make a stretch that was never recorded. A larger compact cut from the full database by use (which
  utterances the full selection takes most over a corpus) is the way to a better compact; the old
  builder is not in this tree.

## Hear it

    python tests/check_tvox_speakers.py [timbres|variants|languages|letters]

plays every timbre, variant, language and some single letters through the speakers, each announced.

## Building the native libraries

```
lib\native\build.bat                  (MSVC x64 -> lib\tvox_dsp.dll: Titan Voice's Klatt and parametric loops)
lib/native/build.sh                   (gcc, Linux/macOS -> lib/tvox_dsp.so)
lib\titan_voice\native\build.bat      (MSVC x64 -> titan_voice\native\tvox_styles.dll: Eloquence, LPC, tract, Votrax, SAM)
lib/titan_voice/native/build.sh       (gcc -> titan_voice/native/libtvox_styles.so)
```

Both .bat files rename a library a running Titan still holds (Windows lets a loaded DLL be renamed, not
replaced).

## Licences

`lib/native/tvox_dsp.c` contains code adapted from **eSpeak NG** (`wavegen.c`: the peak shape table and
the harmonic spectrum algorithm; Copyright (C) 2005-2013 Jonathan Duddington, (C) 2015-2016 Reece H.
Dunn), which is GNU GPL version 3 or later (`lib/native/COPYING`), so the library and this engine as
distributed with it are under the GPL-3. `lib/titan_voice/espeak` is **libespeak-ng** itself and part of
its data, GPL-3 as well.

`lib/titan_voice/native/openevv` is OpenEVV's Klatt synthesiser (MIT, `LICENSE.openevv`) - **except
`klatt_tables.c`, which is IBM's data** lifted out of IBM's objects; OpenEVV's `NOTICE.openevv` says
whose it is and that nobody can license it. It is shipped on the same footing as the IBM/ETI binaries of
Titan's own Eloquence engine. `lib/titan_voice/native/lpc` follows MAME (BSD-3-Clause,
`LICENSE.mame`; the tables are Texas Instruments' ROM as decapped), `lib/titan_voice/native/tract` Pink
Trombone (MIT, `LICENSE.pinktrombone`), `lib/titan_voice/native/votrax` MAME's Votrax SC-01 (BSD-3, its ROM not
used); `lib/titan_voice/native/sam` is Titan Voice's own. The Eloquence glottal pulse in `tvox_dsp.c` (the Klatt-only
fallback) follows openevv as well. `lib/libopus-0.dll` is **libopus** 1.4 (Xiph.Org, BSD-3-Clause), the copy
Titan itself ships in `data/lib`.

## Files

- `__engine__.py` - the engine (the `TitanTTSEngine` interface, variants, timbres, languages, the worker pool)
- `tvox_worker.py` - the synthesis process (one JSON line in, a header line and PCM out; `part` for a share)
- `lib/titan_voice/opus_audio.py` - the Opus container and its reader (by offset) and encoder; `lib/libopus-0.dll`
- `lib/titan_voice/` - Titan Voice; `langs/` the language packs, `levels.py` the levels builder, `styles/`
  the styled timbres, `espeak/` and `native/` (the C back ends and `tvox_styles.dll`) what they need
- `lib/titan_voice/data/concat/` - the recorded voices (the compact and concatenative databases), a
  package of their own; absent in the basic engine
- `lib/native/` - the C source, build scripts and licences; `lib/tvox_dsp.dll` the built library
- `languages/` - Polish and English names of the variants, timbres, voices and settings
