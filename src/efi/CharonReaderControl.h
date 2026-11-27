// Charon resident voice driver <-> Charon menu application (r356).
// GPL-2.0-or-later. Internal interface between CHARONVOICE.EFI (loaded by
// firmware from Driver####, speaks from power-on) and CHARON.EFI of the SAME
// release; a different release is ignored and Charon then runs its own voice.
// Not a public HAK ABI: AudioEffects uses the compiler's default calling
// convention, valid only between images of one build.
#pragma once
#include "BootMenuVoice.h"
#include "CharonCues.h"
namespace hakefi {
static efi_guid readerControlGuid={0x5f0c2c8e,0x8a2d,0x4c8b,{0x9e,0x31,0x6a,0x0d,0x71,0x2b,0x44,0x55}};
constexpr uint32_t charonRelease=411;  // r410 (second part): ReaderControl::more (custom profile, reader bar, absolute colours)
// was: charonRelease=410;  // r410: reading bits 30-31 verbosity profile; Resume progress bits 2-4 formatting groups of the screen model
// was: charonRelease=401;  // r401: Resume polish bit 0x20 = review only (Charon's own screens)
// was: charonRelease=397;  // r397: reading also carries event icons, voice families, stereo
// was: charonRelease=393;  // r393: reading carries formatting and reader extras
// was: charonRelease=390;  // r390: Resume carries reading (symbols, capitals, language, flags); r389: AudioEffects boot-message voice; r382: SetPitch
// r390 reading word: symbol level | capitals<<2 | screen language<<5 | reader flags<<8 (CharSettings V9);
// r393: | formatting<<14 | reader extras<<20 (CharSettings V10).
inline uint32_t PackReading(uint8_t punctuation,uint8_t capitals,uint8_t screenLanguage,uint8_t flags,uint8_t format=60,uint8_t extra=25,uint8_t profile=0) {
    return uint32_t(punctuation&3)|(uint32_t(capitals&7)<<2)|(uint32_t(screenLanguage&3)<<5)|(uint32_t(flags&63)<<8)|(uint32_t(format&63)<<14)|(uint32_t(extra&31)<<20)
        |(uint32_t((format>>6)&3)<<25)|(uint32_t((extra>>5)&3)<<27)|(uint32_t((extra>>7)&1)<<29)   // r397: event icons, voice families, stereo
        |(uint32_t(profile<=2?profile:0)<<30);   // r410
}
// r410 (settings V14): what does not fit the reading word travels in ReaderControl::more, written by the caller
// before Resume: bits 0-1 verbosity profile (0 normal, 1 beginner, 2 expert, 3 custom), bits 2-13 the custom
// profile's items, bit 14 reader bar, bit 15 absolute colours, bit 31 = valid.
constexpr uint32_t PackMore(uint8_t profile,uint16_t items,bool bar,bool absolute) {return uint32_t(profile&3)|(uint32_t(items&4095)<<2)|(bar?1u<<14:0)|(absolute?1u<<15:0)|(1u<<31);}
template<class Options> inline void ApplyMore(Options& o,uint32_t more) {
    if(!(more>>31))return;
    o.profile=uint8_t(more&3);o.profileItems=uint16_t((more>>2)&4095);o.readerBar=((more>>14)&1)!=0;o.absoluteColours=((more>>15)&1)!=0;
}
inline uint8_t ReadingProfile(uint32_t reading) {const uint8_t p=uint8_t(reading>>30);return p<=2?p:0;}
// r410: the Resume call's progress byte: bits 0-1 progress reporting, bits 2-4 formatting groups.
inline uint8_t PackProgress(uint8_t progress,uint8_t groups) {return uint8_t((progress&3)|((groups&7)<<2));}
// r397: the whole formatting byte (bits 0-5 at 14, bits 6-7 at 25) and the stereo mode (27-28).
inline uint8_t ReadingFormat(uint32_t reading) {return uint8_t(((reading>>14)&63)|(((reading>>25)&3)<<6));}
inline uint8_t ReadingStereo(uint32_t reading) {const uint8_t s=uint8_t((reading>>27)&3);return s<3?s:0;}
struct ReaderControl {
    uint32_t version,size,release,reserved;
    MenuVoice* voice;
    AudioEffects* effects;
    // Pause screen reading while Charon's own menu speaks (the menu owns the
    // voice then); resume with the menu's language and levels before Charon
    // starts another program or returns to the firmware boot order.
    efi_status (EFIAPI *Suspend)(ReaderControl*);
    // voice: rate | pitch<<8 | volume<<16 | effects volume<<24 (tenths; 0 keeps the current voice).
    efi_status (EFIAPI *Resume)(ReaderControl*,const char* language,uint8_t polish,uint8_t verbosity,uint8_t progress,uint32_t voice,uint32_t reading);
    uint32_t more;   // r410: PackMore, set by the caller before Resume
};
// r407: a program that reads its own graphical screen (CharonPC reading the controls of a guest
// Windows) installs this GUID, with a null interface, on its image handle while that screen is up.
// The resident reader then leaves pictures alone: no "graphical screen" description, no pixel reading.
static efi_guid graphicsOwnerGuid={0x3d1b7c52,0x61e4,0x4a0f,{0xb7,0x29,0x5e,0x88,0x1c,0x40,0x9a,0x13}};
inline ReaderControl* FindReaderControl(efi_boot_services* boot) {
    ReaderControl* control=nullptr;
    if(!boot || boot->LocateProtocol(&readerControlGuid,nullptr,(void**)&control)!=EFI_SUCCESS || !control)return nullptr;
    if(control->version!=1 || control->size!=sizeof(ReaderControl) || control->release!=charonRelease
        || !ValidMenuVoice(control->voice) || !control->Suspend || !control->Resume)return nullptr;
    return control;
}
}
