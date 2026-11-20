// SPDX-License-Identifier: GPL-2.0-or-later
// CharonDOS application catalog: free DOS programs CharonDOS can download and
// install by itself. GENERATED from the host test run of 2026-11-20 (see
// src/dos/APPS-CATALOG.txt for the method, the results and the emulator gaps).
//
// Every entry of kPrograms was run inside the CharonDOS emulator (host build of
// src/dos as of 2026-11-20) laid out exactly as described here and did one
// real operation. kBlockedByStdHandleBug holds programs that work on their own
// but close DOS handles 0-2 when they end; with the single shared handle table
// of the current emulator that silences or misdirects console output of
// everything run afterwards, so they must stay hidden until that is fixed.
// kRejected is the honest rest.
//
// Installing: fetch `url`, check `zipBytes` and `sha256`, then extract each
// File::zipPath (exact, case-sensitive name inside the ZIP) to
// installDir + "\\" + File::dest. `dest` is an upper-case 8.3 path and may
// hold one subdirectory. `run` is typed with installDir as the current
// directory (some programs look for their data there). `requires_` is the id
// of an entry that must be installed too, or nullptr.
//
// Freestanding C++17: <stddef.h> and <stdint.h> only, constant initialisation.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace hakefi::doscatalog {

struct File {
    const char* zipPath;   // path inside the ZIP
    const char* dest;      // path below installDir, upper-case 8.3
};

struct Program {
    const char* id;            // short ASCII identifier
    const char16_t* namePl;    // spoken name, Polish
    const char16_t* nameEn;    // spoken name, English
    const char* group;         // "edit", "files", "view", "tools", "archive", "devel", "games"
    const char* url;           // https, direct ZIP
    uint32_t zipBytes;
    const char* sha256;        // lower-case hex of the tested ZIP
    const File* files;         // ends with {nullptr, nullptr}
    uint32_t installedBytes;   // sum of the listed files
    const char* installDir;    // on the CharonDOS drive
    const char* run;           // command, typed in installDir
    const char* licence;       // SPDX-like
    const char* requires_;     // id of another entry, or nullptr
};

struct Rejected {
    const char* id;
    const char* reason;
};

namespace detail {
inline constexpr File f_edlin[] = {
    {"BIN/EDLIN.EXE", "EDLIN.EXE"},
    {"DOC/EDLIN/edlin.en", "EDLIN.EN"},
    {"DOC/EDLIN/COPYING", "COPYING"},
    {"APPINFO/EDLIN.LSM", "EDLIN.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_e3[] = {
    {"EDIT/E3/E3-16.COM", "E3-16.COM"},
    {"EDIT/E3/README", "README"},
    {"APPINFO/E3.LSM", "E3.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_uhex[] = {
    {"EDIT/UHEX/UHEX.COM", "UHEX.COM"},
    {"EDIT/UHEX/UHEX.TXT", "UHEX.TXT"},
    {"APPINFO/UHEX.LSM", "UHEX.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_doszip[] = {
    {"BIN/DZ.EXE", "DZ.EXE"},
    {"BIN/dz.dos", "DZ.DOS"},
    {"BIN/dz.txt", "DZ.TXT"},
    {"DOC/DOSZIP/LICENSE", "LICENSE"},
    {"APPINFO/DOSZIP.LSM", "DOSZIP.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_tree[] = {
    {"BIN/TREE.COM", "TREE.COM"},
    {"DOC/TREE/README.TXT", "README.TXT"},
    {"DOC/TREE/LICENSE.TXT", "LICENSE.TXT"},
    {"APPINFO/TREE.LSM", "TREE.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_move[] = {
    {"BIN/MOVE.EXE", "MOVE.EXE"},
    {"DOC/MOVE/COPYING.TXT", "COPYING.TXT"},
    {"DOC/MOVE/readme.md", "README.MD"},
    {"APPINFO/MOVE.LSM", "MOVE.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_deltree[] = {
    {"BIN/DELTREE.COM", "DELTREE.COM"},
    {"DOC/DELTREE/DELTREE.TXT", "DELTREE.TXT"},
    {"DOC/DELTREE/COPYING", "COPYING"},
    {"APPINFO/DELTREE.LSM", "DELTREE.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_xcopy[] = {
    {"BIN/XCOPY.EXE", "XCOPY.EXE"},
    {"DOC/XCOPY/xcopy.txt", "XCOPY.TXT"},
    {"DOC/XCOPY/copying.txt", "COPYING.TXT"},
    {"APPINFO/XCOPY.LSM", "XCOPY.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_xdel[] = {
    {"BIN/XDEL.COM", "XDEL.COM"},
    {"APPINFO/XDEL.LSM", "XDEL.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_attrib[] = {
    {"BIN/ATTRIB.COM", "ATTRIB.COM"},
    {"DOC/ATTRIB/COPYING", "COPYING"},
    {"APPINFO/ATTRIB.LSM", "ATTRIB.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_du[] = {
    {"BIN/DU.EXE", "DU.EXE"},
    {"APPINFO/DU.LSM", "DU.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_search[] = {
    {"BIN/SEARCH.EXE", "SEARCH.EXE"},
    {"DOC/SEARCH/README", "README"},
    {"DOC/SEARCH/COPYING", "COPYING"},
    {"APPINFO/SEARCH.LSM", "SEARCH.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_pg[] = {
    {"BIN/PG.EXE", "PG.EXE"},
    {"BIN/PG.MAN", "PG.MAN"},
    {"DOC/PG/COPYING", "COPYING"},
    {"APPINFO/PG.LSM", "PG.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_ambread[] = {
    {"BIN/amb.com", "AMB.COM"},
    {"DOC/AMBREAD/amb.txt", "AMB.TXT"},
    {"APPINFO/AMBREAD.LSM", "AMBREAD.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_fdhelp[] = {
    {"DOC/AMBHELP/fdhelpen.amb", "FDHELPEN.AMB"},
    {"DOC/AMBHELP/FDL.TXT", "FDL.TXT"},
    {"APPINFO/AMBHELP.LSM", "AMBHELP.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_find[] = {
    {"BIN/FIND.COM", "FIND.COM"},
    {"DOC/FIND/README", "README"},
    {"DOC/FIND/COPYING", "COPYING"},
    {"APPINFO/FIND.LSM", "FIND.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_grep[] = {
    {"BIN/GREP.EXE", "GREP.EXE"},
    {"DOC/GREP/GREP.MAN", "GREP.MAN"},
    {"DOC/GREP/COPYING", "COPYING"},
    {"APPINFO/GREP.LSM", "GREP.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_sort[] = {
    {"BIN/SORT.EXE", "SORT.EXE"},
    {"DOC/SORT/SORT.1", "SORT.1"},
    {"APPINFO/SORT.LSM", "SORT.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_comp[] = {
    {"BIN/COMP.COM", "COMP.COM"},
    {"DOC/COMP/COMP.TXT", "COMP.TXT"},
    {"APPINFO/COMP.LSM", "COMP.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_tail[] = {
    {"BIN/TAIL.EXE", "TAIL.EXE"},
    {"APPINFO/TAIL.LSM", "TAIL.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_choice[] = {
    {"BIN/CHOICE.EXE", "CHOICE.EXE"},
    {"DOC/CHOICE/README", "README"},
    {"APPINFO/CHOICE.LSM", "CHOICE.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_cal[] = {
    {"BIN/CAL.COM", "CAL.COM"},
    {"DOC/CAL/CAL.DOC", "CAL.DOC"},
    {"APPINFO/CAL.LSM", "CAL.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_compute[] = {
    {"BIN/COMPUTE.EXE", "COMPUTE.EXE"},
    {"DOC/COMPUTE/COMPUTE.TXT", "COMPUTE.TXT"},
    {"APPINFO/COMPUTE.LSM", "COMPUTE.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_rcal[] = {
    {"UTIL/RCAL/RCAL.COM", "RCAL.COM"},
    {"UTIL/RCAL/RCAL.TXT", "RCAL.TXT"},
    {"APPINFO/RCAL.LSM", "RCAL.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_gnubc[] = {
    {"BIN/BC.EXE", "BC.EXE"},
    {"DOC/GNUBC/README", "README"},
    {"DOC/GNUBC/COPYING", "COPYING"},
    {"DOC/GNUBC/BC.MAN", "BC.MAN"},
    {"APPINFO/GNUBC.LSM", "GNUBC.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_rpn[] = {
    {"BIN/RPN.COM", "RPN.COM"},
    {"BIN/TR.COM", "TR.COM"},
    {"DOC/DOSUTIL/DOSUTILS.TXT", "DOSUTILS.TXT"},
    {"APPINFO/DOSUTIL.LSM", "DOSUTIL.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_md5sum[] = {
    {"BIN/MD5SUM.EXE", "MD5SUM.EXE"},
    {"DOC/MD5SUM/MD5SUM.TXT", "MD5SUM.TXT"},
    {"APPINFO/MD5SUM.LSM", "MD5SUM.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_bsum[] = {
    {"bin/bsum.com", "BSUM.COM"},
    {"doc/dwol/bsum.txt", "BSUM.TXT"},
    {"appinfo/BSUM.LSM", "BSUM.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_nro[] = {
    {"BIN/NRO.COM", "NRO.COM"},
    {"APPINFO/NRO.LSM", "NRO.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_runtime[] = {
    {"BIN/RUNTIME.COM", "RUNTIME.COM"},
    {"APPINFO/RUNTIME.LSM", "RUNTIME.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_zip[] = {
    {"BIN/ZIP16.EXE", "ZIP16.EXE"},
    {"DOC/ZIP/README", "README"},
    {"DOC/ZIP/LICENSE", "LICENSE"},
    {"APPINFO/ZIP.LSM", "ZIP.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_arj[] = {
    {"BIN/ARJ.EXE", "ARJ.EXE"},
    {"DOC/ARJ/README.TXT", "README.TXT"},
    {"APPINFO/ARJ.LSM", "ARJ.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_tinyasm[] = {
    {"DEVEL/TINYASM/TINYASM.EXE", "TINYASM.EXE"},
    {"DEVEL/TINYASM/README", "README"},
    {"DEVEL/TINYASM/LICENSE", "LICENSE"},
    {"APPINFO/TINYASM.LSM", "TINYASM.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_wasm[] = {
    {"DEVEL/wasm/WASM.COM", "WASM.COM"},
    {"DEVEL/wasm/WASM.TXT", "WASM.TXT"},
    {"DEVEL/wasm/LICENSE.TXT", "LICENSE.TXT"},
    {"APPINFO/WASM.LSM", "WASM.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_jwasm[] = {
    {"DEVEL/JWASM/JWasmr.exe", "JWASMR.EXE"},
    {"DEVEL/JWASM/Readme.txt", "README.TXT"},
    {"DEVEL/JWASM/License.txt", "LICENSE.TXT"},
    {"APPINFO/JWASM.LSM", "JWASM.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_gnuchess[] = {
    {"GAMES/GNUCHESS/GNUCHESR.EXE", "GNUCHESR.EXE"},
    {"GAMES/GNUCHESS/MISC/GNUCHESS.BOO", "MISC\\GNUCHESS.BOO"},
    {"GAMES/GNUCHESS/MISC/GNUCHESS.LAN", "MISC\\GNUCHESS.LAN"},
    {"GAMES/GNUCHESS/MISC/GNUCHESS.DAT", "MISC\\GNUCHESS.DAT"},
    {"GAMES/GNUCHESS/README", "README"},
    {"APPINFO/GNUCHESS.LSM", "GNUCHESS.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_row4[] = {
    {"GAMES/ROW4/ROW4T.COM", "ROW4T.COM"},
    {"GAMES/ROW4/ROW4T-EN.TXT", "ROW4T-EN.TXT"},
    {"GAMES/ROW4/COPYING.TXT", "COPYING.TXT"},
    {"APPINFO/ROW4.LSM", "ROW4.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_eliza[] = {
    {"GAMES/ELIZA/ELIZA.EXE", "ELIZA.EXE"},
    {"GAMES/ELIZA/RESPONSE.DAT", "RESPONSE.DAT"},
    {"GAMES/ELIZA/ELIZA.DOC", "ELIZA.DOC"},
    {"APPINFO/ELIZA.LSM", "ELIZA.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_edit[] = {
    {"BIN/EDIT.EXE", "EDIT.EXE"},
    {"BIN/EDIT.HLP", "EDIT.HLP"},
    {"DOC/EDIT/COPYING", "COPYING"},
    {"APPINFO/EDIT.LSM", "EDIT.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_ospedit[] = {
    {"EDIT/OSPEDIT/DOS/OSPEDIT.EXE", "OSPEDIT.EXE"},
    {"EDIT/OSPEDIT/DOS/README.TXT", "README.TXT"},
    {"APPINFO/OSPEDIT.LSM", "OSPEDIT.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_doshexed[] = {
    {"EDIT/DOSHEXED/HEXED.EXE", "HEXED.EXE"},
    {"EDIT/DOSHEXED/HEXED.CFG", "HEXED.CFG"},
    {"EDIT/DOSHEXED/GPLV3.TXT", "GPLV3.TXT"},
    {"APPINFO/DOSHEXED.LSM", "DOSHEXED.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_more[] = {
    {"BIN/MORE.EXE", "MORE.EXE"},
    {"DOC/MORE/README", "README"},
    {"DOC/MORE/GPL-20.TXT", "GPL-20.TXT"},
    {"APPINFO/MORE.LSM", "MORE.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_less[] = {
    {"BIN/LESS.EXE", "LESS.EXE"},
    {"APPINFO/LESS.LSM", "LESS.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_head[] = {
    {"BIN/HEAD.COM", "HEAD.COM"},
    {"DOC/HEAD/GGPL.TXT", "GGPL.TXT"},
    {"APPINFO/HEAD.LSM", "HEAD.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_fc[] = {
    {"BIN/FC.EXE", "FC.EXE"},
    {"DOC/FC/FC.TXT", "FC.TXT"},
    {"DOC/FC/COPYING", "COPYING"},
    {"APPINFO/FC.LSM", "FC.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_replace[] = {
    {"BIN/REPLACE.EXE", "REPLACE.EXE"},
    {"DOC/REPLACE/README.TXT", "README.TXT"},
    {"DOC/REPLACE/COPYING.TXT", "COPYING.TXT"},
    {"APPINFO/REPLACE.LSM", "REPLACE.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_xgrep[] = {
    {"BIN/XGREP.COM", "XGREP.COM"},
    {"DOC/XGREP/XGREP.DOC", "XGREP.DOC"},
    {"DOC/XGREP/README", "README"},
    {"APPINFO/XGREP.LSM", "XGREP.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_b64[] = {
    {"UTIL/B64/B64.EXE", "B64.EXE"},
    {"UTIL/B64/B64.TXT", "B64.TXT"},
    {"APPINFO/B64.LSM", "B64.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_zoo[] = {
    {"BIN/ZOO.EXE", "ZOO.EXE"},
    {"APPINFO/ZOO.LSM", "ZOO.LSM"},
    {nullptr, nullptr},
};
inline constexpr File f_frotz[] = {
    {"frotz255/frotz.exe", "FROTZ.EXE"},
    {"frotz255/frotz.txt", "FROTZ.TXT"},
    {"frotz255/README.txt", "README.TXT"},
    {"frotz255/COPYING.txt", "COPYING.TXT"},
    {nullptr, nullptr},
};
}  // namespace detail

inline constexpr Program kPrograms[] = {
    {"edlin", u"edytor wierszowy EDLIN", u"line editor EDLIN", "edit",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/edlin.zip",
     377009u, "9db8a0b50bc4be5c2358cd0fa1fb6fc77342c6d5175a6b1084547c370e428f8a",
     detail::f_edlin, 56421u, "\\APPS\\EDLIN", "EDLIN.EXE", "GPL-2.0", nullptr},
    {"e3", u"ma\u0142y edytor E3", u"tiny editor E3", "edit",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/edit/e3.zip",
     209743u, "83dd0810cff06bdafb20319292554c399575f871c8edc867eb318f0a94630abb",
     detail::f_e3, 17217u, "\\APPS\\E3", "E3-16.COM", "GPL-2.0", nullptr},
    {"uhex", u"edytor szesnastkowy UHEX", u"hex editor UHEX", "edit",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/edit/uhex.zip",
     36915u, "d72e39957411d43d9a36974137bd325f64e133b8185e6969218f0b762076dca3",
     detail::f_uhex, 14950u, "\\APPS\\UHEX", "UHEX.COM", "BSD-2-Clause", nullptr},
    {"doszip", u"mened\u017cer plik\u00f3w Doszip", u"file manager Doszip", "files",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/apps/doszip.zip",
     388461u, "d1dfa5f050e7282eafb4b8ceca9d1d445629e370994563285ef4cec67f554b37",
     detail::f_doszip, 188314u, "\\APPS\\DOSZIP", "DZ.EXE", "GPL-2.0", nullptr},
    {"tree", u"drzewo katalog\u00f3w TREE", u"directory tree TREE", "files",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/tree.zip",
     110184u, "a3d11398d116654df293eccf6fbbcf543c799bced21f8045056a28cecccfa10b",
     detail::f_tree, 12214u, "\\APPS\\TREE", "TREE.COM", "GPL-2.0-or-later", nullptr},
    {"move", u"przenoszenie plik\u00f3w MOVE", u"file mover MOVE", "files",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/move.zip",
     104281u, "6b1a090f5257d6dde2fc5536b03222f9c538b2500dc66757f9c640e0fc877c03",
     detail::f_move, 41217u, "\\APPS\\MOVE", "MOVE.EXE", "GPL-2.0", nullptr},
    {"deltree", u"usuwanie drzewa katalog\u00f3w DELTREE", u"directory tree remover DELTREE", "files",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/deltree.zip",
     38651u, "af3867c4c2c98369da497ecf50c9a6b45fea1fb5967e9333a3420d5dda5cc521",
     detail::f_deltree, 36015u, "\\APPS\\DELTREE", "DELTREE.COM", "GPL-2.0", nullptr},
    {"xcopy", u"kopiowanie drzew XCOPY", u"tree copier XCOPY", "files",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/xcopy.zip",
     84444u, "d075f4e59c3c7008c0f4d8c0c8b5b2dd06210e7c4e0b53eec646db66f7e5ddaf",
     detail::f_xcopy, 46385u, "\\APPS\\XCOPY", "XCOPY.EXE", "GPL-2.0", nullptr},
    {"xdel", u"usuwanie plik\u00f3w XDEL", u"file remover XDEL", "files",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/util/xdel.zip",
     50928u, "0feb5d642cb60f0a8dd8b2e23668e8f400acdbd294a1c32cb118f56f76d728cb",
     detail::f_xdel, 21611u, "\\APPS\\XDEL", "XDEL.COM", "GPL-2.0", nullptr},
    {"attrib", u"atrybuty plik\u00f3w ATTRIB", u"file attributes ATTRIB", "files",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/attrib.zip",
     25297u, "6a32c47d5319c11f1e748ab320d678704166f34d0cd6ef4b857fdaf182f4ed69",
     detail::f_attrib, 24376u, "\\APPS\\ATTRIB", "ATTRIB.COM", "GPL-2.0", nullptr},
    {"du", u"zaj\u0119to\u015b\u0107 katalog\u00f3w DU", u"disk usage DU", "files",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/unix/du.zip",
     11783u, "cfaf8960e9493e99480d33008ba485f1f871cfd281bd6835b88e8e1e6636b714",
     detail::f_du, 16964u, "\\APPS\\DU", "DU.EXE", "GPL-2.0", nullptr},
    {"search", u"szukanie plik\u00f3w SEARCH", u"file finder SEARCH", "files",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/util/search.zip",
     24446u, "56fb7442c1e675702b39e086438be6f7249b4a11a80c0928a32ffd2e953beb08",
     detail::f_search, 34514u, "\\APPS\\SEARCH", "SEARCH.EXE", "GPL-2.0", nullptr},
    {"pg", u"przegl\u0105darka tekstu PG", u"text viewer PG", "view",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/util/pg.zip",
     65268u, "2e8db4bf4e58b916ef39d71b582e7b09011031e367ec996228ce404262a99da7",
     detail::f_pg, 53096u, "\\APPS\\PG", "PG.EXE", "GPL-2.0", nullptr},
    {"ambread", u"czytnik ksi\u0105\u017cek AMB", u"AMB book reader", "view",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/tools/ambread.zip",
     37057u, "6bf759f63182634e6bc9ea228950cb49ef23b7e452546cd3fd74d141b62b88f7",
     detail::f_ambread, 13567u, "\\APPS\\AMB", "AMB.COM", "MIT", nullptr},
    {"fdhelp", u"pomoc FreeDOS po angielsku", u"FreeDOS help in English", "view",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/ambhelp.zip",
     1468782u, "2571291d621e907e5002b616b9e09171692b8cc32324cd27038d75a7098443b6",
     detail::f_fdhelp, 1492440u, "\\APPS\\AMB", "AMB.COM FDHELPEN.AMB", "GFDL-1.1", "ambread"},
    {"find", u"szukanie tekstu FIND", u"text search FIND", "tools",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/find.zip",
     102855u, "75dd5bf90821b9ecff67fad36d9da342e48bfb1073e1835e53c9bf560ac5099c",
     detail::f_find, 27379u, "\\APPS\\FIND", "FIND.COM", "GPL-2.0", nullptr},
    {"grep", u"szukanie wzorc\u00f3w GREP", u"pattern search GREP", "tools",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/unix/grep.zip",
     226527u, "32cf9391bd24e366337e731a6027421217d2255fa71f735984597debde78f5b0",
     detail::f_grep, 118733u, "\\APPS\\GREP", "GREP.EXE", "GPL-2.0", nullptr},
    {"sort", u"sortowanie wierszy SORT", u"line sorter SORT", "tools",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/sort.zip",
     40259u, "3c9ed55ef7425272c313431ac3e02605d1e9ce1ce1d8e32cefb575e36077d6cf",
     detail::f_sort, 11249u, "\\APPS\\SORT", "SORT.EXE", "GPL-2.0", nullptr},
    {"comp", u"por\u00f3wnywanie plik\u00f3w COMP", u"file compare COMP", "tools",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/comp.zip",
     14743u, "937035db6aa77f5ed412cc9fd6b0ab6309e639d67f475c05928f08dc910dd9e5",
     detail::f_comp, 3670u, "\\APPS\\COMP", "COMP.COM", "MIT", nullptr},
    {"tail", u"koniec pliku TAIL", u"file tail TAIL", "tools",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/unix/tail.zip",
     38822u, "58b6ee676532c6355bccbad2a4436de5e127d1cb00a6d7f28cf446684fa4a4a9",
     detail::f_tail, 56316u, "\\APPS\\TAIL", "TAIL.EXE", "LicenseRef-OpenSource", nullptr},
    {"choice", u"wyb\u00f3r w skryptach CHOICE", u"batch choice prompt CHOICE", "tools",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/choice.zip",
     67707u, "c64ce36b900feaf98741fb1e0a560106928926ab02e8158d403e5fee91edd8aa",
     detail::f_choice, 6333u, "\\APPS\\CHOICE", "CHOICE.EXE", "GPL-2.0", nullptr},
    {"cal", u"kalendarz CAL", u"calendar CAL", "tools",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/unix/cal.zip",
     8373u, "ee05b828b322908217ff422bc007dfb761931ae63d58d7c5eedff528e02b337e",
     detail::f_cal, 2385u, "\\APPS\\CAL", "CAL.COM", "LicenseRef-Freeware-with-source", nullptr},
    {"compute", u"kalkulator wyra\u017ce\u0144 COMPUTE", u"expression calculator COMPUTE", "tools",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/util/compute.zip",
     40063u, "2aad743fcd1e07f3ed8fd4affe7916449fc5cad500d9c6892840993a3e3b56bc",
     detail::f_compute, 31602u, "\\APPS\\COMPUTE", "COMPUTE.EXE", "GPL-2.0-or-later", nullptr},
    {"rcal", u"kalkulator RCAL", u"calculator RCAL", "tools",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/util/rcal.zip",
     90975u, "48079e2c5ece14ace8c9d8802fb5347a83437ede692567a0f35134988a424ec5",
     detail::f_rcal, 24130u, "\\APPS\\RCAL", "RCAL.COM", "MIT", nullptr},
    {"gnubc", u"kalkulator dowolnej precyzji BC", u"arbitrary precision calculator BC", "tools",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/unix/gnubc.zip",
     185943u, "93b5b319cc7261992ea74ee01af739cc1c47539d52910ea57314f5939eba9db9",
     detail::f_gnubc, 126871u, "\\APPS\\BC", "BC.EXE", "GPL-2.0", nullptr},
    {"rpn", u"kalkulator RPN i narz\u0119dzie TR", u"RPN calculator and TR tool", "tools",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/util/dosutil.zip",
     301505u, "33d57d1d3c4573f68c72dcd9a519fb38e9d946c78ea7060748fd927e779daa9c",
     detail::f_rpn, 109654u, "\\APPS\\DOSUTIL", "RPN.COM", "EUPL-1.1", nullptr},
    {"md5sum", u"suma MD5 MD5SUM", u"MD5 checksum MD5SUM", "tools",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/unix/md5sum.zip",
     158001u, "73cf96fae180477c825fcf3dfe6f1a3586ecbda8cc167eb14ccbe2c8e897079d",
     detail::f_md5sum, 38292u, "\\APPS\\MD5SUM", "MD5SUM.EXE", "GPL-2.0", nullptr},
    {"bsum", u"suma kontrolna BSUM", u"checksum BSUM", "tools",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/util/bsum.zip",
     6081u, "4138ec9cc2f3f6c22a6b77b317e70f3f2a60888ae7c70000ac9780518abbc1d8",
     detail::f_bsum, 2861u, "\\APPS\\BSUM", "BSUM.COM", "MIT", nullptr},
    {"nro", u"formatowanie tekstu NRO", u"text formatter NRO", "tools",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/unix/nro.zip",
     42944u, "941a2e787b78d00180028645141bd9baeb1e615ef527240b91d1db405103d010",
     detail::f_nro, 15446u, "\\APPS\\NRO", "NRO.COM", "PD", nullptr},
    {"runtime", u"pomiar czasu RUNTIME", u"execution timer RUNTIME", "tools",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/devel/runtime.zip",
     19077u, "2939abd0ac5fffba772fa3062b34a78bce6d029a5904dd8dbfa8b3fe588d9ac3",
     detail::f_runtime, 3096u, "\\APPS\\RUNTIME", "RUNTIME.COM", "PD", nullptr},
    {"zip", u"archiwizator ZIP", u"ZIP archiver", "archive",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/archiver/zip.zip",
     1719581u, "d5d313b1a2bb33432e9b8aed8628d84a5ec29f23db0cd1e9320197db3804f762",
     detail::f_zip, 102987u, "\\APPS\\ZIP", "ZIP16.EXE", "Info-ZIP", nullptr},
    {"arj", u"archiwizator ARJ", u"archiver ARJ", "archive",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/archiver/arj.zip",
     635203u, "70c6eb04162bab3928137fcfed0672af41291f2f6826cf61db5cae0aa9aa3e1e",
     detail::f_arj, 184249u, "\\APPS\\ARJ", "ARJ.EXE", "GPL-2.0", nullptr},
    {"tinyasm", u"asembler TINYASM", u"assembler TINYASM", "devel",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/devel/tinyasm.zip",
     180716u, "7274ad15e3d5d06a4889123640cd947d66bf6cee29138505714ccf099afd4029",
     detail::f_tinyasm, 40764u, "\\APPS\\TINYASM", "TINYASM.EXE", "BSD-2-Clause", nullptr},
    {"wasm", u"asembler WASM", u"assembler WASM", "devel",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/devel/wasm.zip",
     483564u, "310d9b28bec9d539ccae6d50379089ba0e6a574a400b0a1e862e4041832e2ffc",
     detail::f_wasm, 228388u, "\\APPS\\WASM", "WASM.COM", "MIT", nullptr},
    {"jwasm", u"asembler JWasm", u"assembler JWasm", "devel",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/devel/jwasm.zip",
     1858798u, "71ed201142c1fdd6e4e25ea4670ed43ffa5f849bf4d9607906d5316bd533e3e7",
     detail::f_jwasm, 320891u, "\\APPS\\JWASM", "JWASMR.EXE", "Watcom-1.0", nullptr},
    {"gnuchess", u"szachy GNU Chess", u"GNU Chess", "games",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/games/gnuchess.zip",
     590986u, "62f040ef7e8c3e86c6f71c81838a521a845662b4fb96c5053e4c2fa8c7ed1139",
     detail::f_gnuchess, 482795u, "\\APPS\\GNUCHESS", "GNUCHESR.EXE", "GPL-2.0", nullptr},
    {"row4", u"gra cztery w rz\u0119dzie ROW4", u"four in a row game ROW4", "games",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/games/row4.zip",
     76002u, "594d64331b18234dd5b09c4698dc543e2841565b4cf973784157eaf6830264c8",
     detail::f_row4, 51760u, "\\APPS\\ROW4", "ROW4T.COM", "AGPL-3.0-or-later", nullptr},
    {"eliza", u"rozmowa z ELIZ\u0104", u"ELIZA chatterbot", "games",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/games/eliza.zip",
     15068u, "cb1ddf8532becc8e7c2d1ba20cfaf3b75472c8ee9e69463729031044bf18439f",
     detail::f_eliza, 10820u, "\\APPS\\ELIZA", "ELIZA.EXE", "PD", nullptr},
};
inline constexpr size_t kProgramsCount = sizeof kPrograms / sizeof kPrograms[0];

// Work when run alone; hidden because of emulator gap E1 (see APPS-CATALOG.txt).
inline constexpr Program kBlockedByStdHandleBug[] = {
    {"edit", u"edytor tekstu EDIT", u"text editor EDIT", "edit",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/edit.zip",
     132858u, "6fc7bb68283f22387d0d5f310bf0cfc40af37324ad6bea7a574b57287bbb9841",
     detail::f_edit, 116508u, "\\APPS\\EDIT", "EDIT.EXE", "GPL-2.0", nullptr},
    {"ospedit", u"edytor tekstu OSPEDIT", u"text editor OSPEDIT", "edit",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/edit/ospedit.zip",
     1095767u, "5ce768ae2a73789cc5a8adcaf82009a666440c0181f906f7bb9a5d392ba7188d",
     detail::f_ospedit, 119264u, "\\APPS\\OSPEDIT", "OSPEDIT.EXE", "GPL-2.0", nullptr},
    {"doshexed", u"edytor szesnastkowy HEXED", u"hex editor HEXED", "edit",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/edit/doshexed.zip",
     48918u, "8779186fa4aac0a16eabab0d4c7b057e0a856c80a92e11b24e379629e2c865c6",
     detail::f_doshexed, 61582u, "\\APPS\\HEXED", "HEXED.EXE", "GPL-3.0", nullptr},
    {"more", u"stronicowanie tekstu MORE", u"text pager MORE", "view",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/more.zip",
     69829u, "a968c39b4de3a946f0d8e3138668991b97c328c8c95f2a380e249e6fd11da91e",
     detail::f_more, 26518u, "\\APPS\\MORE", "MORE.EXE", "GPL-2.0", nullptr},
    {"less", u"przegl\u0105darka tekstu LESS", u"text pager LESS", "view",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/unix/less.zip",
     307248u, "5a8e230decfc7ee9fa23e279355ee0f0c48a9cffb815a7635e6827f78aa6c03e",
     detail::f_less, 96201u, "\\APPS\\LESS", "LESS.EXE", "BSD-2-Clause", nullptr},
    {"head", u"pocz\u0105tek pliku HEAD", u"file head HEAD", "tools",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/unix/head.zip",
     20973u, "06ab5fcbeee6df169d633cb13e1437cbb0dabd5b420d7ec004ba2646ccd75e09",
     detail::f_head, 25423u, "\\APPS\\HEAD", "HEAD.COM", "GPL-2.0", nullptr},
    {"fc", u"por\u00f3wnywanie plik\u00f3w FC", u"file compare FC", "tools",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/fc.zip",
     130243u, "bad0b8e74bd9c1d02c3065a2acbd1d4a54a0a7efa0771ce00700556bafa7d48a",
     detail::f_fc, 38307u, "\\APPS\\FC", "FC.EXE", "GPL-2.0", nullptr},
    {"replace", u"zast\u0119powanie plik\u00f3w REPLACE", u"file replacer REPLACE", "files",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/replace.zip",
     28867u, "968d11e2d9f303431b9c818b25bf527079a4952ebc1a03122defd6bf3b89f691",
     detail::f_replace, 43666u, "\\APPS\\REPLACE", "REPLACE.EXE", "GPL-2.0", nullptr},
    {"xgrep", u"szukanie wzorc\u00f3w XGREP", u"pattern search XGREP", "tools",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/unix/xgrep.zip",
     27029u, "66263984c1fc73148eb81799cc0bc4ff1c6085044e0de2df9a57ad300c71a5ef",
     detail::f_xgrep, 31721u, "\\APPS\\XGREP", "XGREP.COM", "0BSD", nullptr},
    {"b64", u"kodowanie Base64 B64", u"Base64 coder B64", "tools",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/util/b64.zip",
     23438u, "ad6453762ca37702f483c5339d0c00788f3e4c3a0d0f8c427cb6e1e8403aa9da",
     detail::f_b64, 15806u, "\\APPS\\B64", "B64.EXE", "MIT", nullptr},
    {"zoo", u"archiwizator ZOO", u"archiver ZOO", "archive",
     "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/archiver/zoo.zip",
     273432u, "f343733dbff0b20b4760eddc9de1c4d20c8e3cdba40afb36703b860bf22cddc4",
     detail::f_zoo, 67955u, "\\APPS\\ZOO", "ZOO.EXE", "PD", nullptr},
    {"frotz", u"interpreter gier tekstowych Frotz", u"text adventure interpreter Frotz", "games",
     "https://ifarchive.org/if-archive/infocom/interpreters/frotz/frotz255.zip",
     75550u, "08f4102752f3e0db26d0b61fc8ff62e18a00f6d37eba46f5bd02bf676165f153",
     detail::f_frotz, 138792u, "\\APPS\\FROTZ", "FROTZ.EXE", "GPL-2.0-or-later", nullptr},
};
inline constexpr size_t kBlockedByStdHandleBugCount = sizeof kBlockedByStdHandleBug / sizeof kBlockedByStdHandleBug[0];

// Candidates that are not offered, with the reason found on 2026-11-20.
inline constexpr Rejected kRejected[] = {
    {"debug", "A, U, D, R work, but G ends DEBUG itself: INT 21h AX=4B01h, AH=55h and AH=5Dh are missing"},
    {"insight", "\"Can't load program\": INT 21h AX=4B01h and AH=55h are missing; Alt+X did not leave"},
    {"tde", "illegal instruction 66h (TDER.EXE is built for the 80386)"},
    {"sayswho", "illegal instruction 0Fh (80386 code)"},
    {"freemacs", "ends at once; calls INT 10h AH=FEh (video buffer query) 1031 times"},
    {"vitetris", "TETRIS16 draws with ANSI escape sequences; the console has no ANSI driver"},
    {"hangman", "sets graphics mode 04h"},
    {"drmind", "sets graphics mode 13h"},
    {"psrinvad", "sets graphics mode 13h"},
    {"sudoku86", "\"no mouse detected\" (INT 33h) and graphics"},
    {"zmiy", "no reaction, runs until the time limit; hooks INT 1Ch, probably reads the keyboard through INT 09h / port 60h; real-time game"},
    {"dn2", "DOS Navigator prints its banner and hangs (1 MB DN.PRG image)"},
    {"zdir", "prints nothing: uses FCB find first, INT 21h AH=11h/12h"},
    {"gzip", "LONG.TXT was replaced by a file the host shows as LONG.TX; gzip -d cannot find it afterwards (data loss)"},
    {"msa", "\"No output file.\" for a valid source; cause not found"},
    {"which", "prints nothing for a program that is on PATH"},
    {"fdbanner", "prints nothing"},
    {"tee", "copies correctly but prints \"Error loading kitten file\" on every run"},
    {"mem", "works; not listed because the CharonDOS shell has MEM built in"},
    {"senet", "board drawn (probes INT 10h AH=4Fh/5Fh/6Fh), play and exit not verified"},
    {"fdtui", "menu bar drawn, nothing more verified"},
    {"stamp", "help works, STAMP file printed nothing; not verified"},
    {"touch", "no error on an existing file, effect not measured; not verified"},
    {"utf8tocp", "only the usage text seen; not verified"},
    {"exe2bin", "only the usage text seen; not verified"},
    {"cpp2ccmt", "only the usage text seen; not verified"},
    {"dog", "LS.COM lists a directory; the shell itself not evaluated"},
    {"bcc", "not tested: needs its LIB and INCLUDE tree and several passes"},
    {"wcd", "not tested: needs a batch wrapper to change directory"},
    {"topspin", "not tested"},
    {"fmines", "not downloaded: ZIP larger than 12 MB"},
    {"bwbasic", "protected mode (DOS/4GW LE), needs a 386 and a DOS extender"},
    {"bwbasic12", "protected mode (DOS/4GW LE, FreeDOS 1.2 package), needs a 386 and a DOS extender"},
    {"bwbasic11", "protected mode (Win32 PE, FreeDOS 1.1 package), needs a 386 and a DOS extender"},
    {"foxcalc", "protected mode (DJGPP), needs a 386 and a DOS extender"},
    {"foxtype", "protected mode (DJGPP), needs a 386 and a DOS extender"},
    {"gnused", "protected mode (DJGPP), needs a 386 and a DOS extender"},
    {"lha", "protected mode (DJGPP), needs a 386 and a DOS extender"},
    {"lua", "protected mode (DJGPP), needs a 386 and a DOS extender"},
    {"mined", "protected mode (DJGPP), needs a 386 and a DOS extender"},
    {"minibox", "protected mode (DJGPP), needs a 386 and a DOS extender"},
    {"msedit", "protected mode (DJGPP), needs a 386 and a DOS extender"},
    {"pico", "protected mode (DJGPP), needs a 386 and a DOS extender"},
    {"tar", "protected mode (DJGPP), needs a 386 and a DOS extender"},
    {"unzip", "protected mode (DJGPP), needs a 386 and a DOS extender"},
    {"unzip12", "protected mode (DJGPP, FreeDOS 1.2 package), needs a 386 and a DOS extender"},
    {"pdtree", "protected mode (PE), needs a 386 and a DOS extender"},
    {"pbox", "protected mode (DJGPP), needs a 386 and a DOS extender"},
    {"hexcomp", "protected mode (DJGPP), needs a 386 and a DOS extender"},
    {"mistral", "protected mode (DJGPP), needs a 386 and a DOS extender"},
    {"bolitare", "protected mode (DJGPP), needs a 386 and a DOS extender"},
};
inline constexpr size_t kRejectedCount = sizeof kRejected / sizeof kRejected[0];

}  // namespace hakefi::doscatalog
