// SPDX-License-Identifier: GPL-2.0-or-later
//
// Host test of src/pc/pc_winread.cpp.
//
//   CharonWinReadTests FONT_DIR FRAME_DIR [--dump] [--time]
//
// FONT_DIR   holds the raster fonts of a Windows 9x disc (SSERIFE.FON,
//            VGASYS.FON, VGAFIX.FON, SERIFE.FON, VGAOEM.FON).
// FRAME_DIR  holds 640x480 frames as raw B,G,R,0 bytes (<name>.bgrx), made
//            from screen shots of Windows Millennium Setup.
// Neither can be shipped, so the test prints SKIP when they are absent.
// The synthetic part needs only the fonts.

#include "pc_winread.h"

#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace pcwin;

static int gChecks = 0, gFailures = 0;
static bool gDump = false, gTime = false;
static const char* gFrameDir = "";
static Context* gContext;
static Screen gNow, gBefore, gEmpty;
static char gSpeech[32768];
static double gWorst = 0, gTotal = 0;
static int gFrames = 0;

static void Check(bool ok, const char* what, const char* detail = "")
{
	gChecks++;
	if (!ok) {
		gFailures++;
		printf("FAIL: %s %s\n", what, detail);
	}
}

static void CheckText(const char* actual, const char* expected, const char* what)
{
	gChecks++;
	if (strcmp(actual, expected) != 0) {
		gFailures++;
		printf("FAIL: %s\n  expected: %s\n  actual:   %s\n", what, expected, actual);
	}
}

static uint8_t* Load(const char* directory, const char* name, size_t* size)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", directory, name);
	FILE* file = fopen(path, "rb");
	if (file == nullptr)
		return nullptr;
	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	fseek(file, 0, SEEK_SET);
	uint8_t* bytes = (uint8_t*)malloc(length > 0 ? length : 1);
	*size = fread(bytes, 1, length > 0 ? length : 0, file);
	fclose(file);
	return bytes;
}

static const char* kRoles[] = { "text", "button", "edit", "check", "radio", "list",
	"item", "combo", "progress", "group", "menu", "menuitem", "icon", "tab" };

static void Dump(const Screen& s, const char* name)
{
	printf("--- %s: title [%s] kind %u dialog %d,%d %dx%d focus %d\n", name, s.title,
		s.kind, s.dialog.x, s.dialog.y, s.dialog.w, s.dialog.h, s.focus);
	for (unsigned i = 0; i < s.count; i++) {
		const Element& e = s.items[i];
		printf("  %2u %-8s %04x (%d,%d %dx%d) %d %u/%u [%s] label [%s]\n", i,
			e.role < sizeof(kRoles) / sizeof(kRoles[0]) ? kRoles[e.role] : "?",
			e.flags, e.rect.x, e.rect.y, e.rect.w, e.rect.h, e.percent, e.position,
			e.count, Text(s, e), Label(s, e));
	}
}

static void Run(const uint32_t* pixels, unsigned width, unsigned height, const char* name)
{
	memcpy(&gBefore, &gNow, sizeof(gNow));
	auto start = std::chrono::steady_clock::now();
	Analyse(gContext, pixels, width, height, width, &gNow);
	double ms = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - start).count();
	if (gTime) {
		// best of five: the first run pays for cold caches
		for (int i = 0; i < 4; i++) {
			Screen scratch;
			ResetHistory(gContext);
			start = std::chrono::steady_clock::now();
			Analyse(gContext, pixels, width, height, width, &scratch);
			double again = std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - start).count();
			if (again < ms)
				ms = again;
		}
		printf("time %-12s %6.2f ms\n", name, ms);
	}
	gTotal += ms;
	gFrames++;
	if (ms > gWorst)
		gWorst = ms;
	if (gDump) {
		Dump(gNow, name);
		SpeakAll(gNow, kSpeakPolish | kSpeakOutside, gSpeech, sizeof(gSpeech));
		printf("  all: %s\n", gSpeech);
	}
}

static uint32_t* gFrame;

static bool Frame(const char* name)
{
	char file[256];
	snprintf(file, sizeof(file), "%s.bgrx", name);
	size_t size = 0;
	uint8_t* bytes = Load(gFrameDir, file, &size);
	if (bytes == nullptr || size != 640 * 480 * 4) {
		printf("SKIP frame %s (not in %s)\n", name, gFrameDir);
		free(bytes);
		return false;
	}
	free(gFrame);
	gFrame = (uint32_t*)bytes;
	Run(gFrame, 640, 480, name);
	return true;
}

// first element of a role whose text (or label, for fields) is "text"
static const Element* Find(const Screen& s, unsigned role, const char* text)
{
	for (unsigned i = 0; i < s.count; i++) {
		const Element& e = s.items[i];
		if (e.role != role)
			continue;
		if (strcmp(Text(s, e), text) == 0 || strcmp(Label(s, e), text) == 0)
			return &e;
	}
	return nullptr;
}

static const Element* Nth(const Screen& s, unsigned role, unsigned n)
{
	for (unsigned i = 0; i < s.count; i++) {
		if (s.items[i].role == role && !(s.items[i].flags & kFlagOutside) && n-- == 0)
			return &s.items[i];
	}
	return nullptr;
}

static unsigned Count(const Screen& s, unsigned role)
{
	unsigned n = 0;
	for (unsigned i = 0; i < s.count; i++) {
		if (s.items[i].role == role && !(s.items[i].flags & kFlagOutside))
			n++;
	}
	return n;
}

static void Has(unsigned role, const char* text, unsigned set, unsigned clear = 0)
{
	const Element* e = Find(gNow, role, text);
	Check(e != nullptr, "element missing:", text);
	if (e != nullptr) {
		Check((e->flags & set) == set, "flag not set on", text);
		Check((e->flags & clear) == 0, "flag wrongly set on", text);
	}
}

static void Focus(unsigned role, const char* text)
{
	Check(gNow.focus >= 0, "no focus, wanted", text);
	if (gNow.focus < 0)
		return;
	const Element& e = gNow.items[gNow.focus];
	Check(e.role == role, "focus role, wanted", text);
	Check(strcmp(Text(gNow, e), text) == 0 || strcmp(Label(gNow, e), text) == 0,
		"focus is not", text);
}

static const char* Change(unsigned flags = kSpeakPolish)
{
	SpeakChange(gBefore, gNow, flags, gSpeech, sizeof(gSpeech));
	return gSpeech;
}

static const char* All(unsigned flags = kSpeakPolish)
{
	SpeakAll(gNow, flags, gSpeech, sizeof(gSpeech));
	return gSpeech;
}

static void Fresh()
{
	ClearScreen(&gNow);
	ResetHistory(gContext);
}

// ---------------------------------------------------------------------------
// real screen shots of Windows Millennium Setup

static void TestShots()
{
	const unsigned B = kRoleButton, T = kRoleText;

	// first part of Setup: Windows 3.1 kernel, CTL3D look, VGA 16 colours
	Fresh();
	if (Frame("me1-g1")) {
		CheckText(gNow.title, "Windows Millennium Edition Setup Wizard", "g1 title");
		Check(gNow.kind == kWindowNormal, "g1 kind");
		Has(T, "Select Directory", kFlagBold, kFlagOutside);
		Has(T, "Select the directory where you want to install Windows.", 0, kFlagBold);
		Has(kRoleRadio, "C:\\WINDOWS (recommended)", kFlagChecked | kFlagFocused);
		Has(kRoleRadio, "Other directory", 0, kFlagChecked | kFlagFocused);
		const Element* other = Find(gNow, kRoleRadio, "Other directory");
		Check(other && other->accelerator == 'O' && other->position == 2 && other->count == 2,
			"g1 radio accelerator and position");
		Has(kRoleEdit, "C:\\WINDOWS", kFlagDisabled);
		Has(B, "< Back", kFlagDisabled, kFlagDefault);
		Has(B, "Next >", kFlagDefault, kFlagDisabled | kFlagFocused);
		Has(B, "Cancel", 0, kFlagDisabled | kFlagDefault);
		Check(Count(gNow, B) == 3 && Count(gNow, kRoleRadio) == 2 && Count(gNow, kRoleEdit) == 1,
			"g1 control counts");
		Focus(kRoleRadio, "C:\\WINDOWS (recommended)");
		CheckText(Change(), "Windows Millennium Edition Setup Wizard, okno. Select Directory. "
			"Select the directory where you want to install Windows. "
			"C:\\WINDOWS (recommended), przycisk opcji, zaznaczony, 1 z 2", "g1 new page");
		CheckText(All(), "Windows Millennium Edition Setup Wizard, okno. Select Directory. "
			"Select the directory where you want to install Windows. "
			"C:\\WINDOWS (recommended), przycisk opcji, zaznaczony, 1 z 2. "
			"Other directory, przycisk opcji, niezaznaczony, 2 z 2. "
			"pole edycji, C:\\WINDOWS, niedost\xC4\x99pny. Back, przycisk, niedost\xC4\x99pny. "
			"Next, przycisk. Cancel, przycisk.", "g1 whole window");
		CheckText(All(0), "Windows Millennium Edition Setup Wizard, window. Select Directory. "
			"Select the directory where you want to install Windows. "
			"C:\\WINDOWS (recommended), radio button, checked, 1 of 2. "
			"Other directory, radio button, not checked, 2 of 2. "
			"edit, C:\\WINDOWS, unavailable. Back, button, unavailable. "
			"Next, button. Cancel, button.", "g1 whole window, English");
		// the side panel and the banner are Arial (TrueType): reported as unread
		unsigned unread = 0, highlighted = 0;
		for (unsigned i = 0; i < gNow.count; i++) {
			if (gNow.items[i].flags & kFlagUnread) {
				unread++;
				Check((gNow.items[i].flags & kFlagOutside) != 0, "g1 unread text is outside");
				if (gNow.items[i].flags & kFlagHighlighted)
					highlighted++;
			}
		}
		Check(unread >= 10 && highlighted == 2, "g1 unread side panel rows");
	}
	if (Frame("me1-g2")) {
		Has(T, "Preparing Directory", kFlagBold);
		Has(T, "Please wait while Setup prepares your Windows directory and verifies that "
			"your computer has enough free disk space available to install Windows Me.", 0);
		const Element* a = Find(gNow, kRoleProgress, "Checking for installed components...");
		const Element* b = Find(gNow, kRoleProgress, "Checking for available disk space...");
		Check(a && a->percent == 100, "g2 first bar 100%");
		Check(b && b->percent == 37, "g2 second bar 37%");
		Has(B, "< Back", kFlagDisabled);
		Has(B, "Next >", kFlagDisabled);
		Has(B, "Cancel", kFlagDefault | kFlagFocused);
		Focus(B, "Cancel");
		CheckText(Change(kSpeakPolish | kSpeakTerse), "Preparing Directory. Please wait while "
			"Setup prepares your Windows directory and verifies that your computer has enough "
			"free disk space available to install Windows Me. "
			"Checking for installed components..., pasek post\xC4\x99pu, 100%. "
			"Checking for available disk space..., pasek post\xC4\x99pu, 37%. Cancel, przycisk",
			"g2 new page, terse");
	}
	if (Frame("me1-g3")) {
		Has(T, "Setup Options", kFlagBold);
		Has(kRoleRadio, "Typical", kFlagChecked | kFlagFocused);
		Has(kRoleRadio, "Portable", 0, kFlagChecked);
		Has(kRoleRadio, "Compact", 0, kFlagChecked);
		Has(kRoleRadio, "Custom", 0, kFlagChecked);
		const Element* custom = Find(gNow, kRoleRadio, "Custom");
		Check(custom && custom->position == 4 && custom->count == 4 && custom->accelerator == 'u',
			"g3 Custom 4 of 4, accelerator u");
		Has(T, "This option is recommended for most computers.", 0);
		Has(T, "This option is for advanced users and system administrators only. You can "
			"customize all available Setup options. You have to select this when you want to "
			"change the locale of the system.", 0);
		Has(B, "< Back", 0, kFlagDisabled);
	}
	if (Frame("me1-g5")) {
		CheckText(gNow.title, "Windows Millennium Setup Wizard", "g5 title");
		Has(T, "Network Identification", kFlagBold);
		const Element* name = Find(gNow, kRoleEdit, "Computer name:");
		Check(name && strcmp(Text(gNow, *name), "V8H4G2") == 0, "g5 selected text of the field");
		Check(name && (name->flags & kFlagSelected) && name->accelerator == 'C',
			"g5 field selection and accelerator");
		const Element* group = Find(gNow, kRoleEdit, "Workgroup:");
		Check(group && strcmp(Text(gNow, *group), "WORKGROUP") == 0, "g5 workgroup");
		const Element* description = Find(gNow, kRoleEdit, "Computer Description:");
		Check(description && Text(gNow, *description)[0] == 0, "g5 empty field");
		Focus(kRoleEdit, "Computer name:");
		CheckText(Change(kSpeakPolish | kSpeakTerse), "Windows Millennium Setup Wizard, okno. "
			"Network Identification. Specify a name and workgroup to identify your computer on "
			"a network. Please type a name, workgroup, and description of this computer to "
			"identify it on the network. Computer name, pole edycji, V8H4G2", "g5 new page");
	}
	if (Frame("me1-g6")) {
		CheckText(gNow.title, "Windows Millennium Edition Setup", "g6 title");
		const Element* list = Find(gNow, kRoleList, "Select your country/region:");
		Check(list && strcmp(Text(gNow, *list), "United States") == 0, "g6 selected row");
		Check(list && list->position == 3 && list->count == 15, "g6 3 of 15 visible rows");
		Has(kRoleListItem, "United Arab Emirates", kFlagChild, kFlagSelected);
		Has(kRoleListItem, "United States", kFlagChild | kFlagSelected);
		Has(kRoleListItem, "Zimbabwe", kFlagChild, kFlagSelected);
		Check(Count(gNow, kRoleListItem) == 15, "g6 fifteen rows");
		Focus(kRoleList, "Select your country/region:");
		CheckText(Change(0), "Windows Millennium Edition Setup, window. Country/Region. "
			"You can change your country/region. Select your country/region, list, "
			"United States, 3 of 15", "g6 new page, English");
		// review cursor: the row after the selected one
		int at = -1;
		for (unsigned i = 0; i < gNow.count; i++) {
			if (gNow.items[i].role == kRoleListItem && (gNow.items[i].flags & kFlagSelected))
				at = (int)i;
		}
		at = ReviewStep(gNow, at, 1);
		SpeakElement(gNow, at, kSpeakPolish, gSpeech, sizeof(gSpeech));
		CheckText(gSpeech, "Uruguay", "g6 review cursor");
		SpeakElement(gNow, ReviewStep(gNow, at, -1), kSpeakPolish, gSpeech, sizeof(gSpeech));
		CheckText(gSpeech, "United States, wybrany", "g6 review cursor back");
	}
	if (Frame("me1-g7")) {
		Has(T, "Establishing Your Time Zone", kFlagBold);
		const Element* list = Find(gNow, kRoleList, "Select your time zone from the list below.");
		// the mouse pointer covers the "S" of "US" in the selected row
		Check(list && strcmp(Text(gNow, *list),
			"(GMT-08:00) Pacific Time (U & Canada); Tijuana") == 0, "g7 selected row");
		Has(kRoleListItem, "(GMT-12:00) Eniwetok, Kwajalein", kFlagChild, kFlagSelected);
		Has(kRoleListItem, "(GMT-06:00) Central America", kFlagChild, kFlagSelected);
		Has(kRoleCheckBox, "Automatically adjust clock for daylight saving changes.",
			kFlagChecked);
		Focus(kRoleList, "Select your time zone from the list below.");
		SpeakElement(gNow, (unsigned)(Find(gNow, kRoleCheckBox,
			"Automatically adjust clock for daylight saving changes.") - gNow.items),
			kSpeakPolish, gSpeech, sizeof(gSpeech));
		CheckText(gSpeech, "Automatically adjust clock for daylight saving changes, "
			"pole wyboru, zaznaczony", "g7 check box");
	}
	if (Frame("me1-f5")) {
		Has(T, "Congratulations on choosing Windows Millennium Edition, the software that "
			"makes your computer more powerful, reliable, manageable, and entertaining.", 0);
		Has(T, "With Windows Millennium Edition, you can connect to the Internet quickly and "
			"easily. Windows Me is even easier to use than previous versions of Windows.", 0);
		Has(T, "To begin Setup, click Next.", 0);
		// no dotted rectangle on this page: the default button is taken as focused
		Focus(B, "Next >");
	}

	// second part: Windows Millennium itself, Windows 95 look
	Fresh();
	if (Frame("me1-k1")) {
		CheckText(gNow.title, "Windows Millennium Edition Setup Wizard", "k1 title");
		Has(T, "User Information", kFlagBold);
		Has(T, "Type your name and your company name below. If you do not want to include "
			"your company name, leave the text box blank.", 0);
		const Element* name = Find(gNow, kRoleEdit, "Name:");
		Check(name && (name->flags & kFlagCaret) && (name->flags & kFlagFocused)
			&& name->accelerator == 'a', "k1 caret in Name");
		Has(kRoleEdit, "Company:", 0, kFlagFocused);
		Has(B, "< Back", 0, kFlagDisabled);
		Has(B, "Next >", kFlagDefault);
		const Element* back = Find(gNow, B, "< Back");
		Check(back && back->accelerator == 'B', "k1 accelerator of Back");
		// side panel (MS Sans Serif here)
		Has(T, "Preparing to run Windows Setup", kFlagOutside, kFlagHighlighted);
		Has(T, "Setting up hardware and finalizing settings", kFlagOutside | kFlagHighlighted);
		Has(T, "Estimated time remaining:", kFlagOutside, kFlagHighlighted);
		Has(T, "13 minutes", kFlagOutside | kFlagHighlighted);
		Has(T, "Setup will now detect your computer's hardware and may restart your "
			"computer. If your computer stops responding for a long time, turn it off and "
			"then back on.", kFlagOutside);
		CheckText(Change(), "Windows Millennium Edition Setup Wizard, okno. User Information. "
			"Please enter your user information below. Type your name and your company name "
			"below. If you do not want to include your company name, leave the text box blank. "
			"Name, pole edycji, puste. Setting up hardware and finalizing settings. "
			"Estimated time remaining: 13 minutes", "k1 new page with side panel");
		CheckText(All(), "Windows Millennium Edition Setup Wizard, okno. User Information. "
			"Please enter your user information below. Type your name and your company name "
			"below. If you do not want to include your company name, leave the text box blank. "
			"Name, pole edycji, puste. Company, pole edycji, puste. Back, przycisk. "
			"Next, przycisk. Cancel, przycisk.", "k1 whole window");
	}
	if (gFrame != nullptr && strcmp(gNow.title, "Windows Millennium Edition Setup Wizard") == 0) {
		// the same picture inside a 1024 x 768 frame with a wider stride
		const unsigned width = 1024, height = 768, stride = 1100;
		uint32_t* big = (uint32_t*)calloc(stride * height, 4);
		for (unsigned y = 0; y < 480; y++)
			memcpy(big + (y + 144) * stride + 192, gFrame + y * 640, 640 * 4);
		Screen* screen = (Screen*)malloc(sizeof(Screen));
		Context* context = gContext;
		auto start = std::chrono::steady_clock::now();
		Analyse(context, big, width, height, stride, screen);
		double ms = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - start).count();
		if (gTime)
			printf("time %-12s %6.2f ms (1024 x 768)\n", "me1-k1", ms);
		CheckText(screen->title, gNow.title, "1024 x 768: title");
		Check(screen->count == gNow.count && screen->focus == gNow.focus,
			"1024 x 768: same elements");
		Check(screen->dialog.x == gNow.dialog.x + 192 && screen->dialog.y == gNow.dialog.y + 144,
			"1024 x 768: dialog position");
		free(screen);
		free(big);
		ResetHistory(gContext);
	}
	if (Frame("me1-k2")) {
		Has(T, "License Agreement", kFlagBold);
		Has(T, "You must accept the License Agreement to continue Setup.", 0);
		const Element* licence = Nth(gNow, kRoleEdit, 0);
		Check(licence && (licence->flags & kFlagMultiline), "k2 licence text is a field");
		Check(licence && strstr(Text(gNow, *licence), "IMPORTANT-READ CAREFULLY: This End-User "
			"License Agreement (\"EULA\")is a legal agreement between you (either an "
			"individual or a single") != nullptr, "k2 licence text");
		Has(kRoleRadio, "I accept the Agreement", 0, kFlagChecked);
		Has(kRoleRadio, "I don't accept the Agreement", 0, kFlagChecked);
		Has(B, "Next >", kFlagDisabled, kFlagDefault);
		Has(B, "< Back", kFlagDefault | kFlagFocused);
		CheckText(Change(), "Windows Millennium Edition Setup Wizard, okno. License Agreement. "
			"Please read the following License Agreement. You must accept the License "
			"Agreement to continue Setup. Back, przycisk", "k2 new page");
	}
	if (Frame("me1-k3")) {
		Has(kRoleRadio, "I accept the Agreement", kFlagChecked | kFlagFocused);
		Has(B, "Next >", kFlagDefault, kFlagDisabled);
		CheckText(Change(), "I accept the Agreement, przycisk opcji, zaznaczony, 1 z 2",
			"k3 focus moved to the radio button");
		CheckText(Change(0), "I accept the Agreement, radio button, checked, 1 of 2",
			"k3 focus moved, English");
	}
	if (Frame("me1-k4")) {
		CheckText(gNow.title, "Windows Millennium Edition Setup", "k4 title");
		Has(T, "Windows Product Key", kFlagBold);
		Has(T, "You must enter the product key for this software.", 0);
		Has(T, "This Windows product key can be found on the lower right of the Certificate "
			"of Authenticity, immediately above the bar code.", 0);
		Check(Count(gNow, kRoleEdit) == 5, "k4 five fields");
		const Element* third = Nth(gNow, kRoleEdit, 2);
		Check(third && third->position == 3 && third->count == 5, "k4 field 3 of 5");
		Has(B, "Help...", 0, kFlagDisabled);
		Has(B, "Next >", kFlagDisabled);
		Focus(B, "< Back");
		SpeakAll(gNow, kSpeakPolish, gSpeech, sizeof(gSpeech));
		Check(strstr(gSpeech, "pole edycji, puste, 1 z 5. pole edycji, puste, 2 z 5. "
			"pole edycji, puste, 3 z 5. pole edycji, puste, 4 z 5. pole edycji, puste, 5 z 5. "
			"Help..., przycisk. Back, przycisk. Next, przycisk, niedost\xC4\x99pny. "
			"Cancel, przycisk.") != nullptr, "k4 controls of the whole window");
		if (Frame("live-s5985")) {
			// the same page a moment later: the caret is in the first field
			CheckText(Change(), "pole edycji, puste, 1 z 5", "caret moved into the first field");
			const Element* first = Nth(gNow, kRoleEdit, 0);
			Check(first && (first->flags & kFlagCaret), "caret flag");
			Check(Count(gNow, kRoleEdit) == 5, "still five fields");
		}
	}
	Fresh();
	if (Frame("me1-h4")) {
		CheckText(gNow.title, "Setting up Hardware", "h4 title");
		Check(gNow.kind == kWindowMessage, "h4 is a message box");
		SpeakChange(gEmpty, gNow, kSpeakPolish, gSpeech, sizeof(gSpeech));
		Check(strncmp(gSpeech, "Setting up Hardware, okno dialogowe. Windows ", 45) == 0,
			"h4 announced as a dialog");
		Has(T, "16 minutes", kFlagOutside | kFlagHighlighted);
	}
	Fresh();
	if (Frame("me1-h1")) {
		// "Enter Windows Password" (after Setup)
		CheckText(gNow.title, "Enter Windows Password", "password title");
		Has(T, "Type a name to identify yourself to Windows. Enter a password if you want to.", 0);
		Has(T, "Tip: If you don't enter a password, you won't get this prompt again at "
			"startup.", 0);
		Has(kRoleEdit, "User name:", 0, kFlagDisabled);
		Has(kRoleEdit, "Password:", 0, kFlagDisabled);
		Has(B, "OK", kFlagDefault);
		Has(B, "Cancel", 0, kFlagDefault);
		Check(gNow.count == 6, "password dialog: six elements");
		CheckText(All(), "Enter Windows Password, okno. Type a name to identify yourself to "
			"Windows. Enter a password if you want to. OK, przycisk. Tip: If you don't enter a "
			"password, you won't get this prompt again at startup. Cancel, przycisk. "
			"User name, pole edycji, puste. Password, pole edycji, puste.", "password dialog");
	}
	if (Frame("me1-h2")) {
		// the desktop: best effort
		Check(gNow.dialog.w == 0, "desktop has no dialog");
		// (an icon has the dotted focus rectangle: the captions are icons)
		Has(kRoleIcon, "My Computer", kFlagOutside);
		Has(kRoleIcon, "Recycle Bin", kFlagOutside);
		Has(kRoleIcon, "Internet Explorer", kFlagOutside);
		Focus(kRoleIcon, "My Documents");
		Has(B, "Start", kFlagOutside);
	}
	// file copy: only the progress bar is in a raster font
	Fresh();
	if (Frame("me1-h0")) {
		const Element* bar = Nth(gNow, kRoleProgress, 0);
		Check(bar == nullptr, "h0 bar is outside any dialog");
		unsigned index = 0;
		for (; index < gNow.count && gNow.items[index].role != kRoleProgress; index++) {
		}
		Check(index < gNow.count && gNow.items[index].percent == 25, "h0 25%");
		CheckText(Change(), "pasek post\xC4\x99pu, 25%", "h0 bar appears");
		if (Frame("me1-x2"))
			CheckText(Change(), "45%", "bar moved to 45%");
		if (Frame("me1-h0"))
			CheckText(Change(), "25%", "bar moved again");
		Frame("me1-h0");
		CheckText(Change(), "", "nothing changed: nothing to say");
	}
}

// ---------------------------------------------------------------------------
// synthetic Windows 95 dialog in true-colour default colours.  The text is
// drawn with the user's own font, read here the way GDI draws it.

static const uint8_t* gFnt;         // MS Sans Serif 8
static uint32_t* gCanvas;
enum { kW = 640, kH = 480 };
enum : uint32_t { kFace = 0xC0C0C0, kShadow = 0x808080, kWhite = 0xFFFFFF, kBlack = 0,
	kNavy = 0x000080, kDesk = 0x008080 };

static unsigned R16(const uint8_t* p) { return p[0] | (p[1] << 8); }

static const uint8_t* FindFnt(const uint8_t* fon, size_t size, unsigned points)
{
	unsigned ne = fon[0x3C] | (fon[0x3D] << 8);
	size_t table = ne + R16(fon + ne + 0x24);
	unsigned shift = R16(fon + table);
	size_t at = table + 2;
	while (at + 8 <= size) {
		unsigned type = R16(fon + at), count = R16(fon + at + 2);
		if (type == 0)
			break;
		at += 8;
		for (unsigned i = 0; i < count; i++, at += 12) {
			const uint8_t* f = fon + ((size_t)R16(fon + at) << shift);
			if (type == 0x8008 && R16(f + 68) == points)
				return f;
		}
	}
	return nullptr;
}

static void Fill(int x0, int y0, int x1, int y1, uint32_t colour)
{
	for (int y = y0; y <= y1; y++) {
		for (int x = x0; x <= x1; x++)
			gCanvas[y * kW + x] = colour;
	}
}

// returns the advance; code points are font codes (Windows-1252)
static int DrawText(int x, int y, const char* text, uint32_t colour, bool bold = false,
	int underline = -1)
{
	const uint8_t* f = gFnt;
	unsigned height = R16(f + 88), first = f[95], ascent = R16(f + 74);
	int start = x;
	for (int n = 0; text[n]; n++) {
		unsigned code = (uint8_t)text[n];
		unsigned width = R16(f + 118 + (code - first) * 4);
		unsigned offset = R16(f + 118 + (code - first) * 4 + 2);
		for (unsigned gx = 0; gx < width; gx++) {
			for (unsigned gy = 0; gy < height; gy++) {
				if (f[offset + (gx / 8) * height + gy] & (0x80 >> (gx % 8))) {
					gCanvas[(y + gy) * kW + x + gx] = colour;
					if (bold)
						gCanvas[(y + gy) * kW + x + gx + 1] = colour;
				}
			}
		}
		int advance = (int)width + (bold ? 1 : 0);
		if (n == underline)
			Fill(x, y + ascent + 1, x + advance - 1, y + ascent + 1, colour);
		x += advance;
	}
	return x - start;
}

static void Edge(int x0, int y0, int x1, int y1, uint32_t topLeft, uint32_t bottomRight)
{
	Fill(x0, y0, x1, y0, topLeft);
	Fill(x0, y0, x0, y1, topLeft);
	Fill(x0, y1, x1, y1, bottomRight);
	Fill(x1, y0, x1, y1, bottomRight);
}

static void Sunken(int x0, int y0, int x1, int y1, uint32_t inside)
{
	Edge(x0, y0, x1, y1, kShadow, kWhite);
	Edge(x0 + 1, y0 + 1, x1 - 1, y1 - 1, kBlack, kFace);
	Fill(x0 + 2, y0 + 2, x1 - 2, y1 - 2, inside);
}

static void Raised(int x0, int y0, int x1, int y1)
{
	Fill(x0, y0, x1, y1, kFace);
	Edge(x0, y0, x1, y1, kWhite, kBlack);
	Edge(x0 + 1, y0 + 1, x1 - 1, y1 - 1, kFace, kShadow);
}

static void FocusRect(int x0, int y0, int x1, int y1)
{
	for (int x = x0; x <= x1; x++) {
		if ((x + y0) & 1) gCanvas[y0 * kW + x] ^= 0xFFFFFF;
		if ((x + y1) & 1) gCanvas[y1 * kW + x] ^= 0xFFFFFF;
	}
	for (int y = y0 + 1; y < y1; y++) {
		if ((x0 + y) & 1) gCanvas[y * kW + x0] ^= 0xFFFFFF;
		if ((x1 + y) & 1) gCanvas[y * kW + x1] ^= 0xFFFFFF;
	}
}

static void Button(int x, int y, const char* label, bool isDefault, bool disabled,
	bool focused, int underline)
{
	int x1 = x + 74, y1 = y + 22;
	if (isDefault) {
		Edge(x, y, x1, y1, kBlack, kBlack);
		Raised(x + 1, y + 1, x1 - 1, y1 - 1);
	} else {
		Raised(x, y, x1, y1);
	}
	int width = 0;
	for (int n = 0; label[n]; n++)
		width += (int)R16(gFnt + 118 + ((uint8_t)label[n] - gFnt[95]) * 4);
	int tx = x + (75 - width) / 2, ty = y + 5;
	if (disabled) {
		DrawText(tx + 1, ty + 1, label, kWhite, false, underline);
		DrawText(tx, ty, label, kShadow, false, underline);
	} else {
		DrawText(tx, ty, label, kBlack, false, underline);
	}
	if (focused)
		FocusRect(x + 4, y + 4, x1 - 4, y1 - 4);
}

struct Options {
	bool checked;           // "Create a backup copy"
	bool focusCheck;        // dotted rectangle on the check box, else on OK
	int  selected;          // row of the list
	int  blocks;            // of 16
	const char* name;       // content of the edit field
	bool caret;
	bool message;           // a message box instead of the dialog
	bool focusList;         // dotted rectangle on the selected row of the list
};

static void Paint(const Options& o)
{
	Fill(0, 0, kW - 1, kH - 1, kDesk);
	if (o.message) {
		int x0 = 180, y0 = 170, x1 = 460, y1 = 290;
		Raised(x0, y0, x1, y1);
		Fill(x0 + 4, y0 + 4, x1 - 4, y0 + 21, kNavy);
		DrawText(x0 + 7, y0 + 6, "Confirm File Delete", kWhite, true);
		DrawText(x0 + 60, y0 + 40, "Are you sure you want to delete 'Notes.txt'?", kBlack);
		Button(x0 + 60, y0 + 80, "Yes", true, false, true, 0);
		Button(x0 + 146, y0 + 80, "No", false, false, false, 0);
		return;
	}
	int x0 = 100, y0 = 60, x1 = 540, y1 = 400;
	Raised(x0, y0, x1, y1);
	Fill(x0 + 4, y0 + 4, x1 - 4, y0 + 21, kNavy);
	DrawText(x0 + 7, y0 + 6, "Backup Options", kWhite, true);
	DrawText(x0 + 14, y0 + 34, "Choose how the files are saved.", kBlack);

	// group box with a check box and two radio-less statics
	int gx0 = x0 + 14, gy0 = y0 + 60, gx1 = x0 + 250, gy1 = y0 + 130;
	Edge(gx0, gy0, gx1, gy1, kShadow, kWhite);
	Edge(gx0 + 1, gy0 + 1, gx1 + 1, gy1 + 1, kWhite, kWhite);
	Edge(gx0, gy0, gx1, gy1, kShadow, kShadow);
	// (etched: dark line, light line one pixel right and below)
	Fill(gx0 + 8, gy0, gx0 + 8 + 44, gy0 + 1, kFace);
	DrawText(gx0 + 10, gy0 - 6, "Settings", kBlack);
	int cx = gx0 + 12, cy = gy0 + 22;
	Sunken(cx, cy, cx + 12, cy + 12, kWhite);
	if (o.checked) {
		for (int i = 0; i < 7; i++) {
			int px = cx + 3 + i, py = cy + (i < 3 ? 5 + i : 9 - i);
			Fill(px, py, px, py + 2, kBlack);
		}
	}
	int cw = DrawText(cx + 18, cy, "Create a backup copy", kBlack, false, 9);
	if (o.focusCheck)
		FocusRect(cx + 17, cy - 1, cx + 18 + cw, cy + 13);
	DrawText(gx0 + 12, gy0 + 46, "Old copies are kept for 30 days.", kShadow);

	// labelled edit field
	DrawText(x0 + 14, y0 + 150, "File name:", kBlack, false, 5);
	Sunken(x0 + 80, y0 + 146, x0 + 250, y0 + 166, kWhite);
	int tw = DrawText(x0 + 84, y0 + 150, o.name, kBlack);
	if (o.caret)
		Fill(x0 + 84 + tw, y0 + 149, x0 + 84 + tw, y0 + 162, kBlack);

	// combo box
	DrawText(x0 + 14, y0 + 180, "Drive:", kBlack, false, 0);
	Sunken(x0 + 80, y0 + 176, x0 + 250, y0 + 196, kWhite);
	DrawText(x0 + 84, y0 + 180, "C: [SYSTEM]", kBlack);
	Raised(x0 + 232, y0 + 178, x0 + 248, y0 + 194);
	for (int i = 0; i < 4; i++)
		Fill(x0 + 237 + i, y0 + 185 + i, x0 + 243 - i, y0 + 185 + i, kBlack);

	// list box
	DrawText(x0 + 270, y0 + 60, "Folders:", kBlack, false, 2);
	Sunken(x0 + 270, y0 + 76, x0 + 420, y0 + 146, kWhite);
	static const char* const rows[] = { "Documents", "Pictures", "Program Files", "Windows" };
	for (int i = 0; i < 4; i++) {
		int ry = y0 + 78 + i * 13;
		if (i == o.selected) {
			Fill(x0 + 272, ry, x0 + 418, ry + 12, kNavy);
			DrawText(x0 + 274, ry, rows[i], kWhite);
			if (o.focusList)
				FocusRect(x0 + 272, ry, x0 + 418, ry + 12);
		} else {
			DrawText(x0 + 274, ry, rows[i], kBlack);
		}
	}

	// common-control progress bar
	DrawText(x0 + 14, y0 + 220, "Copying...", kBlack);
	Edge(x0 + 14, y0 + 236, x0 + 14 + 16 * 10 + 3, y0 + 252, kShadow, kWhite);
	for (int i = 0; i < o.blocks; i++)
		Fill(x0 + 16 + i * 10, y0 + 238, x0 + 16 + i * 10 + 7, y0 + 250, kNavy);

	Button(x0 + 190, y0 + 300, "OK", true, false, !o.focusCheck && !o.focusList, -1);
	Button(x0 + 271, y0 + 300, "Cancel", false, false, false, -1);
	Button(x0 + 352, y0 + 300, "Apply", false, true, false, 0);
}

static void Synthetic(const Options& o, const char* name)
{
	Paint(o);
	Run(gCanvas, kW, kH, name);
}

static void TestSynthetic()
{
	const unsigned B = kRoleButton, T = kRoleText;
	gCanvas = (uint32_t*)malloc(kW * kH * 4);
	Options o = { true, false, 2, 4, "REPORT.DOC", false, false, false };
	Fresh();
	Synthetic(o, "synthetic-1");
	CheckText(gNow.title, "Backup Options", "synthetic title");
	Check(gNow.kind == kWindowNormal, "synthetic kind");
	Has(T, "Choose how the files are saved.", 0, kFlagDisabled | kFlagOutside);
	Has(kRoleGroup, "Settings", 0);
	Has(kRoleCheckBox, "Create a backup copy", kFlagChecked, kFlagFocused | kFlagDisabled);
	const Element* check = Find(gNow, kRoleCheckBox, "Create a backup copy");
	Check(check && check->accelerator == 'b', "synthetic accelerator of the check box");
	Has(T, "Old copies are kept for 30 days.", kFlagDisabled);
	const Element* file = Find(gNow, kRoleEdit, "File name:");
	Check(file && strcmp(Text(gNow, *file), "REPORT.DOC") == 0 && file->accelerator == 'n',
		"synthetic edit field");
	const Element* drive = Find(gNow, kRoleCombo, "Drive:");
	Check(drive && strcmp(Text(gNow, *drive), "C: [SYSTEM]") == 0, "synthetic combo box");
	const Element* list = Find(gNow, kRoleList, "Folders:");
	Check(list && strcmp(Text(gNow, *list), "Program Files") == 0 && list->position == 3
		&& list->count == 4, "synthetic list");
	const Element* bar = Find(gNow, kRoleProgress, "Copying...");
	Check(bar && bar->percent == 25, "synthetic block progress bar 25%");
	Has(B, "OK", kFlagDefault | kFlagFocused);
	Has(B, "Cancel", 0, kFlagDefault | kFlagDisabled);
	Has(B, "Apply", kFlagDisabled);
	CheckText(All(), "Backup Options, okno. Choose how the files are saved. Settings, grupa. "
		"Create a backup copy, pole wyboru, zaznaczony. Old copies are kept for 30 days. "
		"Folders, lista, Program Files, 3 z 4. File name, pole edycji, REPORT.DOC. "
		"Drive, lista rozwijana, C: [SYSTEM]. Copying..., pasek post\xC4\x99pu, 25%. "
		"OK, przycisk. Cancel, przycisk. Apply, przycisk, niedost\xC4\x99pny.",
		"synthetic whole window");

	// Tab: the focus moves to the check box
	o.focusCheck = true;
	Synthetic(o, "synthetic-2");
	CheckText(Change(), "Create a backup copy, pole wyboru, zaznaczony", "focus to check box");
	// Space: unchecked
	o.checked = false;
	Synthetic(o, "synthetic-3");
	CheckText(Change(), "niezaznaczony", "check box cleared");
	CheckText(Change(0), "not checked", "check box cleared, English");
	// the bar grows
	o.blocks = 8;
	Synthetic(o, "synthetic-4");
	CheckText(Change(), "50%", "progress only");
	// typing in the edit field: nothing is said
	o.name = "REPORT2.DOC";
	Synthetic(o, "synthetic-5");
	CheckText(Change(), "", "typing is silent");
	// another row of the list, which does not have the focus: silent as well
	o.selected = 0;
	Synthetic(o, "synthetic-6");
	CheckText(Change(), "", "list without focus is silent");

	// the list gets the focus (dotted rectangle on its selected row) ...
	o.focusCheck = false;
	o.focusList = true;
	Synthetic(o, "synthetic-7");
	CheckText(Change(), "Folders, lista, Documents, 1 z 4", "focus to the list");
	Has(kRoleCheckBox, "Create a backup copy", 0, kFlagChecked | kFlagFocused);
	// ... and Down moves the selection
	o.selected = 1;
	Synthetic(o, "synthetic-8");
	CheckText(Change(), "Pictures, 2 z 4", "next row of the list");
	CheckText(Change(0), "Pictures, 2 of 4", "next row of the list, English");
	o.focusList = false;

	// caret: focus in the edit field; it stays there while the caret is dark
	o.focusCheck = false;
	o.caret = true;
	Fresh();
	Paint(o);
	// no dotted rectangle on OK this time
	Button(100 + 190, 60 + 300, "OK", true, false, false, -1);
	Run(gCanvas, kW, kH, "synthetic-caret-on");
	Focus(kRoleEdit, "File name:");
	o.caret = false;
	Paint(o);
	Button(100 + 190, 60 + 300, "OK", true, false, false, -1);
	Run(gCanvas, kW, kH, "synthetic-caret-off");
	Focus(kRoleEdit, "File name:");
	CheckText(Change(), "", "caret blink is silent");

	// message box
	Fresh();
	Options m = { false, false, 0, 0, "", false, true, false };
	Synthetic(m, "synthetic-message");
	Check(gNow.kind == kWindowMessage, "message box kind");
	CheckText(Change(), "Confirm File Delete, okno dialogowe. "
		"Are you sure you want to delete 'Notes.txt'? Yes, przycisk", "message box");
	CheckText(Change(0), "Confirm File Delete, dialog. "
		"Are you sure you want to delete 'Notes.txt'? Yes, button", "message box, English");
	const Element* yes = Find(gNow, B, "Yes");
	Check(yes && yes->accelerator == 'Y', "message box accelerator");
	Check(ReviewStepControl(gNow, -1, 1) >= 0
		&& gNow.items[ReviewStepControl(gNow, -1, 1)].role == B, "review: first control");
	Check(ReviewStep(gNow, (int)gNow.count - 1, 1) == -1, "review: end");
}

// A font declared as EASTEUROPE_CHARSET maps its codes through Windows-1250.
static void TestCodePage(const uint8_t* fon, size_t size)
{
	uint8_t* copy = (uint8_t*)malloc(size);
	memcpy(copy, fon, size);
	const uint8_t* fnt = FindFnt(copy, size, 8);
	((uint8_t*)fnt)[85] = 238;
	Context* context = Init(malloc(ContextBytes()), ContextBytes());
	Check(AddFon(context, copy, size), "patched font accepted");
	Fill(0, 0, kW - 1, kH - 1, kFace);
	// codes B3 F3 BF EA in Windows-1250: l-stroke, o-acute, z-dot, e-ogonek
	DrawText(40, 40, "Has\xB3o: \xBF\xF3\xB3w \xEA", kBlack);
	Screen* screen = (Screen*)malloc(sizeof(Screen));
	Analyse(context, gCanvas, kW, kH, kW, screen);
	Check(screen->count == 1, "code page: one text");
	if (screen->count == 1)
		CheckText(Text(*screen, screen->items[0]),
			"Has\xC5\x82o: \xC5\xBC\xC3\xB3\xC5\x82w \xC4\x99", "Windows-1250 to UTF-8");
	free(screen);
	free(context);
	free(copy);
}

// ---------------------------------------------------------------------------
// Windows Me after Setup: message boxes, menus, icons, tabs

static bool HasRole(const Screen& s, unsigned role)
{
	for (unsigned i = 0; i < s.count; i++) {
		if (s.items[i].role == role)
			return true;
	}
	return false;
}

static void TestDesktop()
{
	const unsigned B = kRoleButton, M = kRoleMenuItem;

	// the last page of Setup: the black outline of the focused default button
	// with the empty face right of it is not a progress bar
	Fresh();
	if (Frame("v-finish")) {
		CheckText(gNow.title, "Windows Millennium Edition Setup Wizard", "finish title");
		Has(B, "Finish", kFlagDefault | kFlagFocused, kFlagDisabled | kFlagOutside);
		Has(B, "< Back", kFlagDisabled, kFlagFocused);
		Focus(B, "Finish");
		Check(!HasRole(gNow, kRoleProgress), "finish: no progress bar");
		Check(Count(gNow, B) == 2, "finish: two buttons");
		CheckText(Change(), "Windows Millennium Edition Setup Wizard, okno. Windows Millennium "
			"Edition Setup saved all the information. Click Finish to complete Setup and "
			"start Windows Millennium Edition. Finish, przycisk. Setting up hardware and "
			"finalizing settings. Estimated time remaining: 13 minutes", "finish page");
	}

	// a message box in front of the desktop dimmed with a checkerboard
	Fresh();
	if (Frame("t-logoff")) {
		CheckText(gNow.title, "Log Off Windows", "logoff title");
		Check(gNow.kind == kWindowMessage, "logoff is a message box");
		Has(B, "Yes", kFlagDefault | kFlagFocused);
		Has(B, "No", 0, kFlagDefault | kFlagFocused);
		Focus(B, "Yes");
		Check(gNow.count == 3, "logoff: the dimmed desktop gives no elements");
		CheckText(Change(), "Log Off Windows, okno dialogowe. Are you sure you want to log off? "
			"Yes, przycisk", "logoff new dialog");
		if (Frame("u-logoff-no")) {
			Focus(B, "No");
			Has(B, "Yes", 0, kFlagFocused);
			Check(gNow.count == 3, "logoff 2: the dimmed grey desktop gives no elements");
			CheckText(Change(), "No, przycisk", "logoff: Tab");
		}
	}
	Fresh();
	if (Frame("u-runerr")) {
		CheckText(gNow.title, "qqzz", "error box title");
		Check(gNow.kind == kWindowMessage, "error box is a message box");
		Focus(B, "OK");
		CheckText(Change(), "qqzz, okno dialogowe. Windows cannot find 'qqzz'. You may have "
			"typed the name incorrectly in the Run dialog, or another open program cannot find "
			"a system file. To search for a file, click the Start button, and then click "
			"Search. OK, przycisk", "error box");
	}

	// the caret blinks: once seen, the field keeps the focus
	Fresh();
	if (Frame("t-pass")) {
		Focus(B, "OK");         // no caret was ever seen: the default button
		uint32_t* copy = (uint32_t*)malloc(640 * 480 * 4);
		memcpy(copy, gFrame, 640 * 480 * 4);
		for (int y = 216; y <= 229; y++)
			copy[y * 640 + 253] = 0;
		Run(copy, 640, 480, "t-pass+caret");
		Focus(kRoleEdit, "Password:");
		CheckText(Change(), "Password, pole edycji, puste", "password: the caret appears");
		Run(gFrame, 640, 480, "t-pass-dark");
		Focus(kRoleEdit, "Password:");
		CheckText(Change(), "", "password: the dark half of the blink");
		free(copy);
	}
	Fresh();
	if (Frame("u-run1")) {
		Focus(kRoleCombo, "Open:");
		const Element* open = Find(gNow, kRoleCombo, "Open:");
		Check(open && (open->flags & kFlagCaret), "run: the caret is seen");
		Has(B, "OK", kFlagDefault | kFlagDisabled, kFlagFocused);
		CheckText(Change(), "Run, okno. Type the name of a program, folder, document, or "
			"Internet resource, and Windows will open it for you. Open, lista rozwijana, puste",
			"run dialog");
		if (Frame("u-run2")) {
			open = Find(gNow, kRoleCombo, "Open:");
			Check(open && !(open->flags & kFlagCaret), "run 2: the caret is dark");
			Focus(kRoleCombo, "Open:");
			CheckText(Change(), "", "run 2: nothing new");
		}
	}
	Fresh();
	if (Frame("u-run2"))
		Focus(kRoleCombo, "Open:");     // a disabled default button never has the focus

	// Start menu and its submenus
	Fresh();
	if (Frame("u-desk") && Frame("u-start0")) {
		Check(gNow.focus == -1, "start menu: nothing under the bar yet");
		CheckText(Change(), "menu", "start menu opens");
		const Element* menu = Nth(gNow, kRoleMenu, 0);
		if (menu == nullptr) {
			for (unsigned i = 0; i < gNow.count && menu == nullptr; i++) {
				if (gNow.items[i].role == kRoleMenu)
					menu = &gNow.items[i];
			}
		}
		Check(menu && menu->count == 9, "start menu: nine items");
		Has(M, "Windows Update", 0, kFlagSubmenu | kFlagSelected);
		Has(M, "Programs", kFlagSubmenu, kFlagSelected);
		Has(M, "Run...", 0, kFlagSubmenu);
		const Element* run = Find(gNow, M, "Run...");
		Check(run && run->position == 7 && run->count == 9 && run->accelerator == 'R',
			"start menu: Run is 7 of 9");
		if (Frame("u-start1")) {
			Focus(M, "Shut Down...");
			CheckText(Change(), "Shut Down..., pozycja menu, 9 z 9", "start menu: Up");
		}
		if (Frame("u-start-programs")) {
			CheckText(Change(), "Programs, podmenu, 2 z 9", "start menu: Programs");
			CheckText(Change(0), "Programs, submenu, 2 of 9", "start menu: Programs, English");
		}
		if (Frame("u-sub-programs")) {
			Has(M, "Programs", kFlagSubmenu | kFlagSelected, kFlagFocused);
			Has(M, "Documents", kFlagSubmenu);      // its arrow is beside the submenu
			Focus(M, "Accessories");
			CheckText(Change(), "Accessories, podmenu, 1 z 7", "submenu opens");
		}
		if (Frame("u-sub-programs2"))
			CheckText(Change(), "Games, podmenu, 2 z 7", "submenu: Down");
		if (Frame("u-sub-programs3")) {
			CheckText(Change(), "Classic Hearts, pozycja menu, 1 z 11", "third menu opens");
			Has(M, "Games", kFlagSubmenu | kFlagSelected, kFlagFocused);
			Has(M, "Classic S", kFlagDamaged);      // a tool tip lies on it
			Has(M, "Spider Solitaire", 0, kFlagDamaged | kFlagSubmenu);
		}
	}
	Fresh();
	if (Frame("u-start1")) {
		CheckText(Change(), "menu. Shut Down..., pozycja menu, 9 z 9", "menu seen with its bar");
		CheckText(Change(0), "menu. Shut Down..., menu item, 9 of 9", "the same, English");
	}
	Fresh();
	if (Frame("u-sub-settings")) {
		Focus(M, "Control Panel");
		Has(M, "Settings", kFlagSubmenu | kFlagSelected, kFlagFocused);
		Has(M, "Printers", 0, kFlagSubmenu);
	}
	// context menu
	Fresh();
	if (Frame("u-ctx1")) {
		Check(gNow.focus == -1, "context menu: no bar yet");
		CheckText(Change(), "menu", "context menu opens");
		if (Frame("u-ctx2")) {
			Check(gNow.title[0] == 0, "the bold default item is not a title");
			CheckText(Change(), "Open, pozycja menu, 1 z 8", "context menu: Down");
		}
	}
	// drop-down menus of a window
	Fresh();
	if (Frame("u-mc2") && Frame("u-mc-view2")) {
		CheckText(Change(), "menu. Explorer Bar, podmenu, 3 z 13", "View menu");
		Has(M, "Status Bar", kFlagChecked, kFlagSubmenu);
		Has(M, "Large Icons", kFlagChecked);
		Has(M, "Small Icons", 0, kFlagChecked);
		Has(M, "Toolbars", kFlagSubmenu, kFlagChecked);
		if (Frame("u-mc-edit")) {
			CheckText(Change(), "Undo Ctrl+Z, pozycja menu, niedost\xC4\x99pny, 1 z 9",
				"Edit menu");
			Has(M, "Cut Ctrl+X", kFlagDisabled);
			Has(M, "Select All Ctrl+A", 0, kFlagDisabled);
		}
		if (Frame("u-mc-file")) {
			CheckText(Change(), "New, podmenu, 1 z 6", "File menu");
			Has(M, "Delete", kFlagDisabled);
			Has(M, "Close", 0, kFlagDisabled);
		}
		if (Frame("u-mc2")) {
			Check(!HasRole(gNow, kRoleMenu), "the menu is closed");
			Check(strstr(Change(), "okno") == nullptr, "closing a menu is not a new window");
		}
	}

	// desktop icons: the dotted rectangle is the focus
	Fresh();
	if (Frame("u-desk"))
		Check(gNow.focus == -1 && !HasRole(gNow, kRoleIcon), "desktop without a focused icon");
	if (Frame("u-ic1")) {
		Focus(kRoleIcon, "My Computer");
		CheckText(Change(), "My Computer, 2 z 9", "icon gets the focus");
		if (Frame("u-ic2"))
			CheckText(Change(), "My Network Places, 4 z 9", "icon: Down");
		if (Frame("u-ic3")) {
			Focus(kRoleIcon, "Outlook Express");
			CheckText(Change(), "Outlook Express, 5 z 9", "icon: Ctrl+Right");
		}
	}

	// property sheet
	Fresh();
	if (Frame("u-sys2")) {
		CheckText(gNow.title, "Taskbar and Start Menu Properties", "sheet title");
		Has(kRoleTab, "General", kFlagSelected);
		Has(kRoleTab, "Advanced", 0, kFlagSelected);
		Check(!HasRole(gNow, kRoleMenu), "the picture of a menu is not a menu");
		Focus(kRoleCheckBox, "Always on top");
		Has(kRoleCheckBox, "Auto hide", 0, kFlagChecked);
		if (Frame("u-sys4")) {
			Focus(kRoleTab, "General");
			CheckText(Change(), "General, karta, 1 z 2", "focus on the tab");
			CheckText(Change(0), "General, tab, 1 of 2", "focus on the tab, English");
		}
	}
}

int main(int argc, char** argv)
{
	if (argc < 3) {
		printf("usage: CharonWinReadTests FONT_DIR FRAME_DIR [--dump] [--time]\n");
		printf("SKIP: fonts and frames are not part of the source tree\n");
		return 0;
	}
	gFrameDir = argv[2];
	for (int i = 3; i < argc; i++) {
		if (strcmp(argv[i], "--dump") == 0) gDump = true;
		if (strcmp(argv[i], "--time") == 0) gTime = true;
	}
	gContext = Init(malloc(ContextBytes()), ContextBytes());
	Check(gContext != nullptr, "context");
	printf("context %.1f MB, screen %u bytes\n", ContextBytes() / 1048576.0,
		(unsigned)sizeof(Screen));
	Check(Init(gContext, 100) == nullptr, "too small a block is refused");
	ClearScreen(&gEmpty);
	ClearScreen(&gNow);

	// rubbish is refused, not read
	uint8_t junk[256];
	for (unsigned i = 0; i < sizeof(junk); i++)
		junk[i] = (uint8_t)(i * 7);
	Check(!AddFon(gContext, junk, sizeof(junk)), "junk refused");
	junk[0] = 'M', junk[1] = 'Z';
	Check(!AddFon(gContext, junk, sizeof(junk)), "junk MZ refused");
	Check(FontCount(gContext) == 0, "no fonts yet");

	// the order in which Charon's loader hands them over (alphabetical)
	static const char* const fonts[] = { "SERIFE.FON", "SSERIFE.FON", "VGAFIX.FON",
		"VGAOEM.FON", "VGASYS.FON" };
	uint8_t* sans = nullptr;
	size_t sansSize = 0;
	for (unsigned i = 0; i < 5; i++) {
		size_t size = 0;
		uint8_t* bytes = Load(argv[1], fonts[i], &size);
		if (bytes == nullptr)
			continue;
		bool ok = AddFon(gContext, bytes, size);
		printf("font %s: %s, %u variants so far\n", fonts[i], ok ? "added" : "not usable",
			FontCount(gContext));
		if (i == 1) {
			sans = bytes;
			sansSize = size;
		} else {
			free(bytes);
		}
	}
	if (sans == nullptr) {
		printf("SKIP: %s/SSERIFE.FON not found\n", argv[1]);
		return 0;
	}
	gFnt = FindFnt(sans, sansSize, 8);
	Check(gFnt != nullptr, "MS Sans Serif 8 present");
	if (gFnt == nullptr)
		return 1;

	// an empty and a noisy frame must not hurt
	gCanvas = (uint32_t*)malloc(kW * kH * 4);
	memset(gCanvas, 0, kW * kH * 4);
	Analyse(gContext, gCanvas, kW, kH, kW, &gNow);
	Check(gNow.count == 0 && gNow.focus == -1, "black frame is empty");
	uint32_t seed = 12345;
	for (int i = 0; i < kW * kH; i++) {
		seed = seed * 1664525u + 1013904223u;
		gCanvas[i] = seed >> 8;
	}
	{
		auto start = std::chrono::steady_clock::now();
		Analyse(gContext, gCanvas, kW, kH, kW, &gNow);
		if (gTime)
			printf("time %-12s %6.2f ms\n","noise", std::chrono::duration<double,
				std::milli>(std::chrono::steady_clock::now() - start).count());
	}
	Check(gNow.title[0] == 0, "noise has no title");
	// a checkerboard (dithered desktop) from edge to edge
	for (int i = 0; i < kW * kH; i++)
		gCanvas[i] = ((i % kW) + (i / kW)) & 1 ? 0x008080 : 0x000000;
	{
		auto start = std::chrono::steady_clock::now();
		Analyse(gContext, gCanvas, kW, kH, kW, &gNow);
		if (gTime)
			printf("time %-12s %6.2f ms\n","dither", std::chrono::duration<double,
				std::milli>(std::chrono::steady_clock::now() - start).count());
	}
	Check(gNow.count == 0, "a dithered desktop is empty");
	free(gCanvas);

	TestSynthetic();
	TestCodePage(sans, sansSize);
	TestShots();
	TestDesktop();

	if (gFrames > 0)
		printf("analysed %d frames: average %.2f ms, slowest %.2f ms\n", gFrames,
			gTotal / gFrames, gWorst);
	printf("%d checks, %d failures\n", gChecks, gFailures);
	if (gFailures == 0)
		printf("PASS\n");
	return gFailures == 0 ? 0 : 1;
}
