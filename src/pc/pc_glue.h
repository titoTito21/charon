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
                   shiftRightAlt = 0x10, shiftLeftAlt = 0x20;

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
    bool bns;            // COM1 is the Braille 'n Speak
    char logFile[260];   // volume file for the Bochs log, "" for none
};
extern GlueSettings settings;

}  // namespace pc
