// CharonPC - Charon's control reader for the guest's graphical Windows (r407).
// GPL-2.0-or-later.
//
// Before a screen reader runs inside the guest Windows (Setup's wizard, the
// first desktop, the reader's own installer) nothing would speak. This glue
// keeps a copy of the guest's picture, hands it to pc_winread once drawing
// has paused, and speaks what changed the way a screen reader does: a new
// window with its title and text, then the focused control ("Next, button");
// when the guest moves the focus (Tab, arrows) only the new control.
//
// It is Charon's reader only while no guest reader speaks: text arriving on
// the emulated Braille 'n Speak (the guest reader's synthesizer) silences it,
// and so does a guest reader with a software synthesizer (JAWS with Eloquence)
// once its program is seen running (its button on the taskbar, its own window)
// AND the guest really plays sound: a reader that cannot be heard must not
// leave the user in silence. When that program is gone again (JAWS without an
// authorization ends after 40 minutes) Charon's reader comes back.
//
// Keys (Ctrl+Shift, never passed to the guest):
//   F9  read the whole window          F10  repeat the focused control
//   Up / Down  previous / next element (review, does not move the guest focus)
//   F8  control reader on / off
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_glue.h"
#include "pc_platform.h"
#include "pc_winread.h"
#include "CharonReaderControl.h"

namespace {

constexpr unsigned maxWidth = 1024, maxHeight = 768;
uint32_t frame[maxWidth * maxHeight];
unsigned frameWidth, frameHeight;
bool graphics, changed, userOff, guestReader, fontsTried, announced, ownerInstalled;
uint64_t changedUs, lastFontTryUs;
pcwin::Context* context;
pcwin::Screen screens[2];
unsigned current;          // screens[current] = what was last analysed
bool haveScreen;
int review = -1;
int lastTenth = -1;
uint32_t steeredPage;      // the wizard page already confirmed (Setup without questions)
uint64_t parkedUs, steerUs;
enum { steerNone, steerEnter, steerAccept, steerTab } steer;   // steerAccept: Alt + steerLetter
bool setupSteered;         // a page of Setup was confirmed in this session
bool readerSteered;        // Charon started or led the guest screen reader's installer in this session
uint64_t runDueUs;         // when the reader's installer is started from the desktop (0: not waiting)
bool runDone;
char steerLetter;
// the guest's own screen reader, seen on the screen
bool seenReader;           // its program is on the screen now
uint64_t seenPackets;      // the guest's sound packets when it was first seen
uint64_t goneUs;           // since when the desktop is shown without it (0: it is there)
bool steppedForSeen;       // guestReader was set because of it (and not by text on COM1)
bool keepOn;               // the user turned Charon's reader on again: it stays until Windows restarts
bool installedReader;      // the guest's reader was on the guest's disk when Windows started
uint64_t seenUs;           // when the program was first seen
constexpr uint64_t seenWaitUs = 4000000;   // a new window is held back this long after the program appeared: it may be about to speak itself
constexpr uint64_t heardPackets = 6;       // over half a second of sound after the program appeared
constexpr uint64_t goneWaitUs = 6000000;   // the desktop without the program for this long: it has ended

bool Has(const char* text, const char* what) { return strstr(text, what) != nullptr; }

void Log(const char* line) { pc_log_write(line, strlen(line)); }

bool Jaws() { return !strncmp(pc::settings.reader, "jfw", 3); }

// The guest screen reader's installer, led by Charon like Setup itself: the user chose this reader in
// Charon's dialogue, and its installer cannot speak before it has installed anything. JAWS 4.02: the
// wizard's pages (the "Automatic" installation is the preselected one: Eloquence through the sound
// card), the Windows update it brings, and after the restart "Run as Demo" (without an authorization
// JAWS speaks for 40 minutes per Windows start) and its question about starting with Windows.
// True: this window belongs to the reader's installer (Steer has nothing more to say about it).
bool SteerReader(const pcwin::Screen& now)
{
    if (pc::settings.readerLead) readerSteered = true;
    if (!Jaws() || !now.dialog.w || !(readerSteered || runDone || pc::settings.autoSetup)) return false;
    const bool wizard = !strncmp(now.title, "JAWS", 4) && Has(now.title, "Install");
    const bool update = readerSteered && Has(now.title, "Update");
    const bool authorization = readerSteered && Has(now.title, "Authorization Manager");
    const bool autostart = readerSteered && !strcmp(now.title, "JAWS");
    if (!wizard && !update && !authorization && !autostart) return false;
    // The engine does not read the wizard's page texts (its window has no frame it knows), so all its
    // pages look like one: there the same "page" is answered again when five seconds have passed.
    if (now.pageHash == steeredPage && !(wizard && steer == steerNone && pc_usec() - steerUs > 5000000)) return true;
    if (pc::settings.debugserial || pc::settings.debugcon) {
        char line[200];
        snprintf(line, sizeof line, "CharonPC reader: reader dialog '%s' (wizard %d, update %d, authorization %d, autostart %d), page %08x, answered %08x\n",
                 now.title, wizard, update, authorization, autostart, (unsigned)now.pageHash, (unsigned)steeredPage);
        Log(line);
    }
    int next = -1, yes = -1, demo = -1;
    bool startQuestion = false;
    for (unsigned i = 0; i < now.count; ++i) {
        const pcwin::Element& e = now.items[i];
        // the wizard's window has no frame the engine knows: its buttons count as "outside" the title bar
        if ((e.flags & pcwin::kFlagOutside) && !wizard) continue;
        const char* text = pcwin::Text(now, e);
        if (e.role == pcwin::kRoleText && Has(text, "start automatically")) startQuestion = true;
        if (e.role != pcwin::kRoleButton || (e.flags & pcwin::kFlagDisabled)) continue;
        if (!strncmp(text, "Next", 4) || !strncmp(text, "Finish", 6)) next = (int)i;
        if (!strcmp(text, "Yes")) yes = (int)i;
        if (!strcmp(text, "Run as Demo")) demo = (int)i;
    }
    auto focused = [&](int i) { return (now.items[i].flags & pcwin::kFlagFocused) != 0; };
    if (wizard && next >= 0) {
        readerSteered = true;
        if (focused(next)) steer = steerEnter; else { steer = steerAccept; steerLetter = 'n'; }
    } else if (wizard && yes >= 0) { readerSteered = true; steer = steerAccept; steerLetter = 'y'; }   // the licence page: Back, Yes, No
    else if (update && yes >= 0) { steer = steerAccept; steerLetter = 'y'; }
    else if (authorization && demo >= 0) {
        if (!focused(demo)) { steer = steerTab; steerUs = pc_usec(); return true; }   // the same page again, with the focus moved
        steer = steerEnter;
    } else if (autostart && startQuestion && yes >= 0) { steer = steerAccept; steerLetter = 'y'; }
    else return true;
    steeredPage = now.pageHash;
    steerUs = pc_usec();
    return true;
}

// Setup without questions: the pages of Windows Setup's wizard that the answer file already
// filled in still wait for "Next" (user information, product key, the last page) and the licence
// page waits for "I accept". The user confirmed the installation and gave these answers in
// Charon's dialogue, so CharonPC confirms the page, after reading it out. A page that lacks an
// answer (Next is unavailable: an empty product key) is left to the user.
void Steer(const pcwin::Screen& now)
{
    if (SteerReader(now)) return;
    if (!pc::settings.autoSetup || !now.dialog.w || now.pageHash == steeredPage) return;
    // the first start asks for a Windows password: the user name comes from the answer file, and an
    // empty password (OK) means Windows never asks again - the only answer a user without speech can give.
    // Only in the session in which Charon led Setup: a later start may ask for a password the user set.
    if (Has(now.title, "Enter Windows Password") || Has(now.title, "Welcome to Windows")) {
        if (!setupSteered && !pc::settings.readerRun[0]) return;   // also when the reader's installer waits for the desktop
        for (unsigned i = 0; i < now.count; ++i)
            if (now.items[i].role == pcwin::kRoleButton && !strcmp(pcwin::Text(now, now.items[i]), "OK")) {
                steer = steerEnter;
                steeredPage = now.pageHash;
                steerUs = pc_usec();
                return;
            }
        return;
    }
    if (!Has(now.title, "Setup")) return;
    int next = -1, accept = -1;
    for (unsigned i = 0; i < now.count; ++i) {
        const pcwin::Element& e = now.items[i];
        if (e.flags & pcwin::kFlagOutside) continue;
        const char* text = pcwin::Text(now, e);
        if (e.role == pcwin::kRoleButton && (!strncmp(text, "Next", 4) || !strncmp(text, "Finish", 6))) next = (int)i;
        if (e.role == pcwin::kRoleRadio && !strncmp(text, "I accept", 8)) accept = (int)i;
    }
    if (next < 0) return;
    if (accept >= 0 && !(now.items[accept].flags & pcwin::kFlagChecked)) {
        steer = steerAccept;
        steerLetter = now.items[accept].accelerator && now.items[accept].accelerator < 128 ? (char)now.items[accept].accelerator : 'a';
    } else if (!(now.items[next].flags & pcwin::kFlagDisabled)) {
        steer = steerEnter;
        steeredPage = now.pageHash;
        setupSteered = true;
    } else return;
    steerUs = pc_usec();
}
efi_handle ownerHandle;
char speech[6000];

// Names under which a guest screen reader shows its program: the taskbar button and the title of its
// own windows. Exact names, so that "JAWS 4.02 Setup" or a folder called JAWS is not taken for it.
bool ReaderName(const char* text)
{
    static const char* const names[] = {"JAWS", "JAWS for Windows", "Window-Eyes", "Hal", "outSPOKEN"};
    for (const char* name : names) if (!strcmp(text, name)) return true;
    return false;
}

// desktop: the Start button is on the screen. program: a guest screen reader's button or window is.
void LookForGuestReader(const pcwin::Screen& now, bool& desktop, bool& program)
{
    desktop = program = false;
    int startY = -1;
    for (unsigned i = 0; i < now.count; ++i)
        if (now.items[i].role == pcwin::kRoleButton && !strcmp(pcwin::Text(now, now.items[i]), "Start")) { desktop = true; startY = now.items[i].rect.y; }
    if (now.dialog.w && ReaderName(now.title)) program = true;
    if (startY < 0) return;
    for (unsigned i = 0; i < now.count && !program; ++i) {
        const pcwin::Element& e = now.items[i];
        const int dy = e.rect.y - startY;
        program = e.role == pcwin::kRoleButton && dy > -6 && dy < 6 && ReaderName(pcwin::Text(now, e));
    }
}


// True: the guest's reader has taken over and Charon's control reader says nothing more.
bool GuestReaderTakesOver(const pcwin::Screen& now)
{
    bool desktop, program;
    LookForGuestReader(now, desktop, program);
    const uint64_t packets = pc::SoundGuestPackets();
    if (program) {
        if (!seenReader) {
            seenReader = true;
            seenPackets = packets;
            seenUs = pc_usec();
            Log("CharonPC reader: a guest screen reader's program is on the screen\n");
        }
        goneUs = 0;
        if (!guestReader && !keepOn && packets - seenPackets >= heardPackets) {
            guestReader = steppedForSeen = true;
            Log("CharonPC reader: the guest's screen reader runs and the guest plays sound, Charon's control reader steps back\n");
            pc::StopVoice();
            pc::Say(pc::polish ? "Czytnik w Windows mówi. Czytnik kontrolek Charona milknie; Control Shift F8 włącza go ponownie."
                               : "The Windows screen reader speaks. Charon's control reader goes quiet; Control Shift F8 turns it on again.");
        }
    } else if (seenReader && desktop) {
        const uint64_t t = pc_usec();
        if (!goneUs) goneUs = t ? t : 1;
        else if (t - goneUs > goneWaitUs) {
            seenReader = false;
            goneUs = 0;
            if (guestReader && steppedForSeen) {
                guestReader = steppedForSeen = false;
                haveScreen = false;
                Log("CharonPC reader: the guest's screen reader is gone, Charon's control reader is back\n");
                pc::Say(pc::polish ? "Czytnik w Windows zakończył pracę. Czytnik kontrolek Charona czyta dalej."
                                   : "The Windows screen reader has ended. Charon's control reader reads on.");
            }
        }
    }
    return guestReader;
}

// While the guest's reader speaks: the picture is still looked at, silently, to notice its end.
void Watch()
{
    pcwin::Screen& now = screens[current ^ 1];
    pcwin::Analyse(context, frame, frameWidth, frameHeight, maxWidth, &now);
    current ^= 1;
    GuestReaderTakesOver(now);
    SteerReader(now);
}

void Owner(bool on)
{
    if (on == ownerInstalled) return;
    if (on) {
        ownerHandle = nullptr;
        if (pc::BS->InstallProtocolInterface(&ownerHandle, &hakefi::graphicsOwnerGuid, EFI_NATIVE_INTERFACE, nullptr) == EFI_SUCCESS) ownerInstalled = true;
    } else {
        pc::BS->UninstallProtocolInterface(ownerHandle, &hakefi::graphicsOwnerGuid, nullptr);
        ownerInstalled = false;
        ownerHandle = nullptr;
    }
}

void AddFont(const char* name, const uint8_t* data, size_t size, void*)
{
    const bool took = pcwin::AddFon(context, data, size);
    char line[120];
    const int n = snprintf(line, sizeof line, "CharonPC reader: font %s, %lu bytes, %s\n", name, (unsigned long)size, took ? "used" : "not usable");
    if (n > 0) pc_log_write(line, (size_t)n);
}

bool Fonts()
{
    if (context && pcwin::FontCount(context)) return true;
    const uint64_t now = pc_usec();
    if (fontsTried && now - lastFontTryUs < 30000000) return false;   // again later: Setup may have installed them meanwhile
    fontsTried = true;
    lastFontTryUs = now;
    if (!context) {
        const size_t bytes = pcwin::ContextBytes();
        void* memory = malloc(bytes);
        context = memory ? pcwin::Init(memory, bytes) : nullptr;
        if (!context) { free(memory); return false; }
    }
    const unsigned files = pc::LoadGuestFonts(pc::settings.cdromFile, pc::settings.diskFile, AddFont, nullptr);
    char line[100];
    const int n = snprintf(line, sizeof line, "CharonPC reader: %u font files, %u usable fonts\n", files, pcwin::FontCount(context));
    if (n > 0) pc_log_write(line, (size_t)n);
    return pcwin::FontCount(context) != 0;
}

bool Active() { return graphics && !userOff && !guestReader; }

unsigned Flags() { return pc::polish ? pcwin::kSpeakPolish : 0; }

void Speak(bool interrupt)
{
    if (!speech[0]) return;
    if (interrupt) pc::StopVoice();
    pc::Say(speech);
    if (pc::settings.debugserial || pc::settings.debugcon) {
        pc_log_write("CharonPC reader says: ", 22);
        pc_log_write(speech, strlen(speech));
        pc_log_write("\n", 1);
    }
}

int Percent(const pcwin::Screen& s)
{
    // the last bar: with two of them (a step and the whole job) it is the whole job
    int percent = -1;
    for (unsigned i = 0; i < s.count; ++i) if (s.items[i].role == pcwin::kRoleProgress && s.items[i].percent >= 0) percent = s.items[i].percent;
    return percent;
}

void Read()
{
    pcwin::Screen& before = screens[current];
    pcwin::Screen& now = screens[current ^ 1];
    pcwin::Analyse(context, frame, frameWidth, frameHeight, maxWidth, &now);
    // text under the mouse pointer is damaged: push the pointer into the corner and read again
    {
        bool damaged = false;
        for (unsigned i = 0; i < now.count && !damaged; ++i) damaged = now.items[i].flags & pcwin::kFlagDamaged;
        const uint64_t t = pc_usec();
        static unsigned retries;   // a pointer that cannot be moved must not keep the page unread
        if (!damaged) retries = 0;
        // also on every new page: the guest's mouse driver may not have listened the first time
        const bool newPage = !haveScreen || screens[current].pageHash != now.pageHash || screens[current].dialog.w != now.dialog.w;
        if ((damaged || newPage || !parkedUs) && t - parkedUs > 3000000) {
            parkedUs = t ? t : 1;
            pc::ParkPointer();
            if (pc::settings.debugserial || pc::settings.debugcon) pc_log_write("CharonPC reader: pointer parked\n", 32);
            if (damaged && retries < 2) { ++retries; changed = true; changedUs = t; return; }
        }
    }
    if (GuestReaderTakesOver(now)) { SteerReader(now); current ^= 1; haveScreen = true; return; }
    // its program has just appeared: it may read this window itself in a moment
    if (seenReader && !goneUs && !keepOn && pc_usec() - seenUs < seenWaitUs) { changed = true; changedUs = pc_usec(); return; }
    if (!haveScreen) pcwin::ClearScreen(&before);
    speech[0] = 0;
    size_t n = pcwin::SpeakChange(before, now, Flags() | (haveScreen ? pcwin::kSpeakTerse : 0), speech, sizeof speech);
    // the desktop is no dialog: say that it is there, or the user hears nothing after the last window closed
    {
        static bool wasDesktop;
        bool start = false;
        if (!now.dialog.w)
            for (unsigned i = 0; i < now.count && !start; ++i) start = now.items[i].role == pcwin::kRoleButton && !strcmp(pcwin::Text(now, now.items[i]), "Start");
        // an open menu hides the Start button (pressed, it is no button to the engine): still the desktop
        bool menu = false;
        for (unsigned i = 0; i < now.count && !menu; ++i) menu = now.items[i].role == pcwin::kRoleMenu;
        const bool arrived = start && !wasDesktop && !menu;
        if (!menu) wasDesktop = start;
        if (arrived && pc::settings.readerRun[0] && !runDone && !runDueUs) runDueUs = pc_usec() + 8000000;
        if (arrived && installedReader && !keepOn && !guestReader) {
            // Windows is up and has its own reader: from here on Charon's control reader only watches
            guestReader = steppedForSeen = true;
            Log("CharonPC reader: Windows is up and its screen reader is installed, Charon's control reader is quiet\n");
            pc::StopVoice();
            current ^= 1; haveScreen = true;
            return;
        }
        if (arrived) n =(size_t)snprintf(speech, sizeof speech, "%s", pc::polish ? "Pulpit Windows. Control Shift F9 czyta ekran." : "Windows desktop. Control Shift F9 reads the screen.");
    }
    Steer(now);
    const bool steering = steer != steerNone;
    const bool samePage = haveScreen && before.pageHash == now.pageHash && before.focus == now.focus;
    const int percent = Percent(now), was = haveScreen ? Percent(before) : -1;
    haveScreen = true;
    current ^= 1;
    review = -1;
    if (!n) return;
    if (samePage && percent >= 0 && was >= 0) {
        // running progress bars: every tenth of the whole job is enough, and it never cuts other speech
        if (percent / 10 == lastTenth) return;
        lastTenth = percent / 10;
        snprintf(speech, sizeof speech, "%d%%", percent);
        Speak(false);
        return;
    }
    if (!samePage) lastTenth = percent >= 0 ? percent / 10 : -1;
    if (!announced) {
        announced = true;
        pc::Say(pc::polish
            ? "Czytnik kontrolek Charona czyta ten ekran Windows. Tab i strzałki przechodzą po kontrolkach. Control Shift F9 czyta całe okno, Control Shift F10 powtarza bieżącą kontrolkę."
            : "Charon's control reader reads this Windows screen. Tab and the arrows move between the controls. Control Shift F9 reads the whole window, Control Shift F10 repeats the current control.");
        Speak(false);
        return;
    }
    Speak(!steering);   // pages Charon confirms itself are heard one after another, not cut
}

}  // namespace

void pc::ReaderMode(bool graphicsMode, unsigned width, unsigned height)
{
    const bool usable = graphicsMode && width && height && width <= maxWidth && height <= maxHeight;
    if (!usable || width != frameWidth || height != frameHeight) {
        memset(frame, 0, sizeof frame);
        haveScreen = false;
        if (context) pcwin::ResetHistory(context);
    }
    if (!usable && graphics) { guestReader = seenReader = steppedForSeen = keepOn = false; goneUs = 0; }   // back in text mode (a restart): the guest reader is gone
    // r407: is the guest's screen reader installed already (its folder is on the guest's disk)? Then
    // Windows starts with it, and Charon's control reader reads only the way there (ScanDisk, the
    // password window): once the desktop is up it is quiet without a word - the user's rule (Read).
    if (usable && !graphics) {
        installedReader = false;
        if (Jaws() && pc::settings.diskFile[0]) pc::GuestHasReader(pc::settings.diskFile, "A.EXE", &installedReader);
    }
    graphics = usable;
    frameWidth = usable ? width : 0;
    frameHeight = usable ? height : 0;
    changed = usable;
    changedUs = pc_usec();
    if (!usable) Owner(false);
}

void pc::ReaderTile(unsigned x0, unsigned y0, unsigned width, unsigned height, const uint32_t* pixels)
{
    if (!graphics || x0 >= frameWidth || y0 >= frameHeight) return;
    if (x0 + width > frameWidth) width = frameWidth - x0;
    bool differs = false;
    for (unsigned y = 0; y < height && y0 + y < frameHeight; ++y) {
        uint32_t* row = frame + (size_t)(y0 + y) * maxWidth + x0;
        if (memcmp(row, pixels + (size_t)y * width, (size_t)width * 4)) { memcpy(row, pixels + (size_t)y * width, (size_t)width * 4); differs = true; }
    }
    if (differs) { changed = true; changedUs = pc_usec(); }
}

void pc::ReaderTick()
{
    if (graphics && steer != steerNone && pc_usec() - steerUs > 1500000) {
        if (steer == steerAccept) pc::GuestAltLetter(steerLetter); else if (steer == steerTab) pc::GuestTab(); else pc::GuestEnter();
        Log(steer == steerAccept ? "CharonPC reader: page answered with Alt and a letter\n" : steer == steerTab ? "CharonPC reader: Tab to the wanted button\n" : "CharonPC reader: page confirmed with Enter\n");
        steer = steerNone;
    }
    if (graphics && runDueUs && !runDone && pc_usec() > runDueUs) {
        runDone = true;
        runDueUs = 0;
        char command[40];
        snprintf(command, sizeof command, "%s", pc::settings.readerRun);
        Log("CharonPC reader: starting the screen reader's installer from the desktop\n");
        pc::Say(pc::polish ? "Uruchamiam instalator czytnika ekranu i prowadzę go sam. Potem Windows uruchomi się ponownie."
                           : "Starting the screen reader's installer and leading it myself. Windows will restart afterwards.");
        pc::GuestRun(command);
    }
    if (!Active()) {
        // the guest's reader speaks (or the user asked for silence): the picture stays "owned", so that
        // Charon's resident reader does not start describing it over the guest's voice
        Owner(graphics && (guestReader || userOff) && context && pcwin::FontCount(context));
        if (graphics && guestReader && steppedForSeen && !userOff) {
            const uint64_t t = pc_usec();
            static uint64_t watchedUs;
            const bool due = changed ? t - changedUs >= 250000 && t - watchedUs >= 2000000 : goneUs && t - watchedUs >= 2000000;
            if (due) { watchedUs = t; changed = false; Watch(); }
        }
        return;
    }
    if (!changed || pc_usec() - changedUs < 250000) return;
    if (!Fonts()) { Owner(false); return; }   // without the fonts the resident reader keeps describing the picture
    Owner(true);
    changed = false;
    Read();
}

void pc::ReaderGuestSpoke()
{
    if (guestReader) return;
    guestReader = true;
    pc_log_write("CharonPC reader: the guest's screen reader speaks, Charon's control reader steps back\n", 86);
    Owner(false);
}

// scan: UEFI scan code of a key pressed with Ctrl+Shift. True: the key was the reader's.
bool pc::ReaderKey(unsigned scan)
{
    if (!graphics) return false;
    if (scan == 0x12) {   // F8
        userOff = !userOff;
        if (!userOff) { keepOn = guestReader; guestReader = seenReader = steppedForSeen = false; goneUs = 0; changed = true; changedUs = 0; haveScreen = false; }
        pc::StopVoice();
        pc::Say(userOff ? (pc::polish ? "Czytnik kontrolek wyłączony." : "Control reader off.") : (pc::polish ? "Czytnik kontrolek włączony." : "Control reader on."));
        return true;
    }
    if (scan != 0x13 && scan != 0x14 && scan != 0x01 && scan != 0x02) return false;
    if (!Active() || !haveScreen || !context) return true;
    const pcwin::Screen& now = screens[current];
    speech[0] = 0;
    if (scan == 0x13) pcwin::SpeakAll(now, Flags() | pcwin::kSpeakOutside, speech, sizeof speech);
    else if (scan == 0x14) {
        if (now.focus >= 0) pcwin::SpeakElement(now, (unsigned)now.focus, Flags(), speech, sizeof speech);
        else snprintf(speech, sizeof speech, "%s", pc::polish ? "Nie widzę zaznaczonej kontrolki." : "I see no focused control.");
    } else {
        const int from = review >= 0 ? review : now.focus;
        const int next = pcwin::ReviewStep(now, from, scan == 0x02 ? 1 : -1);
        if (next < 0 || next == from) snprintf(speech, sizeof speech, "%s", scan == 0x02 ? (pc::polish ? "Koniec okna." : "End of the window.") : (pc::polish ? "Początek okna." : "Start of the window."));
        else { review = next; pcwin::SpeakElement(now, (unsigned)next, Flags(), speech, sizeof speech); }
    }
    Speak(true);
    return true;
}

void pc::ReaderShutdown()
{
    Owner(false);
    graphics = false;
}
