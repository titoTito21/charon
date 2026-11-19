// Charon loadable synthesizer modules (r393). GPL-2.0-or-later (this header);
// each module carries its own engine licence.
//
// A module is a separate file \EFI\HAK\synths\<name>.tts on Charon's disk:
// a PE32+ x64 EFI boot service driver image (subsystem 11; user's choice
// 2026-11-18: firmware keeps a driver resident, no self-copy, no RWX pages),
// never built into
// CHARON.EFI or CHARONVOICE.EFI. Its data (eSpeak NG dictionaries, RHVoice
// voices) lies beside it in \EFI\HAK\synths\<name>\ and may be compressed.
//
// Modules are add-ons: none has to exist. Charon lists a synthesizer only
// when its manifest \EFI\HAK\synths\<name>.manifest is present (UTF-8 text,
// "key = value" lines, '#' comments), e.g.
//   abi = 3   (2 still accepted)
//   name = eSpeak NG
//   version = 1.52.0
//   module = espeak.tts
//   data = espeak
//   licence = GPL-3.0-or-later
//   voice = espeak-pl | eSpeak NG polski | pl
//   voice = espeak-en | eSpeak NG English | en
//   sha256 = <hex of the .tts file>
// Charon reads the manifest without loading the module (the voice lists in
// the settings come from it), checks sha256 before LoadImage, and loads a
// module only when one of its voices is chosen. A manifest whose module is
// missing or does not match is listed as unusable, never loaded.
//
// Loading: Charon LoadImage()s the file and StartImage()s it with
// LoadOptions = UTF-16 path of the module's data folder (for example
// u"\\EFI\\HAK\\synths\\espeak"), LoadOptionsSize in bytes incl. the 0.
// The module's entry point opens its data index from the volume it was loaded
// from (its LoadedImage->DeviceHandle), installs CharonSynthModule on a NEW
// handle (InstallProtocolInterface, never on the image handle) and returns
// EFI_SUCCESS (a driver stays resident). It loads no voice yet: Charon calls
// Prepare for the chosen voice (only that voice's data is decompressed). It must not print to ConOut, read keys, call
// ExitBootServices or allocate more than 64 MiB. Returning an error means
// "module unusable"; Charon then keeps speaking with TVox and says which
// file failed.
//
// Threading: Charon calls every function at TPL_APPLICATION or TPL_CALLBACK
// (its timer), one call at a time, never re-entrantly. Render must return
// within about 20 ms of work for maxSamples <= 1024 (Charon pumps audio
// between calls); Begin may take up to 50 ms and must not synthesize the
// whole text at once.
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace hakefi {
// {6c7e5a21-3b9d-4f0e-9a41-c4a7e2d51393}
// Every function uses the UEFI calling convention (EFIAPI = ms_abi on x64),
// so modules built with EDK2/MSVC or clang interoperate.
#ifndef CHARON_SYNTH_API
#define CHARON_SYNTH_API __attribute__((ms_abi))
#endif
#define CHARON_SYNTH_MODULE_GUID {0x6c7e5a21,0x3b9d,0x4f0e,{0x9a,0x41,0xc4,0xa7,0xe2,0xd5,0x13,0x93}}
inline constexpr uint32_t charonSynthAbi=3;      // 2: driver image + Prepare; 3: + SetPersona (r397)
inline constexpr uint32_t charonSynthAbiOldest=2;   // Charon still loads ABI 2 modules (no SetPersona)
// r397 Emacspeak ACSS persona (ABI 3). Factors: 1 = the voice's own value.
//   range      pitch range (0.05 monotone .. 2.5 animated)
//   stress     loudness contrast of stressed syllables (0.3 .. 2.2)
//   richness   voice quality (0.4 breathy .. 1.8 tense and rich)
//   smoothness 1, or < 1 for a smoothed (darker) voice
//   family     0 the voice itself, 1..7 another voice "family" for another
//              style (bold, selection, warm colour, title, small print,
//              another font, dim): a module with voice variants picks one.
struct CharonSynthPersona {uint32_t size;uint32_t family;double range,stress,richness,smoothness;};
// Bits SetPersona returns for the dimensions it applies itself (Charon
// approximates the others after the module's audio).
inline constexpr uint32_t charonPersonaRange=1,charonPersonaStress=2,charonPersonaRichness=4,charonPersonaSmoothness=8,charonPersonaFamily=16;
struct CharonSynthVoice {
    char id[32];          // stable id, ASCII: "espeak-pl", "rhvoice-natan", "rhvoice-alan"
    char16_t name[48];    // spoken name: u"eSpeak NG polski", u"Natan"
    char language[8];     // BCP 47 primary: "pl", "en"
    uint32_t sampleRate;  // PCM rate of Render for this voice (Charon resamples)
};
struct CharonSynthModule {
    uint32_t abi;                 // charonSynthAbi
    uint32_t voiceCount;          // <= 16
    char16_t engine[32];          // u"eSpeak NG 1.52", u"RHVoice 1.16"
    const CharonSynthVoice* voices;
    // Loads one voice's data (may take seconds; Charon calls it before
    // speaking with it, at TPL_APPLICATION or TPL_CALLBACK: r395 prepares the
    // other language's voice when the reader first speaks that language). A voice not prepared makes
    // Begin fail with -3. Preparing another voice may release this one.
    int (CHARON_SYNTH_API *Prepare)(CharonSynthModule* self,uint32_t voice);
    // Starts a new utterance (cancels the previous one). text: UTF-8, may
    // hold Charon's in-band switches ESC 'p' / ESC 'e' (language) and
    // ESC 'v' + 4 bytes / ESC 'i' + 2 bytes (formatting): a module that does
    // not use them skips them. rate, pitch: 1.0 = the voice's default,
    // 0.5..2.0 (Charon's NVDA-style 0..100% scale); volume 0..1.
    // Returns 0 or a negative error (-1 invalid, -2 no memory, -3 voice).
    int (CHARON_SYNTH_API *Begin)(CharonSynthModule* self,uint32_t voice,const char* text,size_t bytes,double rate,double pitch,double volume);
    // Writes up to maxSamples mono int16 samples at the voice's sampleRate.
    // Returns the number written (0 is allowed while it prepares), or -1 at
    // the end of the utterance, or < -1 on error.
    int (CHARON_SYNTH_API *Render)(CharonSynthModule* self,int16_t* out,size_t maxSamples);
    void (CHARON_SYNTH_API *Cancel)(CharonSynthModule* self);
    void* context;                // module private
    // ABI 3 (r397): the persona for the following Begin calls (until the
    // next SetPersona). Returns the charonPersona* bits it honours, or < 0.
    // Present only when abi >= 3 (an ABI 2 module's structure ends above).
    int (CHARON_SYNTH_API *SetPersona)(CharonSynthModule* self,const CharonSynthPersona* persona);
};
}  // namespace hakefi
