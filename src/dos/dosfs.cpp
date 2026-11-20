// SPDX-License-Identifier: GPL-2.0-or-later
// CharonDOS - drives, DOS path resolution, 8.3 aliases for long names,
// file handles, directory search.
//
// Aliases are computed here, not taken from the disk: a name that is not a
// valid 8.3 name gets STEM~N.EXT, where N is its rank among the names of
// the same directory with the same stem and extension, in case-insensitive
// name order. The result depends only on the directory contents.
#include "dos.h"

enum { DC_MAX = 2048, DC_POOL = 49152, NOFILE = 32, NHANDLE = 48, NSEARCH = 16 };

struct DcEnt { u16 nameoff; char alias[13]; u8 attr; u8 lossy; u32 size; u16 date, time; };
struct OFile { bool used; u8 dev; u8 mode; u8 refs; void* pf; u32 pos; u8 drive; bool written, set_time; u16 date, time; };
struct SearchDir { int drive; bool root; u16 generation; dchar16 path[400]; };

static DcEnt* s_dc;
static dchar16* s_pool;
static int s_dc_n, s_dc_drive = -1;
static u32 s_pool_used;
static bool s_dc_valid;
static dchar16 s_dc_path[400];
static char s_cwd[26][68];
static int s_cur = 2;
static OFile s_oft[NOFILE];
static int8_t s_jft[NHANDLE];
static u16 s_howner[NHANDLE];
static u16 s_owner;
static SearchDir* s_search;
static int s_search_next;
static u16 s_generation;
static char s_conbuf[260];
static int s_conlen, s_conpos;
static PlatDirEnt s_ent;                 // scratch, too large for the stack
static dchar16 s_tmp[400];

void fs_invalidate(void) { s_dc_valid = false; }
int fs_cur_drive(void) { return s_cur; }
bool fs_set_drive(int drive) { if (drive < 0 || drive > 25 || !plat_drive_ok(drive)) return false; s_cur = drive; return true; }
int fs_last_drive(void) { int last = 2; for (int i = 0; i < 26; i++) if (plat_drive_ok(i)) last = i; return last; }
const char* fs_cwd(int drive) { return (drive >= 0 && drive < 26) ? s_cwd[drive] : ""; }

// ---- short names ---------------------------------------------------------------
static bool short_char_ok(u8 c)
{
    if (c <= 0x20 || c == 0x7F) return false;
    switch (c) {
    case '"': case '*': case '+': case ',': case '/': case ':': case ';': case '<':
    case '=': case '>': case '?': case '[': case '\\': case ']': case '|': case '.': return false;
    default: return true;
    }
}
static void make_short(const dchar16* name, char* base, char* ext, bool* lossy)
{
    const int n = (int)d_wlen(name);
    int lastdot = -1, bi = 0, ei = 0;
    *lossy = false;
    for (int i = 1; i < n; i++) if (name[i] == '.') lastdot = i;
    const int base_end = lastdot < 0 ? n : lastdot;
    for (int part = 0; part < 2; part++) {
        const int from = part ? lastdot + 1 : 0, to = part ? n : base_end;
        if (part && lastdot < 0) break;
        for (int i = from; i < to; i++) {
            const dchar16 u = name[i];
            if (u == ' ' || u == '.') { *lossy = true; continue; }
            bool exact;
            u8 c = uni_to_cp437(u, &exact);
            if (!c || !exact) { c = '_'; *lossy = true; }
            c = (u8)d_upper((char)c);
            if (!short_char_ok(c)) { c = '_'; *lossy = true; }
            if (part) { if (ei < 3) ext[ei++] = (char)c; else *lossy = true; }
            else { if (bi < 8) base[bi++] = (char)c; else *lossy = true; }
        }
    }
    if (!bi) { base[bi++] = '_'; *lossy = true; }
    if (lastdot >= 0 && !ei) *lossy = true;
    base[bi] = 0; ext[ei] = 0;
}
static void compose(char* out, const char* base, const char* ext)
{
    d_strcpy(out, base, 13);
    if (ext[0]) { d_strcat(out, ".", 13); d_strcat(out, ext, 13); }
}

static const dchar16* dc_name(int i) { return s_pool + s_dc[i].nameoff; }
static bool alias_taken(const char* alias)
{
    for (int i = 0; i < s_dc_n; i++) if (s_dc[i].alias[0] && !d_strcmp(s_dc[i].alias, alias)) return true;
    return false;
}

static bool dc_load(int drive, const dchar16* path)
{
    if (s_dc_valid && s_dc_drive == drive && !d_wicmp(s_dc_path, path)) return true;
    s_dc_valid = false; s_dc_n = 0; s_pool_used = 0;
    void* dir = plat_opendir(drive, path);
    if (!dir) return false;
    while (s_dc_n < DC_MAX && plat_readdir(dir, &s_ent)) {
        if (s_ent.name[0] == '.' && (!s_ent.name[1] || (s_ent.name[1] == '.' && !s_ent.name[2]))) continue;
        const u32 len = (u32)d_wlen(s_ent.name) + 1;
        if (s_pool_used + len > DC_POOL) break;
        DcEnt* e = &s_dc[s_dc_n++];
        e->nameoff = (u16)s_pool_used;
        d_memcpy(s_pool + s_pool_used, s_ent.name, len * sizeof(dchar16));
        s_pool_used += len;
        e->attr = s_ent.attr; e->size = s_ent.size; e->date = s_ent.date; e->time = s_ent.time;
        e->alias[0] = 0; e->lossy = 0;
    }
    plat_closedir(dir);
    // Shell sort by name.
    for (int gap = s_dc_n / 2; gap > 0; gap /= 2)
        for (int i = gap; i < s_dc_n; i++) {
            const DcEnt t = s_dc[i];
            int j = i;
            for (; j >= gap && d_wicmp(s_pool + s_dc[j - gap].nameoff, s_pool + t.nameoff) > 0; j -= gap) s_dc[j] = s_dc[j - gap];
            s_dc[j] = t;
        }
    char base[9], ext[4], prev_base[9] = "", prev_ext[4] = "";
    bool lossy;
    for (int i = 0; i < s_dc_n; i++) {
        make_short(dc_name(i), base, ext, &lossy);
        s_dc[i].lossy = lossy;
        if (!lossy) compose(s_dc[i].alias, base, ext);
    }
    u32 number = 0;
    for (int i = 0; i < s_dc_n; i++) {
        if (!s_dc[i].lossy) continue;
        make_short(dc_name(i), base, ext, &lossy);
        base[6] = 0;
        if (d_strcmp(base, prev_base) || d_strcmp(ext, prev_ext)) number = 0;
        d_strcpy(prev_base, base, sizeof prev_base); d_strcpy(prev_ext, ext, sizeof prev_ext);
        char cand[13], digits[12], stem[9];
        for (;;) {
            number++;
            const int nd = d_utoa(number, digits, 10);
            d_strcpy(stem, base, sizeof stem);
            if ((int)d_strlen(stem) > 7 - nd) stem[7 - nd] = 0;
            d_strcat(stem, "~", sizeof stem); d_strcat(stem, digits, sizeof stem);
            compose(cand, stem, ext);
            if (!alias_taken(cand)) break;
        }
        d_strcpy(s_dc[i].alias, cand, 13);
    }
    s_dc_drive = drive;
    d_wcpy(s_dc_path, path, 400);
    s_dc_valid = true;
    return true;
}
static int dc_find(const char* alias)
{
    for (int i = 0; i < s_dc_n; i++) if (!d_strcmp(s_dc[i].alias, alias)) return i;
    return -1;
}

static void wappend(dchar16* path, const char* comp)
{
    size_t n = d_wlen(path);
    if (n + 14 >= 400) return;
    path[n++] = '\\';
    for (; *comp; comp++) path[n++] = cp437_to_uni((u8)*comp);
    path[n] = 0;
}
static void wappend_w(dchar16* path, const dchar16* name)
{
    const size_t n = d_wlen(path);
    if (n + d_wlen(name) + 2 >= 400) return;
    path[n] = '\\'; path[n + 1] = 0;
    d_wcat(path, name, 400);
}

// Finds the entry with the DOS name `comp` in a directory; fills s_ent.
static bool lookup(int drive, const dchar16* parent, const char* comp)
{
    bool tilde = false;
    for (const char* p = comp; *p; p++) if (*p == '~') tilde = true;
    if (!tilde) {
        d_wcpy(s_tmp, parent, 400);
        wappend(s_tmp, comp);
        if (plat_stat(drive, s_tmp, &s_ent)) {
            char base[9], ext[4], alias[13];
            bool lossy;
            make_short(s_ent.name, base, ext, &lossy);
            compose(alias, base, ext);
            if (!lossy && !d_strcmp(alias, comp)) return true;
        }
    }
    if (!dc_load(drive, parent)) return false;
    const int i = dc_find(comp);
    if (i < 0) return false;
    d_wcpy(s_ent.name, dc_name(i), 256);
    s_ent.attr = s_dc[i].attr; s_ent.size = s_dc[i].size; s_ent.date = s_dc[i].date; s_ent.time = s_dc[i].time;
    return true;
}

static bool has_wild(const char* s) { for (; *s; s++) if (*s == '*' || *s == '?') return true; return false; }

// "name.ext" in any case and length -> upper case 8.3.
static void normalize(const char* raw, char* out)
{
    int n = 0, i = 0;
    if (raw[0] == '.') { d_strcpy(out, raw, 13); return; }
    for (; raw[i] && raw[i] != '.'; i++) if (n < 8 && raw[i] != ' ') out[n++] = d_upper(raw[i]);
    if (raw[i] == '.') {
        int e = 0;
        const int dot = n;
        out[n++] = '.';
        for (i++; raw[i]; i++) if (raw[i] != '.' && raw[i] != ' ' && e < 3) { out[n++] = d_upper(raw[i]); e++; }
        if (!e) n = dot;
    }
    out[n] = 0;
}

static u8 device_of(const char* comp)
{
    char base[9];
    int n = 0;
    for (; comp[n] && comp[n] != '.' && n < 8; n++) base[n] = comp[n];
    base[n] = 0;
    if (!d_strcmp(base, "CON")) return DEV_CON;
    if (!d_strcmp(base, "NUL")) return DEV_NUL;
    static const char* const aux[] = {"AUX", "PRN", "COM1", "COM2", "COM3", "COM4", "LPT1", "LPT2", "LPT3", "CLOCK$"};
    for (size_t i = 0; i < sizeof aux / sizeof aux[0]; i++) if (!d_strcmp(base, aux[i])) return DEV_AUX;
    return DEV_NONE;
}

int fs_resolve(const char* in, DosPath* o, bool allow_wild)
{
    char comps[24][13];
    int nc = 0;
    o->dos[0] = 0; o->last[0] = 0; o->lpath[0] = 0; o->lparent[0] = 0;
    o->exists = o->isdir = o->isdev = false; o->dev = 0; o->attr = 0; o->size = 0; o->date = o->time = 0;
    while (*in == ' ') in++;
    int drive = s_cur;
    if (in[0] && in[1] == ':') {
        drive = d_upper(in[0]) - 'A';
        in += 2;
    }
    if (drive < 0 || drive > 25 || !plat_drive_ok(drive)) return 15;
    o->drive = drive;
    if (*in != '\\' && *in != '/') {
        const char* c = s_cwd[drive];
        while (*c) {
            if (*c == '\\') c++;
            int n = 0;
            for (; *c && *c != '\\'; c++) if (n < 12) comps[nc][n++] = *c;
            comps[nc][n] = 0;
            if (n && nc < 23) nc++;
        }
    }
    while (*in) {
        while (*in == '\\' || *in == '/') in++;
        if (!*in) break;
        char raw[80];
        int n = 0;
        for (; *in && *in != '\\' && *in != '/'; in++) if (n < 79) raw[n++] = *in;
        while (n && raw[n - 1] == ' ') n--;
        raw[n] = 0;
        if (!n || !d_strcmp(raw, ".")) continue;
        if (!d_strcmp(raw, "..")) { if (nc) nc--; continue; }
        if (nc >= 23) return 3;
        normalize(raw, comps[nc]);
        if (!comps[nc][0]) return 3;
        nc++;
    }
    for (int i = 0; i < nc; i++) {
        if (d_strlen(o->dos) + d_strlen(comps[i]) + 2 >= sizeof o->dos) return 3;
        d_strcat(o->dos, "\\", sizeof o->dos);
        d_strcat(o->dos, comps[i], sizeof o->dos);
    }
    if (!nc) { o->exists = o->isdir = true; o->attr = 0x10; return 0; }
    d_strcpy(o->last, comps[nc - 1], 13);
    const u8 dev = device_of(comps[nc - 1]);
    if (dev) { o->isdev = true; o->dev = dev; o->exists = true; return 0; }
    for (int i = 0; i < nc; i++) {
        const bool last = i == nc - 1;
        if (last) {
            d_wcpy(o->lparent, o->lpath, 400);
            if (has_wild(comps[i])) {
                if (!allow_wild) return 2;
                return 0;
            }
        }
        const bool found = lookup(drive, o->lpath, comps[i]);
        if (!last) {
            if (!found || !(s_ent.attr & 0x10)) return 3;
            wappend_w(o->lpath, s_ent.name);
            continue;
        }
        if (found) {
            wappend_w(o->lpath, s_ent.name);
            o->exists = true; o->attr = s_ent.attr; o->isdir = (s_ent.attr & 0x10) != 0;
            o->size = s_ent.size; o->date = s_ent.date; o->time = s_ent.time;
        } else wappend(o->lpath, comps[i]);
    }
    return 0;
}

void fs_full_name(const DosPath* p, char* out, size_t cap)
{
    char d[3] = {(char)('A' + p->drive), ':', 0};
    d_strcpy(out, d, cap);
    d_strcat(out, p->dos[0] ? p->dos : "\\", cap);
}

// ---- directories and names -------------------------------------------------------
static DosPath s_p, s_q;                 // scratch for the functions below (not re-entrant)

int fs_chdir(const char* path)
{
    const int e = fs_resolve(path, &s_p, false);
    if (e) return e == 2 ? 3 : e;
    if (!s_p.exists || !s_p.isdir || s_p.isdev) return 3;
    if (d_strlen(s_p.dos) > 64) return 3;
    d_strcpy(s_cwd[s_p.drive], s_p.dos, sizeof s_cwd[0]);
    return 0;
}
int fs_mkdir(const char* path)
{
    const int e = fs_resolve(path, &s_p, false);
    if (e) return e == 2 ? 3 : e;
    if (s_p.exists || s_p.isdev) return 5;
    fs_invalidate();
    return plat_mkdir(s_p.drive, s_p.lpath);
}
int fs_rmdir(const char* path)
{
    const int e = fs_resolve(path, &s_p, false);
    if (e) return e == 2 ? 3 : e;
    if (!s_p.exists || !s_p.isdir || s_p.isdev) return 3;
    if (!s_p.dos[0]) return 5;
    if (!d_strcmp(s_p.dos, s_cwd[s_p.drive])) return 16;
    fs_invalidate();
    return plat_rmdir(s_p.drive, s_p.lpath);
}
int fs_delete(const char* path)
{
    const int e = fs_resolve(path, &s_p, false);
    if (e) return e;
    if (!s_p.exists) return 2;
    if (s_p.isdir || s_p.isdev || (s_p.attr & 1)) return 5;
    fs_invalidate();
    return plat_delete(s_p.drive, s_p.lpath);
}
int fs_rename(const char* from, const char* to)
{
    int e = fs_resolve(from, &s_p, false);
    if (e) return e;
    if (!s_p.exists) return 2;
    if (s_p.isdev || !s_p.dos[0]) return 5;
    e = fs_resolve(to, &s_q, false);
    if (e) return e == 2 ? 3 : e;
    if (s_q.drive != s_p.drive) return 17;
    if (s_q.exists || s_q.isdev) return 5;
    fs_invalidate();
    return plat_rename(s_p.drive, s_p.lpath, s_q.lpath);
}
int fs_getattr(const char* path, u8* attr)
{
    const int e = fs_resolve(path, &s_p, false);
    if (e) return e;
    if (!s_p.exists) return 2;
    *attr = s_p.isdev ? 0 : s_p.attr;
    return 0;
}
int fs_setattr(const char* path, u8 attr)
{
    const int e = fs_resolve(path, &s_p, false);
    if (e) return e;
    if (!s_p.exists) return 2;
    if (s_p.isdev || !s_p.dos[0]) return 5;
    fs_invalidate();
    return plat_set_attr(s_p.drive, s_p.lpath, attr) ? 0 : 5;
}

// ---- handles ------------------------------------------------------------------------
static int new_file(void)
{
    for (int i = 0; i < NOFILE; i++) if (!s_oft[i].used) {
        d_memset(&s_oft[i], 0, sizeof s_oft[i]);
        s_oft[i].used = true; s_oft[i].refs = 1;
        return i;
    }
    return -1;
}
static int new_handle(int file)
{
    for (int h = 0; h < NHANDLE; h++) if (s_jft[h] < 0) { s_jft[h] = (int8_t)file; s_howner[h] = s_owner; return h; }
    return -1;
}
static OFile* file_of(int handle)
{
    if (handle < 0 || handle >= NHANDLE || s_jft[handle] < 0) return nullptr;
    return &s_oft[s_jft[handle]];
}
static void release(int file)
{
    OFile* f = &s_oft[file];
    if (--f->refs) return;
    if (f->pf) {
        if (f->set_time) plat_set_ftime(f->pf, f->date, f->time);
        plat_close(f->pf);
    }
    if (f->written || f->set_time) fs_invalidate();
    f->used = false;
}
static int attach(const DosPath* p, void* pf, int mode, int* handle)
{
    const int fi = new_file();
    if (fi < 0) { if (pf) plat_close(pf); return 4; }
    const int h = new_handle(fi);
    if (h < 0) { if (pf) plat_close(pf); s_oft[fi].used = false; return 4; }
    OFile* f = &s_oft[fi];
    f->dev = p->isdev ? p->dev : 0; f->pf = pf; f->mode = (u8)(mode & 3); f->drive = (u8)p->drive;
    f->date = p->date; f->time = p->time;
    if (!p->exists) {
        PlatTime t;
        plat_time(&t);
        f->date = (u16)(((t.year - 1980) << 9) | (t.month << 5) | t.day);
        f->time = (u16)((t.hour << 11) | (t.minute << 5) | (t.second / 2));
    }
    *handle = h;
    return 0;
}
int fs_open(const char* path, int mode, int* handle)
{
    const int e = fs_resolve(path, &s_p, false);
    if (e) return e;
    if (s_p.isdev) return attach(&s_p, nullptr, mode, handle);
    if (!s_p.exists) return 2;
    if (s_p.isdir) return 5;
    if ((mode & 3) == 3) return 12;
    if ((mode & 3) != 0 && (s_p.attr & 1)) return 5;
    void* pf = nullptr;
    const int pe = plat_open(s_p.drive, s_p.lpath, mode & 3, &pf);
    if (pe) return pe;
    return attach(&s_p, pf, mode, handle);
}
int fs_create(const char* path, u8 attr, bool must_be_new, int* handle)
{
    (void)attr;
    const int e = fs_resolve(path, &s_p, false);
    if (e) return e == 2 ? 3 : e;
    if (s_p.isdev) return attach(&s_p, nullptr, 2, handle);
    if (!s_p.dos[0] || s_p.isdir) return 5;
    if (s_p.exists && must_be_new) return 80;
    if (s_p.exists && (s_p.attr & 1)) return 5;
    void* pf = nullptr;
    const int pe = plat_open(s_p.drive, s_p.lpath, PO_RW | PO_CREATE | PO_TRUNC, &pf);
    if (pe) return pe;
    fs_invalidate();
    s_p.exists = false;
    const int r = attach(&s_p, pf, 2, handle);
    if (!r) file_of(*handle)->written = true;
    return r;
}
int fs_close(int handle)
{
    if (!file_of(handle)) return 6;
    release(s_jft[handle]);
    s_jft[handle] = -1;
    return 0;
}
long fs_read(int handle, void* buffer, u32 count, int* err)
{
    OFile* f = file_of(handle);
    *err = 0;
    if (!f) { *err = 6; return -1; }
    if (f->dev == DEV_CON) {
        if (!count) return 0;
        if (s_conpos >= s_conlen) {
            const int n = con_readline(s_conbuf, 255, false, true);
            s_conpos = s_conlen = 0;
            if (n < 0) return 0;
            if (n == 1 && s_conbuf[0] == 0x1A) return 0;
            s_conbuf[n] = 13; s_conbuf[n + 1] = 10;
            s_conlen = n + 2;
        }
        u32 n = (u32)(s_conlen - s_conpos);
        if (n > count) n = count;
        d_memcpy(buffer, s_conbuf + s_conpos, n);
        s_conpos += (int)n;
        return (long)n;
    }
    if (f->dev) return 0;
    if (f->mode == 1) { *err = 5; return -1; }
    if (!plat_seek(f->pf, f->pos)) { *err = 5; return -1; }
    const long n = plat_read(f->pf, buffer, count);
    if (n < 0) { *err = 5; return -1; }
    f->pos += (u32)n;
    return n;
}
long fs_write(int handle, const void* buffer, u32 count, int* err)
{
    OFile* f = file_of(handle);
    *err = 0;
    if (!f) { *err = 6; return -1; }
    if (f->dev == DEV_CON) {
        for (u32 i = 0; i < count; i++) con_putc(((const u8*)buffer)[i]);
        return (long)count;
    }
    if (f->dev) return (long)count;
    if (f->mode == 0) { *err = 5; return -1; }
    f->written = true;
    if (!count) {
        if (!plat_truncate(f->pf, f->pos)) { *err = 5; return -1; }
        return 0;
    }
    if (!plat_seek(f->pf, f->pos)) { *err = 5; return -1; }
    const long n = plat_write(f->pf, buffer, count);
    if (n < 0) { *err = 5; return -1; }
    f->pos += (u32)n;
    return n;
}
int fs_seek(int handle, int whence, int32_t offset, u32* newpos)
{
    OFile* f = file_of(handle);
    if (!f) return 6;
    if (f->dev) { *newpos = 0; return 0; }
    u32 base;
    if (whence == 0) base = 0;
    else if (whence == 1) base = f->pos;
    else if (whence == 2) base = plat_size(f->pf);
    else return 1;
    f->pos = base + (u32)offset;
    *newpos = f->pos;
    return 0;
}
int fs_dup(int handle, int* out)
{
    if (!file_of(handle)) return 6;
    const int h = new_handle(s_jft[handle]);
    if (h < 0) return 4;
    s_oft[s_jft[handle]].refs++;
    *out = h;
    return 0;
}
int fs_dup2(int handle, int target)
{
    if (!file_of(handle) || target < 0 || target >= NHANDLE) return 6;
    if (handle == target) return 0;
    if (s_jft[target] >= 0) release(s_jft[target]);
    s_jft[target] = s_jft[handle];
    s_howner[target] = s_owner;
    s_oft[s_jft[handle]].refs++;
    return 0;
}
int fs_devinfo(int handle, u16* info)
{
    OFile* f = file_of(handle);
    if (!f) return 6;
    if (f->dev == DEV_CON) *info = 0x80D3;
    else if (f->dev == DEV_NUL) *info = 0x8084;
    else if (f->dev) *info = 0x80C0;
    else *info = (u16)(f->drive | (f->written ? 0 : 0x40));
    return 0;
}
int fs_ftime(int handle, bool set, u16* date, u16* time)
{
    OFile* f = file_of(handle);
    if (!f) return 6;
    if (set) { f->date = *date; f->time = *time; if (!f->dev) f->set_time = true; }
    else { *date = f->date; *time = f->time; }
    return 0;
}
bool fs_is_con(int handle) { OFile* f = file_of(handle); return f && f->dev == DEV_CON; }
bool fs_at_eof(int handle)
{
    OFile* f = file_of(handle);
    if (!f || f->dev) return !f || f->dev != DEV_CON;
    return f->pos >= plat_size(f->pf);
}
void fs_set_owner(u16 psp) { s_owner = psp; }
void fs_close_owned(u16 psp)
{
    for (int h = 5; h < NHANDLE; h++) if (s_jft[h] >= 0 && s_howner[h] == psp) fs_close(h);
}
int fs_redirect(int handle, int to)
{
    const int saved = s_jft[handle];
    s_jft[handle] = s_jft[to];
    s_oft[s_jft[to]].refs++;
    return saved;
}
void fs_restore(int handle, int saved)
{
    if (s_jft[handle] >= 0) release(s_jft[handle]);
    s_jft[handle] = (int8_t)saved;
}

// ---- directory search -----------------------------------------------------------------
static void to11(const char* s, u8* out)
{
    d_memset(out, ' ', 11);
    if (s[0] == '.') { out[0] = '.'; if (s[1] == '.') out[1] = '.'; return; }
    int i = 0;
    for (; *s && *s != '.'; s++) {
        if (*s == '*') { while (i < 8) out[i++] = '?'; }
        else if (i < 8) out[i++] = (u8)d_upper(*s);
    }
    if (*s == '.') s++;
    i = 8;
    for (; *s; s++) {
        if (*s == '*') { while (i < 11) out[i++] = '?'; }
        else if (i < 11) out[i++] = (u8)d_upper(*s);
    }
}
static bool match11(const u8* name, const u8* pattern)
{
    for (int i = 0; i < 11; i++) if (pattern[i] != '?' && pattern[i] != name[i]) return false;
    return true;
}
bool fs_match(const char* name83, const char* pattern)
{
    u8 a[11], b[11];
    to11(name83, a); to11(pattern, b);
    return match11(a, b);
}

int fs_find_first(const char* spec, u8 attr, u8* dta)
{
    const int e = fs_resolve(spec, &s_p, true);
    if (e) return e == 2 ? 18 : e;
    if (s_p.isdev || !s_p.last[0]) return 18;
    SearchDir* sd = &s_search[s_search_next];
    sd->drive = s_p.drive; sd->root = s_p.lparent[0] == 0; sd->generation = ++s_generation;
    d_wcpy(sd->path, s_p.lparent, 400);
    d_memset(dta, 0, 43);
    dta[0] = (u8)(s_p.drive + 1);
    to11(s_p.last, dta + 1);
    dta[12] = attr;
    dta[15] = (u8)s_search_next;
    dta[16] = (u8)sd->generation; dta[17] = (u8)(sd->generation >> 8);
    s_search_next = (s_search_next + 1) % NSEARCH;
    fs_invalidate();
    return fs_find_next(dta);
}
int fs_find_next(u8* dta)
{
    if (dta[15] >= NSEARCH) return 18;
    SearchDir* sd = &s_search[dta[15]];
    if (sd->generation != (u16)(dta[16] | (dta[17] << 8))) return 18;
    const u8 attr = dta[12];
    if (attr == 0x08) return 18;
    if (!dc_load(sd->drive, sd->path)) return 18;
    int index = dta[13] | (dta[14] << 8);
    const int dots = sd->root ? 0 : 2;
    for (; index < s_dc_n + dots; index++) {
        u8 eattr; u32 size = 0; u16 date = 0, time = 0;
        const char* name;
        if (index < dots) { name = index ? ".." : "."; eattr = 0x10; }
        else { const DcEnt* de = &s_dc[index - dots]; name = de->alias; eattr = de->attr; size = de->size; date = de->date; time = de->time; }
        if ((eattr & 0x16) & ~attr) continue;
        u8 n11[11];
        to11(name, n11);
        if (!match11(n11, dta + 1)) continue;
        dta[0x15] = eattr;
        dta[0x16] = (u8)time; dta[0x17] = (u8)(time >> 8);
        dta[0x18] = (u8)date; dta[0x19] = (u8)(date >> 8);
        dta[0x1A] = (u8)size; dta[0x1B] = (u8)(size >> 8); dta[0x1C] = (u8)(size >> 16); dta[0x1D] = (u8)(size >> 24);
        d_memset(dta + 0x1E, 0, 13);
        d_strcpy((char*)dta + 0x1E, name, 13);
        index++;
        dta[13] = (u8)index; dta[14] = (u8)(index >> 8);
        return 0;
    }
    dta[13] = (u8)index; dta[14] = (u8)(index >> 8);
    return 18;
}
bool fs_find_long(const u8* dta, dchar16* out, int cap)
{
    out[0] = 0;
    if (dta[15] >= NSEARCH) return false;
    SearchDir* sd = &s_search[dta[15]];
    if (!dc_load(sd->drive, sd->path)) return false;
    const int index = (dta[13] | (dta[14] << 8)) - 1 - (sd->root ? 0 : 2);
    if (index < 0 || index >= s_dc_n) return false;
    d_wcpy(out, dc_name(index), (size_t)cap);
    return s_dc[index].lossy != 0;
}

void fs_init(void)
{
    s_dc = (DcEnt*)plat_alloc(sizeof(DcEnt) * DC_MAX);
    s_pool = (dchar16*)plat_alloc(sizeof(dchar16) * DC_POOL);
    s_search = (SearchDir*)plat_alloc(sizeof(SearchDir) * NSEARCH);
    for (int h = 0; h < NHANDLE; h++) s_jft[h] = -1;
    s_oft[0].used = true; s_oft[0].dev = DEV_CON; s_oft[0].refs = 3; s_oft[0].mode = 2;
    s_oft[1].used = true; s_oft[1].dev = DEV_AUX; s_oft[1].refs = 1; s_oft[1].mode = 2;
    s_oft[2].used = true; s_oft[2].dev = DEV_AUX; s_oft[2].refs = 1; s_oft[2].mode = 2;
    s_jft[0] = s_jft[1] = s_jft[2] = 0; s_jft[3] = 1; s_jft[4] = 2;
    s_cur = plat_drive_ok(2) ? 2 : 0;
    for (int i = 0; i < 26 && !plat_drive_ok(s_cur); i++) s_cur = i;
}
