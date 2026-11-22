// SPDX-License-Identifier: GPL-2.0-or-later
// CharonDOS - the built-in command interpreter (native, reachable by
// programs as COMSPEC=C:\COMMAND.COM) and the entry point dos_main().
#include "dos.h"
#include "charon_cmd.h"      // the CHARON built-in (controls Charon's reader and boot manager)

// ---- messages: English and Polish in one table ---------------------------------------
enum {
    M_ILLEGAL = MSG_ILLEGAL, M_DIVIDE = MSG_DIVIDE, M_MCB = MSG_MCB,
    M_BANNER, M_HINT, M_BADCMD, M_NOTFOUND, M_PATHNF, M_DENIED, M_BADDRIVE, M_NOMEM, M_SYNTAX,
    M_ERRN, M_DIROF, M_FILES, M_DIRS, M_PAUSE, M_COPIED, M_ECHO_ON, M_ECHO_OFF, M_DATE, M_TIME,
    M_MEM1, M_MEM2, M_LABEL, M_EXEFAIL, M_NOENV, M_BATNEST, M_REQ, M_EXISTS, M_CURDIR, M_HELP, M_NOWINNT, M_NOWINPROF, M_STARTWINNT, M_STARTWIN, M_COUNT
};
static const char* const s_msg[M_COUNT][2] = {
    {"Illegal instruction at %s. Program terminated.", "Niedozwolona instrukcja pod adresem %s. Program zakończony."},
    {"Divide overflow. Program terminated.", "Błąd dzielenia. Program zakończony."},
    {"Memory allocation error. Memory was reset.", "Błąd przydziału pamięci. Pamięć została wyzerowana."},
    {"CharonDOS version 1.0 - DOS 5.0 compatible environment for UEFI", "CharonDOS wersja 1.0 - środowisko zgodne z DOS 5.0 dla UEFI"},
    {"Type HELP for the list of commands, EXIT to return.", "Wpisz HELP, aby poznać listę poleceń, EXIT, aby wrócić."},
    {"Bad command or file name", "Nieznane polecenie lub nazwa pliku"},
    {"File not found", "Nie znaleziono pliku"},
    {"Path not found", "Nie znaleziono ścieżki"},
    {"Access denied", "Odmowa dostępu"},
    {"Invalid drive", "Nieprawidłowy dysk"},
    {"Not enough memory", "Za mało pamięci"},
    {"Syntax error", "Błąd składni"},
    {"Error %u", "Błąd %u"},
    {" Directory of %s", " Katalog %s"},
    {"%u file(s) %s bytes", "Plików: %u, bajtów: %s"},
    {"%u dir(s) %s bytes free", "Katalogów: %u, wolnych bajtów: %s"},
    {"Press any key to continue . . .", "Naciśnij dowolny klawisz, aby kontynuować . . ."},
    {"%u file(s) copied", "Skopiowano plików: %u"},
    {"ECHO is on", "ECHO jest włączone"},
    {"ECHO is off", "ECHO jest wyłączone"},
    {"Current date is %s", "Bieżąca data: %s"},
    {"Current time is %s", "Bieżący czas: %s"},
    {"%u bytes total conventional memory", "Pamięć konwencjonalna, bajtów razem: %u"},
    {"%u bytes free (largest block)", "Wolne bajty (największy blok): %u"},
    {"Label not found", "Nie znaleziono etykiety"},
    {"Cannot execute %s", "Nie można uruchomić %s"},
    {"Out of environment space", "Brak miejsca w środowisku"},
    {"Batch files nested too deeply", "Zbyt głębokie zagnieżdżenie plików wsadowych"},
    {"Required parameter missing", "Brak wymaganego parametru"},
    {"File already exists", "Plik już istnieje"},
    {"Cannot remove the current directory", "Nie można usunąć bieżącego katalogu"},
    {"Commands:\n"
     "DIR [path] [/W] [/P] [/B]  list files       CD [path]      change directory\n"
     "TYPE file                  show a file      COPY src dst   copy files\n"
     "DEL file                   delete files     REN old new    rename a file\n"
     "MD dir                     make directory   RD dir         remove directory\n"
     "D:                         change drive     CLS            clear the screen\n"
     "SET [name=value]           environment      PATH [dirs]    program search path\n"
     "PROMPT [text]              set the prompt   ECHO [text]    print text\n"
     "DATE, TIME, VER, MEM       information      HELP           this list\n"
     "EXIT                       return to the boot manager\n"
     "WINNT                      start the installed Windows (CharonDOS ends)\n"
     "WIN [profile]              start a Windows profile installed into CharonDOS\n"
     "CHARON [command]           control Charon's reader and boot manager\n"
     "name [arguments]           run name.COM, name.EXE or name.BAT\n"
     "command > file, >> file, < file redirect output and input\n"
     "Batch files: REM, PAUSE, GOTO, IF, CALL, SHIFT, %1..%9, @ECHO OFF\n"
     "Ctrl+C stops a program that waits for the keyboard.",
     "Polecenia:\n"
     "DIR [ścieżka] [/W] [/P] [/B]  lista plików      CD [ścieżka]   zmiana katalogu\n"
     "TYPE plik                  pokaż plik          COPY skąd dokąd  kopiuj pliki\n"
     "DEL plik                   usuń pliki          REN stara nowa  zmień nazwę\n"
     "MD katalog                 utwórz katalog      RD katalog     usuń katalog\n"
     "D:                         zmiana dysku        CLS            wyczyść ekran\n"
     "SET [nazwa=wartość]        środowisko          PATH [katalogi]  ścieżka programów\n"
     "PROMPT [tekst]             znak zachęty        ECHO [tekst]   wypisz tekst\n"
     "DATE, TIME, VER, MEM       informacje          HELP           ta lista\n"
     "EXIT                       powrót do menedżera rozruchu\n"
     "WINNT                      uruchom zainstalowany Windows (CharonDOS się kończy)\n"
     "WIN [profil]               uruchom profil Windows zainstalowany w CharonDOS\n"
     "CHARON [polecenie]         steruj czytnikiem i menedżerem rozruchu Charona\n"
     "nazwa [argumenty]          uruchom nazwa.COM, nazwa.EXE lub nazwa.BAT\n"
     "polecenie > plik, >> plik, < plik  przekierowanie wyjścia i wejścia\n"
     "Pliki wsadowe: REM, PAUSE, GOTO, IF, CALL, SHIFT, %1..%9, @ECHO OFF\n"
     "Ctrl+C przerywa program, który czeka na klawiaturę."},
    {"Windows was not found on this computer (no \\EFI\\Microsoft\\Boot\\bootmgfw.efi on any volume).",
     "Nie znaleziono Windows na tym komputerze (brak \\EFI\\Microsoft\\Boot\\bootmgfw.efi na żadnym woluminie)."},
    {"No Windows profile %s is installed in CharonDOS. Install one with Charon's installer, group CharonDOS.",
     "W CharonDOS nie ma zainstalowanego profilu Windows %s. Zainstaluj go instalatorem Charona, grupa CharonDOS."},
    {"Starting Windows. CharonDOS ends.", "Uruchamiam Windows. CharonDOS się kończy."},
    {"Starting the Windows profile %s.", "Uruchamiam profil Windows %s."}
};

enum { MAXBATCH = 6, LINE = 520 };
struct Batch { char path[160]; u32 pos; char args[260]; u8 argoff[32]; int nargs, shift; bool done; char line[LINE]; char raw[260]; };

static bool s_polish, s_echo = true, s_exit_requested, s_abort_batch, s_call;
static int s_errorlevel;
static Batch* s_batch;
static int s_nbatch;
static char s_lines[8][LINE];
static int s_line_depth;
static u8 s_copy[4096];

static int run_line(char* line);

// ---- output ------------------------------------------------------------------------------
static void out(u8 c) { dos_out(1, c); }
static void outs(const char* s) { for (; *s; s++) out((u8)*s); }
static void nl(void) { out(13); out(10); }
static void out_uni(int u)
{
    if (u == '\n') { nl(); return; }
    if (fs_is_con(1)) { con_put_uni((dchar16)u); return; }
    u8 c = uni_to_cp437((dchar16)u, nullptr);
    out(c ? c : '?');
}
static void outn(u32 v) { char b[12]; d_utoa(v, b, 10); outs(b); }
static void msg(int id, const char* text, u32 number)
{
    const char* f = s_msg[id][s_polish ? 1 : 0];
    while (*f) {
        if (f[0] == '%' && f[1] == 's') { outs(text ? text : ""); f += 2; }
        else if (f[0] == '%' && f[1] == 'u') { outn(number); f += 2; }
        else out_uni(utf8_next(&f));
    }
}
static void msgln(int id, const char* text, u32 number) { msg(id, text, number); nl(); }
void shell_notice(int id, const char* text, u32 number)
{
    int x, y;
    vid_get_cursor(&x, &y);
    if (x) { con_putc(13); con_putc(10); }
    const int saved = fs_redirect(1, 2);          // always to the console
    msgln(id, text, number);
    fs_restore(1, saved);
}
static void error(int code)
{
    switch (code) {
    case 2: case 18: msgln(M_NOTFOUND, nullptr, 0); break;
    case 3: msgln(M_PATHNF, nullptr, 0); break;
    case 5: msgln(M_DENIED, nullptr, 0); break;
    case 8: msgln(M_NOMEM, nullptr, 0); break;
    case 15: msgln(M_BADDRIVE, nullptr, 0); break;
    case 16: msgln(M_CURDIR, nullptr, 0); break;
    case 80: msgln(M_EXISTS, nullptr, 0); break;
    default: msgln(M_ERRN, nullptr, (u32)code); break;
    }
    s_errorlevel = 1;
}

static char* skip(char* s) { while (*s == ' ' || *s == '\t') s++; return s; }
static void rtrim(char* s) { size_t n = d_strlen(s); while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0; }
// Takes the next blank separated word out of *s (in place).
static char* word(char** s)
{
    char* p = skip(*s);
    char* start = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    if (*p) *p++ = 0;
    *s = p;
    return start;
}
static void two(u32 v, char* out2) { out2[0] = (char)('0' + v / 10 % 10); out2[1] = (char)('0' + v % 10); out2[2] = 0; }
static void fmt_date(u32 y, u32 m, u32 d, char* o)
{
    char t[12];
    d_utoa(y, o, 10);
    two(m, t); d_strcat(o, "-", 16); d_strcat(o, t, 16);
    two(d, t); d_strcat(o, "-", 16); d_strcat(o, t, 16);
}
static void fmt_time(u32 h, u32 m, char* o)
{
    char t[4];
    two(h, o); two(m, t); d_strcat(o, ":", 8); d_strcat(o, t, 8);
}
// Waits for a key; false when Ctrl+C or quitting.
static bool wait_key(void)
{
    u16 key;
    if (!kbd_wait(&key)) { g_break = false; return false; }
    return lo8(key) != 3;
}

static void prompt(void)
{
    const char* p = env_get("PROMPT");
    if (!p) p = "$P$G";
    char t[20];
    PlatTime now;
    for (; *p; p++) {
        if (*p != '$') { con_putc((u8)*p); continue; }
        p++;
        switch (d_upper(*p)) {
        case 'P': con_putc((u8)('A' + fs_cur_drive())); con_putc(':'); { const char* c = fs_cwd(fs_cur_drive()); if (!*c) c = "\\"; for (; *c; c++) con_putc((u8)*c); } break;
        case 'N': con_putc((u8)('A' + fs_cur_drive())); break;
        case 'G': con_putc('>'); break;
        case 'L': con_putc('<'); break;
        case 'B': con_putc('|'); break;
        case 'Q': con_putc('='); break;
        case '$': con_putc('$'); break;
        case '_': con_putc(13); con_putc(10); break;
        case 'D': plat_time(&now); fmt_date(now.year, now.month, now.day, t); for (char* c = t; *c; c++) con_putc((u8)*c); break;
        case 'T': plat_time(&now); fmt_time(now.hour, now.minute, t); for (char* c = t; *c; c++) con_putc((u8)*c); break;
        case 0: p--; break;
        default: break;
        }
    }
}

// ---- built-in commands ---------------------------------------------------------------------
static void cmd_dir(char* args)
{
    bool wide = false, page = false, bare = false;
    char spec[160] = "";
    for (;;) {
        char* w = word(&args);
        if (!*w) break;
        // Switches may be glued to the path: DIR C:\X/W
        char* sw = w;
        for (; *sw; sw++) if (*sw == '/' && (d_upper(sw[1]) == 'W' || d_upper(sw[1]) == 'P' || d_upper(sw[1]) == 'B') && (!sw[2] || sw[2] == '/')) {
            for (char* q = sw; *q; q++) if (*q == '/') { const char o = d_upper(q[1]); if (o == 'W') wide = true; else if (o == 'P') page = true; else if (o == 'B') bare = true; }
            *sw = 0;
            break;
        }
        if (*w) d_strcpy(spec, w, sizeof spec);
    }
    DosPath p;
    int e = fs_resolve(spec[0] ? spec : ".", &p, true);
    if (e) { error(e); return; }
    char dir[170];
    if (p.exists && p.isdir) {
        fs_full_name(&p, dir, sizeof dir);
        d_strcpy(spec, dir, sizeof spec);
        if (spec[d_strlen(spec) - 1] != '\\') d_strcat(spec, "\\", sizeof spec);
        d_strcat(spec, "*.*", sizeof spec);
    } else {
        fs_full_name(&p, dir, sizeof dir);
        char* cut = dir;
        for (char* c = dir; *c; c++) if (*c == '\\') cut = c;
        if (cut == dir + 2) cut++;
        *cut = 0;
        bool dot = false;
        for (const char* c = p.last; *c; c++) if (*c == '.') dot = true;
        d_strcpy(spec, dir, sizeof spec);
        if (spec[d_strlen(spec) - 1] != '\\') d_strcat(spec, "\\", sizeof spec);
        d_strcat(spec, p.last, sizeof spec);
        if (!dot) d_strcat(spec, ".*", sizeof spec);
    }
    int lines = 0;
    if (!bare) { nl(); msgln(M_DIROF, dir, 0); nl(); lines = 3; }
    u8 dta[43];
    static dchar16 lname[256];
    u32 files = 0, dirs = 0, col = 0;
    u64 bytes = 0;
    char num[24], t[16];
    for (e = fs_find_first(spec, 0x10, dta); !e; e = fs_find_next(dta)) {
        const char* name = (const char*)dta + 0x1E;
        const bool isdir = (dta[0x15] & 0x10) != 0;
        const u32 size = (u32)(dta[0x1A] | (dta[0x1B] << 8) | (dta[0x1C] << 16) | ((u32)dta[0x1D] << 24));
        if (isdir) dirs++; else { files++; bytes += size; }
        if (bare) { if (name[0] != '.') { outs(name); nl(); lines++; } }
        else if (wide) {
            char cell[20] = "";
            if (isdir) d_strcat(cell, "[", sizeof cell);
            d_strcat(cell, name, sizeof cell);
            if (isdir) d_strcat(cell, "]", sizeof cell);
            outs(cell);
            if (++col == 5) { nl(); col = 0; lines++; }
            else for (size_t i = d_strlen(cell); i < 16; i++) out(' ');
        } else {
            char base[9] = "", ext[4] = "";
            if (name[0] == '.') d_strcpy(base, name, sizeof base);
            else {
                int i = 0;
                for (; name[i] && name[i] != '.' && i < 8; i++) base[i] = name[i];
                base[i] = 0;
                if (name[i] == '.') d_strcpy(ext, name + i + 1, sizeof ext);
            }
            outs(base); for (size_t i = d_strlen(base); i < 9; i++) out(' ');
            outs(ext); for (size_t i = d_strlen(ext); i < 4; i++) out(' ');
            if (isdir) outs("<DIR>     ");
            else { const int n = d_utoa(size, num, 10); for (int i = n; i < 10; i++) out(' '); outs(num); }
            const u16 date = (u16)(dta[0x18] | (dta[0x19] << 8)), time = (u16)(dta[0x16] | (dta[0x17] << 8));
            fmt_date(1980u + (date >> 9), (date >> 5) & 15, date & 31, t); outs("  "); outs(t);
            fmt_time(time >> 11, (time >> 5) & 63, t); outs("  "); outs(t);
            if (fs_find_long(dta, lname, 256)) { outs("  "); for (const dchar16* c = lname; *c; c++) out_uni(*c); }
            nl(); lines++;
        }
        if (page && lines >= 22 && fs_is_con(1)) {
            lines = 0;
            msg(M_PAUSE, nullptr, 0);
            const bool go = wait_key();
            nl();
            if (!go) return;
        }
        if (g_quit) return;
    }
    if (wide && col) nl();
    if (!files && !dirs) { msgln(M_NOTFOUND, nullptr, 0); s_errorlevel = 1; return; }
    if (bare) return;
    d_u64toa(bytes, num);
    outs("    "); msgln(M_FILES, num, files);
    u64 total = 0, free_bytes = 0;
    plat_disk_space(p.drive, &total, &free_bytes);
    d_u64toa(free_bytes, num);
    outs("    "); msgln(M_DIRS, num, dirs);
}

static void cmd_cd(char* args)
{
    args = skip(args);
    rtrim(args);
    if (!*args || (args[1] == ':' && !args[2])) {
        const int drive = *args ? d_upper(args[0]) - 'A' : fs_cur_drive();
        if (drive < 0 || drive > 25 || !plat_drive_ok(drive)) { error(15); return; }
        out((u8)('A' + drive)); out(':');
        const char* c = fs_cwd(drive);
        outs(*c ? c : "\\"); nl();
        return;
    }
    const int e = fs_chdir(args);
    if (e) error(e);
}

static void cmd_type(char* args)
{
    char* name = word(&args);
    if (!*name) { msgln(M_REQ, nullptr, 0); return; }
    int h, err;
    const int e = fs_open(name, 0, &h);
    if (e) { error(e); return; }
    for (;;) {
        const long n = fs_read(h, s_copy, sizeof s_copy, &err);
        if (n <= 0) break;
        bool eof = false;
        for (long i = 0; i < n; i++) { if (s_copy[i] == 0x1A) { eof = true; break; } out(s_copy[i]); }
        u16 key;
        if (eof || g_quit) break;
        if (kbd_peek(&key) && lo8(key) == 3) { kbd_get(&key); outs("^C"); nl(); break; }
    }
    fs_close(h);
}

static int copy_file(const char* from, const char* to)
{
    int in, o, err = 0;
    // A file is never copied onto itself (creating the target would empty it).
    static DosPath a, b;
    if (!fs_resolve(from, &a, false) && !fs_resolve(to, &b, false) && a.drive == b.drive && !a.isdev && !d_strcmp(a.dos, b.dos)) return 5;
    int e = fs_open(from, 0, &in);
    if (e) return e;
    e = fs_create(to, 0, false, &o);
    if (e) { fs_close(in); return e; }
    for (;;) {
        const long n = fs_read(in, s_copy, sizeof s_copy, &err);
        if (n < 0) { e = err; break; }
        if (!n) break;
        if (fs_write(o, s_copy, (u32)n, &err) != n) { e = err ? err : 5; break; }
    }
    u16 date, time;
    if (!e && !fs_ftime(in, false, &date, &time)) fs_ftime(o, true, &date, &time);
    fs_close(in); fs_close(o);
    return e;
}
static void dir_part(const char* spec, char* dirout, size_t cap)
{
    d_strcpy(dirout, spec, cap);
    char* cut = dirout;
    for (char* c = dirout; *c; c++) if (*c == '\\' || *c == '/' || *c == ':') cut = c + 1;
    *cut = 0;
}
static bool wild(const char* s) { for (; *s; s++) if (*s == '*' || *s == '?') return true; return false; }

static void cmd_copy(char* args)
{
    char* src = word(&args);
    char* dst = word(&args);
    if (!*src) { msgln(M_REQ, nullptr, 0); return; }
    DosPath p;
    char srcdir[160], from[180], to[180];
    bool dst_dir = true;
    if (*dst) {
        const int e = fs_resolve(dst, &p, false);
        if (e) { error(e); return; }
        dst_dir = p.exists && p.isdir && !p.isdev;
    }
    int e = fs_resolve(src, &p, true);
    if (e) { error(e); return; }
    if (p.exists && p.isdir) { d_strcpy(srcdir, src, sizeof srcdir); d_strcat(srcdir, "\\", sizeof srcdir); d_strcpy(from, srcdir, sizeof from); d_strcat(from, "*.*", sizeof from); }
    else { dir_part(src, srcdir, sizeof srcdir); d_strcpy(from, src, sizeof from); }
    const bool many = wild(from);
    u8 dta[43];
    u32 copied = 0;
    char spec[180];
    d_strcpy(spec, from, sizeof spec);
    // The names are collected first: a copy into the searched directory changes
    // its (sorted) contents, and a running search would meet a file twice.
    enum { MAXCOPY = 1024 };
    static char names[MAXCOPY][13];
    int count = 0;
    for (e = fs_find_first(spec, 0, dta); !e && count < MAXCOPY; e = fs_find_next(dta)) d_strcpy(names[count++], (const char*)dta + 0x1E, 13);
    for (int i = 0; i < count; i++) {
        const char* name = names[i];
        d_strcpy(from, srcdir, sizeof from); d_strcat(from, name, sizeof from);
        if (!*dst) d_strcpy(to, name, sizeof to);
        else if (dst_dir) {
            d_strcpy(to, dst, sizeof to);
            const char last = to[d_strlen(to) - 1];
            if (last != '\\' && last != '/' && last != ':') d_strcat(to, "\\", sizeof to);
            d_strcat(to, name, sizeof to);
        } else d_strcpy(to, dst, sizeof to);
        if (many) { outs(from); nl(); }
        const int ce = copy_file(from, to);
        if (ce) { error(ce); break; }
        copied++;
        if (g_quit) break;
    }
    if (!count) { error(2); return; }
    outs("        "); msgln(M_COPIED, nullptr, copied);
}

static void cmd_del(char* args)
{
    char* spec = word(&args);
    if (!*spec) { msgln(M_REQ, nullptr, 0); return; }
    DosPath p;
    char dir[160], path[180], pattern[180];
    int e = fs_resolve(spec, &p, true);
    if (e) { error(e); return; }
    if (p.exists && p.isdir) { d_strcpy(dir, spec, sizeof dir); d_strcat(dir, "\\", sizeof dir); d_strcpy(pattern, dir, sizeof pattern); d_strcat(pattern, "*.*", sizeof pattern); }
    else { dir_part(spec, dir, sizeof dir); d_strcpy(pattern, spec, sizeof pattern); }
    u8 dta[43];
    u32 done = 0;
    // Deleting changes the directory, so the search starts again each time.
    for (int guard = 0; guard < 4096; guard++) {
        e = fs_find_first(pattern, 0, dta);
        if (e) break;
        d_strcpy(path, dir, sizeof path); d_strcat(path, (const char*)dta + 0x1E, sizeof path);
        e = fs_delete(path);
        if (e) { error(e); return; }
        done++;
    }
    if (!done) error(2);
}

static void cmd_ren(char* args)
{
    char* from = word(&args);
    char* to = word(&args);
    if (!*from || !*to) { msgln(M_REQ, nullptr, 0); return; }
    if (wild(from) || wild(to)) { msgln(M_SYNTAX, nullptr, 0); s_errorlevel = 1; return; }
    char dest[180];
    bool has_dir = false;
    for (const char* c = to; *c; c++) if (*c == '\\' || *c == '/' || *c == ':') has_dir = true;
    if (has_dir) d_strcpy(dest, to, sizeof dest);
    else { dir_part(from, dest, sizeof dest); d_strcat(dest, to, sizeof dest); }
    const int e = fs_rename(from, dest);
    if (e) error(e);
}

static void cmd_set(char* args)
{
    args = skip(args);
    rtrim(args);
    if (!*args) {
        for (const char* p = g_env; *p; p += d_strlen(p) + 1) { outs(p); nl(); }
        return;
    }
    char* eq = args;
    while (*eq && *eq != '=') eq++;
    if (!*eq) {
        const char* v = env_get(args);
        if (v) { d_strupr(args); outs(args); out('='); outs(v); nl(); }
        return;
    }
    *eq = 0;
    rtrim(args);
    if (!env_set(args, eq + 1)) msgln(M_NOENV, nullptr, 0);
}

static void cmd_echo(char* args)
{
    // "ECHO." and "ECHO text": the separator is one character.
    if (*args == '.' || *args == ' ' || *args == '\t') args++;
    char* t = skip(args);
    if (!*t && args[-1] != '.') { msgln(s_echo ? M_ECHO_ON : M_ECHO_OFF, nullptr, 0); return; }
    if (!d_stricmp(t, "ON")) { s_echo = true; return; }
    if (!d_stricmp(t, "OFF")) { s_echo = false; return; }
    outs(args); nl();
}

static bool batch_read_line(Batch* b, u32* pos, char* out_line, int cap)
{
    int h, err;
    u32 np;
    if (fs_open(b->path, 0, &h)) return false;
    fs_seek(h, 0, (int32_t)*pos, &np);
    const long n = fs_read(h, b->raw, sizeof b->raw - 1, &err);
    fs_close(h);
    if (n <= 0) return false;
    long i = 0;
    int o = 0;
    for (; i < n && b->raw[i] != '\n' && b->raw[i] != 0x1A; i++) if (b->raw[i] != '\r' && o < cap - 1) out_line[o++] = b->raw[i];
    out_line[o] = 0;
    if (i < n && b->raw[i] == 0x1A) { *pos += (u32)n; return o > 0; }
    *pos += (u32)(i < n ? i + 1 : i);
    return true;
}

static const char* batch_arg(const Batch* b, int index)
{
    index += b->shift;
    return index < b->nargs ? b->args + b->argoff[index] : "";
}

// %0..%9, %NAME%, %% -> text.
static void expand(const Batch* b, const char* in, char* o, int cap)
{
    int n = 0;
    while (*in && n < cap - 1) {
        if (*in != '%') { o[n++] = *in++; continue; }
        in++;
        if (*in == '%') { o[n++] = *in++; continue; }
        if (b && *in >= '0' && *in <= '9') {
            for (const char* a = batch_arg(b, *in - '0'); *a && n < cap - 1; a++) o[n++] = *a;
            in++;
            continue;
        }
        const char* end = in;
        while (*end && *end != '%' && *end != ' ') end++;
        if (*end == '%' && end > in) {
            char name[64];
            size_t len = (size_t)(end - in);
            if (len > 63) len = 63;
            d_memcpy(name, in, len); name[len] = 0;
            const char* v = env_get(name);
            if (v) for (; *v && n < cap - 1; v++) o[n++] = *v;
            in = end + 1;
            continue;
        }
        if (!b) o[n++] = '%';
    }
    o[n] = 0;
}

static void cmd_goto(char* args)
{
    if (!s_nbatch) return;
    Batch* b = &s_batch[s_nbatch - 1];
    char* label = word(&args);
    if (*label == ':') label++;
    u32 pos = 0;
    char line[260];
    while (batch_read_line(b, &pos, line, sizeof line)) {
        char* p = skip(line);
        if (*p != ':') continue;
        p = skip(p + 1);
        char* w = word(&p);
        if (!d_strnicmp(w, label, 8)) { b->pos = pos; return; }
    }
    msgln(M_LABEL, nullptr, 0);
    b->done = true;
}

static void cmd_if(char* args)
{
    bool negate = false, result = false;
    char* save = args;
    char* w = word(&args);
    if (!d_stricmp(w, "NOT")) { negate = true; save = args; w = word(&args); }
    if (!d_stricmp(w, "ERRORLEVEL")) {
        u32 n = 0;
        d_atou(word(&args), &n);
        result = (u32)s_errorlevel >= n;
    } else if (!d_stricmp(w, "EXIST")) {
        char* name = word(&args);
        u8 dta[43];
        result = *name && fs_find_first(name, 0x10, dta) == 0;
    } else {
        // a==b : `w` may already hold all of it, or blanks surround the ==
        char* eq = w;
        while (*eq && !(eq[0] == '=' && eq[1] == '=')) eq++;
        char* left = w;
        char* right;
        if (*eq) { *eq = 0; right = eq + 2; if (!*right) right = word(&args); }
        else {
            char* next = word(&args);
            if (next[0] != '=' || next[1] != '=') { (void)save; msgln(M_SYNTAX, nullptr, 0); return; }
            right = next + 2;
            if (!*right) right = word(&args);
        }
        result = !d_strcmp(left, right);
    }
    if (result != negate) run_line(args);
}

static int run_batch(const DosPath* bat, const char* typed, const char* args)
{
    if (s_nbatch >= MAXBATCH) { msgln(M_BATNEST, nullptr, 0); return 1; }
    Batch* b = &s_batch[s_nbatch++];
    fs_full_name(bat, b->path, sizeof b->path);
    b->pos = 0; b->done = false; b->shift = 0; b->nargs = 0;
    d_strcpy(b->args, typed, 100);
    d_strcat(b->args, " ", sizeof b->args);
    d_strcat(b->args, args, sizeof b->args);
    for (char* p = b->args; b->nargs < 32;) {
        char* w = word(&p);
        if (!*w) break;
        b->argoff[b->nargs++] = (u8)(w - b->args);
    }
    while (!b->done && !g_quit && !s_abort_batch && !s_exit_requested) {
        static char raw[260];
        if (!batch_read_line(b, &b->pos, raw, sizeof raw)) break;
        expand(b, raw, b->line, LINE);
        char* p = skip(b->line);
        rtrim(p);
        if (!*p || *p == ':') continue;
        if (*p == '@') p++;
        else if (s_echo) {
            int x, y;
            vid_get_cursor(&x, &y);
            if (x && fs_is_con(1)) nl();
            prompt(); outs(p); nl();
        }
        run_line(p);
        if ((dos_last_exit() >> 8) == 1) s_abort_batch = true;      // a program ended by Ctrl+C
        u16 key;
        if (kbd_peek(&key) && lo8(key) == 3) { kbd_get(&key); outs("^C"); nl(); s_abort_batch = true; }
    }
    s_nbatch--;
    return s_errorlevel;
}

// ---- external programs ----------------------------------------------------------------------
static int try_program(const char* dir, const char* token, bool has_ext, DosPath* p)
{
    static const char* const exts[] = {".COM", ".EXE", ".BAT"};
    char path[200];
    for (int i = 0; i < (has_ext ? 1 : 3); i++) {
        d_strcpy(path, dir, sizeof path);
        if (path[0]) { const char last = path[d_strlen(path) - 1]; if (last != '\\' && last != '/' && last != ':') d_strcat(path, "\\", sizeof path); }
        d_strcat(path, token, sizeof path);
        if (!has_ext) d_strcat(path, exts[i], sizeof path);
        if (fs_resolve(path, p, false) || !p->exists || p->isdir || p->isdev) continue;
        const char* ext = p->last;
        while (*ext && *ext != '.') ext++;
        if (!d_strcmp(ext, ".BAT")) return 2;
        if (!d_strcmp(ext, ".COM") || !d_strcmp(ext, ".EXE")) return 1;
    }
    return 0;
}
static int find_program(const char* token, DosPath* p)
{
    bool has_dir = false, has_ext = false;
    for (const char* c = token; *c; c++) {
        if (*c == '\\' || *c == '/' || *c == ':') { has_dir = true; has_ext = false; }
        else if (*c == '.') has_ext = true;
    }
    int kind = try_program("", token, has_ext, p);
    if (kind || has_dir) return kind;
    const char* path = env_get("PATH");
    while (path && *path) {
        char dir[140];
        int n = 0;
        for (; *path && *path != ';'; path++) if (n < 139) dir[n++] = *path;
        dir[n] = 0;
        if (*path == ';') path++;
        if (!n) continue;
        kind = try_program(dir, token, has_ext, p);
        if (kind) return kind;
    }
    return 0;
}

static void run_external(char* token, char* args)
{
    DosPath p;
    const bool call = s_call;
    s_call = false;
    const int kind = find_program(token, &p);
    if (!kind) { msgln(M_BADCMD, nullptr, 0); s_errorlevel = 1; return; }
    if (kind == 2) {
        const bool chained = s_nbatch > 0 && !call;
        run_batch(&p, token, args);
        if (chained) s_batch[s_nbatch - 1].done = true;       // without CALL the first batch file does not resume
        return;
    }
    int code = 0;
    const int e = dos_run(&p, skip(args), &code);
    if (e) {
        if (e == 8) msgln(M_NOMEM, nullptr, 0); else msgln(M_EXEFAIL, token, 0);
        s_errorlevel = 1;
        return;
    }
    s_errorlevel = code;
    int x, y;
    vid_get_cursor(&x, &y);
    if (x && fs_is_con(1)) nl();
}

// ---- command line ---------------------------------------------------------------------------
struct Builtin { const char* name; int id; };
enum { C_DIR, C_CD, C_TYPE, C_COPY, C_DEL, C_REN, C_MD, C_RD, C_CLS, C_VER, C_ECHO, C_SET, C_PATH, C_PROMPT,
       C_DATE, C_TIME, C_MEM, C_HELP, C_EXIT, C_REM, C_PAUSE, C_GOTO, C_IF, C_CALL, C_SHIFT, C_LH, C_WINNT, C_WIN, C_CHARON };
static const Builtin s_builtins[] = {
    {"DIR", C_DIR}, {"CD", C_CD}, {"CHDIR", C_CD}, {"TYPE", C_TYPE}, {"COPY", C_COPY}, {"DEL", C_DEL}, {"ERASE", C_DEL},
    {"REN", C_REN}, {"RENAME", C_REN}, {"MD", C_MD}, {"MKDIR", C_MD}, {"RD", C_RD}, {"RMDIR", C_RD}, {"CLS", C_CLS},
    {"VER", C_VER}, {"ECHO", C_ECHO}, {"SET", C_SET}, {"PATH", C_PATH}, {"PROMPT", C_PROMPT}, {"DATE", C_DATE},
    {"TIME", C_TIME}, {"MEM", C_MEM}, {"HELP", C_HELP}, {"EXIT", C_EXIT}, {"WINNT", C_WINNT}, {"WIN", C_WIN}, {"CHARON", C_CHARON}, {"REM", C_REM}, {"PAUSE", C_PAUSE},
    {"GOTO", C_GOTO}, {"IF", C_IF}, {"CALL", C_CALL}, {"SHIFT", C_SHIFT}, {"LH", C_LH}, {"LOADHIGH", C_LH},
};

static void builtin(int id, char* args)
{
    char t[24];
    PlatTime now;
    int e;
    switch (id) {
    case C_DIR: cmd_dir(args); break;
    case C_CD: cmd_cd(args); break;
    case C_TYPE: cmd_type(args); break;
    case C_COPY: cmd_copy(args); break;
    case C_DEL: cmd_del(args); break;
    case C_REN: cmd_ren(args); break;
    case C_MD: case C_RD: {
        char* name = word(&args);
        if (!*name) { msgln(M_REQ, nullptr, 0); break; }
        e = id == C_MD ? fs_mkdir(name) : fs_rmdir(name);
        if (e) error(e);
        break; }
    case C_CLS: vid_set_mode(3, true); break;
    case C_VER: nl(); msgln(M_BANNER, nullptr, 0); nl(); break;
    case C_ECHO: cmd_echo(args); break;
    case C_SET: cmd_set(args); break;
    case C_PATH: {
        args = skip(args);
        if (*args == '=') args = skip(args + 1);
        rtrim(args);
        if (!*args) { const char* v = env_get("PATH"); outs("PATH="); outs(v ? v : ""); nl(); break; }
        d_strupr(args);
        if (!env_set("PATH", !d_strcmp(args, ";") ? "" : args)) msgln(M_NOENV, nullptr, 0);
        break; }
    case C_PROMPT:
        args = skip(args); rtrim(args);
        if (!env_set("PROMPT", *args ? args : "$P$G")) msgln(M_NOENV, nullptr, 0);
        break;
    case C_DATE: plat_time(&now); fmt_date(now.year, now.month, now.day, t); msgln(M_DATE, t, 0); break;
    case C_TIME: {
        char s2[4];
        plat_time(&now); fmt_time(now.hour, now.minute, t); two(now.second, s2); d_strcat(t, ":", sizeof t); d_strcat(t, s2, sizeof t);
        msgln(M_TIME, t, 0);
        break; }
    case C_MEM: nl(); msgln(M_MEM1, nullptr, 655360); msgln(M_MEM2, nullptr, dos_free_bytes()); nl(); break;
    case C_HELP: msgln(M_HELP, nullptr, 0); break;
    case C_EXIT: s_exit_requested = true; break;
    case C_WINNT:
        if (plat_request_launch(PL_WINNT, "", nullptr, 0)) { msgln(M_NOWINNT, nullptr, 0); break; }
        msgln(M_STARTWINNT, nullptr, 0); s_exit_requested = true;
        break;
    case C_WIN: {
        char* name = word(&args);
        char chosen[64]; chosen[0] = 0;
        if (plat_request_launch(PL_WIN, name, chosen, sizeof chosen)) { msgln(M_NOWINPROF, name, 0); break; }
        msgln(M_STARTWIN, chosen, 0); s_exit_requested = true;
        break; }
    case C_CHARON: s_errorlevel = charon_command(args, s_polish ? 1 : 0); break;
    case C_REM: break;
    case C_PAUSE:
        msg(M_PAUSE, nullptr, 0);
        if (!wait_key()) s_abort_batch = true;
        nl();
        break;
    case C_GOTO: cmd_goto(args); break;
    case C_IF: cmd_if(args); break;
    case C_CALL: s_call = true; run_line(skip(args)); s_call = false; break;
    case C_SHIFT: if (s_nbatch) s_batch[s_nbatch - 1].shift++; break;
    default: run_line(skip(args)); break;
    }
}

// Cuts "> file", ">> file" and "< file" out of the line.
static void take_redirect(char* line, char* in_name, char* out_name, bool* append)
{
    in_name[0] = out_name[0] = 0;
    *append = false;
    bool quoted = false;
    for (char* p = line; *p; p++) {
        if (*p == '"') quoted = !quoted;
        if (quoted || (*p != '>' && *p != '<')) continue;
        char* start = p;
        const bool input = *p == '<';
        p++;
        if (!input && *p == '>') { *append = true; p++; }
        p = skip(p);
        char* name = input ? in_name : out_name;
        int n = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '>' && *p != '<') { if (n < 139) name[n++] = *p; p++; }
        name[n] = 0;
        d_memmove(start, p, d_strlen(p) + 1);
        p = start - 1;
    }
}

static int run_line(char* line)
{
    line = skip(line);
    if (*line == '@') line = skip(line + 1);
    rtrim(line);
    if (!*line || *line == ':') return s_errorlevel;

    // IF and REM keep their text untouched (IF passes the rest to run_line again).
    char first[8];
    int fn = 0;
    for (; fn < 7 && line[fn] && line[fn] != ' ' && line[fn] != '\t'; fn++) first[fn] = d_upper(line[fn]);
    first[fn] = 0;
    const bool plain = !d_strcmp(first, "IF") || !d_strcmp(first, "REM") || !d_strcmp(first, "CALL");

    char in_name[140] = "", out_name[140] = "";
    bool append = false;
    int saved_out = -2, saved_in = -2, h;
    if (!plain) take_redirect(line, in_name, out_name, &append);
    if (out_name[0]) {
        int e = 0;
        u32 pos;
        if (append && !fs_open(out_name, 2, &h)) fs_seek(h, 2, 0, &pos);
        else e = fs_create(out_name, 0, false, &h);
        if (e) { error(e); return s_errorlevel; }
        vid_flush();
        saved_out = fs_redirect(1, h);
        fs_close(h);
    }
    if (in_name[0]) {
        const int e = fs_open(in_name, 0, &h);
        if (e) { if (saved_out != -2) fs_restore(1, saved_out); error(e); return s_errorlevel; }
        saved_in = fs_redirect(0, h);
        fs_close(h);
    }

    rtrim(line);
    if (line[0] && line[1] == ':' && !line[2]) {
        if (!fs_set_drive(d_upper(line[0]) - 'A')) error(15);
    } else if (*line) {
        // Command name: letters only; "CD..", "CD\X", "DIR/W" and "ECHO." are built-ins too.
        int n = 0;
        while ((line[n] >= 'A' && line[n] <= 'Z') || (line[n] >= 'a' && line[n] <= 'z')) n++;
        int id = -1;
        const char next = line[n];
        if (n && n < 10) {
            char name[12];
            for (int i = 0; i < n; i++) name[i] = d_upper(line[i]);
            name[n] = 0;
            for (size_t i = 0; i < sizeof s_builtins / sizeof s_builtins[0]; i++) if (!d_strcmp(name, s_builtins[i].name)) id = s_builtins[i].id;
            if (id >= 0) {
                if (next == '.') { if (!(id == C_ECHO || ((id == C_CD || id == C_DIR) && (line[n + 1] == '.' || !line[n + 1])))) id = -1; }
                else if (next == '\\' || next == '/') { if (id != C_CD && id != C_DIR && id != C_MD && id != C_RD && id != C_TYPE && id != C_DEL) id = -1; }
                else if (next && next != ' ' && next != '\t' && next != '=' && next != ';' && next != ',') id = -1;
            }
        }
        if (id >= 0) { s_errorlevel = (id == C_IF || id == C_REM || id == C_GOTO || id == C_SHIFT || id == C_ECHO || id == C_CALL || id == C_PAUSE || id == C_EXIT) ? s_errorlevel : 0; builtin(id, line + n); }
        else {
            char* rest = line;
            char* token = word(&rest);
            run_external(token, rest);
        }
    }
    if (saved_in != -2) fs_restore(0, saved_in);
    if (saved_out != -2) { vid_flush(); fs_restore(1, saved_out); }
    return s_errorlevel;
}

int shell_command(const char* text)
{
    if (s_line_depth >= 8) return 1;
    char* line = s_lines[s_line_depth++];
    expand(nullptr, text, line, LINE);
    const int r = run_line(line);
    s_line_depth--;
    return r;
}

void shell_interactive(void)
{
    static char typed[8][130];
    if (s_line_depth >= 8) return;
    char* input = typed[s_line_depth];
    while (!g_quit && !s_exit_requested) {
        s_abort_batch = false;
        int x, y;
        vid_get_cursor(&x, &y);
        if (x) { con_putc(13); con_putc(10); }
        prompt();
        const int n = con_readline(input, 128, true, true);
        if (n < 0) continue;
        shell_command(input);
    }
    s_exit_requested = false;
}

int shell_from_program(const char* tail)
{
    while (*tail == ' ') tail++;
    if (tail[0] == '/' && (d_upper(tail[1]) == 'C' || d_upper(tail[1]) == 'K')) {
        const bool keep = d_upper(tail[1]) == 'K';
        const int r = shell_command(tail + 2);
        if (keep) shell_interactive();
        return r;
    }
    shell_interactive();
    return 0;
}

void shell_init(bool polish)
{
    s_polish = polish;
    s_batch = (Batch*)plat_alloc(sizeof(Batch) * MAXBATCH);
}

int dos_main(const DosOptions* options)
{
    g_mem = (u8*)plat_alloc(0x120000);
    if (!g_mem) return 1;
    bios_init();
    vid_init();
    fs_init();
    dos_init();
    shell_init(options->polish);
    if (!plat_drive_ok(fs_cur_drive())) { msgln(M_BADDRIVE, nullptr, 0); vid_sync(); return 1; }
    if (!options->command) { msgln(M_BANNER, nullptr, 0); msgln(M_HINT, nullptr, 0); }
    DosPath p;
    if (!fs_resolve("C:\\AUTOEXEC.BAT", &p, false) && p.exists && !p.isdir) shell_command("C:\\AUTOEXEC.BAT");
    if (options->command) shell_command(options->command);
    else shell_interactive();
    vid_sync();
    return s_errorlevel;
}
