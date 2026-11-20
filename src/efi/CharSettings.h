#pragma once
#include <stddef.h>
#include <stdint.h>
namespace hakefi {
// Only implemented controls are persisted. Language 0 follows installation /
// firmware locale, 1 forces English, 2 forces Polish. No audio capabilities are
// implied by this settings format. V4 bytes20/21 select start/background
// melodies (0 off, 1..4 packaged original HAK melodies). V1/V2/V3 decode
// to the defaults: start melody 1, background melody off (r355: the user
// asked for no background melody unless chosen); UI effects and background selection remain independent.
// V5 byte22 = speech rate in tenths 5..20 (default 10). Older versions decode to 10.
// V6 byte23 = speech volume 1..10, byte24 = effects volume 0..10 (tenths of the
// default mix), byte25 bits0-1 = boot messages 0 off / 1 short / 2 detailed and
// bits2-3 = progress reporting 0 off / 1 beeps / 2 speech every 10% / 3 both,
// byte26 = remember the last started device as Continue target (0/1),
// byte27 = automatic Continue delay in seconds (one of autoDelays; 255 never).
// Older versions decode to 10/10/short/off/10 s, the r354 behaviour.
// V7 (r382) byte26 bits1-3 = welcome: bit0 hint to open the options menu,
// bit1 welcome sound, bit2 spoken greeting. Older versions decode to all on.
// V7 records are 40 bytes (V1-V6 32): byte28 menu voice pitch 5..20 (tenths),
// byte29/30/31 screen reader rate 5..20, pitch 5..20, volume 1..10 (the
// resident postboot reader speaks with its own voice), bytes32-35 zero,
// CRC at 36. Older versions decode to the menu's rate and volume, pitch 10.
// byte32 = detail: bit0 disk name, bit1 boot manager name, bit2 list positions (default 7).
// byte33 = countdowns (r384): bit0 say the time left every 10 s, bit1 a tick
// every second and a 500 ms tone at the end (default 0; r389 V8: tones on,
// and V7 files with 0 read as tones on, since the user asked for them).
// V9 (r390) records are 48 bytes, CRC at 44: byte36 screen reader symbol
// level 0 none / 1 some / 2 most / 3 all (NVDA; default 1), byte37 capitals
// bit0 say "cap", bit1 beep, bit2 higher pitch (default 1), byte38 screen
// text language 0 automatic / 1 menu language / 2 English / 3 Polish
// (default 0), byte39 reader flags (readerFlag*: messages, typed characters, automatic reading, cursor echo, typed words, hints; default all but typed words), 40-43 zero.
// Older versions decode to the defaults.
// V10 (r393, magic "CHARS00A", 48 bytes): byte40 text formatting in speech
// (formatting::flag*: bit0 voice, bit1 sound icons, bits2-5 colours, bright/bold,
// font and size, inverse; default 60 = attributes on, outputs off), byte41
// reader extras: bits0-1 hidden boot-manager menus 0 off / 1 announce and wait
// 2 s / 2 always reveal, bit2 stop boot-manager countdowns, bit3 braille
// attribute dots, bit4 firmware pronunciation dictionary (default 25), 42-43 zero.
// V11 (r393, magic "CHARS00B"): byte42 synthesizer add-on (0 TVox, else the
// manifest key, CharonSynthHost.h), byte43 its voice (index in the manifest).
// V12 (r397, magic "CHARS00C"): byte40 bit6 Emacspeak event icons, bit7 voice
// families; byte41 bits5-6 stereo position 0 off / 1 icons / 2 speech and icons.
// Older versions decode to all three off.
constexpr uint8_t autoNever=255;
constexpr uint8_t autoDelays[]={0,3,5,10,15,20,30,60,autoNever};
constexpr size_t autoDelayCount=sizeof(autoDelays);
inline bool ValidAutoDelay(uint8_t value) {for(auto delay:autoDelays)if(delay==value)return true;return false;}
inline size_t AutoDelayIndex(uint8_t value) {for(size_t i=0;i<autoDelayCount;++i)if(autoDelays[i]==value)return i;return 3;}
struct CharSettings {uint64_t sequence=0;uint8_t language=0,startMelody=1,backgroundMelody=0,rate=10;bool wrap=false,sounds=true,explicitMessages=false;
    uint8_t speechVolume=10,effectsVolume=10,verbosity=1;bool rememberLast=false;uint8_t autoSeconds=10,progress=1;
    uint8_t welcome=7,pitch=10,readerRate=10,readerPitch=10,readerVolume=10,detail=7,timers=2;
    uint8_t messageRate=14,messagePitch=10;   // r389 V8 bytes 34/35: the boot-message voice (tenths)
    uint8_t punctuation=1,capitals=1,screenLanguage=0,readerFlags=47;   // r390 V9 bytes 36-39
    uint8_t formatFlags=255,readerExtra=153;   // r399 non-monotone voice (colour/font/style/position) + per-event icons
    uint8_t synthKey=0,synthVoice=0;};      // r393 V11 bytes 42-43
constexpr uint8_t readerFlagMessages=1,readerFlagTyped=2,readerFlagAutoRead=4,readerFlagCursor=8,readerFlagWords=16,readerFlagHints=32;
constexpr uint8_t readerFlagsDefault=47;   // all but typed words (NVDA's defaults)
constexpr uint8_t timerSpeech=1,timerSound=2;
constexpr uint8_t extraHiddenMask=3,extraStopCountdown=4,extraBraille=8,extraDictionary=16,extraLinger=128 /* r400: loading cue after ExitBootServices */,readerExtraDefault=153;
// r390 (user): rate and pitch are said on NVDA's scale: the default (1.0x,
// 10 tenths) is 50%, the slowest / lowest (0.5x) 0%, the fastest / highest
// (2.0x) 100%. Stored values are unchanged (tenths 5..20).
inline unsigned VoicePercent(unsigned tenths) {
    if(tenths<=5)return 0;
    if(tenths<=10)return (tenths-5)*10;
    return tenths>=20?100:50+(tenths-10)*5;
}
constexpr uint8_t detailDisk=1,detailManager=2,detailPosition=4;
constexpr uint8_t welcomeHint=1,welcomeSound=2,welcomeSpeech=4;
constexpr size_t charSettingsBytes=48,charSettingsV7Bytes=40,charSettingsLegacyBytes=32;
inline size_t SettingsBytesFor(char version) {return version>='9'?charSettingsBytes:version>='7'?charSettingsV7Bytes:charSettingsLegacyBytes;}
inline uint32_t SettingsCRC(const uint8_t* bytes,size_t length) {
    uint32_t crc=0xffffffff;
    for(size_t i=0;i<length;++i) {crc^=bytes[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&uint32_t(0-int32_t(crc&1)));}
    return ~crc;
}
inline bool EncodeSettings(const CharSettings& value,uint8_t* bytes,size_t capacity) {
    if(!bytes || capacity!=charSettingsBytes || !value.sequence || value.language>2 || value.startMelody>4 || value.backgroundMelody>4 || value.rate<5 || value.rate>20
        || value.speechVolume<1 || value.speechVolume>10 || value.effectsVolume>10 || value.verbosity>2 || value.progress>3 || !ValidAutoDelay(value.autoSeconds) || value.welcome>7
        || value.pitch<5 || value.pitch>20 || value.readerRate<5 || value.readerRate>20 || value.readerPitch<5 || value.readerPitch>20
        || value.readerVolume<1 || value.readerVolume>10 || value.detail>7 || value.timers>3
        || value.messageRate<5 || value.messageRate>20 || value.messagePitch<5 || value.messagePitch>20
        || value.punctuation>3 || value.capitals>7 || value.screenLanguage>3 || value.readerFlags>63
        || (value.readerExtra&3)>2 || ((value.readerExtra>>5)&3)>2 || value.synthVoice>15)return false;
    for(size_t i=0;i<capacity;++i)bytes[i]=0;
    const char magic[]="CHARS00C";for(unsigned i=0;i<8;++i)bytes[i]=uint8_t(magic[i]);
    for(unsigned i=0;i<8;++i)bytes[8+i]=uint8_t(value.sequence>>(i*8));
    bytes[16]=value.language;bytes[17]=value.wrap?1:0;
    bytes[18]=value.sounds?1:0;bytes[19]=value.explicitMessages?1:0;bytes[20]=value.startMelody;bytes[21]=value.backgroundMelody;bytes[22]=value.rate;
    bytes[23]=value.speechVolume;bytes[24]=value.effectsVolume;bytes[25]=uint8_t(value.verbosity|(value.progress<<2));bytes[26]=uint8_t((value.rememberLast?1:0)|(value.welcome<<1));bytes[27]=value.autoSeconds;
    bytes[28]=value.pitch;bytes[29]=value.readerRate;bytes[30]=value.readerPitch;bytes[31]=value.readerVolume;bytes[32]=value.detail;bytes[33]=value.timers;bytes[34]=value.messageRate;bytes[35]=value.messagePitch;
    bytes[36]=value.punctuation;bytes[37]=value.capitals;bytes[38]=value.screenLanguage;bytes[39]=value.readerFlags;
    bytes[40]=value.formatFlags;bytes[41]=value.readerExtra;bytes[42]=value.synthKey;bytes[43]=value.synthVoice;
    const auto crc=SettingsCRC(bytes,44);for(unsigned i=0;i<4;++i)bytes[44+i]=uint8_t(crc>>(i*8));
    return true;
}
inline bool DecodeSettings(const uint8_t* bytes,size_t length,CharSettings& result) {
    if(!bytes || (length!=charSettingsBytes && length!=charSettingsV7Bytes && length!=charSettingsLegacyBytes))return false;
    const char magic[]="CHARS00";for(unsigned i=0;i<7;++i)if(bytes[i]!=uint8_t(magic[i]))return false;
    const char version=char(bytes[7]);
    if(version<'1' || (version>'9' && version!='A' && version!='B' && version!='C') || length!=SettingsBytesFor(version))return false;
    const size_t crcAt=version>='9'?44:version>='7'?36:28;
    uint32_t crc=0;for(unsigned i=0;i<4;++i)crc|=uint32_t(bytes[crcAt+i])<<(i*8);
    if(crc!=SettingsCRC(bytes,crcAt) || bytes[16]>2 || bytes[17]>1)return false;
    if(bytes[18]>(version=='1'?0:1))return false;
    if(bytes[19]>(version>='3'?1:0))return false;
    if(bytes[20]>(version>='4'?4:0) || bytes[21]>(version>='4'?4:0))return false;
    if(version>='5'?(bytes[22]<5 || bytes[22]>20):bytes[22]!=0)return false;
    if(version>='6') {
        if(bytes[23]<1 || bytes[23]>10 || bytes[24]>10 || (bytes[25]&3)>2 || bytes[25]>15 || bytes[26]>(version>='7'?15:1) || !ValidAutoDelay(bytes[27]))return false;
    } else for(unsigned i=23;i<28;++i)if(bytes[i])return false;
    if(version>='7') {
        for(unsigned i=28;i<31;++i)if(bytes[i]<5 || bytes[i]>20)return false;
        if(bytes[31]<1 || bytes[31]>10 || bytes[32]>7 || bytes[33]>3)return false;
        if(version=='7'?(bytes[34] || bytes[35]):(bytes[34]<5 || bytes[34]>20 || bytes[35]<5 || bytes[35]>20))return false;
    }
    if(version>='9') {
        if(bytes[36]>3 || bytes[37]>7 || bytes[38]>3 || bytes[39]>63)return false;
        if(version>='A') {if((version<'C' && (bytes[40]>63 || bytes[41]>31)) || bytes[41]>127 || ((bytes[41]>>5)&3)>2 || (bytes[41]&3)>2 || (version=='A' && (bytes[42] || bytes[43])) || bytes[43]>15)return false;}
        else for(unsigned i=40;i<44;++i)if(bytes[i])return false;
    }
    CharSettings candidate;for(unsigned i=0;i<8;++i)candidate.sequence|=uint64_t(bytes[8+i])<<(i*8);
    if(!candidate.sequence)return false;
    candidate.language=bytes[16];candidate.wrap=bytes[17]!=0;
    candidate.sounds=version=='1' || bytes[18]!=0;candidate.explicitMessages=version>='3' && bytes[19]!=0;
    if(version>='4') {candidate.startMelody=bytes[20];candidate.backgroundMelody=bytes[21];}
    if(version>='5')candidate.rate=bytes[22];
    if(version>='6') {candidate.speechVolume=bytes[23];candidate.effectsVolume=bytes[24];candidate.verbosity=bytes[25]&3;candidate.progress=uint8_t(bytes[25]>>2);
        candidate.rememberLast=(bytes[26]&1)!=0;candidate.autoSeconds=bytes[27];
        if(version>='7')candidate.welcome=uint8_t(bytes[26]>>1);}
    candidate.readerRate=candidate.rate;candidate.readerVolume=candidate.speechVolume;
    if(version>='7') {candidate.pitch=bytes[28];candidate.readerRate=bytes[29];candidate.readerPitch=bytes[30];candidate.readerVolume=bytes[31];candidate.detail=bytes[32];candidate.timers=bytes[33];
        if(version=='7' && !candidate.timers)candidate.timers=timerSound;
        if(version>='8') {candidate.messageRate=bytes[34];candidate.messagePitch=bytes[35];}}
    if(version>='9') {candidate.punctuation=bytes[36];candidate.capitals=bytes[37];candidate.screenLanguage=bytes[38];candidate.readerFlags=bytes[39];}
    if(version>='A') {candidate.formatFlags=bytes[40];candidate.readerExtra=bytes[41];}
    if(version>='B') {candidate.synthKey=bytes[42];candidate.synthVoice=bytes[43];}
    result=candidate;return true;
}
inline bool SameSettings(const CharSettings& a,const CharSettings& b) {
    return a.language==b.language && a.wrap==b.wrap && a.sounds==b.sounds && a.explicitMessages==b.explicitMessages
        && a.startMelody==b.startMelody && a.backgroundMelody==b.backgroundMelody && a.rate==b.rate
        && a.speechVolume==b.speechVolume && a.effectsVolume==b.effectsVolume && a.verbosity==b.verbosity
        && a.rememberLast==b.rememberLast && a.autoSeconds==b.autoSeconds && a.progress==b.progress && a.welcome==b.welcome
        && a.pitch==b.pitch && a.readerRate==b.readerRate && a.readerPitch==b.readerPitch && a.readerVolume==b.readerVolume && a.detail==b.detail && a.timers==b.timers && a.messageRate==b.messageRate && a.messagePitch==b.messagePitch
        && a.punctuation==b.punctuation && a.capitals==b.capitals && a.screenLanguage==b.screenLanguage && a.readerFlags==b.readerFlags
        && a.formatFlags==b.formatFlags && a.readerExtra==b.readerExtra && a.synthKey==b.synthKey && a.synthVoice==b.synthVoice;
}
inline bool ChooseSettings(const uint8_t* a,size_t aSize,const uint8_t* b,size_t bSize,CharSettings& result) {
    CharSettings first,second;const bool validA=DecodeSettings(a,aSize,first),validB=DecodeSettings(b,bSize,second);
    if(!validA && !validB)return false;
    if(validA && validB && first.sequence==second.sequence && !SameSettings(first,second))return false;
    result=!validB || (validA && first.sequence>=second.sequence) ? first : second;return true;
}
inline bool PolishSettings(const CharSettings& value,const char* locale) {
    if(value.language)return value.language==2;
    if(!locale)return false;
    return (locale[0]=='p'||locale[0]=='P') && (locale[1]=='l'||locale[1]=='L')
        && (!locale[2] || locale[2]=='-' || locale[2]=='_' || locale[2]=='.' || locale[2]=='@');
}
}
