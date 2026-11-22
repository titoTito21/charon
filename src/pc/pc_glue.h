// CharonPC - state shared by the C++ glue (pc_efi.cpp, pc_gui.cc, pc_bns.cpp,
// pc_config.cpp). GPL-2.0-or-later.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <efi/types.h>
#include <efi/system-table.h>
#include <efi/boot-services.h>
#include <efi/runtime-services.h>
#include <efi/protocol/simple-text-input.h>
#include <efi/protocol/simple-text-output.h>
#include "BootMenuVoice.h"
#include "CharonGraphicsTypes.h"

namespace pc {

// EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL (UEFI 2.10, 12.3), the parts used here.
struct KeyState { uint32_t shift; uint8_t toggle; };
struct KeyData { efi_input_key key; KeyState state; };
struct InputEx {
    efi_status (EFIAPI *Reset)(InputEx*, bool);
    efi_status (EFIAPI *ReadKeyStrokeEx)(InputEx*, KeyData*);
    efi_event WaitForKeyEx;
    efi_status (EFIAPI *SetState)(InputEx*, uint8_t*);
    void* RegisterKeyNotify;
    void* UnregisterKeyNotify;
};
constexpr uint32_t shiftValid = 0x80000000u;
constexpr uint32_t shiftRightShift = 0x01, shiftLeftShift = 0x02, shiftRightCtrl = 0x04, shiftLeftCtrl = 0x08,
                   shiftRightAlt = 0x10, shiftLeftAlt = 0x20, shiftRightLogo = 0x40, shiftLeftLogo = 0x80;

extern efi_system_table* ST;
extern efi_boot_services* BS;
extern efi_simple_text_output_protocol* Out;
extern InputEx* InEx;
extern bool polish;
// Set by the GUI when the user switched the machine off (Ctrl+Shift+F12).
extern bool userQuit;
// Console size chosen at start (80x25 when the firmware has it).
extern size_t consoleColumns, consoleRows;

// Charon's published voice, looked up when first needed (null without Charon).
hakefi::MenuVoice* Voice();
// Lets the voice render a bounded slice; called from the GUI event handler.
void PumpVoice();
// Speaks UTF-8 text (copied by the provider); false without a voice.
bool Say(const char* utf8);
void StopVoice();

// r407 sound: the emulated machine's 48 kHz stereo PCM goes to Charon's PCM sink (mixed under the
// speech) or, without any Charon voice, to the sound card CharonPC opens itself (pc_audio.cpp).
extern efi_handle Image;                                      // this image (to load AudioDxe)
bool AudioOpen();                                             // false: no output (AudioRoute says why)
const char* AudioRoute();
size_t AudioWrite(const int16_t* stereo, size_t frames);      // accepted frames, never blocks
size_t AudioQueued();                                         // frames accepted and not yet at the card
void AudioService();                                          // moves queued frames to CharonPC's own card
void AudioClose();
// The Bochs sound driver "charon" (pc_sound.cc): renders the time since the last call. From the GUI event hook.
void SoundTick();
// Packets of sound the guest has played since the start (0 without a sound device): how the control
// reader knows that a guest screen reader with a software synthesizer is really heard.
uint64_t SoundGuestPackets();

// The Braille 'n Speak synthesizer on COM1 (pc_bns.cpp).
void BnsPoll();          // speaks text that waited long enough without CR

// CHARONPC.CFG -> Bochs configuration text (pc_config.cpp). `directory` is
// the volume folder of the .CFG ('\'-separated, with a leading '\').
// Returns false and fills `error` when the file is unusable.
bool TranslateConfig(const char* cfg, size_t bytes, const char* directory, char* out, size_t capacity,
                     char* error, size_t errorCapacity);
// Settings the glue itself reads from CHARONPC.CFG.
struct GlueSettings {
    bool debugcon;       // mirror the Bochs log to I/O port 0x402
    bool debugserial;    // r406: mirror the Bochs log to the first serial port (0x3F8), for machines without port 0x402
    bool bns;            // COM1 is the Braille 'n Speak
    char logFile[260];   // volume file for the Bochs log, "" for none
    bool installing;     // r406: the profile says state=install (first starts, from the user's disc)
    char diskFile[300];  // r406: volume path of the first hard disk image, "" for none
    char cdromFile[300]; // r407: volume path of the disc image, "" for none
    char reader[40];     // r407: the profile's guest screen reader ("charon", "none", "jfw402", ...)
    bool autoSetup;      // r407: Setup without questions is in force: its already answered pages are confirmed
    char readerRun[24];  // r407: the reader's installer was put onto an installed system's disk now (C:\READER\<this>): the desktop starts it
    bool readerLead;     // r407: reader_lead=1: Charon answers the guest reader's start dialogs at every start (JAWS: Run as Demo)
    bool floppySetup;    // r407: started from SETUP.IMG; the boot order turns to the disk once it starts a system
};
extern GlueSettings settings;

// r407: Windows 98 / ME Setup without questions (pc_unattend.cpp). The answers come from
// CHARONPC.CFG (setup_key, setup_name, setup_org, setup_computer, setup_zone).
struct SetupAnswers { char key[40], name[64], org[64], computer[20], zone[64]; bool sound; };   // sound: the profile has sound=sb16
// Makes SETUP.IMG (the disc's start floppy with MSBATCH.INF) and formats the untouched disk.
// False: nothing was changed that matters; the profile then installs the interactive way.
bool PrepareUnattended(const char* isoPath, const char* floppyPath, const char* diskPath, const SetupAnswers& answers,
                       const char* readerPath);
// r407: an installed system gets its screen reader later (a ready disk image, or a profile whose reader
// was chosen after Setup): the installer goes onto a disc of its own (pc_unattend.cpp). The guest's
// disk is only read.
int GuestHasReader(const char* diskPath, const char* readerPath, bool* jawsInstalled = nullptr);   // jawsInstalled: a folder JAWS... or JFW... is in the root   // 1 has C:\READER\<file>, 0 Windows without it, -1 unknown
bool MakeReaderDisc(const char* readerPath, const char* isoPath, char* name, size_t nameCapacity);   // true: made now
// r407: the guest Windows's own fonts for the control reader (pc_winfonts.cpp): every .FON of the
// profile's disc (MINI.CAB) or, without one, of the installed system (\WINDOWS\FONTS). Returns the count.
typedef void (*FontSink)(const char* name, const uint8_t* data, size_t size, void* context);
unsigned LoadGuestFonts(const char* isoPath, const char* diskPath, FontSink found, void* context);
// r407: Charon's control reader for the guest's graphical Windows (pc_reader.cpp).
void ReaderMode(bool graphicsMode, unsigned width, unsigned height);
void ReaderTile(unsigned x0, unsigned y0, unsigned width, unsigned height, const uint32_t* pixels);
void ReaderTick();
void ReaderGuestSpoke();           // the guest's own reader sent text to its synthesizer
bool ReaderKey(unsigned scan);     // a key pressed with Ctrl+Shift; true: taken
void ReaderShutdown();
// Keys and pointer for the control reader and the Setup steering (pc_gui.cc).
void GuestEnter();
void GuestAltLetter(char letter);  // Alt + a..z
void GuestTab();
// Start menu -> Run -> the command -> Enter, typed with pauses (pc_gui.cc); one at a time.
void GuestRun(const char* command);
void ParkPointer();                // moves the guest's mouse pointer into the lower right corner
// True when the profile's first hard disk starts a system (its root folder holds IO.SYS), or cannot be judged.
bool DiskStartable();

}  // namespace pc
