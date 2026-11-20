#pragma once
// r401: the voice marker Charon puts before a list item it speaks itself: the
// position in the list maps to modulation (pitch range), as screen rows do
// for the screen reader, so moving through a list is not monotone. One code
// for the boot menu (r399) and for every list and question of the workspace
// and the installer. Used only when voice formatting is on (formatFlags bit 0).
// The marker is the reader's ESC 'f' + 12-character payload (CharonFormatting.h).
// GPL-2.0-or-later.
#include <stddef.h>
#include <stdint.h>
namespace hakefi {
namespace menuvoice {
constexpr size_t markUnits=15;   // ESC, 'f', 12 payload characters, a space
// Writes the marker (no terminator); `out` needs markUnits units. Returns markUnits.
inline size_t PositionMark(size_t index,char16_t* out) {
    int rangeStep=4-int(index)*8/12;if(rangeStep<-4)rangeStep=-4;if(rangeStep>4)rangeStep=4;
    size_t w=0;
    out[w++]=0x001b;out[w++]=u'f';
    out[w++]=u'm';out[w++]=u'm';out[w++]=u'm';out[w++]=u'0';out[w++]=u'-';out[w++]=u'a';
    out[w++]=char16_t(u'm'+rangeStep);out[w++]=u'm';out[w++]=u'm';out[w++]=u'0';out[w++]=u'-';out[w++]=u'-';out[w++]=u' ';
    return w;
}
}  // namespace menuvoice
}  // namespace hakefi
