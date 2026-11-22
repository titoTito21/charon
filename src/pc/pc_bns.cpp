// CharonPC - a Braille 'n Speak serial speech synthesizer on COM1.
// GPL-2.0-or-later.
//
// DOS and Windows 9x screen readers of the period (JAWS for DOS, Provox,
// ASAP, Window-Eyes 9x) speak through external synthesizers on a serial
// port. Bochs's COM1 runs in "file" mode with the device charon:bns, so
// every byte the guest transmits arrives here (pc_posix.c FD_BNS) and is
// spoken with Charon's published voice (MenuVoice, src/efi/BootMenuVoice.h).
//
// Braille 'n Speak protocol, as far as the readers use it:
//   printable text       buffered
//   CR (0x0D)            ends an utterance: it is spoken
//   Ctrl+X (0x18)        silence: stop speaking, drop buffered text
//   Ctrl+E (0x05) ...    command: optional digits then one letter
//                        (rate, pitch, volume, punctuation ...); consumed
//                        and logged, not applied yet (PLAN.txt)
//   other controls       ignored (LF, BEL)
// Lines that arrive in a burst are joined into one utterance (MenuVoice's
// SpeakUtf8 cancels the previous utterance, so speaking each line at once
// would leave only the last one audible). Guest text is code page 437.
#include <string.h>
#include "pc_glue.h"
#include "pc_platform.h"

namespace {

constexpr uint64_t joinGapUs = 120000;    // CR seen and no byte for this long: speak
constexpr uint64_t idleGapUs = 400000;    // no CR, but no byte for this long: speak

const uint16_t cp437[128] = {
    0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,
    0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x20A7,0x0192,
    0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,
    0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
    0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
    0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
    0x03B1,0x00DF,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,0x03A6,0x0398,0x03A9,0x03B4,0x221E,0x03C6,0x03B5,0x2229,
    0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,0x00B0,0x2219,0x00B7,0x221A,0x207F,0x00B2,0x25A0,0x00A0};

char text[2048];
size_t used;
bool lineEnded;
uint64_t lastByteUs;
int command;            // 0: none, 1: after Ctrl+E (digits may follow)
char commandText[16];
size_t commandUsed;

void Log(const char* prefix, const char* body)
{
    pc_log_write(prefix, strlen(prefix));
    pc_log_write(body, strlen(body));
    pc_log_write("\n", 1);
}

void Flush()
{
    while (used && text[used - 1] == ' ') --used;
    text[used] = 0;
    size_t start = 0;
    while (text[start] == ' ') ++start;
    if (text[start]) {
        pc::ReaderGuestSpoke();   // r407: a guest screen reader talks: Charon's control reader steps back
        const bool spoken = pc::Say(text + start);
        Log(spoken ? "BNS speak: " : "BNS (no voice): ", text + start);
    }
    used = 0;
    lineEnded = false;
}

void Append(uint32_t c)
{
    char utf8[3];
    size_t n = 0;
    if (c < 0x80) utf8[n++] = (char)c;
    else if (c < 0x800) { utf8[n++] = (char)(0xC0 | (c >> 6)); utf8[n++] = (char)(0x80 | (c & 0x3F)); }
    else { utf8[n++] = (char)(0xE0 | (c >> 12)); utf8[n++] = (char)(0x80 | ((c >> 6) & 0x3F)); utf8[n++] = (char)(0x80 | (c & 0x3F)); }
    if (used + n + 1 >= sizeof text) Flush();
    memcpy(text + used, utf8, n);
    used += n;
}

}  // namespace

extern "C" void pc_bns_write(const unsigned char* data, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i) {
        const unsigned char b = data[i];
        lastByteUs = pc_usec();
        if (command) {
            if (commandUsed + 1 < sizeof commandText) commandText[commandUsed++] = (char)b;
            if (b >= '0' && b <= '9') continue;
            commandText[commandUsed] = 0;
            Log("BNS command: Ctrl+E ", commandText);
            command = 0;
            continue;
        }
        if (b == 0x05) { command = 1; commandUsed = 0; continue; }
        if (b == 0x18) { used = 0; lineEnded = false; pc::StopVoice(); Log("BNS: ", "silence (Ctrl+X)"); continue; }
        if (b == 0x0D) {
            if (used && text[used - 1] != ' ') Append(' ');
            lineEnded = true;
            continue;
        }
        if (b < 0x20 || b == 0x7F) continue;
        if (lineEnded && used > 1500) Flush();
        Append(b < 0x80 ? b : cp437[b - 0x80]);
    }
}

void pc::BnsPoll()
{
    if (!used) return;
    const uint64_t quiet = pc_usec() - lastByteUs;
    if ((lineEnded && quiet >= joinGapUs) || quiet >= idleGapUs) Flush();
}
