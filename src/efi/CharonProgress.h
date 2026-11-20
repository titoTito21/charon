#pragma once
// r401: one progress feedback for the whole of Charon. The screen reader
// (CharonResident.h, bars it sees in boot managers and firmware) and Charon's
// own long tasks (downloads and loading in the workspace installer) use the
// same tone, the same spoken steps and the same setting (0 off, 1 beeps,
// 2 speech every 10%, 3 both). Moved here unchanged from the reader.
// GPL-2.0-or-later.
#include <stddef.h>
#include <stdint.h>
namespace hakefi {
namespace progressfb {
// NVDA progress beep: 110 Hz * 2^(percent/25), 40 ms, one beep per change.
inline unsigned Hertz(int percent) {
    static const uint16_t steps[25]={1000,1028,1057,1087,1117,1149,1181,1214,1248,1283,1320,1357,1395,1434,1474,1516,1558,1602,1647,1693,1741,1790,1840,1892,1945};
    if(percent<0)percent=0;
    if(percent>100)percent=100;
    return unsigned((110u<<(percent/25))*steps[percent%25]/1000);
}
constexpr unsigned toneMs=40;
// Speech: the first value, every new ten and the end.
inline bool SpeakAt(int previous,int percent) {return previous<0 || percent/10!=previous/10 || percent==100;}
// "40 procent" (64 units). In the explicit mode the unit swears, varied per step.
inline void Phrase(int percent,bool polish,bool explicitMessages,unsigned variety,char16_t* text) {
    size_t at=0;
    if(percent>=100)text[at++]=u'1';
    if(percent>=10)text[at++]=char16_t(u'0'+(percent/10)%10);
    text[at++]=char16_t(u'0'+percent%10);
    const char16_t* unit=polish?u" procent":u" percent";
    if(explicitMessages) {
        // r375: progress said explicitly too, varied per step.
        static const char16_t* plUnit[]={u" procent, kurwa",u" procent tego mielenia, ja pierdolę",u" procent, czekaj, łamago",u" procent, ślimaczy się to w chuj",u" procent, ja pierdolę, jak wolno",u" procent i dalej mieli, w chuj"};
        static const char16_t* enUnit[]={u" percent, damn it",u" percent of this grinding, fuck it",u" percent, wait, numpty",u" percent, slow as fuck",u" percent, Jesus, so slow, damn it",u" percent and still grinding, shit"};
        unit=(polish?plUnit:enUnit)[(unsigned(percent/10)+variety)%6];
        if(percent>=100)unit=polish?u" procent, kurwa, wreszcie":u" percent, fucking finally";
    }
    while(*unit && at<63)text[at++]=*unit++;
    text[at]=0;
}
}  // namespace progressfb
}  // namespace hakefi
