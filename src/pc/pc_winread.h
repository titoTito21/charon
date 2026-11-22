// SPDX-License-Identifier: GPL-2.0-or-later
//
// pc_winread - reads a classic Windows 3.x/9x screen (text and controls) from
// the emulated frame buffer, for the Charon screen reader.  Pure logic: no
// file or device access, no exceptions, no global state.  See WINREAD.txt.
//
// The raster fonts are NOT part of Charon.  The caller loads the .FON files
// from the user's own Windows media and hands the bytes to AddFon().

#ifndef CHARON_PC_WINREAD_H
#define CHARON_PC_WINREAD_H

#include <stddef.h>
#include <stdint.h>

namespace pcwin {

enum Role : uint8_t {
	kRoleText = 0,      // static text (one paragraph)
	kRoleButton,
	kRoleEdit,
	kRoleCheckBox,
	kRoleRadio,
	kRoleList,
	kRoleListItem,      // visible row of the preceding kRoleList (kFlagChild)
	kRoleCombo,
	kRoleProgress,
	kRoleGroup,
	kRoleMenu,          // an open pop-up menu (Start menu, drop-down, context menu); rect = its
	                    // frame, count = number of items; its items follow it
	kRoleMenuItem,      // text = caption (and shortcut); kFlagSelected = under the bar;
	                    // kFlagSubmenu, kFlagChecked, kFlagDisabled; position/count in its menu
	kRoleIcon,          // caption of an icon (desktop, folder window); only reported while one
	                    // of them has the dotted focus rectangle; position/count in reading order
	kRoleTab            // tab of a property sheet; kFlagSelected = the open page
};

enum Flag : uint16_t {
	kFlagFocused     = 0x0001,
	kFlagDefault     = 0x0002,  // default push button (not announced)
	kFlagDisabled    = 0x0004,
	kFlagChecked     = 0x0008,
	kFlagSelected    = 0x0010,  // list row, or edit field with a selection
	kFlagHighlighted = 0x0020,  // text outside the dialog in the minority colour (current setup step)
	kFlagOutside     = 0x0040,  // not inside the dialog whose title is Screen::title
	kFlagBold        = 0x0080,  // heading
	kFlagMultiline   = 0x0100,
	kFlagDamaged     = 0x0200,  // part of the text was covered (mouse pointer) and is missing
	kFlagChild       = 0x0400,  // list row; skipped by SpeakAll, visited by the review cursor
	kFlagCaret       = 0x0800,  // a caret was seen in this edit field
	kFlagUnread      = 0x1000,  // looks like text but no loaded font explains it (TrueType);
	                            // text is empty, rect is its place, count = number of rows
	kFlagSubmenu     = 0x2000   // menu item that opens another menu
};

enum WindowKind : uint16_t {
	kWindowNone = 0,
	kWindowNormal,          // announced as "window"
	kWindowMessage          // only text and a few buttons: announced as "dialog"
};

struct Rect {
	int16_t x, y, w, h;
};

struct Element {
	uint8_t  role;
	int8_t   percent;       // progress bar value, -1 when unknown / not a progress bar
	uint16_t flags;
	Rect     rect;
	uint16_t text;          // offset in Screen::pool: label of a button/check/radio, content of an
	                        // edit, selected row of a list, text of a static; 0 = empty
	uint16_t label;         // offset in Screen::pool: caption found beside/above an edit, list,
	                        // combo or progress bar; 0 = none
	uint16_t accelerator;   // Unicode of the underlined letter, 0 = none
	uint8_t  position;      // 1-based position in a radio group / row of edit fields; for a
	uint8_t  count;         // list and its rows: among the VISIBLE rows; 0 = none
	uint32_t colour;        // text colour 0x00RRGGBB (statics), 0 otherwise
};

enum {
	kMaxElements = 192,
	kTextPool    = 12288,
	kTitleBytes  = 160
};

// About 19 KB.  Do not put it on a small stack.
struct Screen {
	uint16_t count;
	int16_t  focus;         // index of the focused element, -1 = none found
	uint16_t width, height;
	Rect     dialog;        // w == 0: no dialog found
	uint32_t pageHash;      // hash of the title and the static texts of the dialog
	uint16_t poolUsed;
	uint16_t kind;          // WindowKind
	char     title[kTitleBytes];
	Element  items[kMaxElements];
	char     pool[kTextPool];
};

inline const char* Text(const Screen& screen, const Element& element)
{
	return screen.pool + element.text;
}

inline const char* Label(const Screen& screen, const Element& element)
{
	return screen.pool + element.label;
}

struct Context;

// The context is one block (about 11 MB) that the caller allocates once.
size_t ContextBytes();
Context* Init(void* memory, size_t bytes);      // nullptr when the block is too small

// Adds every raster font of a .FON (NE) file, or a bare .FNT: call it once
// per file, in any order.  Returns false when the file holds nothing usable
// (vector fonts, other formats, no room left); such a file is simply ignored.
// The bytes are copied; the caller may free them.
bool AddFon(Context* context, const uint8_t* bytes, size_t size);
unsigned FontCount(const Context* context);     // variants, synthetic bold included

// Forgets the focus remembered for the blinking caret.
void ResetHistory(Context* context);

void ClearScreen(Screen* screen);

// pixels: 0x00RRGGBB, stride in pixels, at most 1024 x 768.
void Analyse(Context* context, const uint32_t* pixels, unsigned width,
	unsigned height, unsigned stride, Screen* out);

enum SpeakFlag : unsigned {
	kSpeakPolish  = 1,      // Polish role and state words (so "true" selects Polish)
	kSpeakTerse   = 2,      // SpeakChange: do not repeat an unchanged dialog title
	kSpeakOutside = 4       // SpeakAll: also read what is outside the dialog
};

// All return the number of bytes written (without the NUL); 0 = nothing to say.
size_t SpeakChange(const Screen& before, const Screen& now, unsigned flags,
	char* out, size_t capacity);
size_t SpeakAll(const Screen& now, unsigned flags, char* out, size_t capacity);
size_t SpeakElement(const Screen& now, unsigned index, unsigned flags,
	char* out, size_t capacity);

// For kFlagUnread statics: lets the caller store what another recogniser read
// in Element::rect.  Clears the flag; SpeakChange/SpeakAll then use the text.
bool SetElementText(Screen* screen, unsigned index, const char* utf8);

// Review cursor: next (direction > 0) or previous element, statics and list
// rows included.  index -1 starts before the first / after the last element.
// Returns -1 at either end.
int ReviewStep(const Screen& screen, int index, int direction);
// Same, but stops only on controls (no statics, no list rows).
int ReviewStepControl(const Screen& screen, int index, int direction);

}	// namespace pcwin

#endif	// CHARON_PC_WINREAD_H
