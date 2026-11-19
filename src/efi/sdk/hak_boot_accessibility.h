/*
 * HAK Boot Accessibility Protocol, revision 1 (Charon r396).
 * SPDX-License-Identifier: MIT-0
 *
 * A UEFI protocol through which a boot loader, boot manager or any other
 * UEFI program tells a screen reader what it shows: its menu, the focused
 * item (role, name, value, state, position, the caret of an edit field),
 * countdowns, progress and its own keys. Charon's resident screen reader
 * (CHARON.EFI and the CHARONVOICE.EFI add-on) publishes it while it runs;
 * other screen readers may publish it too.
 *
 * Without it Charon reads a program from its pixels and console text
 * (recognition, heuristics, built-in profiles). A program that describes its
 * screen gets exact, fast speech and braille in the user's language and
 * settings, also with fonts and pictures Charon cannot recognise.
 *
 * Use (C, no other header needed; with EDK II include it after Uefi.h):
 *
 *   HAK_BOOT_A11Y_PROTOCOL *a11y = NULL;
 *   EFI_GUID guid = HAK_BOOT_A11Y_PROTOCOL_GUID;
 *   if (gBS->LocateProtocol(&guid, NULL, (void **)&a11y) == EFI_SUCCESS
 *       && HakBootA11yUsable(a11y)) {
 *       a11y->BeginScreen(a11y, u"My loader", HAK_A11Y_SCREEN_MENU);
 *       HAK_BOOT_A11Y_FOCUS f = HAK_BOOT_A11Y_FOCUS_INIT;
 *       f.Role = HAK_A11Y_ROLE_MENU_ITEM; f.Name = u"Debian GNU/Linux";
 *       f.State = HAK_A11Y_STATE_DEFAULT; f.Index = 1; f.Count = 3;
 *       a11y->Focus(a11y, &f);              // after every selection change
 *       ...
 *       a11y->EndScreen(a11y);              // before starting the chosen system
 *   }
 *
 * Rules
 *  - All calls are made at TPL_APPLICATION, before ExitBootServices. Strings
 *    are copied before the call returns; the caller keeps ownership.
 *  - Every call is optional; the screen is still drawn as usual. A program
 *    must work the same when the protocol is absent or a call fails.
 *  - Between BeginScreen and EndScreen the reader does not read the
 *    program's screen by itself (no double speech); review keys still read
 *    what is drawn. The reader ends the screen itself when the program
 *    returns, starts another image or ends boot services.
 *  - Texts are spoken in the reader's language rules (Polish and English are
 *    detected per text); the program sends the text it shows, not its own
 *    spoken phrasing. Roles and states are spoken by the reader in the
 *    user's language, so do not add words like "menu" or "selected".
 *  - Revision changes only add fields at the end; check Size before using a
 *    field that a later revision adds.
 */
#ifndef HAK_BOOT_ACCESSIBILITY_H
#define HAK_BOOT_ACCESSIBILITY_H
#include <stdint.h>

#ifndef HAK_A11Y_EFIAPI
#if defined(_MSC_VER)
#define HAK_A11Y_EFIAPI
#elif defined(__x86_64__)
#define HAK_A11Y_EFIAPI __attribute__((ms_abi))
#else
#define HAK_A11Y_EFIAPI
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* {7a3c5e21-4b9d-4f1e-a652-1c8e930d27b4} */
#define HAK_BOOT_A11Y_PROTOCOL_GUID \
    { 0x7a3c5e21, 0x4b9d, 0x4f1e, { 0xa6, 0x52, 0x1c, 0x8e, 0x93, 0x0d, 0x27, 0xb4 } }
#define HAK_BOOT_A11Y_REVISION 1

typedef uint16_t HAK_A11Y_CHAR16;   /* UCS-2, like CHAR16 */
typedef uintptr_t HAK_A11Y_STATUS;  /* EFI_STATUS */

/* Roles (spoken by the reader in the user's language). */
enum {
    HAK_A11Y_ROLE_TEXT = 0,       /* plain text, a label */
    HAK_A11Y_ROLE_MENU_ITEM = 1,  /* a bootable entry or a menu command */
    HAK_A11Y_ROLE_SUBMENU = 2,    /* an item that opens a menu or folder */
    HAK_A11Y_ROLE_BUTTON = 3,
    HAK_A11Y_ROLE_CHECK_BOX = 4,
    HAK_A11Y_ROLE_RADIO = 5,
    HAK_A11Y_ROLE_EDIT = 6,       /* a line of editable text: Value + Caret */
    HAK_A11Y_ROLE_PASSWORD = 7,   /* like EDIT; Value is never spoken */
    HAK_A11Y_ROLE_COMBO = 8,      /* a value chosen from a list */
    HAK_A11Y_ROLE_SLIDER = 9,     /* a number in a range (Value) */
    HAK_A11Y_ROLE_TAB = 10,
    HAK_A11Y_ROLE_DIALOG = 11,    /* a message or question window (Name = title, Description = text) */
    HAK_A11Y_ROLE_LIST_ITEM = 12,
    HAK_A11Y_ROLE_HEADING = 13,
    HAK_A11Y_ROLE_COUNT
};

/* States (bits). */
#define HAK_A11Y_STATE_CHECKED    0x0001u
#define HAK_A11Y_STATE_EXPANDED   0x0002u
#define HAK_A11Y_STATE_COLLAPSED  0x0004u
#define HAK_A11Y_STATE_DISABLED   0x0008u
#define HAK_A11Y_STATE_DEFAULT    0x0010u  /* the entry started when the countdown ends */
#define HAK_A11Y_STATE_READ_ONLY  0x0020u
#define HAK_A11Y_STATE_HIDDEN     0x0040u  /* shown only on request (auxiliary entries) */
#define HAK_A11Y_STATE_ERROR      0x0080u  /* invalid value, failed entry */

#define HAK_A11Y_NO_CARET 0xFFFFFFFFu

typedef struct {
    uint32_t Size;                         /* sizeof(HAK_BOOT_A11Y_FOCUS) */
    uint32_t Role;                         /* HAK_A11Y_ROLE_* */
    uint32_t State;                        /* HAK_A11Y_STATE_* */
    uint32_t Index;                        /* 1-based position among Count siblings; 0 = none */
    uint32_t Count;
    uint32_t Caret;                        /* EDIT/PASSWORD: caret position in Value; HAK_A11Y_NO_CARET otherwise */
    uint32_t Level;                        /* nesting depth, 0 = top (folders, submenus) */
    uint32_t Reserved;
    const HAK_A11Y_CHAR16 *Name;           /* the visible label; may be NULL */
    const HAK_A11Y_CHAR16 *Value;          /* the value, the edited text; may be NULL */
    const HAK_A11Y_CHAR16 *Description;    /* extra help line; may be NULL */
} HAK_BOOT_A11Y_FOCUS;
#define HAK_BOOT_A11Y_FOCUS_INIT { sizeof(HAK_BOOT_A11Y_FOCUS), 0, 0, 0, 0, HAK_A11Y_NO_CARET, 0, 0, 0, 0, 0 }

/* Speak flags. */
#define HAK_A11Y_SPEAK_INTERRUPT 0x1u  /* stop current speech first */
#define HAK_A11Y_SPEAK_MESSAGE   0x2u  /* a status message: the reader's message voice */
#define HAK_A11Y_SPEAK_ALERT     0x4u  /* an error or warning (higher pitch, interrupts) */

/* BeginScreen flags. */
#define HAK_A11Y_SCREEN_MENU     0x1u  /* a choice of entries */
#define HAK_A11Y_SCREEN_EDITOR   0x2u  /* an entry / command line editor */
#define HAK_A11Y_SCREEN_DIALOG   0x4u

/* Capabilities and settings flags. */
#define HAK_A11Y_HAS_SPEECH          0x1u
#define HAK_A11Y_HAS_BRAILLE         0x2u
#define HAK_A11Y_STOP_COUNTDOWNS     0x4u  /* the user wants countdowns stopped */
#define HAK_A11Y_SAY_POSITIONS       0x8u  /* the user wants "2 of 5" */

typedef struct {
    uint32_t Size;           /* in: sizeof(HAK_BOOT_A11Y_SETTINGS) */
    uint32_t Flags;          /* HAK_A11Y_* above */
    char Language[8];        /* "pl", "en" (ASCII, zero-terminated) */
    uint32_t Verbosity;      /* 1 short, 2 detailed */
    uint32_t Reserved;
} HAK_BOOT_A11Y_SETTINGS;

typedef struct HAK_BOOT_A11Y_PROTOCOL HAK_BOOT_A11Y_PROTOCOL;
struct HAK_BOOT_A11Y_PROTOCOL {
    uint32_t Revision;       /* HAK_BOOT_A11Y_REVISION */
    uint32_t Size;           /* sizeof(HAK_BOOT_A11Y_PROTOCOL) */
    uint32_t Capabilities;   /* HAK_A11Y_HAS_* */
    uint32_t Reserved;
    /* Speak a text now (a status line, a message the program shows). */
    HAK_A11Y_STATUS (HAK_A11Y_EFIAPI *Speak)(HAK_BOOT_A11Y_PROTOCOL *This, const HAK_A11Y_CHAR16 *Text, uint32_t Flags);
    /* The program describes this screen itself from now on; Title is spoken
       (e.g. "GNU GRUB version 2.12", may be NULL), then the first Focus. */
    HAK_A11Y_STATUS (HAK_A11Y_EFIAPI *BeginScreen)(HAK_BOOT_A11Y_PROTOCOL *This, const HAK_A11Y_CHAR16 *Title, uint32_t Flags);
    /* The focused item changed, or its value / caret did. The reader speaks
       the change only: a new item in full, an edit field's typed or deleted
       character, the character or line at a moved caret. */
    HAK_A11Y_STATUS (HAK_A11Y_EFIAPI *Focus)(HAK_BOOT_A11Y_PROTOCOL *This, const HAK_BOOT_A11Y_FOCUS *Focus);
    /* Once per second of an automatic-start countdown (0 = ends now). The
       reader gives its countdown sounds and speech. *StopRequested is set
       when the user asked the reader to stop countdowns: stop it then, as
       if a key had been pressed. StopRequested may be NULL. */
    HAK_A11Y_STATUS (HAK_A11Y_EFIAPI *Countdown)(HAK_BOOT_A11Y_PROTOCOL *This, uint32_t SecondsLeft, uint8_t *StopRequested);
    /* Loading progress 0..100 (beeps and/or speech, as the user set). */
    HAK_A11Y_STATUS (HAK_A11Y_EFIAPI *Progress)(HAK_BOOT_A11Y_PROTOCOL *This, uint32_t Percent, const HAK_A11Y_CHAR16 *Label);
    /* The program's own keys, one line each ("e: edit the entry"); listed
       first in the reader's key list (Insert+F1). Count 0 clears them. */
    HAK_A11Y_STATUS (HAK_A11Y_EFIAPI *SetKeyHelp)(HAK_BOOT_A11Y_PROTOCOL *This, const HAK_A11Y_CHAR16 *const *Lines, uint32_t Count);
    /* The reader reads the screen by itself again. */
    HAK_A11Y_STATUS (HAK_A11Y_EFIAPI *EndScreen)(HAK_BOOT_A11Y_PROTOCOL *This);
    /* The user's reader settings (language, positions, countdown wish). */
    HAK_A11Y_STATUS (HAK_A11Y_EFIAPI *GetSettings)(HAK_BOOT_A11Y_PROTOCOL *This, HAK_BOOT_A11Y_SETTINGS *Settings);
};

static inline int HakBootA11yUsable(const HAK_BOOT_A11Y_PROTOCOL *p)
{
    return p && p->Revision >= 1 && p->Size >= sizeof(HAK_BOOT_A11Y_PROTOCOL)
        && p->Speak && p->BeginScreen && p->Focus && p->Countdown && p->Progress
        && p->SetKeyHelp && p->EndScreen && p->GetSettings;
}

#ifdef __cplusplus
}
#endif
#endif
