// SPDX-License-Identifier: GPL-2.0-or-later
// CharonDOS - the CHARON command (shared code). Queries and changes Charon's
// own pre-boot screen reader and boot manager from the CharonDOS prompt.
//
// Reader settings persist into Charon's CharSettings record, which lives in two
// slots on the EFI system partition (\EFI\HAK\charon-settings-a.bin and -b.bin).
// The record format is re-derived here from src/efi/CharSettings.h (read-only);
// no Charon EFI header is compiled into CharonDOS. The resident reader loads
// these settings when it starts, so a change persisted here is DEFERRED: it is
// heard after the next start of Charon's reader, not mid-session. CHARON SAY and
// every spoken confirmation use Charon's live voice now; the boot-manager
// actions act now. What each command does and when it takes effect is written in
// src/dos/CHARON-COMMANDS.txt.
#include "dos.h"
#include "charon_cmd.h"

namespace {

int s_pl;                                       // message language for this invocation
const char* L(const char* en, const char* pl) { return s_pl ? pl : en; }

// ---- output through handle 1 (honours redirection, read by the reader) -------
void cout(u8 c) { dos_out(1, c); }
void cnl(void) { cout(13); cout(10); }
void cout_uni(int u)
{
    if (u == '\n') { cnl(); return; }
    if (fs_is_con(1)) { con_put_uni((dchar16)u); return; }
    const u8 c = uni_to_cp437((dchar16)u, nullptr);
    cout(c ? c : '?');
}
void cputs(const char* s) { while (*s) cout_uni(utf8_next(&s)); }
void cputln(const char* s) { cputs(s); cnl(); }
void cnum(u32 v) { char b[12]; d_utoa(v, b, 10); for (char* p = b; *p; p++) cout((u8)*p); }
void cbil(const char* en, const char* pl) { cputs(L(en, pl)); }

// Speaks `text` now through Charon's live voice, and prints it (so the resident
// reader says it too). Used for confirmations and for CHARON SAY.
void speak_and_show(const char* text)
{
    cputln(text);                                   // on screen / Braille, read by the resident reader
    charon_plat_speak(s_pl ? "pl" : "en", text);    // the exact phrase on Charon's live voice
}

// ---- small parsing helpers ---------------------------------------------------
char* skipws(char* s) { while (*s == ' ' || *s == '\t') s++; return s; }
void rtrim(char* s) { size_t n = d_strlen(s); while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0; }
// Next blank-separated word, upper-cased copy in `out`; advances *s.
char* nextword(char** s) { char* p = skipws(*s); char* start = p; while (*p && *p != ' ' && *p != '\t') p++; if (*p) *p++ = 0; *s = p; return start; }

// Typed confirmation for a destructive / firmware-touching action: Y (yes) or
// T (Polish "tak"). Anything else, or no input, cancels.
bool confirm(const char* question)
{
    speak_and_show(question);
    cbil("Type Y and Enter to confirm: ", "Wpisz T i naciśnij Enter, aby potwierdzić: ");
    char line[16];
    const int n = con_readline(line, (int)sizeof line - 1, false, true);
    if (n < 0) return false;
    char* p = skipws(line);
    const char c = d_upper(*p);
    return c == 'Y' || c == 'T';
}

// ================= CharSettings record (re-derived from CharSettings.h) ========
struct Settings {
    u64 sequence;
    u8 language, startMelody, backgroundMelody, rate, wrap, sounds, explicitMessages;
    u8 speechVolume, effectsVolume, verbosity, rememberLast, autoSeconds, progress;
    u8 welcome, pitch, readerRate, readerPitch, readerVolume, detail, timers;
    u8 messageRate, messagePitch;
    u8 punctuation, capitals, screenLanguage, readerFlags;
    u8 formatFlags, readerExtra, synthKey, synthVoice;
};

const u8 kAutoDelays[] = {0, 3, 5, 10, 15, 20, 30, 60, 255};
bool valid_auto(u8 v) { for (u8 d : kAutoDelays) if (d == v) return true; return false; }

u32 setcrc(const u8* b, size_t n)
{
    u32 crc = 0xffffffffu;
    for (size_t i = 0; i < n; i++) { crc ^= b[i]; for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xedb88320u & (u32)(0 - (int32_t)(crc & 1))); }
    return ~crc;
}
size_t bytes_for(char version) { return version >= '9' ? 48 : version >= '7' ? 40 : 32; }

// True when the buffer is a settings record whose version char is newer than
// this code understands ('C' = r397 V12). Then the store must not be rewritten.
bool record_too_new(const u8* b, size_t len)
{
    if (len < 8) return false;
    for (int i = 0; i < 7; i++) if (b[i] != (u8)"CHARS00"[i]) return false;
    const char v = (char)b[7];
    return v > 'C';
}

// Mirror of hakefi::DecodeSettings (CharSettings.h). Fills out with version-aware
// defaults; returns false on any inconsistency (bad magic, length, CRC, range).
bool decode(const u8* b, size_t len, Settings& out)
{
    if (len != 48 && len != 40 && len != 32) return false;
    for (int i = 0; i < 7; i++) if (b[i] != (u8)"CHARS00"[i]) return false;
    const char version = (char)b[7];
    if (version < '1' || (version > '9' && version != 'A' && version != 'B' && version != 'C')) return false;
    if (len != bytes_for(version)) return false;
    const size_t crcAt = version >= '9' ? 44 : version >= '7' ? 36 : 28;
    u32 crc = 0; for (int i = 0; i < 4; i++) crc |= (u32)b[crcAt + i] << (i * 8);
    if (crc != setcrc(b, crcAt) || b[16] > 2 || b[17] > 1) return false;
    if (b[18] > (version == '1' ? 0 : 1)) return false;
    if (b[19] > (version >= '3' ? 1 : 0)) return false;
    if (b[20] > (version >= '4' ? 4 : 0) || b[21] > (version >= '4' ? 4 : 0)) return false;
    if (version >= '5' ? (b[22] < 5 || b[22] > 20) : b[22] != 0) return false;
    if (version >= '6') {
        if (b[23] < 1 || b[23] > 10 || b[24] > 10 || (b[25] & 3) > 2 || b[25] > 15 || b[26] > (version >= '7' ? 15 : 1) || !valid_auto(b[27])) return false;
    } else for (int i = 23; i < 28; i++) if (b[i]) return false;
    if (version >= '7') {
        for (int i = 28; i < 31; i++) if (b[i] < 5 || b[i] > 20) return false;
        if (b[31] < 1 || b[31] > 10 || b[32] > 7 || b[33] > 3) return false;
        if (version == '7' ? (b[34] || b[35]) : (b[34] < 5 || b[34] > 20 || b[35] < 5 || b[35] > 20)) return false;
    }
    if (version >= '9') {
        if (b[36] > 3 || b[37] > 7 || b[38] > 3 || b[39] > 63) return false;
        if (version >= 'A') {
            if ((version < 'C' && (b[40] > 63 || b[41] > 31)) || b[41] > 127 || ((b[41] >> 5) & 3) > 2 || (b[41] & 3) > 2 || (version == 'A' && (b[42] || b[43])) || b[43] > 15) return false;
        } else for (int i = 40; i < 44; i++) if (b[i]) return false;
    }
    Settings c;
    c.sequence = 0; for (int i = 0; i < 8; i++) c.sequence |= (u64)b[8 + i] << (i * 8);
    if (!c.sequence) return false;
    // defaults (the header's decode-to-default behaviour for older versions)
    c.startMelody = 1; c.backgroundMelody = 0; c.rate = 10; c.explicitMessages = 0;
    c.speechVolume = 10; c.effectsVolume = 10; c.verbosity = 1; c.rememberLast = 0; c.autoSeconds = 10; c.progress = 1;
    c.welcome = 7; c.pitch = 10; c.readerRate = 10; c.readerPitch = 10; c.readerVolume = 10; c.detail = 7; c.timers = 2;
    c.messageRate = 14; c.messagePitch = 10; c.punctuation = 1; c.capitals = 1; c.screenLanguage = 0; c.readerFlags = 47;
    // formatFlags/readerExtra defaults used only when upgrading a record older
    // than V10; kept inside the ranges DecodeSettings accepts (readerExtra <= 127).
    c.formatFlags = 60; c.readerExtra = 25; c.synthKey = 0; c.synthVoice = 0;
    c.language = b[16]; c.wrap = b[17] != 0;
    c.sounds = (version == '1' || b[18] != 0) ? 1 : 0;
    c.explicitMessages = (version >= '3' && b[19] != 0) ? 1 : 0;
    if (version >= '4') { c.startMelody = b[20]; c.backgroundMelody = b[21]; }
    if (version >= '5') c.rate = b[22];
    if (version >= '6') {
        c.speechVolume = b[23]; c.effectsVolume = b[24]; c.verbosity = b[25] & 3; c.progress = (u8)(b[25] >> 2);
        c.rememberLast = (b[26] & 1) != 0; c.autoSeconds = b[27];
        if (version >= '7') c.welcome = (u8)(b[26] >> 1);
    }
    c.readerRate = c.rate; c.readerVolume = c.speechVolume;
    if (version >= '7') {
        c.pitch = b[28]; c.readerRate = b[29]; c.readerPitch = b[30]; c.readerVolume = b[31]; c.detail = b[32]; c.timers = b[33];
        if (version == '7' && !c.timers) c.timers = 2;
        if (version >= '8') { c.messageRate = b[34]; c.messagePitch = b[35]; }
    }
    if (version >= '9') { c.punctuation = b[36]; c.capitals = b[37]; c.screenLanguage = b[38]; c.readerFlags = b[39]; }
    if (version >= 'A') { c.formatFlags = b[40]; c.readerExtra = b[41]; }
    if (version >= 'B') { c.synthKey = b[42]; c.synthVoice = b[43]; }
    out = c;
    return true;
}

// Mirror of hakefi::EncodeSettings: always writes a 48-byte CHARS00C record.
bool encode(const Settings& v, u8* b)
{
    if (!v.sequence || v.language > 2 || v.startMelody > 4 || v.backgroundMelody > 4 || v.rate < 5 || v.rate > 20
        || v.speechVolume < 1 || v.speechVolume > 10 || v.effectsVolume > 10 || v.verbosity > 2 || v.progress > 3
        || !valid_auto(v.autoSeconds) || v.welcome > 7 || v.pitch < 5 || v.pitch > 20 || v.readerRate < 5 || v.readerRate > 20
        || v.readerPitch < 5 || v.readerPitch > 20 || v.readerVolume < 1 || v.readerVolume > 10 || v.detail > 7 || v.timers > 3
        || v.messageRate < 5 || v.messageRate > 20 || v.messagePitch < 5 || v.messagePitch > 20
        || v.punctuation > 3 || v.capitals > 7 || v.screenLanguage > 3 || v.readerFlags > 63
        || (v.readerExtra & 3) > 2 || ((v.readerExtra >> 5) & 3) > 2 || v.synthVoice > 15) return false;
    for (int i = 0; i < 48; i++) b[i] = 0;
    for (int i = 0; i < 8; i++) b[i] = (u8)"CHARS00C"[i];
    for (int i = 0; i < 8; i++) b[8 + i] = (u8)(v.sequence >> (i * 8));
    b[16] = v.language; b[17] = v.wrap ? 1 : 0; b[18] = v.sounds ? 1 : 0; b[19] = v.explicitMessages ? 1 : 0;
    b[20] = v.startMelody; b[21] = v.backgroundMelody; b[22] = v.rate;
    b[23] = v.speechVolume; b[24] = v.effectsVolume; b[25] = (u8)(v.verbosity | (v.progress << 2)); b[26] = (u8)((v.rememberLast ? 1 : 0) | (v.welcome << 1)); b[27] = v.autoSeconds;
    b[28] = v.pitch; b[29] = v.readerRate; b[30] = v.readerPitch; b[31] = v.readerVolume; b[32] = v.detail; b[33] = v.timers; b[34] = v.messageRate; b[35] = v.messagePitch;
    b[36] = v.punctuation; b[37] = v.capitals; b[38] = v.screenLanguage; b[39] = v.readerFlags;
    b[40] = v.formatFlags; b[41] = v.readerExtra; b[42] = v.synthKey; b[43] = v.synthVoice;
    const u32 crc = setcrc(b, 44); for (int i = 0; i < 4; i++) b[44 + i] = (u8)(crc >> (i * 8));
    return true;
}

const char* const kSlotA = "charon-settings-a.bin";
const char* const kSlotB = "charon-settings-b.bin";

// Reads both slots, applies the same validity + slot-parity check as Charon's
// ReadSettingsSlot, chooses the newest valid record. Returns:
//   1  have = a valid record was chosen (out, seq filled)
//   0  no valid record
//  -1  a slot holds a newer-than-understood format -> never rewrite
int load_settings(Settings& out, u64& seq)
{
    u8 a[49], b[49];
    const long na = charon_plat_hak_read(kSlotA, a, sizeof a);
    const long nb = charon_plat_hak_read(kSlotB, b, sizeof b);
    if ((na > 0 && record_too_new(a, (size_t)na)) || (nb > 0 && record_too_new(b, (size_t)nb))) return -1;
    Settings sa, sb; bool va = false, vb = false;
    if (na > 0 && decode(a, (size_t)na, sa) && (sa.sequence & 1) == 1) va = true;     // slot a: odd sequence
    if (nb > 0 && decode(b, (size_t)nb, sb) && (sb.sequence & 1) == 0) vb = true;     // slot b: even sequence
    if (!va && !vb) return 0;
    if (va && (!vb || sa.sequence >= sb.sequence)) { out = sa; seq = sa.sequence; }
    else { out = sb; seq = sb.sequence; }
    return 1;
}

// Persists `v` into BOTH slots, each the way Charon saves: next sequence, encode,
// write the inactive slot (by parity), read it back and compare. Two writes, one
// per slot, leave a consistent ping-pong (newest in one slot, previous in the
// other). Returns 0 ok, 1 encode out of range, 2 a write did not verify.
int persist(Settings v, u64 seqBase)
{
    for (int step = 1; step <= 2; step++) {
        v.sequence = seqBase + (u64)step;
        u8 rec[48];
        if (!encode(v, rec)) return 1;
        const char* slot = (v.sequence & 1) ? kSlotA : kSlotB;
        if (!charon_plat_hak_write(slot, rec, sizeof rec)) return 2;
        u8 back[49];
        const long n = charon_plat_hak_read(slot, back, sizeof back);
        Settings chk;
        if (n != (long)sizeof rec || !decode(back, (size_t)n, chk)
            || (chk.sequence & 1) != (u64)((slot == kSlotA) ? 1 : 0)) return 2;
        for (int i = 0; i < 48; i++) if (back[i] != rec[i]) return 2;
    }
    return 0;
}

// ---- percent <-> stored units (CharSettings.h, r390 NVDA scale) --------------
u8 tenths_from_percent(u32 p)                 // rate / pitch: 5..20
{
    if (p >= 100) return 20;
    if (p <= 50) { u32 t = 5 + (p + 5) / 10; return (u8)(t < 5 ? 5 : t); }
    u32 t = 10 + (p - 50 + 2) / 5; return (u8)(t > 20 ? 20 : t);
}
u32 percent_from_tenths(u8 t)
{
    if (t <= 5) return 0;
    if (t <= 10) return (u32)(t - 5) * 10;
    return t >= 20 ? 100u : 50u + (u32)(t - 10) * 5;
}
u8 level_from_percent(u32 p) { u32 v = (p + 5) / 10; if (v < 1) v = 1; if (v > 10) v = 10; return (u8)v; }   // volume 1..10

// ---- synthesizer manifest keys (CharSettings.h V11 / CharonSynthHost.h) ------
u8 synth_key_of(const char* manifestBasename)
{
    u32 h = 2166136261u;
    for (const char* p = manifestBasename; *p; p++) { char c = *p; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); h = (h ^ (u8)c) * 16777619u; }
    const u8 k = (u8)(h ^ (h >> 8) ^ (h >> 16) ^ (h >> 24));
    return k ? k : 1;
}

// ================= status / help ==============================================
void punct_name(u8 level) { const char* en[] = {"none", "some", "most", "all"}; const char* pl[] = {"brak", "trochę", "większość", "wszystko"}; const int i = level > 3 ? 1 : level; cbil(en[i], pl[i]); }

void report_reader(const Settings& s)
{
    cbil("Reader rate ", "Tempo czytnika "); cnum(percent_from_tenths(s.readerRate)); cout('%');
    cbil(", pitch ", ", wysokość "); cnum(percent_from_tenths(s.readerPitch)); cout('%');
    cbil(", volume ", ", głośność "); cnum((u32)s.readerVolume * 10); cout('%'); cnl();
    cbil("Punctuation ", "Interpunkcja "); punct_name(s.punctuation);
    cbil(", language ", ", język "); cbil(s.language == 2 ? "Polish" : s.language == 1 ? "English" : "automatic", s.language == 2 ? "polski" : s.language == 1 ? "angielski" : "automatyczny");
    cbil(", sounds ", ", dźwięki "); cbil(s.sounds ? "on" : "off", s.sounds ? "włączone" : "wyłączone"); cnl();
    cbil("Synthesizer ", "Syntezator "); if (!s.synthKey) cbil("TVox (built-in)", "TVox (wbudowany)"); else { cbil("add-on key ", "dodatek nr "); cnum(s.synthKey); cbil(", voice ", ", głos "); cnum(s.synthVoice); } cnl();
}

void list_subcommands(void)
{
    cbil("Sub-commands:", "Podpolecenia:"); cnl();
    cputln("  CHARON RATE|PITCH|VOLUME <0-100>");
    cputln("  CHARON PUNCT none|some|most|all   CHARON LANG pl|en|auto");
    cputln("  CHARON SOUNDS on|off              CHARON VOICE <name>");
    cputln("  CHARON SAY <text>");
    cputln("  CHARON BOOT   CHARON CONTINUE <n>   CHARON MENU");
    cputln("  CHARON FIRMWARE   CHARON RESTART   CHARON OFF");
}

void cmd_status(void)
{
    CharonInfo info; charon_plat_info(&info);
    cbil("Charon", "Charon"); cout(' ');
    if (info.release) { cbil("release r", "wydanie r"); cnum(info.release); } else cbil("reader not detected", "czytnik niewykryty");
    cnl();
    Settings s; u64 seq;
    const int have = load_settings(s, seq);
    if (have == 1) report_reader(s);
    else if (have < 0) cbil("Settings are a newer format than this CharonDOS understands.\n", "Ustawienia są w nowszym formacie niż rozumie ten CharonDOS.\n");
    else cbil("Settings record not found.\n", "Nie znaleziono rekordu ustawień.\n");
    // Continue target
    u8 cont[64];
    const long cn = charon_plat_hak_read("continue.bin", cont, sizeof cont);
    cbil("Continue target: ", "Cel kontynuacji: ");
    if (cn >= 10 && cont[0] == 'H' && cont[1] == 'A' && cont[2] == 'K') {
        const u16 id = (u16)(cont[8] | (cont[9] << 8));
        CharonBootEntry entries[64];
        const int ne = charon_plat_boot_list(entries, 64);
        const char* desc = nullptr;
        for (int i = 0; i < ne; i++) if (entries[i].id == id) desc = entries[i].desc;
        if (desc && *desc) cputs(desc); else { cbil("entry ", "wpis "); cnum(id); }
    } else cbil("none (firmware default)", "brak (domyślny rozruch firmware)");
    cnl();
    cbil("Secure Boot: ", "Secure Boot: ");
    cbil(info.secure_boot == 1 ? "on" : info.secure_boot == 0 ? "off" : "unknown",
         info.secure_boot == 1 ? "włączony" : info.secure_boot == 0 ? "wyłączony" : "nieznany"); cnl();
    if (!info.have_voice) cbil("Live voice not reachable; confirmations are shown, not spoken.\n", "Brak dostępu do żywego głosu; potwierdzenia są wypisywane, nie wymawiane.\n");
    cnl();
    list_subcommands();
}

// ================= reader settings ============================================
const char* kDeferEn = "This takes effect at the next start of Charon's reader.";
const char* kDeferPl = "Zmiana zadziała przy następnym uruchomieniu czytnika Charona.";

// Shared tail for a persisted change: report range errors, save, speak readback.
// `which` 0 rate, 1 pitch, 2 volume (percent commands).
int set_voice_value(int which, char* args)
{
    Settings s; u64 seq;
    const int have = load_settings(s, seq);
    const bool report_only = !*skipws(args);
    if (report_only) {
        if (have != 1) { cbil("Settings unavailable.", "Ustawienia niedostępne."); cnl(); return 1; }
        u32 cur = which == 0 ? percent_from_tenths(s.readerRate) : which == 1 ? percent_from_tenths(s.readerPitch) : (u32)s.readerVolume * 10;
        cbil(which == 0 ? "Reader rate " : which == 1 ? "Reader pitch " : "Reader volume ",
             which == 0 ? "Tempo czytnika " : which == 1 ? "Wysokość czytnika " : "Głośność czytnika ");
        cnum(cur); cout('%'); cnl(); return 0;
    }
    if (have != 1) { cbil(have < 0 ? "Settings are a newer format than understood; not changed." : "Settings record not found; change it from Charon's menu.",
                          have < 0 ? "Ustawienia w nowszym formacie; nie zmieniono." : "Nie znaleziono ustawień; zmień je w menu Charona."); cnl(); return 1; }
    u32 p = 0;
    if (!d_atou(skipws(args), &p) || p > 100) { cbil("Give a number 0 to 100.", "Podaj liczbę od 0 do 100."); cnl(); return 1; }
    if (which == 0) s.readerRate = tenths_from_percent(p);
    else if (which == 1) s.readerPitch = tenths_from_percent(p);
    else s.readerVolume = level_from_percent(p);
    const int e = persist(s, seq);
    if (e) { cbil("Could not save the setting.", "Nie udało się zapisać ustawienia."); cnl(); return 1; }
    u32 back = which == 2 ? (u32)s.readerVolume * 10 : percent_from_tenths(which == 0 ? s.readerRate : s.readerPitch);
    char msg[80]; char nb[12]; d_utoa(back, nb, 10);
    d_strcpy(msg, L(which == 0 ? "Reader rate " : which == 1 ? "Reader pitch " : "Reader volume ",
                    which == 0 ? "Tempo czytnika " : which == 1 ? "Wysokość czytnika " : "Głośność czytnika "), sizeof msg);
    d_strcat(msg, nb, sizeof msg); d_strcat(msg, " %", sizeof msg);
    speak_and_show(msg);
    cbil(kDeferEn, kDeferPl); cnl();
    return 0;
}

int set_punct(char* args)
{
    Settings s; u64 seq;
    const int have = load_settings(s, seq);
    char* w = skipws(args);
    if (!*w) { if (have != 1) { cbil("Settings unavailable.", "Ustawienia niedostępne."); cnl(); return 1; } cbil("Punctuation ", "Interpunkcja "); punct_name(s.punctuation); cnl(); return 0; }
    if (have != 1) { cbil("Settings not available to change.", "Ustawień nie można zmienić."); cnl(); return 1; }
    int level = -1;
    rtrim(w);
    if (!d_stricmp(w, "none") || !d_stricmp(w, "brak") || !d_strcmp(w, "0")) level = 0;
    else if (!d_stricmp(w, "some") || !d_stricmp(w, "troche") || !d_strcmp(w, "1")) level = 1;
    else if (!d_stricmp(w, "most") || !d_strcmp(w, "2")) level = 2;
    else if (!d_stricmp(w, "all") || !d_stricmp(w, "wszystko") || !d_strcmp(w, "3")) level = 3;
    if (level < 0) { cbil("Use none, some, most or all.", "Użyj: brak, trochę, większość lub wszystko."); cnl(); return 1; }
    s.punctuation = (u8)level;
    if (persist(s, seq)) { cbil("Could not save.", "Nie udało się zapisać."); cnl(); return 1; }
    cbil("Punctuation ", "Interpunkcja "); punct_name((u8)level);
    { const char* t = L("Punctuation changed.", "Interpunkcja zmieniona."); charon_plat_speak(s_pl ? "pl" : "en", t); }
    cnl(); cbil(kDeferEn, kDeferPl); cnl();
    return 0;
}

int set_lang(char* args)
{
    Settings s; u64 seq;
    const int have = load_settings(s, seq);
    char* w = skipws(args); rtrim(w);
    if (!*w) { if (have != 1) { cbil("Settings unavailable.", "Ustawienia niedostępne."); cnl(); return 1; }
        cbil("Language ", "Język "); cbil(s.language == 2 ? "Polish" : s.language == 1 ? "English" : "automatic", s.language == 2 ? "polski" : s.language == 1 ? "angielski" : "automatyczny"); cnl(); return 0; }
    if (have != 1) { cbil("Settings not available to change.", "Ustawień nie można zmienić."); cnl(); return 1; }
    int lang = -1;
    if (!d_stricmp(w, "pl") || !d_stricmp(w, "polish") || !d_stricmp(w, "polski")) lang = 2;
    else if (!d_stricmp(w, "en") || !d_stricmp(w, "english") || !d_stricmp(w, "angielski")) lang = 1;
    else if (!d_stricmp(w, "auto") || !d_stricmp(w, "automatic")) lang = 0;
    if (lang < 0) { cbil("Use pl, en or auto.", "Użyj: pl, en lub auto."); cnl(); return 1; }
    s.language = (u8)lang;
    if (persist(s, seq)) { cbil("Could not save.", "Nie udało się zapisać."); cnl(); return 1; }
    speak_and_show(L(lang == 2 ? "Reader language Polish." : lang == 1 ? "Reader language English." : "Reader language automatic.",
                     lang == 2 ? "Język czytnika polski." : lang == 1 ? "Język czytnika angielski." : "Język czytnika automatyczny."));
    cbil(kDeferEn, kDeferPl); cnl();
    return 0;
}

int set_sounds(char* args)
{
    Settings s; u64 seq;
    const int have = load_settings(s, seq);
    char* w = skipws(args); rtrim(w);
    if (!*w) { if (have != 1) { cbil("Settings unavailable.", "Ustawienia niedostępne."); cnl(); return 1; }
        cbil("Sounds ", "Dźwięki "); cbil(s.sounds ? "on" : "off", s.sounds ? "włączone" : "wyłączone"); cnl(); return 0; }
    if (have != 1) { cbil("Settings not available to change.", "Ustawień nie można zmienić."); cnl(); return 1; }
    int on = -1;
    if (!d_stricmp(w, "on") || !d_stricmp(w, "wlaczone") || !d_strcmp(w, "1")) on = 1;
    else if (!d_stricmp(w, "off") || !d_stricmp(w, "wylaczone") || !d_strcmp(w, "0")) on = 0;
    if (on < 0) { cbil("Use on or off.", "Użyj: on lub off."); cnl(); return 1; }
    s.sounds = (u8)on;
    if (persist(s, seq)) { cbil("Could not save.", "Nie udało się zapisać."); cnl(); return 1; }
    speak_and_show(L(on ? "Charon sounds on." : "Charon sounds off.", on ? "Dźwięki Charona włączone." : "Dźwięki Charona wyłączone."));
    cbil(kDeferEn, kDeferPl); cnl();
    return 0;
}

int set_voice(char* args)
{
    Settings s; u64 seq;
    const int have = load_settings(s, seq);
    char* w = skipws(args); rtrim(w);
    static char names[8][64];
    const int nsyn = charon_plat_synth_list(names, 8);
    if (!*w) {
        if (have == 1) { cbil("Current synthesizer: ", "Bieżący syntezator: "); if (!s.synthKey) cbil("TVox (built-in)", "TVox (wbudowany)"); else { cbil("add-on key ", "dodatek nr "); cnum(s.synthKey); } cnl(); }
        cbil("Available: TVox", "Dostępne: TVox");
        for (int i = 0; i < nsyn; i++) { cbil(", ", ", "); cputs(names[i]); }
        cnl(); return 0;
    }
    if (have != 1) { cbil("Settings not available to change.", "Ustawień nie można zmienić."); cnl(); return 1; }
    if (!d_stricmp(w, "tvox") || !d_stricmp(w, "default") || !d_strcmp(w, "0")) { s.synthKey = 0; s.synthVoice = 0; }
    else {
        int found = -1;
        for (int i = 0; i < nsyn; i++) {
            // match the manifest basename with or without ".manifest"
            char base[64]; d_strcpy(base, names[i], sizeof base);
            size_t bl = d_strlen(base);
            if (bl > 9 && !d_stricmp(base + bl - 9, ".manifest")) base[bl - 9] = 0;
            if (!d_stricmp(w, names[i]) || !d_stricmp(w, base)) { found = i; break; }
        }
        if (found < 0) { cbil("No such synthesizer add-on. Try CHARON VOICE with no name to list them.", "Nie ma takiego dodatku syntezatora. Wpisz CHARON VOICE bez nazwy, aby je wypisać."); cnl(); return 1; }
        s.synthKey = synth_key_of(names[found]); s.synthVoice = 0;
    }
    if (persist(s, seq)) { cbil("Could not save.", "Nie udało się zapisać."); cnl(); return 1; }
    speak_and_show(L("Synthesizer changed.", "Syntezator zmieniony."));
    cbil(kDeferEn, kDeferPl); cnl();
    return 0;
}

int cmd_say(char* args)
{
    char* t = skipws(args);
    if (!*t) { cbil("Give the text to speak.", "Podaj tekst do wymówienia."); cnl(); return 1; }
    if (!charon_plat_speak(s_pl ? "pl" : "en", t)) { cbil("Live voice not reachable.", "Brak dostępu do żywego głosu."); cnl(); }
    cputln(t);
    return 0;
}

// ================= boot manager ===============================================
int cmd_boot(void)
{
    CharonBootEntry entries[64];
    const int n = charon_plat_boot_list(entries, 64);
    if (n < 0) { cbil("Firmware boot order is not readable from here.", "Kolejność rozruchu firmware jest stąd nieczytelna."); cnl(); return 1; }
    if (!n) { cbil("No firmware boot entries.", "Brak wpisów rozruchowych firmware."); cnl(); return 0; }
    cbil("Firmware boot entries (in order):", "Wpisy rozruchowe firmware (w kolejności):"); cnl();
    for (int i = 0; i < n; i++) {
        cout(' '); cnum((u32)(i + 1)); cbil(". ", ". "); cputs(entries[i].desc);
        if (!d_stricmp(entries[i].desc, "Charon")) cbil("  <- Charon boot manager", "  <- menedżer rozruchu Charon");
        else if (!d_stricmp(entries[i].desc, "CharonDOS")) cbil("  <- CharonDOS (here)", "  <- CharonDOS (tutaj)");
        cnl();
    }
    cbil("Use CHARON CONTINUE <n> to set Charon's Continue target.", "Użyj CHARON CONTINUE <n>, aby ustawić cel kontynuacji Charona."); cnl();
    return 0;
}

int cmd_continue(char* args)
{
    char* w = skipws(args); rtrim(w);
    if (!*w) { cbil("Give the entry number from CHARON BOOT.", "Podaj numer wpisu z CHARON BOOT."); cnl(); return 1; }
    u32 idx = 0;
    if (!d_atou(w, &idx) || !idx) { cbil("Give a number from the CHARON BOOT list.", "Podaj numer z listy CHARON BOOT."); cnl(); return 1; }
    CharonBootEntry entries[64];
    const int n = charon_plat_boot_list(entries, 64);
    if (n < 0 || (int)idx > n) { cbil("No such entry. Run CHARON BOOT.", "Nie ma takiego wpisu. Uruchom CHARON BOOT."); cnl(); return 1; }
    const CharonBootEntry& e = entries[idx - 1];
    const int r = charon_plat_set_continue(e.id);
    if (r == 0) {
        char msg[96]; d_strcpy(msg, L("Continue target set to ", "Cel kontynuacji ustawiony na "), sizeof msg);
        d_strcat(msg, e.desc, sizeof msg); speak_and_show(msg);
        return 0;
    }
    if (r == 3 || r == 1) cbil("This entry cannot be set as the Continue target from CharonDOS; set it in Charon's menu.", "Tego wpisu nie można ustawić jako celu kontynuacji z CharonDOS; ustaw go w menu Charona.");
    else cbil("Could not write the Continue target.", "Nie udało się zapisać celu kontynuacji.");
    cnl();
    return 1;
}

int cmd_menu(void)
{
    const int r = charon_plat_boot_next_charon();
    if (r == 0) { speak_and_show(L("Charon's menu will show at the next restart.", "Menu Charona pokaże się przy następnym uruchomieniu.")); return 0; }
    if (r == 1) cbil("Charon's own boot entry was not found; open its menu from Charon.", "Nie znaleziono wpisu rozruchowego Charona; otwórz jego menu z poziomu Charona.");
    else cbil("BootNext is not writable from here; open Charon's menu from Charon.", "Zmiennej BootNext nie można stąd zapisać; otwórz menu Charona z poziomu Charona.");
    cnl();
    return 1;
}

int cmd_firmware(void)
{
    if (!confirm(L("Restart into firmware setup?", "Uruchomić ponownie do ustawień firmware?"))) { cbil("Cancelled.", "Anulowano."); cnl(); return 1; }
    const int r = charon_plat_firmware_setup();
    if (r == 1) { cbil("This firmware does not offer a restart into its setup.", "To firmware nie oferuje restartu do swoich ustawień."); cnl(); return 1; }
    if (r == 2) { cbil("Could not arm the firmware-setup restart from here.", "Nie udało się stąd ustawić restartu do firmware."); cnl(); return 1; }
    speak_and_show(L("Restarting into firmware setup.", "Uruchamiam ponownie do ustawień firmware."));
    charon_plat_reset(0);
    return 0;
}

int cmd_reset(int mode)
{
    if (!confirm(mode ? L("Turn the computer off?", "Wyłączyć komputer?") : L("Restart the computer?", "Uruchomić ponownie komputer?"))) { cbil("Cancelled.", "Anulowano."); cnl(); return 1; }
    speak_and_show(mode ? L("Turning off.", "Wyłączam.") : L("Restarting.", "Uruchamiam ponownie."));
    charon_plat_reset(mode);
    return 0;
}

}  // namespace

// ================= dispatch ===================================================
int charon_command(char* args, int polish)
{
    s_pl = polish;
    char* rest = args;
    char* sub = nextword(&rest);
    if (!*sub || !d_stricmp(sub, "HELP") || !d_stricmp(sub, "/?")) {
        if (!*sub) { cmd_status(); return 0; }
        list_subcommands(); return 0;
    }
    if (!d_stricmp(sub, "RATE")) return set_voice_value(0, rest);
    if (!d_stricmp(sub, "PITCH")) return set_voice_value(1, rest);
    if (!d_stricmp(sub, "VOLUME")) return set_voice_value(2, rest);
    if (!d_stricmp(sub, "PUNCT")) return set_punct(rest);
    if (!d_stricmp(sub, "LANG")) return set_lang(rest);
    if (!d_stricmp(sub, "SOUNDS")) return set_sounds(rest);
    if (!d_stricmp(sub, "VOICE")) return set_voice(rest);
    if (!d_stricmp(sub, "SAY")) return cmd_say(rest);
    if (!d_stricmp(sub, "BOOT")) return cmd_boot();
    if (!d_stricmp(sub, "CONTINUE")) return cmd_continue(rest);
    if (!d_stricmp(sub, "MENU")) return cmd_menu();
    if (!d_stricmp(sub, "FIRMWARE")) return cmd_firmware();
    if (!d_stricmp(sub, "RESTART")) return cmd_reset(0);
    if (!d_stricmp(sub, "OFF")) return cmd_reset(1);
    cbil("Unknown CHARON sub-command. Type CHARON for the list.", "Nieznane podpolecenie CHARON. Wpisz CHARON, aby zobaczyć listę."); cnl();
    return 1;
}
