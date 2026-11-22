// CharonPC - the Bochs display library "charon". GPL-2.0-or-later.
//
// Text modes are written as text to the UEFI console (Simple Text Output),
// cell by cell where they changed, so Charon's resident reader hears the
// emulated screen exactly as it hears CharonDOS. Graphics modes go to the
// Graphics Output Protocol (centred, unscaled). Keys come from Simple Text
// Input Ex and are typed into the emulated PS/2 keyboard (US layout; Polish
// letters and Right Alt as AltGr, Polish programmer's layout, r405).
// Charon's reader keeps its own keys (Ctrl+Alt+key, Alt+arrows, Alt+\);
// Ctrl+Shift+F12 switches the emulated machine off.
#include "bochs.h"
#include "param_names.h"
#include "iodev.h"
#include "gui/gui.h"
#include "plugin.h"
#include "cpu/cpu.h"
bool charon_acpi_power_button(void);   // added to Bochs's iodev/acpi.cc by tools/build-charon-bochs.py
#include "pc_glue.h"
#include "pc_platform.h"

namespace pc { bool userQuit; }

class bx_charon_gui_c : public bx_gui_c {
public:
    bx_charon_gui_c() {}
    DECLARE_GUI_VIRTUAL_METHODS()
};

static bx_charon_gui_c* theGui = nullptr;
// Bochs's macro does not use all parameters of the plugin entry.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
IMPLEMENT_GUI_PLUGIN_CODE(charon)
#pragma clang diagnostic pop
#define LOG_THIS theGui->

namespace {

const char16_t cp437[256] = {
    u' ',0x263A,0x263B,0x2665,0x2666,0x2663,0x2660,0x2022,0x25D8,0x25CB,0x25D9,0x2642,0x2640,0x266A,0x266B,0x263C,
    0x25BA,0x25C4,0x2195,0x203C,0x00B6,0x00A7,0x25AC,0x21A8,0x2191,0x2193,0x2192,0x2190,0x221F,0x2194,0x25B2,0x25BC,
    0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,
    0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,
    0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x5B,0x5C,0x5D,0x5E,0x5F,0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,
    0x68,0x69,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x7B,0x7C,0x7D,0x7E,0x2302,
    0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,
    0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x20A7,0x0192,
    0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,
    0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
    0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
    0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
    0x03B1,0x00DF,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,0x03A6,0x0398,0x03A9,0x03B4,0x221E,0x03C6,0x03B5,0x2229,
    0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,0x00B0,0x2219,0x00B7,0x221A,0x207F,0x00B2,0x25A0,0x00A0};

constexpr unsigned maxColumns = 132, maxRows = 60;
uint16_t shown[maxRows][maxColumns];   // char | attribute << 8 last written to the console
bool forceText = true;
unsigned textColumns = 80, textRows = 25;
int consoleAttribute = -1;
bool cursorShown = true;

hakefi::Gop* gop;
bool gopLooked;
uint32_t hostPalette[256];
unsigned originX, originY;
uint32_t tileBuffer[128 * 128];
uint64_t lastKeyUs;

void SetAttribute(uint8_t vga)
{
    const int efi = (vga & 0x0F) | ((vga & 0x70));
    if (efi != consoleAttribute) { consoleAttribute = efi; pc::Out->SetAttribute(pc::Out, (size_t)efi); }
}

hakefi::Gop* Gop()
{
    if (!gopLooked) {
        gopLooked = true;
        if (pc::BS->LocateProtocol(&hakefi::gopGuid, nullptr, reinterpret_cast<void**>(&gop)) != EFI_SUCCESS) gop = nullptr;
    }
    return gop;
}

// US layout: Bochs key and whether Shift is part of the character.
bool MapCharacter(char16_t c, uint32_t* key, bool* shift)
{
    static const char plain[] = "1234567890-=[];'`\\,./ ";
    static const char shifted[] = "!@#$%^&*()_+{}:\"~|<>? ";
    static const uint32_t keys[] = {BX_KEY_1, BX_KEY_2, BX_KEY_3, BX_KEY_4, BX_KEY_5, BX_KEY_6, BX_KEY_7, BX_KEY_8,
        BX_KEY_9, BX_KEY_0, BX_KEY_MINUS, BX_KEY_EQUALS, BX_KEY_LEFT_BRACKET, BX_KEY_RIGHT_BRACKET, BX_KEY_SEMICOLON,
        BX_KEY_SINGLE_QUOTE, BX_KEY_GRAVE, BX_KEY_BACKSLASH, BX_KEY_COMMA, BX_KEY_PERIOD, BX_KEY_SLASH, BX_KEY_SPACE};
    *shift = false;
    if (c >= u'a' && c <= u'z') { *key = BX_KEY_A + (c - u'a'); return true; }
    if (c >= u'A' && c <= u'Z') { *key = BX_KEY_A + (c - u'A'); *shift = true; return true; }
    for (unsigned i = 0; plain[i]; ++i) {
        if (c == (char16_t)plain[i]) { *key = keys[i]; return true; }
        if (c == (char16_t)shifted[i] && c != u' ') { *key = keys[i]; *shift = true; return true; }
    }
    switch (c) {
    case 0x08: *key = BX_KEY_BACKSPACE; return true;
    case 0x09: *key = BX_KEY_TAB; return true;
    case 0x0A: case 0x0D: *key = BX_KEY_ENTER; return true;
    case 0x1B: *key = BX_KEY_ESC; return true;
    default: return false;
    }
}

// r405: Polish letters the firmware hands over as characters (a Polish UEFI
// layout) go to the guest as Right Alt (AltGr) + letter: the Polish
// programmer's layout every Windows from 95 on has.
bool MapPolish(char16_t c, uint32_t* key, bool* shift)
{
    static const char16_t lower[] = u"\u0105\u0107\u0119\u0142\u0144\u00f3\u015b\u017a\u017c";
    static const char16_t upper[] = u"\u0104\u0106\u0118\u0141\u0143\u00d3\u015a\u0179\u017b";
    static const char letters[] = "acelnosxz";
    for (int i = 0; letters[i]; ++i) {
        if (c == lower[i]) { *key = BX_KEY_A + (letters[i] - 'a'); *shift = false; return true; }
        if (c == upper[i]) { *key = BX_KEY_A + (letters[i] - 'a'); *shift = true; return true; }
    }
    return false;
}

bool MapScan(uint16_t scan, uint32_t* key)
{
    static const uint32_t table[] = {0, BX_KEY_UP, BX_KEY_DOWN, BX_KEY_RIGHT, BX_KEY_LEFT, BX_KEY_HOME, BX_KEY_END,
        BX_KEY_INSERT, BX_KEY_DELETE, BX_KEY_PAGE_UP, BX_KEY_PAGE_DOWN, BX_KEY_F1, BX_KEY_F2, BX_KEY_F3, BX_KEY_F4,
        BX_KEY_F5, BX_KEY_F6, BX_KEY_F7, BX_KEY_F8, BX_KEY_F9, BX_KEY_F10, BX_KEY_F11, BX_KEY_F12, BX_KEY_ESC};
    if (scan == 0x48) { *key = BX_KEY_PAUSE; return true; }
    if (scan && scan < sizeof table / sizeof table[0]) { *key = table[scan]; return true; }
    return false;
}

// altGr: the right Alt key (r405), which Polish layouts read as AltGr.
// win (r407): the Windows key held with it (Win+M, Win+R, Win+E, Win+D ...).
// r407: the emulated keyboard holds few bytes; when the guest reads slowly (a screen reader's keyboard
// hook), the release of a modifier can be lost and the guest then sees Ctrl or Alt held for good: no
// key works any more (seen with JAWS after a quick series of shortcuts). The modifiers used are
// therefore released once more when the keyboard has been quiet for a moment (ReleaseModifiers).
unsigned usedModifiers;     // 1 shift, 2 ctrl, 4 left alt, 8 right alt, 16 win
uint64_t modifiersUs;
void Type(uint32_t key, bool shift, bool ctrl, bool alt, bool altGr = false, bool win = false)
{
    const uint32_t altKey = altGr ? BX_KEY_ALT_R : BX_KEY_ALT_L;
    usedModifiers |= (shift ? 1u : 0) | (ctrl ? 2u : 0) | ((alt && !altGr) ? 4u : 0) | (altGr ? 8u : 0) | (win ? 16u : 0);
    modifiersUs = pc_usec();
    if (win) DEV_kbd_gen_scancode(BX_KEY_WIN_L);
    if (shift) DEV_kbd_gen_scancode(BX_KEY_SHIFT_L);
    if (ctrl) DEV_kbd_gen_scancode(BX_KEY_CTRL_L);
    if (alt || altGr) DEV_kbd_gen_scancode(altKey);
    DEV_kbd_gen_scancode(key);
    DEV_kbd_gen_scancode(key | BX_KEY_RELEASED);
    if (alt || altGr) DEV_kbd_gen_scancode(altKey | BX_KEY_RELEASED);
    if (ctrl) DEV_kbd_gen_scancode(BX_KEY_CTRL_L | BX_KEY_RELEASED);
    if (shift) DEV_kbd_gen_scancode(BX_KEY_SHIFT_L | BX_KEY_RELEASED);
    if (win) DEV_kbd_gen_scancode(BX_KEY_WIN_L | BX_KEY_RELEASED);
}
void ReleaseModifiers()
{
    if (!usedModifiers || pc_usec() - modifiersUs < 400000) return;
    if (usedModifiers & 1) DEV_kbd_gen_scancode(BX_KEY_SHIFT_L | BX_KEY_RELEASED);
    if (usedModifiers & 2) DEV_kbd_gen_scancode(BX_KEY_CTRL_L | BX_KEY_RELEASED);
    if (usedModifiers & 4) DEV_kbd_gen_scancode(BX_KEY_ALT_L | BX_KEY_RELEASED);
    if (usedModifiers & 8) DEV_kbd_gen_scancode(BX_KEY_ALT_R | BX_KEY_RELEASED);
    if (usedModifiers & 16) DEV_kbd_gen_scancode(BX_KEY_WIN_L | BX_KEY_RELEASED);
    usedModifiers = 0;
}

// One key per call and at most one every 25 ms: the emulated keyboard
// controller holds 16 bytes, a key with modifiers needs up to 8.
// r406: the start menu of a Windows 98 / ME disc ("1. Boot from Hard Disk  2. Boot from CD-ROM") starts the
// hard disk after ten seconds. While the profile is being installed and its disk has no boot sector yet,
// that leads nowhere, and ten seconds are short for someone listening: CharonPC then answers 2 itself.
// Once Setup has made the disk startable, the menu is left alone and its own default continues the installation.
}  // namespace
bool pc::DiskStartable()
{
    if (!pc::settings.diskFile[0]) return true;
    void* file = nullptr; uint64_t size = 0;
    if (pc_vol_open(pc::settings.diskFile, 0, 0, 0, &file, &size)) return true;   // unknown: do not interfere
    // A partition table alone (after FDISK) or a formatted but empty partition does not start:
    // the disk counts as startable when the root folder of its first FAT partition holds IO.SYS.
    static unsigned char buffer[64 * 512];
    auto read = [&](uint64_t lba, unsigned count) { return pc_vol_read(file, lba * 512, buffer, (size_t)count * 512) == (long)count * 512; };
    auto le16 = [](const unsigned char* p) { return (uint32_t)(p[0] | p[1] << 8); };
    auto le32 = [](const unsigned char* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; };
    auto holdsSystem = [&](unsigned sectors) {
        for (unsigned i = 0; i + 32 <= sectors * 512; i += 32) {
            if (!buffer[i]) return 2;                       // end of the folder
            if (!memcmp(buffer + i, "IO      SYS", 11)) return 1;
        }
        return 0;
    };
    bool found = false, known = false;
    do {
        if (!read(0, 1)) break;
        known = true;
        if (buffer[510] != 0x55 || buffer[511] != 0xAA) break;
        uint64_t start = 0;
        for (unsigned e = 0; e < 4; ++e) {
            const unsigned char* p = buffer + 446 + e * 16;
            const unsigned char type = p[4];
            if (type == 0x01 || type == 0x04 || type == 0x06 || type == 0x0B || type == 0x0C || type == 0x0E) { start = le32(p + 8); break; }
        }
        if (!start || !read(start, 1) || buffer[510] != 0x55 || buffer[511] != 0xAA || le16(buffer + 11) != 512) break;
        const unsigned perCluster = buffer[13], reserved = le16(buffer + 14), fats = buffer[16], rootEntries = le16(buffer + 17);
        const uint32_t fat16 = le16(buffer + 22), fat32 = le32(buffer + 36);
        if (!perCluster || perCluster > 64 || !fats) break;
        if (fat16) {   // FAT12/16: the root folder has its own place
            const uint64_t root = start + reserved + (uint64_t)fats * fat16;
            unsigned left = (rootEntries * 32 + 511) / 512;
            for (uint64_t at = root; left; ) {
                const unsigned n = left > 64 ? 64 : left;
                if (!read(at, n)) break;
                const int r = holdsSystem(n);
                if (r == 1) found = true;
                if (r) break;
                at += n; left -= n;
            }
        } else {       // FAT32: the root folder is a chain of clusters
            const uint64_t fat = start + reserved, data = fat + (uint64_t)fats * fat32;
            uint32_t cluster = le32(buffer + 44);
            for (unsigned hops = 0; hops < 64 && cluster >= 2 && cluster < 0x0FFFFFF8; ++hops) {
                if (!read(data + (uint64_t)(cluster - 2) * perCluster, perCluster)) break;
                const int r = holdsSystem(perCluster);
                if (r == 1) found = true;
                if (r) break;
                if (!read(fat + cluster / 128, 1)) break;
                cluster = le32(buffer + (cluster % 128) * 4) & 0x0FFFFFFF;
            }
        }
    } while (false);
    pc_vol_close(file);
    return !known || found;
}
namespace {
using pc::DiskStartable;
// r407: Setup without questions starts from SETUP.IMG. When Setup has made the disk startable (it restarts
// the machine by itself after copying the files), the emulated BIOS must start the disk, not the floppy again.
void FollowSetup()
{
    static uint64_t last;
    if (!pc::settings.floppySetup) return;
    const uint64_t now = pc_usec();
    if (now - last < 3000000) return;
    last = now;
    if (!DiskStartable()) return;
    pc::settings.floppySetup = false;
    SIM->get_param_enum(BXPN_BOOTDRIVE1)->set(BX_BOOT_DISKC);
    SIM->get_param_enum(BXPN_BOOTDRIVE2)->set(BX_BOOT_NONE);
    DEV_cmos_set_reg(0x3d, BX_BOOT_DISKC);
    BX_INFO(("Setup made the profile's disk startable: the next start is from the disk"));
}
bool RowHolds(unsigned y, const char* text)
{
    size_t n = 0; while (text[n]) ++n;
    for (unsigned x = 0; x + n <= maxColumns; ++x) {
        size_t i = 0;
        while (i < n && (shown[y][x + i] & 0xFF) == (unsigned char)text[i]) ++i;
        if (i == n) return true;
    }
    return false;
}
void AnswerDiscMenu()
{
    static bool answered;
    if (!pc::settings.installing) return;
    bool menu = false, prompt = false;
    for (unsigned y = 0; y < maxRows && y < 25; ++y) {
        if (RowHolds(y, "2.  Boot from CD-ROM")) menu = true;
        if (RowHolds(y, "Enter your choice")) prompt = true;
    }
    if (!menu || !prompt) { answered = false; return; }
    if (answered) return;
    answered = true;
    if (DiskStartable()) return;
    BX_INFO(("the profile's disk holds no system yet: answering the disc's start menu with 2 (Boot from CD-ROM)"));
    pc::Say(pc::polish ? "Na dysku profilu nie ma jeszcze systemu: uruchamiam instalator z płyty." : "The profile's disk holds no system yet: starting Setup from the disc.");
    DEV_kbd_gen_scancode(BX_KEY_2); DEV_kbd_gen_scancode(BX_KEY_2 | BX_KEY_RELEASED);
    DEV_kbd_gen_scancode(BX_KEY_ENTER); DEV_kbd_gen_scancode(BX_KEY_ENTER | BX_KEY_RELEASED);
}

// r407: Setup without questions. The text-mode welcome of Windows 98 / ME Setup still waits for Enter
// ("To set up Windows now, press ENTER"); the profile was prepared for an installation the user
// already confirmed in Charon's dialogue, so CharonPC presses it, once per appearance.
void AnswerSetupWelcome()
{
    static bool answered;
    if (!pc::settings.floppySetup) return;
    bool welcome = false, enter = false;
    for (unsigned y = 0; y < maxRows && y < 25; ++y) {
        if (RowHolds(y, "Welcome to Setup")) welcome = true;
        if (RowHolds(y, "To continue with Setup, press ENTER")) enter = true;
    }
    if (!welcome || !enter) { answered = false; return; }
    if (answered) return;
    answered = true;
    BX_INFO(("Setup without questions: Enter on Setup's text welcome"));
    pc::Say(pc::polish ? "Instalator Windows wystartował. Charon prowadzi go sam; potrwa to około pół godziny."
                       : "Windows Setup has started. Charon runs it by itself; it takes about half an hour.");
    DEV_kbd_gen_scancode(BX_KEY_ENTER); DEV_kbd_gen_scancode(BX_KEY_ENTER | BX_KEY_RELEASED);
}

bool PollKey()
{
    if (pc_usec() - lastKeyUs < 25000) return false;
    for (int guard = 0; guard < 8; ++guard) {
        pc::KeyData data;
        data.state.shift = 0;
        if (pc::InEx) {
            if (pc::InEx->ReadKeyStrokeEx(pc::InEx, &data) != EFI_SUCCESS) return false;
        } else if (pc::ST->ConIn->ReadKeyStroke(pc::ST->ConIn, &data.key) != EFI_SUCCESS) return false;
        const uint32_t state = (data.state.shift & pc::shiftValid) ? data.state.shift : 0;
        const bool ctrl = state & (pc::shiftLeftCtrl | pc::shiftRightCtrl);
        const bool alt = state & (pc::shiftLeftAlt | pc::shiftRightAlt);
        const bool rightAlt = (state & pc::shiftRightAlt) && !(state & pc::shiftLeftAlt);   // r405: AltGr
        const bool shifted = state & (pc::shiftLeftShift | pc::shiftRightShift);
        const bool logo = state & (pc::shiftLeftLogo | pc::shiftRightLogo);   // r407: the Windows key
        const efi_input_key& k = data.key;
        // Charon's resident reader owns these.
        if (ctrl && alt) continue;
        if (alt && ((k.ScanCode >= 1 && k.ScanCode <= 4) || k.UnicodeChar == u'\\')) continue;
        if (ctrl && shifted && k.ScanCode == 0x16) { pc::userQuit = true; return true; }
        if (ctrl && shifted && k.ScanCode && pc::ReaderKey(k.ScanCode)) continue;   // r407: the control reader's keys
        // r405: Ctrl+Shift+F11 = Ctrl+Alt+Del in the guest (XP logon, Task Manager);
        // Ctrl+Alt itself belongs to Charon's reader.
        if (ctrl && shifted && k.ScanCode == 0x15) {
            lastKeyUs = pc_usec();
            DEV_kbd_gen_scancode(BX_KEY_CTRL_L); DEV_kbd_gen_scancode(BX_KEY_ALT_L);
            DEV_kbd_gen_scancode(BX_KEY_DELETE); DEV_kbd_gen_scancode(BX_KEY_DELETE | BX_KEY_RELEASED);
            DEV_kbd_gen_scancode(BX_KEY_ALT_L | BX_KEY_RELEASED); DEV_kbd_gen_scancode(BX_KEY_CTRL_L | BX_KEY_RELEASED);
            return false;
        }
        uint32_t key = 0;
        bool shift = false;
        bool altGr = false;
        if (k.ScanCode) {
            if (!MapScan(k.ScanCode, &key)) continue;
            shift = shifted;
        } else if (ctrl && k.UnicodeChar >= 1 && k.UnicodeChar <= 26) {
            key = BX_KEY_A + (k.UnicodeChar - 1);
        } else if (MapPolish(k.UnicodeChar, &key, &shift)) {
            altGr = true;   // r405: a Polish letter as a character
        } else if (!MapCharacter(k.UnicodeChar, &key, &shift)) {
            continue;   // no key of the US or Polish programmer's layout for this character
        } else if (k.UnicodeChar == 0x09) shift = shifted;
        // r405: a US firmware layout hands AltGr+a over as Right Alt + 'a': passed on as AltGr.
        if (rightAlt && !altGr) altGr = true;
        lastKeyUs = pc_usec();
        Type(key, shift, ctrl, alt && !altGr, altGr, logo);
        return false;
    }
    return false;
}

}  // namespace

void pc::GuestEnter()
{
    DEV_kbd_gen_scancode(BX_KEY_ENTER); DEV_kbd_gen_scancode(BX_KEY_ENTER | BX_KEY_RELEASED);
}

void pc::GuestAltLetter(char letter)
{
    if (letter >= 'A' && letter <= 'Z') letter = (char)(letter + 32);
    if (letter < 'a' || letter > 'z') return;
    const Bit32u key = BX_KEY_A + (Bit32u)(letter - 'a');
    usedModifiers |= 4; modifiersUs = pc_usec();
    DEV_kbd_gen_scancode(BX_KEY_ALT_L);
    DEV_kbd_gen_scancode(key); DEV_kbd_gen_scancode(key | BX_KEY_RELEASED);
    DEV_kbd_gen_scancode(BX_KEY_ALT_L | BX_KEY_RELEASED);
}

void pc::GuestTab() { Type(BX_KEY_TAB, false, false, false, false); }

// r407: Start menu -> Run -> a command -> Enter, with the pauses Windows needs between them.
namespace {
char runText[80];
unsigned runStep;
size_t runAt;
uint64_t runUs;
void RunScript()
{
    if (!runStep) return;
    const uint64_t now = pc_usec();
    switch (runStep) {
    case 1:
        usedModifiers |= 2; modifiersUs = now;
        DEV_kbd_gen_scancode(BX_KEY_CTRL_L);
        DEV_kbd_gen_scancode(BX_KEY_ESC); DEV_kbd_gen_scancode(BX_KEY_ESC | BX_KEY_RELEASED);
        DEV_kbd_gen_scancode(BX_KEY_CTRL_L | BX_KEY_RELEASED);
        runStep = 2; runUs = now; break;
    case 2:
        if (now - runUs < 3000000) return;
        Type(BX_KEY_R, false, false, false, false);
        runStep = 3; runUs = now; runAt = 0; break;
    case 3: {
        if (now - runUs < (runAt ? 60000u : 4000000u)) return;
        if (!runText[runAt]) { runStep = 4; runUs = now; break; }
        uint32_t key = 0; bool shift = false;
        if (MapCharacter((char16_t)(unsigned char)runText[runAt], &key, &shift)) Type(key, shift, false, false, false);
        ++runAt; runUs = now; break;
    }
    default:
        if (now - runUs < 1000000) return;
        pc::GuestEnter();
        runStep = 0; break;
    }
}
}  // namespace
void pc::GuestRun(const char* command)
{
    if (runStep) return;
    snprintf(runText, sizeof runText, "%s", command);
    runStep = 1;
}

// The pointer of a mouse nobody moves sits in the middle of the screen, on top of the text
// the control reader has to read. Relative moves right and down push it into the corner.
void pc::ParkPointer()
{
    // into the lower right corner, then up along the right edge: there the arrow is off the screen
    // and rests on nothing that answers with a tool tip (the clock, the Start button, an icon)
    for (int i = 0; i < 14; ++i) DEV_mouse_motion(200, -200, 0, 0, 0);
    for (int i = 0; i < 2; ++i) DEV_mouse_motion(0, 120, 0, 0, 0);
}

void bx_charon_gui_c::specific_init(int argc, char** argv, unsigned headerbar_y)
{
    UNUSED(headerbar_y);
    put("CHARON");
    for (int i = 1; i < argc; ++i)
        if (!parse_common_gui_options(argv[i], BX_GUI_OPT_HIDE_IPS)) BX_ERROR(("unknown charon option '%s'", argv[i]));
    for (unsigned i = 0; i < 256; ++i) hostPalette[i] = 0;
    forceText = true;
    BX_INFO(("CharonPC display: text console %ux%u, %s", (unsigned)pc::consoleColumns, (unsigned)pc::consoleRows,
             Gop() ? "graphics output present" : "no graphics output"));
}

// r406: with debugserial/debugcon a line every five seconds tells how the emulated machine runs.
static unsigned long diagText, diagTiles, diagEvents;
void bx_charon_gui_c::handle_events(void)
{
    ++diagEvents;
    if (pc::settings.debugserial || pc::settings.debugcon) {
        static uint64_t last;
        const uint64_t now = pc_usec();
        if (now - last > 5000000) {
            last = now;
            static Bit64u lastCount;
            const Bit64u count = BX_CPU(0)->get_icount();
            BX_INFO(("CharonPC state: host %lu s, events %lu, text updates %lu, tiles %lu, guest %s %ux%u, executed %lu million instructions in 5 s",
                     (unsigned long)(now / 1000000), diagEvents, diagText, diagTiles, guest_textmode ? "text" : "graphics", guest_xres, guest_yres,
                     (unsigned long)((count - lastCount) / 1000000)));
            lastCount = count;
        }
    }
    pc::BnsPoll();
    pc::SoundTick();   // r407: the emulated sound card's output, before the voice pump that carries it to the card
    pc::PumpVoice();
    FollowSetup();
    ReleaseModifiers();
    RunScript();
    pc::ReaderTick();
    if (PollKey() && pc::userQuit) {
        // r407: under a graphical Windows the first press is the machine's power button (Windows shuts
        // down by itself and the machine then switches off); a second press, or a press on a text
        // screen (DOS, a start menu), switches the machine off at once as before.
        static uint64_t askedUs;
        const uint64_t now = pc_usec();
        if (!guest_textmode && (!askedUs || now - askedUs > 120000000) && charon_acpi_power_button()) {
            askedUs = now ? now : 1;
            pc::userQuit = false;
            BX_INFO(("Ctrl+Shift+F12: the power button, the guest is asked to shut down"));
            pc::StopVoice();
            pc::Say(pc::polish ? "Zamykam Windows. Naciśnij Control Shift F12 jeszcze raz, aby wyłączyć emulator natychmiast, bez zamykania."
                               : "Shutting Windows down. Press Control Shift F12 again to switch the emulator off at once, without a shutdown.");
        } else {
            BX_INFO(("Ctrl+Shift+F12: switching the emulated machine off"));
            power_handler();
        }
    }
}

void bx_charon_gui_c::flush(void) {}

void bx_charon_gui_c::clear_screen(void)
{
    if (guest_textmode) {
        SetAttribute(0x07);
        pc::Out->ClearScreen(pc::Out);
        forceText = true;
    } else if (Gop()) {
        uint32_t black = 0;
        gop->Blt(gop, &black, hakefi::bltVideoFill, 0, 0, 0, 0, gop->Mode->info->horizontal, gop->Mode->info->vertical, 0);
    }
}

void bx_charon_gui_c::text_update(Bit8u* old_text, Bit8u* new_text, unsigned long cursor_x, unsigned long cursor_y,
                                  bx_vga_tminfo_t* tm_info)
{
    UNUSED(old_text);
    ++diagText;
    const unsigned rows = textRows < pc::consoleRows ? textRows : (unsigned)pc::consoleRows;
    const unsigned columns = textColumns < pc::consoleColumns ? textColumns : (unsigned)pc::consoleColumns;
    if (charmap_updated) { forceText = true; charmap_updated = 0; }
    bool wrote = false;
    for (unsigned y = 0; y < rows && y < maxRows; ++y) {
        const Bit8u* line = new_text + y * tm_info->line_offset;
        unsigned x = 0;
        while (x < columns && x < maxColumns) {
            const uint16_t cell = (uint16_t)(line[x * 2] | line[x * 2 + 1] << 8);
            // The last console cell is never written: the console would scroll.
            const bool last = y + 1 == pc::consoleRows && x + 1 == pc::consoleColumns;
            if ((!forceText && shown[y][x] == cell) || last) { ++x; continue; }
            // A run of changed cells with one attribute: one OutputString.
            char16_t run[maxColumns + 1];
            unsigned n = 0;
            const uint8_t attribute = (uint8_t)(cell >> 8);
            const unsigned start = x;
            while (x < columns && x < maxColumns) {
                const uint16_t c = (uint16_t)(line[x * 2] | line[x * 2 + 1] << 8);
                if ((uint8_t)(c >> 8) != attribute || (!forceText && shown[y][x] == c)) break;
                if (y + 1 == pc::consoleRows && x + 1 == pc::consoleColumns) break;
                run[n++] = cp437[c & 0xFF];
                shown[y][x] = c;
                ++x;
            }
            run[n] = 0;
            // r406: the firmware's cursor is hidden while cells are written: VMware's console puts the
            // cell it saved under the cursor back afterwards, which left old letters on the screen.
            if (cursorShown) { pc::Out->EnableCursor(pc::Out, false); cursorShown = false; }
            const efi_status moved = pc::Out->SetCursorPosition(pc::Out, start, y);
            SetAttribute(attribute);
            const efi_status put = pc::Out->OutputString(pc::Out, run);
            {static unsigned shownDiag; if ((pc::settings.debugserial || pc::settings.debugcon) && n > 3 && run[0] != u' ' && shownDiag < 12) { ++shownDiag;
                BX_INFO(("CharonPC text run: row %u col %u length %u attribute %02x first %04x %04x %04x cursor %lx output %lx", y, start, n, attribute, run[0], run[1], run[2], (unsigned long)moved, (unsigned long)put)); }}
            wrote = true;
        }
    }
    forceText = false;
    const bool visible = cursor_x < columns && cursor_y < rows && tm_info->cs_start <= tm_info->cs_end;
    if (visible) pc::Out->SetCursorPosition(pc::Out, cursor_x, cursor_y);
    if (visible != cursorShown) { pc::Out->EnableCursor(pc::Out, visible); cursorShown = visible; }
    if (wrote) { AnswerDiscMenu(); AnswerSetupWelcome(); }
}

int bx_charon_gui_c::get_clipboard_text(Bit8u** bytes, Bit32s* nbytes)
{
    UNUSED(bytes);
    UNUSED(nbytes);
    return 0;
}

int bx_charon_gui_c::set_clipboard_text(char* text_snapshot, Bit32u len)
{
    UNUSED(text_snapshot);
    UNUSED(len);
    return 0;
}

bool bx_charon_gui_c::palette_change(Bit8u index, Bit8u red, Bit8u green, Bit8u blue)
{
    hostPalette[index] = (uint32_t)blue | (uint32_t)green << 8 | (uint32_t)red << 16;
    return true;   // the whole screen is redrawn with the new colours
}

void bx_charon_gui_c::graphics_tile_update(Bit8u* tile, unsigned x0, unsigned y0)
{
    ++diagTiles;
    if (!Gop() || x0 >= guest_xres || y0 >= guest_yres) return;
    unsigned w = x_tilesize, h = y_tilesize;
    if (x0 + w > guest_xres) w = guest_xres - x0;
    if (y0 + h > guest_yres) h = guest_yres - y0;
    const unsigned screenW = gop->Mode->info->horizontal, screenH = gop->Mode->info->vertical;
    if (originX + x0 >= screenW || originY + y0 >= screenH) return;
    if (originX + x0 + w > screenW) w = screenW - originX - x0;
    if (originY + y0 + h > screenH) h = screenH - originY - y0;
    if (w * h > sizeof tileBuffer / sizeof tileBuffer[0]) return;
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            const unsigned i = y * x_tilesize + x;
            uint32_t pixel;
            switch (guest_bpp) {
            case 8: pixel = hostPalette[tile[i]]; break;
            case 15: { const unsigned v = tile[i * 2] | tile[i * 2 + 1] << 8;
                       pixel = ((v & 0x1F) << 3) | ((v >> 5 & 0x1F) << 11) | ((v >> 10 & 0x1F) << 19); break; }
            case 16: { const unsigned v = tile[i * 2] | tile[i * 2 + 1] << 8;
                       pixel = ((v & 0x1F) << 3) | ((v >> 5 & 0x3F) << 10) | ((v >> 11 & 0x1F) << 19); break; }
            case 24: pixel = tile[i * 3] | tile[i * 3 + 1] << 8 | tile[i * 3 + 2] << 16; break;
            default: pixel = tile[i * 4] | tile[i * 4 + 1] << 8 | tile[i * 4 + 2] << 16; break;
            }
            tileBuffer[y * w + x] = pixel;
        }
    pc::ReaderTile(x0, y0, w, h, tileBuffer);
    gop->Blt(gop, tileBuffer, hakefi::bltBufferToVideo, 0, 0, originX + x0, originY + y0, w, h, w * 4);
}

void bx_charon_gui_c::dimension_update(unsigned x, unsigned y, unsigned fheight, unsigned fwidth, unsigned bpp)
{
    const bool wasText = guest_textmode;
    guest_textmode = fheight > 0;
    guest_xres = x;
    guest_yres = y;
    guest_bpp = bpp;
    guest_fwidth = fwidth;
    guest_fheight = fheight;
    pc::ReaderMode(!guest_textmode, x, y);
    if (guest_textmode) {
        textColumns = fwidth ? x / fwidth : 80;
        textRows = y / fheight;
        if (!wasText || textColumns > pc::consoleColumns || textRows > pc::consoleRows)
            BX_INFO(("text mode %ux%u (console %ux%u)", textColumns, textRows, (unsigned)pc::consoleColumns, (unsigned)pc::consoleRows));
        consoleAttribute = -1;
        SetAttribute(0x07);
        pc::Out->ClearScreen(pc::Out);
        forceText = true;
    } else {
        BX_INFO(("graphics mode %ux%u, %u bpp", x, y, bpp));
        if (Gop()) {
            const unsigned screenW = gop->Mode->info->horizontal, screenH = gop->Mode->info->vertical;
            originX = x < screenW ? (screenW - x) / 2 : 0;
            originY = y < screenH ? (screenH - y) / 2 : 0;
            uint32_t black = 0;
            gop->Blt(gop, &black, hakefi::bltVideoFill, 0, 0, 0, 0, screenW, screenH, 0);
        }
    }
}

unsigned bx_charon_gui_c::create_bitmap(const unsigned char* bmap, unsigned xdim, unsigned ydim)
{
    UNUSED(bmap);
    UNUSED(xdim);
    UNUSED(ydim);
    return 0;
}

unsigned bx_charon_gui_c::headerbar_bitmap(unsigned bmap_id, unsigned alignment, void (*f)(void))
{
    UNUSED(bmap_id);
    UNUSED(alignment);
    UNUSED(f);
    return 0;
}

void bx_charon_gui_c::show_headerbar(void) {}

void bx_charon_gui_c::replace_bitmap(unsigned hbar_id, unsigned bmap_id)
{
    UNUSED(hbar_id);
    UNUSED(bmap_id);
}

void bx_charon_gui_c::exit(void) { pc::ReaderShutdown(); }

void bx_charon_gui_c::mouse_enabled_changed_specific(bool val) { UNUSED(val); }
