// SPDX-License-Identifier: GPL-2.0-or-later
// Charon: the document core of a speaking text web browser. Turns an HTML (or
// text/plain) response into lines a screen reader can read one by one, with
// numbered links, headings, lists, table rows and form controls, resolves
// URLs (RFC 3986) and builds GET form submissions.
// Pure logic: no allocation, the caller supplies every buffer; when a buffer
// is too small rendering stops cleanly and Page::truncated is set.
//
// Layout of a rendered Page
//   text     UTF-16 pool. Every Line is the range [start, start+length) of it.
//            Consecutive lines are separated in the pool by exactly one unit
//            (the space a line was wrapped at, or '\n' after a line that was
//            ended), except where a word longer than the width was split
//            (no separator). The pool is therefore the readable plain text
//            of the whole page, and FindText() matches across wrapped lines.
//   strings  UTF-8 pool: link URLs exactly as written in the document (entity
//            decoded, trimmed, NOT resolved - use ResolveLink()), form
//            actions, field names, values and labels, anchor names, <base>.
//   links    in document order; a link is "number index+1" for the user. With
//            Options::marker == markerAfter the text "[number]" follows the
//            link text in the line ("Kontakt[12]"); markerBefore puts it in
//            front ("[12]Kontakt"); markerNone leaves the text untouched.
//            Link::textStart/textLength is the link text without the marker.
//   No line is longer than Options::width: a marker is wrapped together with
//   the word it is glued to.
//
// Not handled (by design, see the end of the file for the list): scripts, CSS
// (display:none content is read), UTF-16 documents, POST bodies.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace hakefi::html {

constexpr uint32_t kNone = 0xFFFFFFFFu;    // "no line / link / form / field"
constexpr uint16_t kNoField = 0xFFFF;      // Line::field and Field::form
constexpr uint32_t kTitleChars = 160;      // Page::title capacity, units
constexpr uint32_t kMaxAttribute = 2048;   // longer attribute values are cut
constexpr uint32_t kMaxValue = 8192;       // longer <textarea> text is cut
constexpr uint32_t kMaxDepth = 32;         // nested lists/tables/quotes kept

enum class Charset : uint8_t {
	Auto,         // BOM, then <meta>, then UTF-8 if valid, else Options::fallback
	Utf8,
	Latin1,       // ISO-8859-1, bytes 0x80-0x9F are C1 controls
	Latin2,       // ISO-8859-2
	Windows1250,
	Windows1252,
};

enum Kind : uint8_t {
	kindText,      // paragraph text
	kindHeading,   // level 1-6
	kindListItem,  // level = list nesting depth (1..); ordered items start "N. "
	kindLink,      // paragraph line that consists of links only
	kindForm,      // description of a form control; Line::field names it
	kindPre,       // preformatted
	kindTableRow,  // cells joined with " | "
	kindQuote,     // level = quote nesting depth
	kindRule,      // <hr>
	kindImage,     // text is the alt text (Words::image without one)
};

enum LineFlags : uint16_t {
	lineFirst = 1,      // first line of a block (paragraph, item, row, ...)
	lineContinues = 2,  // continues the previous line (it was wrapped)
	lineLinkOnly = 4,   // everything on the line belongs to links
	lineInQuote = 8,
	lineInList = 16,
	lineInTable = 32,
};

struct Line {
	uint32_t start, length;  // text pool
	uint8_t kind;            // Kind
	uint8_t level;           // heading level, list depth or quote depth
	uint16_t flags;          // LineFlags
	uint32_t firstLink;      // first link whose text starts on this line, or kNone
	uint16_t linkCount;      // links whose text starts on this line
	uint16_t field;          // kindForm: index into Page::fields, else kNoField
};

struct Link {
	uint32_t urlStart, urlLength;    // strings pool, UTF-8, unresolved
	uint32_t line;                   // line the link text starts on
	uint32_t textStart, textLength;  // text pool, without the marker
};

struct Form {
	uint32_t actionStart, actionLength;  // strings pool, unresolved; may be empty
	uint32_t firstField, fieldCount;     // contiguous run in Page::fields
	uint8_t method;                      // 0 GET, 1 POST
};

enum FieldType : uint8_t {
	fieldText,      // text, search, email, url, tel, number, date, ...
	fieldPassword,
	fieldHidden,    // no line
	fieldCheckbox,
	fieldRadio,
	fieldSubmit,    // <input type=submit>, <button>, <input type=image>
	fieldButton,    // type=button: does not submit
	fieldReset,
	fieldFile,      // described, never submitted
	fieldTextarea,
	fieldSelect,    // value/label = the selected (or first) option
	fieldOption,    // follows its fieldSelect; no line; label = visible text
};

enum FieldFlags : uint8_t {
	fieldChecked = 1,   // checkbox/radio; the UI may toggle it before BuildFormGetUrl
	fieldDisabled = 2,
	fieldSelected = 4,  // option
	fieldHasValue = 8,  // a value attribute was present
};

struct Field {
	uint32_t nameStart, nameLength;    // strings pool
	uint32_t valueStart, valueLength;  // strings pool: the default value
	uint32_t labelStart, labelLength;  // strings pool: aria-label/placeholder/
	                                   // title; option text; selected option text
	uint32_t line;                     // first line describing it, or kNone
	uint16_t form;                     // index into Page::forms or kNoField
	uint8_t type;                      // FieldType
	uint8_t flags;                     // FieldFlags
};

// An element id (or <a name>). `position` is a text pool offset; LineAt()
// turns it into the line a "#fragment" jump should land on.
struct Anchor {
	uint32_t nameStart, nameLength;  // strings pool
	uint32_t position;
};

enum Overflow : uint8_t {
	overflowText = 1, overflowLines = 2, overflowLinks = 4, overflowStrings = 8,
	overflowForms = 16, overflowFields = 32,
	overflowAnchors = 64,  // not fatal: later anchors are just not recorded
};

struct Page {
	// Set by the caller before Render(): buffers and capacities. Arrays with
	// capacity 0 may be null (a page that needs them is then truncated,
	// except anchors, which are optional).
	char16_t* text; uint32_t textCapacity;
	Line* lines; uint32_t lineCapacity;
	Link* links; uint32_t linkCapacity;
	char* strings; uint32_t stringCapacity;
	Form* forms; uint32_t formCapacity;
	Field* fields; uint32_t fieldCapacity;
	Anchor* anchors; uint32_t anchorCapacity;
	// Set by Render().
	uint32_t textCount, lineCount, linkCount, stringCount, formCount,
		fieldCount, anchorCount;
	char16_t title[kTitleChars];  // whitespace collapsed, NUL ended
	uint32_t titleLength;
	uint32_t baseStart, baseLength;  // <base href>, strings pool, unresolved
	Charset charset;                 // the one that was used
	bool truncated;                  // a buffer was too small: the page is cut
	uint8_t overflow;                // Overflow bits
};

// Words the renderer writes itself. Replace them to localise the page text.
struct Words {
	const char16_t* image = u"image";
	const char16_t* rule = u"----------";
	const char16_t* edit = u"edit";
	const char16_t* password = u"password";
	const char16_t* checkbox = u"check box";
	const char16_t* radio = u"radio button";
	const char16_t* button = u"button";
	const char16_t* select = u"list";
	const char16_t* textarea = u"text area";
	const char16_t* file = u"file";
	const char16_t* checked = u"checked";
	const char16_t* unchecked = u"not checked";
	const char16_t* submit = u"Submit";
	const char16_t* reset = u"Reset";
	const char16_t* bullet = nullptr;  // prefix of unordered items, e.g. u"*"
};

enum Marker : uint8_t { markerAfter, markerBefore, markerNone };

struct Options {
	uint32_t width = 78;      // columns (UTF-16 units); 0 = never wrap; min 16
	Marker marker = markerAfter;
	Charset fallback = Charset::Windows1252;  // Auto, undeclared, not UTF-8
	bool skipUnlabelledImages = false;  // images without alt/title: no line
	Words words;
};

namespace detail {

// Generated once from the Unicode mapping tables (bytes without a mapping keep
// their value, as Windows does).
inline constexpr uint16_t kCp1250[128] = {
	0x20AC, 0x0081, 0x201A, 0x0083, 0x201E, 0x2026, 0x2020, 0x2021,
	0x0088, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
	0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x0098, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
	0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7,
	0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
	0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
	0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
	0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
	0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
	0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
	0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
	0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
	0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
	0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
	0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9,
};
inline constexpr uint16_t kCp1252[32] = {
	0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
	0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};
inline constexpr uint16_t kLatin2[96] = {
	0x00A0, 0x0104, 0x02D8, 0x0141, 0x00A4, 0x013D, 0x015A, 0x00A7,
	0x00A8, 0x0160, 0x015E, 0x0164, 0x0179, 0x00AD, 0x017D, 0x017B,
	0x00B0, 0x0105, 0x02DB, 0x0142, 0x00B4, 0x013E, 0x015B, 0x02C7,
	0x00B8, 0x0161, 0x015F, 0x0165, 0x017A, 0x02DD, 0x017E, 0x017C,
	0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
	0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
	0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
	0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
	0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
	0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
	0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
	0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9,
};
struct Entity { char name[9]; uint16_t code; };
inline constexpr Entity kEntities[363] = {
	{"AElig", 0x00C6}, {"Aacute", 0x00C1}, {"Abreve", 0x0102}, {"Acirc", 0x00C2},
	{"Agrave", 0x00C0}, {"Alpha", 0x0391}, {"Aogon", 0x0104}, {"Aring", 0x00C5},
	{"Atilde", 0x00C3}, {"Auml", 0x00C4}, {"Beta", 0x0392}, {"Cacute", 0x0106},
	{"Ccaron", 0x010C}, {"Ccedil", 0x00C7}, {"Chi", 0x03A7}, {"Dagger", 0x2021},
	{"Dcaron", 0x010E}, {"Delta", 0x0394}, {"Dstrok", 0x0110}, {"ETH", 0x00D0},
	{"Eacute", 0x00C9}, {"Ecaron", 0x011A}, {"Ecirc", 0x00CA}, {"Egrave", 0x00C8},
	{"Eogon", 0x0118}, {"Epsilon", 0x0395}, {"Eta", 0x0397}, {"Euml", 0x00CB}, {"Gamma", 0x0393},
	{"Gbreve", 0x011E}, {"Hat", 0x005E}, {"Iacute", 0x00CD}, {"Icirc", 0x00CE}, {"Idot", 0x0130},
	{"Igrave", 0x00CC}, {"Iota", 0x0399}, {"Iuml", 0x00CF}, {"Kappa", 0x039A}, {"Lacute", 0x0139},
	{"Lambda", 0x039B}, {"Lcaron", 0x013D}, {"Lstrok", 0x0141}, {"Mu", 0x039C},
	{"Nacute", 0x0143}, {"Ncaron", 0x0147}, {"NewLine", 0x000A}, {"Ntilde", 0x00D1},
	{"Nu", 0x039D}, {"OElig", 0x0152}, {"Oacute", 0x00D3}, {"Ocirc", 0x00D4}, {"Odblac", 0x0150},
	{"Ograve", 0x00D2}, {"Omega", 0x03A9}, {"Omicron", 0x039F}, {"Oslash", 0x00D8},
	{"Otilde", 0x00D5}, {"Ouml", 0x00D6}, {"Phi", 0x03A6}, {"Pi", 0x03A0}, {"Prime", 0x2033},
	{"Psi", 0x03A8}, {"Racute", 0x0154}, {"Rcaron", 0x0158}, {"Rho", 0x03A1}, {"Sacute", 0x015A},
	{"Scaron", 0x0160}, {"Scedil", 0x015E}, {"Sigma", 0x03A3}, {"THORN", 0x00DE}, {"Tab", 0x0009},
	{"Tau", 0x03A4}, {"Tcaron", 0x0164}, {"Tcedil", 0x0162}, {"Theta", 0x0398},
	{"Uacute", 0x00DA}, {"Ucirc", 0x00DB}, {"Udblac", 0x0170}, {"Ugrave", 0x00D9},
	{"Upsilon", 0x03A5}, {"Uring", 0x016E}, {"Uuml", 0x00DC}, {"Xi", 0x039E}, {"Yacute", 0x00DD},
	{"Yuml", 0x0178}, {"Zacute", 0x0179}, {"Zcaron", 0x017D}, {"Zdot", 0x017B}, {"Zeta", 0x0396},
	{"aacute", 0x00E1}, {"abreve", 0x0103}, {"acirc", 0x00E2}, {"acute", 0x00B4},
	{"aelig", 0x00E6}, {"agrave", 0x00E0}, {"alefsym", 0x2135}, {"alpha", 0x03B1},
	{"amp", 0x0026}, {"and", 0x2227}, {"ang", 0x2220}, {"aogon", 0x0105}, {"apos", 0x0027},
	{"aring", 0x00E5}, {"ast", 0x002A}, {"asymp", 0x2248}, {"atilde", 0x00E3}, {"auml", 0x00E4},
	{"bdquo", 0x201E}, {"beta", 0x03B2}, {"brvbar", 0x00A6}, {"bsol", 0x005C}, {"bull", 0x2022},
	{"bullet", 0x2022}, {"cacute", 0x0107}, {"cap", 0x2229}, {"ccaron", 0x010D},
	{"ccedil", 0x00E7}, {"cedil", 0x00B8}, {"cent", 0x00A2}, {"check", 0x2713}, {"chi", 0x03C7},
	{"circ", 0x02C6}, {"clubs", 0x2663}, {"colon", 0x003A}, {"comma", 0x002C}, {"commat", 0x0040},
	{"cong", 0x2245}, {"copy", 0x00A9}, {"copysr", 0x2117}, {"crarr", 0x21B5}, {"cross", 0x2717},
	{"cup", 0x222A}, {"curren", 0x00A4}, {"dArr", 0x21D3}, {"dagger", 0x2020}, {"darr", 0x2193},
	{"dash", 0x2010}, {"dcaron", 0x010F}, {"deg", 0x00B0}, {"delta", 0x03B4}, {"diams", 0x2666},
	{"divide", 0x00F7}, {"dollar", 0x0024}, {"dstrok", 0x0111}, {"eacute", 0x00E9},
	{"ecaron", 0x011B}, {"ecirc", 0x00EA}, {"egrave", 0x00E8}, {"empty", 0x2205},
	{"emsp", 0x2003}, {"ensp", 0x2002}, {"eogon", 0x0119}, {"epsilon", 0x03B5},
	{"equals", 0x003D}, {"equiv", 0x2261}, {"eta", 0x03B7}, {"eth", 0x00F0}, {"euml", 0x00EB},
	{"euro", 0x20AC}, {"excl", 0x0021}, {"exist", 0x2203}, {"female", 0x2640}, {"fnof", 0x0192},
	{"forall", 0x2200}, {"frac12", 0x00BD}, {"frac13", 0x2153}, {"frac14", 0x00BC},
	{"frac23", 0x2154}, {"frac34", 0x00BE}, {"frasl", 0x2044}, {"gamma", 0x03B3},
	{"gbreve", 0x011F}, {"ge", 0x2265}, {"grave", 0x0060}, {"gt", 0x003E}, {"hArr", 0x21D4},
	{"hairsp", 0x200A}, {"half", 0x00BD}, {"harr", 0x2194}, {"hearts", 0x2665},
	{"hellip", 0x2026}, {"horbar", 0x2015}, {"hyphen", 0x2010}, {"iacute", 0x00ED},
	{"icirc", 0x00EE}, {"iexcl", 0x00A1}, {"igrave", 0x00EC}, {"image", 0x2111},
	{"imath", 0x0131}, {"incare", 0x2105}, {"infin", 0x221E}, {"int", 0x222B}, {"iota", 0x03B9},
	{"iquest", 0x00BF}, {"isin", 0x2208}, {"iuml", 0x00EF}, {"kappa", 0x03BA}, {"lArr", 0x21D0},
	{"lacute", 0x013A}, {"lambda", 0x03BB}, {"lang", 0x2329}, {"laquo", 0x00AB}, {"larr", 0x2190},
	{"lbrace", 0x007B}, {"lbrack", 0x005B}, {"lcaron", 0x013E}, {"lceil", 0x2308},
	{"lcub", 0x007B}, {"ldquo", 0x201C}, {"ldquor", 0x201E}, {"le", 0x2264}, {"lfloor", 0x230A},
	{"lowast", 0x2217}, {"lowbar", 0x005F}, {"loz", 0x25CA}, {"lpar", 0x0028}, {"lrm", 0x200E},
	{"lsaquo", 0x2039}, {"lsqb", 0x005B}, {"lsquo", 0x2018}, {"lsquor", 0x201A},
	{"lstrok", 0x0142}, {"lt", 0x003C}, {"macr", 0x00AF}, {"male", 0x2642}, {"mdash", 0x2014},
	{"micro", 0x00B5}, {"middot", 0x00B7}, {"minus", 0x2212}, {"mu", 0x03BC}, {"nabla", 0x2207},
	{"nacute", 0x0144}, {"nbhyp", 0x2011}, {"nbsp", 0x00A0}, {"ncaron", 0x0148},
	{"ndash", 0x2013}, {"ne", 0x2260}, {"ni", 0x220B}, {"not", 0x00AC}, {"notin", 0x2209},
	{"nsub", 0x2284}, {"ntilde", 0x00F1}, {"nu", 0x03BD}, {"num", 0x0023}, {"numero", 0x2116},
	{"numsp", 0x2007}, {"oacute", 0x00F3}, {"ocirc", 0x00F4}, {"odblac", 0x0151},
	{"oelig", 0x0153}, {"ograve", 0x00F2}, {"ohm", 0x2126}, {"oline", 0x203E}, {"omega", 0x03C9},
	{"omicron", 0x03BF}, {"oplus", 0x2295}, {"or", 0x2228}, {"ordf", 0x00AA}, {"ordm", 0x00BA},
	{"oslash", 0x00F8}, {"otilde", 0x00F5}, {"otimes", 0x2297}, {"ouml", 0x00F6},
	{"para", 0x00B6}, {"part", 0x2202}, {"percnt", 0x0025}, {"period", 0x002E},
	{"permil", 0x2030}, {"perp", 0x22A5}, {"phi", 0x03C6}, {"phone", 0x260E}, {"pi", 0x03C0},
	{"piv", 0x03D6}, {"plus", 0x002B}, {"plusmn", 0x00B1}, {"pound", 0x00A3}, {"prime", 0x2032},
	{"prod", 0x220F}, {"prop", 0x221D}, {"psi", 0x03C8}, {"puncsp", 0x2008}, {"quest", 0x003F},
	{"quot", 0x0022}, {"rArr", 0x21D2}, {"racute", 0x0155}, {"radic", 0x221A}, {"rang", 0x232A},
	{"raquo", 0x00BB}, {"rarr", 0x2192}, {"rbrace", 0x007D}, {"rbrack", 0x005D},
	{"rcaron", 0x0159}, {"rceil", 0x2309}, {"rcub", 0x007D}, {"rdquo", 0x201D}, {"real", 0x211C},
	{"reg", 0x00AE}, {"rfloor", 0x230B}, {"rho", 0x03C1}, {"rlm", 0x200F}, {"rpar", 0x0029},
	{"rsaquo", 0x203A}, {"rsqb", 0x005D}, {"rsquo", 0x2019}, {"sacute", 0x015B},
	{"sbquo", 0x201A}, {"scaron", 0x0161}, {"scedil", 0x015F}, {"sdot", 0x22C5}, {"sect", 0x00A7},
	{"semi", 0x003B}, {"shy", 0x00AD}, {"sigma", 0x03C3}, {"sigmaf", 0x03C2}, {"sim", 0x223C},
	{"sol", 0x002F}, {"spades", 0x2660}, {"star", 0x2606}, {"starf", 0x2605}, {"sub", 0x2282},
	{"sube", 0x2286}, {"sum", 0x2211}, {"sup", 0x2283}, {"sup1", 0x00B9}, {"sup2", 0x00B2},
	{"sup3", 0x00B3}, {"supe", 0x2287}, {"szlig", 0x00DF}, {"tau", 0x03C4}, {"tcaron", 0x0165},
	{"tcedil", 0x0163}, {"there4", 0x2234}, {"theta", 0x03B8}, {"thetasym", 0x03D1},
	{"thinsp", 0x2009}, {"thorn", 0x00FE}, {"tilde", 0x02DC}, {"times", 0x00D7},
	{"trade", 0x2122}, {"uArr", 0x21D1}, {"uacute", 0x00FA}, {"uarr", 0x2191}, {"ucirc", 0x00FB},
	{"udblac", 0x0171}, {"ugrave", 0x00F9}, {"uml", 0x00A8}, {"upsih", 0x03D2},
	{"upsilon", 0x03C5}, {"uring", 0x016F}, {"uuml", 0x00FC}, {"verbar", 0x007C},
	{"vert", 0x007C}, {"weierp", 0x2118}, {"xi", 0x03BE}, {"yacute", 0x00FD}, {"yen", 0x00A5},
	{"yuml", 0x00FF}, {"zacute", 0x017A}, {"zcaron", 0x017E}, {"zdot", 0x017C}, {"zeta", 0x03B6},
	{"zwj", 0x200D}, {"zwnj", 0x200C},
};

inline bool Space(uint32_t c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}
inline bool Alpha(uint32_t c) { return (c | 0x20) >= 'a' && (c | 0x20) <= 'z'; }
inline bool Digit(uint32_t c) { return c >= '0' && c <= '9'; }
inline char Lower(char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; }

// `text` (any case) equals the lower-case NUL ended `word`.
inline bool Same(const char* text, size_t length, const char* word)
{
	size_t i = 0;
	for (; i < length && word[i]; i++)
		if (Lower(text[i]) != word[i])
			return false;
	return i == length && !word[i];
}

// One code point of UTF-8; malformed input gives U+FFFD and consumes a byte.
inline uint32_t DecodeUtf8(const uint8_t* in, size_t end, size_t& p)
{
	const uint32_t b = in[p++];
	if (b < 0x80)
		return b;
	uint32_t need, cp, least;
	if (b >= 0xC2 && b <= 0xDF) { need = 1; cp = b & 0x1F; least = 0x80; }
	else if (b >= 0xE0 && b <= 0xEF) { need = 2; cp = b & 0x0F; least = 0x800; }
	else if (b >= 0xF0 && b <= 0xF4) { need = 3; cp = b & 0x07; least = 0x10000; }
	else return 0xFFFD;
	if (end - p < need)
		return 0xFFFD;
	for (uint32_t i = 0; i < need; i++) {
		const uint32_t c = in[p + i];
		if ((c & 0xC0) != 0x80)
			return 0xFFFD;
		cp = (cp << 6) | (c & 0x3F);
	}
	if (cp < least || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
		return 0xFFFD;
	p += need;
	return cp;
}

inline bool ValidUtf8(const uint8_t* in, size_t size)
{
	for (size_t p = 0; p < size;) {
		if (in[p] < 0x80) { p++; continue; }
		if (DecodeUtf8(in, size, p) == 0xFFFD)
			return false;  // (a literal U+FFFD in a page counts as invalid too)
	}
	return true;
}

inline uint32_t DecodeByte(Charset charset, const uint8_t* in, size_t end, size_t& p)
{
	if (charset == Charset::Utf8)
		return DecodeUtf8(in, end, p);
	const uint32_t b = in[p++];
	if (b < 0x80)
		return b;
	switch (charset) {
	case Charset::Latin2: return b < 0xA0 ? b : kLatin2[b - 0xA0];
	case Charset::Windows1250: return kCp1250[b - 0x80];
	case Charset::Windows1252: return b < 0xA0 ? kCp1252[b - 0x80] : b;
	default: return b;
	}
}

// Character reference at in[p] == '&'. Returns the character and moves p past
// the reference, or returns '&' and moves p by one when it is not a reference.
inline uint32_t Reference(const uint8_t* in, size_t end, size_t& p, bool attribute)
{
	size_t q = p + 1;
	p++;
	if (q >= end)
		return '&';
	if (in[q] == '#') {
		q++;
		const bool hex = q < end && (in[q] | 0x20) == 'x';
		if (hex)
			q++;
		uint32_t value = 0, digits = 0;
		for (; q < end; q++, digits++) {
			const uint32_t c = in[q];
			uint32_t d;
			if (Digit(c)) d = c - '0';
			else if (hex && (c | 0x20) >= 'a' && (c | 0x20) <= 'f') d = (c | 0x20) - 'a' + 10;
			else break;
			if (value < 0x110000)
				value = value * (hex ? 16 : 10) + d;
		}
		if (!digits)
			return '&';
		if (q < end && in[q] == ';')
			q++;
		p = q;
		if (value >= 0x80 && value <= 0x9F)
			value = kCp1252[value - 0x80];
		if (value == 0 || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
			value = 0xFFFD;
		return value;
	}
	char name[9];
	uint32_t n = 0;
	while (q < end && (Alpha(in[q]) || Digit(in[q]))) {
		if (n == 8)
			return '&';
		name[n++] = char(in[q++]);
	}
	if (!n)
		return '&';
	name[n] = 0;
	const bool semicolon = q < end && in[q] == ';';
	for (const Entity& e : kEntities) {
		uint32_t i = 0;
		while (i < n && e.name[i] == name[i])
			i++;
		if (i != n || e.name[n])
			continue;
		// Without ';' only the old Latin-1 names count, and never inside
		// an attribute (href="?a=1&copy=2").
		if (!semicolon && (attribute || e.code > 0xFF))
			return '&';
		p = q + (semicolon ? 1 : 0);
		return e.code;
	}
	return '&';
}

enum : uint8_t {
	tUnknown, tBlock, tP, tBr, tHeading, tUl, tOl, tLi, tTable, tTr, tTd,
	tCaption, tBlockquote, tPre, tHr, tA, tImg, tInput, tTextarea, tSelect,
	tOption, tOptgroup, tButton, tForm, tSkip, tTitle, tBase,
};
struct TagName { char name[12]; uint8_t id; };
inline constexpr TagName kTags[] = {
	{"a", tA}, {"p", tP}, {"br", tBr}, {"li", tLi}, {"td", tTd}, {"th", tTd},
	{"tr", tTr}, {"div", tBlock}, {"img", tImg}, {"ul", tUl}, {"ol", tOl},
	{"hr", tHr}, {"pre", tPre}, {"table", tTable}, {"input", tInput},
	{"option", tOption}, {"script", tSkip}, {"style", tSkip}, {"form", tForm},
	{"button", tButton}, {"select", tSelect}, {"textarea", tTextarea},
	{"title", tTitle}, {"base", tBase}, {"blockquote", tBlockquote},
	{"optgroup", tOptgroup}, {"caption", tCaption}, {"menu", tUl}, {"dir", tUl},
	{"section", tBlock}, {"article", tBlock}, {"header", tBlock},
	{"footer", tBlock}, {"nav", tBlock}, {"main", tBlock}, {"aside", tBlock},
	{"address", tBlock}, {"figure", tBlock}, {"figcaption", tBlock},
	{"details", tBlock}, {"summary", tBlock}, {"fieldset", tBlock},
	{"legend", tBlock}, {"dl", tBlock}, {"dt", tBlock}, {"dd", tBlock},
	{"center", tBlock}, {"hgroup", tBlock}, {"dialog", tBlock},
	{"noscript", tBlock}, {"body", tBlock}, {"svg", tSkip}, {"template", tSkip},
	{"iframe", tSkip}, {"noembed", tSkip}, {"noframes", tSkip},
};

enum : uint8_t {
	aHref, aAlt, aTitle, aName, aValue, aType, aAction, aMethod, aStart,
	aChecked, aSelected, aDisabled, aPlaceholder, aAriaLabel, aId, aCount,
	aNone = 0xFF,
};
struct AttributeName { char name[12]; uint8_t id; };
inline constexpr AttributeName kAttributes[] = {
	{"href", aHref}, {"alt", aAlt}, {"title", aTitle}, {"name", aName},
	{"value", aValue}, {"type", aType}, {"action", aAction},
	{"method", aMethod}, {"start", aStart}, {"checked", aChecked},
	{"selected", aSelected}, {"disabled", aDisabled},
	{"placeholder", aPlaceholder}, {"aria-label", aAriaLabel}, {"id", aId},
};

struct Tag {
	char name[12];
	uint8_t id;
	uint8_t heading;  // 1-6 for h1-h6
	bool closing;
	uint32_t present;  // bit per attribute id
	uint32_t clipped;
	uint32_t start[aCount], length[aCount];  // strings pool
	bool Has(uint8_t a) const { return (present >> a) & 1; }
	// A cut URL or name is worse than none; cut text (alt, value) is still text.
	bool Usable(uint8_t a) const
	{
		const bool exact = a == aHref || a == aAction || a == aName || a == aId;
		return Has(a) && !(exact && ((clipped >> a) & 1));
	}
};

enum : uint8_t { ctxList, ctxTable, ctxQuote };
struct Context {
	uint8_t type;
	bool ordered;      // list
	bool rowOpen;      // table: inside <tr>
	uint32_t counter;  // list: number of the last item
	uint32_t cells;    // table: cells started in the row
	uint32_t pending;  // table: " | " separators owed before the next text
	bool filled;       // table: the row has text (leading empty cells are dropped)
};

struct State {
	const uint8_t* in;
	size_t size;
	Page* page;
	const Options* options;
	Charset charset;
	uint32_t width;
	bool stop;           // overflow: nothing more is written
	bool plain;          // text/plain
	// current (unfinished) line = text[lineStart, textCount)
	uint32_t lineStart;
	uint32_t prefixEnd;  // end of the list number; no wrapping before it
	bool pendingSpace, blockFirst, continues;
	uint8_t forceKind;   // kind + 1, 0 = from the context
	uint32_t nextLink;   // first link not yet given to a line
	uint32_t openLink;   // kNone or the link being read
	bool linkPending;    // the open link has no text yet
	uint32_t linkLabelStart, linkLabelLength;  // title / aria-label fallback
	uint8_t heading;
	bool pre, preSkipNewline;
	bool itemPending, itemOrdered;
	uint32_t itemNumber;
	uint32_t field;      // control whose line is being written, or kNone
	bool buttonOpen;
	uint32_t form;       // open form or kNone
	uint32_t select;     // open select or kNone
	uint32_t selectLabelStart, selectLabelLength;
	uint32_t option;     // open option or kNone
	bool optionSpace;
	bool titleSet, baseSet;
	bool keep;           // the tag's attribute strings stay in the pool
	Context context[kMaxDepth];
	uint32_t depth;
};

inline bool Fail(State& s, uint8_t what)
{
	s.page->overflow |= what;
	s.page->truncated = true;
	s.stop = true;
	return false;
}

inline bool PutRaw(State& s, char16_t c)
{
	Page& g = *s.page;
	if (s.stop)
		return false;
	if (g.textCount >= g.textCapacity)
		return Fail(s, overflowText);
	g.text[g.textCount++] = c;
	return true;
}

inline bool PutString(State& s, char c)
{
	Page& g = *s.page;
	if (s.stop)
		return false;
	if (g.stringCount >= g.stringCapacity)
		return Fail(s, overflowStrings);
	g.strings[g.stringCount++] = c;
	return true;
}

inline bool PutStringCp(State& s, uint32_t cp)
{
	if (cp < 0x80)
		return PutString(s, char(cp));
	if (cp < 0x800)
		return PutString(s, char(0xC0 | (cp >> 6))) && PutString(s, char(0x80 | (cp & 0x3F)));
	if (cp < 0x10000)
		return PutString(s, char(0xE0 | (cp >> 12)))
			&& PutString(s, char(0x80 | ((cp >> 6) & 0x3F)))
			&& PutString(s, char(0x80 | (cp & 0x3F)));
	return PutString(s, char(0xF0 | (cp >> 18)))
		&& PutString(s, char(0x80 | ((cp >> 12) & 0x3F)))
		&& PutString(s, char(0x80 | ((cp >> 6) & 0x3F)))
		&& PutString(s, char(0x80 | (cp & 0x3F)));
}

inline uint32_t Digits(uint32_t n)
{
	uint32_t d = 1;
	while (n >= 10) { n /= 10; d++; }
	return d;
}

inline uint32_t MarkerLength(const State& s, uint32_t link)
{
	return s.options->marker == markerNone ? 0 : Digits(link + 1) + 2;
}

inline uint32_t Visible(const Page& g, uint32_t from, uint32_t to)
{
	uint32_t n = 0;
	for (uint32_t i = from; i < to; i++)
		if (g.text[i] != ' ')
			n++;
	return n;
}

inline bool InCell(const State& s)
{
	return s.depth && s.context[s.depth - 1].type == ctxTable
		&& s.context[s.depth - 1].rowOpen;
}

// Stores text[lineStart, end) as a line.
inline bool StoreLine(State& s, uint32_t end)
{
	Page& g = *s.page;
	if (s.stop)
		return false;
	if (g.lineCount >= g.lineCapacity)
		return Fail(s, overflowLines);
	Line& l = g.lines[g.lineCount];
	l.start = s.lineStart;
	l.length = end - s.lineStart;
	uint32_t lists = 0, quotes = 0, tables = 0;
	for (uint32_t i = 0; i < s.depth; i++) {
		if (s.context[i].type == ctxList) lists++;
		else if (s.context[i].type == ctxQuote) quotes++;
		else tables++;
	}
	l.kind = kindText;
	l.level = 0;
	if (s.field != kNone) l.kind = kindForm;
	else if (s.forceKind) l.kind = uint8_t(s.forceKind - 1);
	else if (s.heading) { l.kind = kindHeading; l.level = s.heading; }
	else if (s.pre) l.kind = kindPre;
	else if (s.depth) {
		const Context& c = s.context[s.depth - 1];
		if (c.type == ctxList) { l.kind = kindListItem; l.level = uint8_t(lists > 255 ? 255 : lists); }
		else if (c.type == ctxQuote) { l.kind = kindQuote; l.level = uint8_t(quotes); }
		else if (c.rowOpen) l.kind = kindTableRow;
	}
	l.flags = uint16_t((s.blockFirst ? lineFirst : 0) | (s.continues ? lineContinues : 0)
		| (quotes ? lineInQuote : 0) | (lists ? lineInList : 0) | (tables ? lineInTable : 0));
	l.field = kNoField;
	if (s.field != kNone) {
		if (s.field < kNoField)
			l.field = uint16_t(s.field);
		if (g.fields[s.field].line == kNone)
			g.fields[s.field].line = g.lineCount;
	}
	l.firstLink = kNone;
	l.linkCount = 0;
	while (s.nextLink < g.linkCount && g.links[s.nextLink].textStart < end) {
		if (l.firstLink == kNone)
			l.firstLink = s.nextLink;
		if (l.linkCount < 0xFFFF)
			l.linkCount++;
		g.links[s.nextLink++].line = g.lineCount;
	}
	// Is every visible character part of a link (or its marker)?
	const uint32_t from = s.prefixEnd > s.lineStart && s.prefixEnd <= end ? s.prefixEnd : s.lineStart;
	const uint32_t all = Visible(g, from, end);
	if (all && s.nextLink) {
		uint32_t covered = 0;
		for (uint32_t k = s.nextLink; k-- > 0;) {
			const Link& link = g.links[k];
			const uint32_t mark = MarkerLength(s, k);
			uint32_t a = link.textStart, b;
			if (k == s.openLink)
				b = end;
			else
				b = link.textStart + link.textLength + (s.options->marker == markerAfter ? mark : 0);
			if (s.options->marker == markerBefore)
				a = a >= mark ? a - mark : 0;
			if (b <= from)
				break;
			if (a < from) a = from;
			if (b > end) b = end;
			if (a < b)
				covered += Visible(g, a, b);
		}
		if (covered == all) {
			l.flags |= lineLinkOnly;
			if (l.kind == kindText)
				l.kind = kindLink;
		}
	}
	s.blockFirst = false;
	g.lineCount++;
	return true;
}

// Ends the current line (an empty one only when forced).
inline bool FinishLine(State& s, bool force = false)
{
	Page& g = *s.page;
	bool ok = true;
	if (g.textCount > s.lineStart || force) {
		ok = StoreLine(s, g.textCount) && PutRaw(s, u'\n');
		s.lineStart = s.prefixEnd = g.textCount;
	}
	s.continues = false;
	s.pendingSpace = false;
	s.forceKind = 0;
	return ok;
}

// Appends units that must stay together, wrapping the line first if needed.
// `space`: the unit is a word separator that may become the line separator.
inline bool PutUnits(State& s, const char16_t* units, uint32_t count, bool space)
{
	Page& g = *s.page;
	while (s.width && g.textCount > s.lineStart
		&& g.textCount - s.lineStart + count > s.width) {
		if (space) {
			if (!StoreLine(s, g.textCount) || !PutRaw(s, u' '))
				return false;
			s.lineStart = s.prefixEnd = g.textCount;
			s.continues = true;
			return true;
		}
		uint32_t at = g.textCount;
		bool found = false;
		if (!s.pre) {
			const uint32_t low = s.prefixEnd > s.lineStart ? s.prefixEnd : s.lineStart;
			while (at > low) {
				if (g.text[--at] == ' ') { found = true; break; }
			}
		}
		if (found) {
			if (!StoreLine(s, at))
				return false;
			s.lineStart = at + 1;
		} else {
			if (!StoreLine(s, g.textCount))
				return false;
			s.lineStart = g.textCount;
		}
		s.prefixEnd = s.lineStart;
		s.continues = true;
	}
	for (uint32_t i = 0; i < count; i++)
		if (!PutRaw(s, units[i]))
			return false;
	return true;
}

inline bool PutMarker(State& s, uint32_t link)
{
	char16_t m[12];
	uint32_t n = 0, number = link + 1;
	const uint32_t digits = Digits(number);
	m[n++] = u'[';
	for (uint32_t i = digits; i-- > 0; number /= 10)
		m[1 + i] = char16_t(u'0' + number % 10);
	n += digits;
	m[n++] = u']';
	// One unit at a time: the marker continues the word it follows.
	for (uint32_t i = 0; i < n; i++)
		if (!PutUnits(s, m + i, 1, false))
			return false;
	return true;
}

// Everything owed before a visible character: list number, cell separators,
// the collapsed space, the start of the open link.
inline bool BeginContent(State& s)
{
	Page& g = *s.page;
	if (s.stop)
		return false;
	if (s.itemPending) {
		s.itemPending = false;
		if (s.itemOrdered) {
			char16_t d[12];
			uint32_t number = s.itemNumber;
			const uint32_t digits = Digits(number);
			for (uint32_t i = digits; i-- > 0; number /= 10)
				d[i] = char16_t(u'0' + number % 10);
			d[digits] = u'.';
			d[digits + 1] = u' ';
			if (!PutUnits(s, d, digits + 2, false))
				return false;
			s.prefixEnd = g.textCount;
		} else if (s.options->words.bullet && s.options->words.bullet[0]) {
			for (const char16_t* b = s.options->words.bullet; *b; b++)
				if (!PutUnits(s, b, 1, false))
					return false;
			if (!PutUnits(s, u" ", 1, false))
				return false;
			s.prefixEnd = g.textCount;
		}
	}
	if (s.depth && s.context[s.depth - 1].type == ctxTable) {
		Context& c = s.context[s.depth - 1];
		if (c.rowOpen && !c.filled)
			c.pending = 0;
		c.filled = true;
		for (; c.pending; c.pending--) {
			if (g.textCount > s.prefixEnd && !PutUnits(s, u" ", 1, true))
				return false;
			if (!PutUnits(s, u"|", 1, false))
				return false;
			s.pendingSpace = true;
		}
	}
	if (s.pendingSpace) {
		s.pendingSpace = false;
		if (g.textCount > s.prefixEnd && !PutUnits(s, u" ", 1, true))
			return false;
	}
	if (s.linkPending) {
		if (s.options->marker == markerBefore && !PutMarker(s, s.openLink))
			return false;
		g.links[s.openLink].textStart = g.textCount;
		s.linkPending = false;
	}
	return true;
}

inline bool PutVisible(State& s, uint32_t cp)
{
	if (!BeginContent(s))
		return false;
	char16_t u[2];
	uint32_t n = 1;
	if (cp >= 0x10000) {
		cp -= 0x10000;
		u[0] = char16_t(0xD800 + (cp >> 10));
		u[1] = char16_t(0xDC00 + (cp & 0x3FF));
		n = 2;
	} else {
		u[0] = char16_t(cp);
	}
	return PutUnits(s, u, n, false);
}

// Characters that are never read: controls, soft hyphen, zero-width marks.
inline bool Ignorable(uint32_t cp)
{
	return cp < 0x20 || cp == 0x7F || cp == 0xAD || (cp >= 0x200B && cp <= 0x200F)
		|| cp == 0xFEFF || cp == 0x2060 || (cp >= 0x202A && cp <= 0x202E)
		|| (cp >= 0x80 && cp < 0xA0);
}

// Flowing text: whitespace (and no-break space) collapses to one space.
inline void Flow(State& s, uint32_t cp)
{
	if (Space(cp) || cp == 0xA0 || cp == 0x2028 || cp == 0x2029) {
		s.pendingSpace = true;
		return;
	}
	if (!Ignorable(cp))
		PutVisible(s, cp);
}

inline void FlowWord(State& s, const char16_t* word)
{
	if (word)
		for (; *word; word++)
			Flow(s, *word);
}

// A range of the strings pool (valid UTF-8 written by PutStringCp).
inline void FlowString(State& s, uint32_t start, uint32_t length)
{
	const Page& g = *s.page;
	size_t p = start;
	const size_t end = size_t(start) + length;
	while (p < end && !s.stop)
		Flow(s, DecodeUtf8(reinterpret_cast<const uint8_t*>(g.strings), end, p));
}

inline bool BlankString(const State& s, uint32_t start, uint32_t length)
{
	for (uint32_t i = 0; i < length; i++)
		if (!Space(uint8_t(s.page->strings[start + i])))
			return false;
	return true;
}

inline void Break(State& s, bool block)
{
	FinishLine(s);
	if (block) {
		s.blockFirst = true;
		if (s.buttonOpen) {
			s.buttonOpen = false;
			s.field = kNone;
		}
	}
}

// A block boundary that must not split a table row.
inline void SoftBreak(State& s)
{
	if (InCell(s))
		s.pendingSpace = true;
	else
		Break(s, true);
}

inline void CloseOption(State& s)
{
	Page& g = *s.page;
	if (s.option == kNone)
		return;
	Field& o = g.fields[s.option];
	o.labelLength = g.stringCount - o.labelStart;
	if (!(o.flags & fieldHasValue)) {
		o.valueStart = o.labelStart;
		o.valueLength = o.labelLength;
	}
	if (s.select != kNone) {
		Field& sel = g.fields[s.select];
		if ((o.flags & fieldSelected) || !(sel.flags & fieldHasValue)) {
			sel.valueStart = o.valueStart;
			sel.valueLength = o.valueLength;
			sel.labelStart = o.labelStart;
			sel.labelLength = o.labelLength;
			sel.flags |= fieldHasValue;
		}
	}
	s.option = kNone;
}

// Writes the line of a form control: "word label: value suffix".
inline void ControlLine(State& s, uint32_t field, const char16_t* word,
	uint32_t labelStart, uint32_t labelLength, uint32_t valueStart,
	uint32_t valueLength, const char16_t* suffix)
{
	FinishLine(s);
	s.buttonOpen = false;
	s.field = field;
	FlowWord(s, word);
	if (labelLength && !BlankString(s, labelStart, labelLength)) {
		s.pendingSpace = true;
		FlowString(s, labelStart, labelLength);
	}
	if (valueLength && !BlankString(s, valueStart, valueLength)) {
		FlowWord(s, u":");
		s.pendingSpace = true;
		FlowString(s, valueStart, valueLength);
	}
	if (suffix && suffix[0]) {
		s.pendingSpace = true;
		FlowWord(s, suffix);
	}
	FinishLine(s);
	s.field = kNone;
}

inline void CloseSelect(State& s)
{
	Page& g = *s.page;
	if (s.select == kNone)
		return;
	CloseOption(s);
	const uint32_t f = s.select;
	s.select = kNone;
	if (s.stop)
		return;
	ControlLine(s, f, s.options->words.select, s.selectLabelStart,
		s.selectLabelLength, g.fields[f].labelStart, g.fields[f].labelLength, nullptr);
}

inline void CloseLink(State& s)
{
	Page& g = *s.page;
	if (s.openLink == kNone)
		return;
	const uint32_t index = s.openLink;
	if (s.linkPending && s.linkLabelLength && !s.stop
		&& !BlankString(s, s.linkLabelStart, s.linkLabelLength))
		FlowString(s, s.linkLabelStart, s.linkLabelLength);
	s.openLink = kNone;
	if (s.linkPending) {  // no text at all: not a link the user could pick
		s.linkPending = false;
		g.linkCount = index;
		return;
	}
	Link& l = g.links[index];
	if (s.options->marker != markerAfter || s.stop) {
		l.textLength = g.textCount - l.textStart;
		return;
	}
	// The link text ended with the line (<a><div>text</div></a>): put the
	// marker on that line when it fits.
	if (g.textCount == s.lineStart && g.lineCount) {
		Line& last = g.lines[g.lineCount - 1];
		if (last.start + last.length + 1 == g.textCount && last.length
			&& l.textStart < last.start + last.length
			&& (!s.width || last.length + MarkerLength(s, index) <= s.width)) {
			g.textCount--;
			l.textLength = g.textCount - l.textStart;
			const uint32_t before = g.textCount;
			const uint32_t width = s.width;
			s.lineStart = s.prefixEnd = g.textCount;  // no wrapping here
			s.width = 0;
			PutMarker(s, index);
			s.width = width;
			last.length += g.textCount - before;
			PutRaw(s, u'\n');
			s.lineStart = s.prefixEnd = g.textCount;
			return;
		}
	}
	// A link that ended on an earlier line than the current one keeps its
	// text range up to that line's end.
	uint32_t end = g.textCount;
	if (g.textCount == s.lineStart && end > l.textStart
		&& (g.text[end - 1] == u'\n' || g.text[end - 1] == u' '))
		end--;
	l.textLength = end > l.textStart ? end - l.textStart : 0;
	PutMarker(s, index);
}

inline bool PopTo(State& s, uint8_t type, bool crossTables)
{
	for (uint32_t i = s.depth; i-- > 0;) {
		if (s.context[i].type == type) {
			s.depth = i;
			return true;
		}
		if (s.context[i].type == ctxTable && !crossTables)
			return false;
	}
	return false;
}

// Index of the innermost table, or kNone.
inline uint32_t InnerTable(const State& s)
{
	for (uint32_t i = s.depth; i-- > 0;)
		if (s.context[i].type == ctxTable)
			return i;
	return kNone;
}

inline void Push(State& s, uint8_t type, bool ordered, uint32_t counter)
{
	if (s.depth >= kMaxDepth)
		return;
	Context& c = s.context[s.depth++];
	c.type = type;
	c.ordered = ordered;
	c.rowOpen = false;
	c.counter = counter;
	c.cells = 0;
	c.pending = 0;
	c.filled = false;
}

inline uint32_t Number(const State& s, uint32_t start, uint32_t length, uint32_t fallback)
{
	uint32_t value = 0, i = 0;
	const char* t = s.page->strings + start;
	for (; i < length && Digit(uint8_t(t[i])); i++) {
		if (value > 100000000)
			return fallback;
		value = value * 10 + uint32_t(t[i] - '0');
	}
	return i ? value : fallback;
}

// Index of "</name" at or after `from`, or size.
inline size_t FindClose(const State& s, size_t from, const char* name)
{
	size_t n = 0;
	while (name[n])
		n++;
	for (size_t i = from; i + 2 + n <= s.size; i++) {
		if (s.in[i] != '<' || s.in[i + 1] != '/')
			continue;
		size_t k = 0;
		while (k < n && Lower(char(s.in[i + 2 + k])) == name[k])
			k++;
		if (k != n)
			continue;
		const size_t after = i + 2 + n;
		if (after >= s.size || !(Alpha(s.in[after]) || Digit(s.in[after])))
			return i;
	}
	return s.size;
}

inline size_t AfterTag(const State& s, size_t from)
{
	while (from < s.size && s.in[from] != '>')
		from++;
	return from < s.size ? from + 1 : s.size;
}

inline uint32_t NextText(const State& s, size_t& p, size_t end, bool attribute)
{
	if (s.in[p] == '&')
		return Reference(s.in, end, p, attribute);
	return DecodeByte(s.charset, s.in, end, p);
}

inline uint32_t AddField(State& s, const Tag& t, uint8_t type)
{
	Page& g = *s.page;
	if (g.fieldCount >= g.fieldCapacity) {
		Fail(s, overflowFields);
		return kNone;
	}
	Field& f = g.fields[g.fieldCount];
	memset(&f, 0, sizeof f);
	if (t.Usable(aName)) { f.nameStart = t.start[aName]; f.nameLength = t.length[aName]; }
	if (t.Usable(aValue)) {
		f.valueStart = t.start[aValue];
		f.valueLength = t.length[aValue];
		f.flags |= fieldHasValue;
	}
	static constexpr uint8_t kLabels[] = {aAriaLabel, aPlaceholder, aTitle};
	for (const uint8_t a : kLabels) {
		if (t.Usable(a) && t.length[a]) {
			f.labelStart = t.start[a];
			f.labelLength = t.length[a];
			break;
		}
	}
	if (t.Has(aChecked)) f.flags |= fieldChecked;
	if (t.Has(aDisabled)) f.flags |= fieldDisabled;
	if (t.Has(aSelected)) f.flags |= fieldSelected;
	f.line = kNone;
	f.type = type;
	f.form = kNoField;
	if (s.form != kNone) {
		f.form = uint16_t(s.form);
		Form& form = g.forms[s.form];
		if (!form.fieldCount)
			form.firstField = g.fieldCount;
		form.fieldCount = g.fieldCount + 1 - form.firstField;
	}
	s.keep = true;
	return g.fieldCount++;
}

inline void StartTag(State& s, const Tag& t, size_t& p)
{
	Page& g = *s.page;
	const Words& w = s.options->words;
	switch (t.id) {
	case tBlock:
		SoftBreak(s);
		break;
	case tP:
		if (!InCell(s))
			s.heading = 0;
		SoftBreak(s);
		break;
	case tBr:
		if (s.pre)
			FinishLine(s, true);
		else if (InCell(s))
			s.pendingSpace = true;
		else
			FinishLine(s);
		break;
	case tHeading:
		Break(s, true);
		s.heading = t.heading;
		break;
	case tUl:
	case tOl: {
		Break(s, true);
		s.heading = 0;
		s.itemPending = false;
		uint32_t first = 1;
		if (t.Usable(aStart))
			first = Number(s, t.start[aStart], t.length[aStart], 1);
		Push(s, ctxList, t.id == tOl, first - 1);
		break;
	}
	case tLi: {
		Break(s, true);
		s.heading = 0;
		s.itemPending = false;
		for (uint32_t i = s.depth; i-- > 0;) {
			if (s.context[i].type == ctxTable)
				break;
			if (s.context[i].type == ctxList) {
				s.depth = i + 1;  // closes quotes left open in the last item
				Context& c = s.context[i];
				if (t.Usable(aValue))
					c.counter = Number(s, t.start[aValue], t.length[aValue], c.counter + 1);
				else
					c.counter++;
				s.itemPending = true;
				s.itemOrdered = c.ordered;
				s.itemNumber = c.counter;
				break;
			}
		}
		break;
	}
	case tTable:
		Break(s, true);
		s.heading = 0;
		Push(s, ctxTable, false, 0);
		break;
	case tTr: {
		Break(s, true);
		s.heading = 0;
		const uint32_t table = InnerTable(s);
		if (table != kNone) {
			s.depth = table + 1;
			Context& c = s.context[table];
			c.rowOpen = true;
			c.cells = 0;
			c.pending = 0;
			c.filled = false;
		}
		break;
	}
	case tTd: {
		const uint32_t table = InnerTable(s);
		if (table == kNone) {
			SoftBreak(s);
			break;
		}
		if (table + 1 != s.depth) {
			Break(s, true);
			s.depth = table + 1;
		}
		Context& c = s.context[table];
		if (!c.rowOpen) {
			Break(s, true);
			c.rowOpen = true;
			c.cells = 0;
			c.pending = 0;
			c.filled = false;
		}
		if (c.cells)
			c.pending++;
		c.cells++;
		s.heading = 0;
		break;
	}
	case tCaption: {
		Break(s, true);
		const uint32_t table = InnerTable(s);
		if (table != kNone)
			s.context[table].rowOpen = false;
		break;
	}
	case tBlockquote:
		Break(s, true);
		s.heading = 0;
		Push(s, ctxQuote, false, 0);
		break;
	case tPre:
		Break(s, true);
		s.heading = 0;
		s.pre = true;
		s.preSkipNewline = true;
		break;
	case tHr:
		Break(s, true);
		s.heading = 0;
		s.forceKind = kindRule + 1;
		FlowWord(s, w.rule);
		FinishLine(s, true);
		s.blockFirst = true;
		break;
	case tA:
		CloseLink(s);
		if (s.stop || !t.Usable(aHref))
			break;
		if (g.linkCount >= g.linkCapacity) {
			Fail(s, overflowLinks);
			break;
		}
		{
			Link& l = g.links[g.linkCount];
			l.urlStart = t.start[aHref];
			l.urlLength = t.length[aHref];
			l.line = kNone;
			l.textStart = kNone;
			l.textLength = 0;
			s.openLink = g.linkCount++;
			s.linkPending = true;
			s.linkLabelStart = s.linkLabelLength = 0;
			static constexpr uint8_t kLabels[] = {aAriaLabel, aTitle};
			for (const uint8_t a : kLabels) {
				if (t.Usable(a) && t.length[a]) {
					s.linkLabelStart = t.start[a];
					s.linkLabelLength = t.length[a];
					break;
				}
			}
			s.keep = true;
		}
		break;
	case tImg: {
		uint32_t start = 0, length = 0;
		const bool inLine = InCell(s) || s.heading || s.field != kNone || s.pre;
		const bool needed = s.openLink != kNone && s.linkPending;
		if (t.Usable(aAlt) && !BlankString(s, t.start[aAlt], t.length[aAlt])) {
			start = t.start[aAlt];
			length = t.length[aAlt];
		} else if (t.Usable(aTitle) && !BlankString(s, t.start[aTitle], t.length[aTitle])) {
			start = t.start[aTitle];
			length = t.length[aTitle];
		} else if (t.Has(aAlt) && !needed) {
			break;  // alt="": decorative
		} else if (!needed && (inLine || s.options->skipUnlabelledImages)) {
			break;
		}
		if (!inLine) {
			FinishLine(s);
			s.forceKind = kindImage + 1;
		}
		if (length)
			FlowString(s, start, length);
		else
			FlowWord(s, w.image);
		if (!inLine)
			FinishLine(s);
		else
			s.pendingSpace = true;
		break;
	}
	case tForm: {
		CloseSelect(s);
		Break(s, true);
		if (s.stop)
			break;
		if (g.formCount >= g.formCapacity || g.formCount >= kNoField) {
			Fail(s, overflowForms);
			break;
		}
		Form& f = g.forms[g.formCount];
		memset(&f, 0, sizeof f);
		if (t.Usable(aAction)) { f.actionStart = t.start[aAction]; f.actionLength = t.length[aAction]; }
		f.method = t.Usable(aMethod)
			&& Same(g.strings + t.start[aMethod], t.length[aMethod], "post") ? 1 : 0;
		s.form = g.formCount++;
		s.keep = true;
		break;
	}
	case tInput: {
		CloseSelect(s);
		if (s.stop)
			break;
		uint8_t type = fieldText;
		bool image = false;
		if (t.Usable(aType)) {
			const char* v = g.strings + t.start[aType];
			const size_t n = t.length[aType];
			if (Same(v, n, "hidden")) type = fieldHidden;
			else if (Same(v, n, "password")) type = fieldPassword;
			else if (Same(v, n, "checkbox")) type = fieldCheckbox;
			else if (Same(v, n, "radio")) type = fieldRadio;
			else if (Same(v, n, "submit")) type = fieldSubmit;
			else if (Same(v, n, "image")) { type = fieldSubmit; image = true; }
			else if (Same(v, n, "button")) type = fieldButton;
			else if (Same(v, n, "reset")) type = fieldReset;
			else if (Same(v, n, "file")) type = fieldFile;
		}
		const uint32_t index = AddField(s, t, type);
		if (index == kNone || type == fieldHidden)
			break;
		const Field f = g.fields[index];
		uint32_t labelStart = f.labelStart, labelLength = f.labelLength;
		if (!labelLength) { labelStart = f.nameStart; labelLength = f.nameLength; }
		switch (type) {
		case fieldPassword:
			ControlLine(s, index, w.password, labelStart, labelLength, 0, 0, nullptr);
			break;
		case fieldCheckbox:
		case fieldRadio:
			ControlLine(s, index, type == fieldRadio ? w.radio : w.checkbox, labelStart,
				labelLength, f.valueStart, f.valueLength,
				(f.flags & fieldChecked) ? w.checked : w.unchecked);
			break;
		case fieldSubmit:
		case fieldButton:
		case fieldReset: {
			uint32_t start = f.valueStart, length = f.valueLength;
			if (image && t.Usable(aAlt) && t.length[aAlt]) { start = t.start[aAlt]; length = t.length[aAlt]; }
			if (!length) { start = f.labelStart; length = f.labelLength; }
			const bool named = length && !BlankString(s, start, length);
			ControlLine(s, index, w.button, start, named ? length : 0, 0, 0,
				named ? nullptr : type == fieldReset ? w.reset : type == fieldSubmit ? w.submit : nullptr);
			break;
		}
		case fieldFile:
			ControlLine(s, index, w.file, labelStart, labelLength, 0, 0, nullptr);
			break;
		default:
			ControlLine(s, index, w.edit, labelStart, labelLength, f.valueStart, f.valueLength, nullptr);
			break;
		}
		break;
	}
	case tTextarea: {
		CloseSelect(s);
		if (s.stop)
			break;
		const uint32_t index = AddField(s, t, fieldTextarea);
		const size_t end = FindClose(s, p, "textarea");
		if (index != kNone) {
			Field& f = g.fields[index];
			f.valueStart = g.stringCount;
			size_t q = p;
			if (q < end && s.in[q] == '\r') q++;
			if (q < end && s.in[q] == '\n') q++;
			while (q < end && !s.stop && g.stringCount - f.valueStart + 4 <= kMaxValue) {
				const uint32_t cp = NextText(s, q, end, false);
				if (cp != '\r')
					PutStringCp(s, cp ? cp : 0xFFFD);
			}
			f.valueLength = g.stringCount - f.valueStart;
			f.flags |= fieldHasValue;
			uint32_t labelStart = f.labelStart, labelLength = f.labelLength;
			if (!labelLength) { labelStart = f.nameStart; labelLength = f.nameLength; }
			if (!s.stop)
				ControlLine(s, index, w.textarea, labelStart, labelLength, f.valueStart, f.valueLength, nullptr);
		}
		p = end < s.size ? AfterTag(s, end) : s.size;
		break;
	}
	case tSelect: {
		CloseSelect(s);
		if (s.stop)
			break;
		const uint32_t index = AddField(s, t, fieldSelect);
		if (index == kNone)
			break;
		Field& f = g.fields[index];
		s.selectLabelStart = f.labelLength ? f.labelStart : f.nameStart;
		s.selectLabelLength = f.labelLength ? f.labelLength : f.nameLength;
		f.labelStart = f.labelLength = f.valueStart = f.valueLength = 0;
		f.flags &= uint8_t(~fieldHasValue);
		s.select = index;
		s.option = kNone;
		break;
	}
	case tOptgroup:
		CloseOption(s);
		break;
	case tOption: {
		CloseOption(s);
		if (s.select == kNone || s.stop)
			break;
		const uint32_t index = AddField(s, t, fieldOption);
		if (index == kNone)
			break;
		Field& f = g.fields[index];
		f.nameStart = f.nameLength = 0;
		f.labelStart = g.stringCount;
		f.labelLength = 0;
		s.option = index;
		s.optionSpace = false;
		break;
	}
	case tButton: {
		CloseSelect(s);
		if (s.stop)
			break;
		uint8_t type = fieldSubmit;
		if (t.Usable(aType)) {
			const char* v = g.strings + t.start[aType];
			if (Same(v, t.length[aType], "button")) type = fieldButton;
			else if (Same(v, t.length[aType], "reset")) type = fieldReset;
		}
		const uint32_t index = AddField(s, t, type);
		if (index == kNone)
			break;
		FinishLine(s);
		s.field = index;
		s.buttonOpen = true;
		FlowWord(s, w.button);
		s.pendingSpace = true;
		if (g.fields[index].labelLength) {  // aria-label: also for icon buttons
			FlowString(s, g.fields[index].labelStart, g.fields[index].labelLength);
			s.pendingSpace = true;
		}
		break;
	}
	case tSkip: {
		// <svg .../> and friends: only real containers are skipped.
		const bool raw = t.name[0] == 's' && (t.name[1] == 'c' || t.name[1] == 't');
		if (!raw && p >= 2 && s.in[p - 2] == '/')
			break;
		const size_t end = FindClose(s, p, t.name);
		p = end < s.size ? AfterTag(s, end) : s.size;
		break;
	}
	case tTitle: {
		const size_t end = FindClose(s, p, "title");
		if (!s.titleSet) {
			bool space = false;
			size_t q = p;
			while (q < end) {
				const uint32_t cp = NextText(s, q, end, false);
				if (Space(cp) || cp == 0xA0) {
					space = g.titleLength != 0;
					continue;
				}
				if (Ignorable(cp))
					continue;
				const uint32_t need = (space ? 1 : 0) + (cp >= 0x10000 ? 2 : 1);
				if (g.titleLength + need >= kTitleChars)
					break;
				if (space)
					g.title[g.titleLength++] = u' ';
				space = false;
				if (cp >= 0x10000) {
					g.title[g.titleLength++] = char16_t(0xD800 + ((cp - 0x10000) >> 10));
					g.title[g.titleLength++] = char16_t(0xDC00 + ((cp - 0x10000) & 0x3FF));
				} else {
					g.title[g.titleLength++] = char16_t(cp);
				}
			}
			g.title[g.titleLength] = 0;
			s.titleSet = g.titleLength != 0;
		}
		p = end < s.size ? AfterTag(s, end) : s.size;
		break;
	}
	case tBase:
		if (!s.baseSet && t.Usable(aHref) && t.length[aHref]) {
			g.baseStart = t.start[aHref];
			g.baseLength = t.length[aHref];
			s.baseSet = true;
			s.keep = true;
		}
		break;
	default:
		break;
	}
	// Jump targets.
	if (!s.stop && t.id != tOption) {
		const uint8_t a = t.Usable(aId) && t.length[aId] ? aId
			: t.id == tA && t.Usable(aName) && t.length[aName] ? aName : aNone;
		if (a != aNone) {
			if (g.anchorCount < g.anchorCapacity) {
				Anchor& anchor = g.anchors[g.anchorCount++];
				anchor.nameStart = t.start[a];
				anchor.nameLength = t.length[a];
				anchor.position = g.textCount;
				s.keep = true;
			} else if (g.anchorCapacity) {
				g.overflow |= overflowAnchors;
			}
		}
	}
}

inline void EndTag(State& s, const Tag& t)
{
	switch (t.id) {
	case tBlock:
	case tP:
		SoftBreak(s);
		break;
	case tHeading:
		Break(s, true);
		s.heading = 0;
		break;
	case tUl:
	case tOl:
		Break(s, true);
		s.itemPending = false;
		PopTo(s, ctxList, false);
		break;
	case tLi:
		Break(s, true);
		s.itemPending = false;
		break;
	case tTable:
		Break(s, true);
		PopTo(s, ctxTable, true);
		break;
	case tTr: {
		Break(s, true);
		const uint32_t table = InnerTable(s);
		if (table != kNone) {
			s.depth = table + 1;
			s.context[table].rowOpen = false;
		}
		break;
	}
	case tTd: {
		const uint32_t table = InnerTable(s);
		if (table != kNone && table + 1 != s.depth) {
			Break(s, true);
			s.depth = table + 1;
		}
		s.pendingSpace = true;
		break;
	}
	case tCaption:
		Break(s, true);
		break;
	case tBlockquote:
		Break(s, true);
		PopTo(s, ctxQuote, false);
		break;
	case tPre:
		Break(s, true);
		s.pre = false;
		break;
	case tA:
		CloseLink(s);
		break;
	case tForm:
		CloseSelect(s);
		Break(s, true);
		s.form = kNone;
		break;
	case tSelect:
		CloseSelect(s);
		break;
	case tOption:
	case tOptgroup:
		CloseOption(s);
		break;
	case tButton:
		if (s.buttonOpen) {
			FinishLine(s);
			s.buttonOpen = false;
			s.field = kNone;
		}
		break;
	default:
		break;
	}
}

inline void Text(State& s, uint32_t cp)
{
	if (s.select != kNone) {  // only option text counts inside <select>
		if (s.option == kNone)
			return;
		Page& g = *s.page;
		const Field& o = g.fields[s.option];
		if (Space(cp) || cp == 0xA0) {
			s.optionSpace = g.stringCount > o.labelStart;
		} else if (!Ignorable(cp) && g.stringCount - o.labelStart + 5 <= kMaxAttribute) {
			if (s.optionSpace)
				PutString(s, ' ');
			s.optionSpace = false;
			PutStringCp(s, cp);
		}
		return;
	}
	if (!s.pre) {
		Flow(s, cp);
		return;
	}
	if (cp == '\n') {
		if (!s.preSkipNewline)
			FinishLine(s, true);
		s.preSkipNewline = false;
		return;
	}
	if (cp == '\r')
		return;
	s.preSkipNewline = false;
	if (cp == '\t') {
		uint32_t n = 8 - ((s.page->textCount - s.lineStart) & 7);
		while (n--)
			PutVisible(s, ' ');
		return;
	}
	if (cp == 0xA0)
		cp = ' ';
	if (!Ignorable(cp))
		PutVisible(s, cp);
}

// Stores one attribute value in the strings pool.
inline bool StoreAttribute(State& s, Tag& t, uint8_t id, size_t from, size_t to)
{
	Page& g = *s.page;
	if (id == aNone || t.Has(id))
		return true;
	t.present |= 1u << id;
	t.start[id] = g.stringCount;
	const bool trim = id != aValue;
	bool any = false;
	while (from < to) {
		uint32_t cp = NextText(s, from, to, true);
		if (cp < 0x20 || cp == 0x7F)
			cp = ' ';
		if (trim && !any && cp == ' ')
			continue;
		if (g.stringCount - t.start[id] + 4 > kMaxAttribute) {
			t.clipped |= 1u << id;
			break;
		}
		any = true;
		if (!PutStringCp(s, cp))
			return false;
	}
	if (trim)
		while (g.stringCount > t.start[id] && g.strings[g.stringCount - 1] == ' ')
			g.stringCount--;
	t.length[id] = g.stringCount - t.start[id];
	return true;
}

// Parses the tag at in[p] == '<' (the caller checked that a name follows).
// Returns false when the document ends inside the tag.
inline bool ParseTag(State& s, size_t& p, Tag& t)
{
	const uint8_t* in = s.in;
	const size_t size = s.size;
	size_t q = p + 1;
	t.closing = in[q] == '/';
	if (t.closing)
		q++;
	t.present = t.clipped = 0;
	t.id = tUnknown;
	t.heading = 0;
	uint32_t n = 0;
	bool known = true;
	while (q < size && !Space(in[q]) && in[q] != '>' && in[q] != '/') {
		if (n < sizeof t.name - 1)
			t.name[n++] = Lower(char(in[q]));
		else
			known = false;
		q++;
	}
	t.name[n] = 0;
	if (known) {
		if (n == 2 && t.name[0] == 'h' && t.name[1] >= '1' && t.name[1] <= '6') {
			t.id = tHeading;
			t.heading = uint8_t(t.name[1] - '0');
		} else {
			for (const TagName& k : kTags) {
				if (Same(t.name, n, k.name)) {
					t.id = k.id;
					break;
				}
			}
		}
	}
	for (;;) {
		while (q < size && (Space(in[q]) || in[q] == '/'))
			q++;
		if (q >= size) {
			p = size;
			return false;
		}
		if (in[q] == '>') {
			q++;
			break;
		}
		char name[12];
		uint32_t an = 0;
		bool fits = true;
		const size_t nameStart = q;
		while (q < size && !Space(in[q]) && in[q] != '>' && in[q] != '/'
			&& (in[q] != '=' || q == nameStart)) {
			if (an < sizeof name - 1)
				name[an++] = Lower(char(in[q]));
			else
				fits = false;
			q++;
		}
		uint8_t id = aNone;
		if (fits && !t.closing) {
			for (const AttributeName& k : kAttributes) {
				if (Same(name, an, k.name)) {
					id = k.id;
					break;
				}
			}
			if (t.id == tUnknown && id != aId)
				id = aNone;  // <span id=...> is a jump target, nothing else
		}
		while (q < size && Space(in[q]))
			q++;
		size_t from = q, to = q;
		if (q < size && in[q] == '=') {
			q++;
			while (q < size && Space(in[q]))
				q++;
			if (q >= size) {
				p = size;
				return false;
			}
			if (in[q] == '"' || in[q] == '\'') {
				const uint8_t quote = in[q++];
				from = q;
				while (q < size && in[q] != quote)
					q++;
				if (q >= size) {
					p = size;
					return false;
				}
				to = q++;
			} else {
				from = q;
				while (q < size && !Space(in[q]) && in[q] != '>')
					q++;
				to = q;
			}
		}
		if (!StoreAttribute(s, t, id, from, to)) {
			p = size;
			return false;
		}
	}
	p = q;
	return true;
}

// Handles markup at in[p] == '<'. Returns false when it is just a '<'.
inline bool Markup(State& s, size_t& p)
{
	const uint8_t* in = s.in;
	const size_t size = s.size;
	if (p + 1 >= size)
		return false;
	const uint8_t c = in[p + 1];
	if (c == '!') {
		if (p + 3 < size && in[p + 2] == '-' && in[p + 3] == '-') {
			size_t q = p + 4;
			// "<!-->" and "<!--->" are empty comments.
			if (q < size && in[q] == '>') { p = q + 1; return true; }
			if (q + 1 < size && in[q] == '-' && in[q + 1] == '>') { p = q + 2; return true; }
			for (; q + 2 < size; q++) {
				if (in[q] == '-' && in[q + 1] == '-' && in[q + 2] == '>') {
					p = q + 3;
					return true;
				}
			}
			p = size;
			return true;
		}
		if (p + 9 <= size && in[p + 2] == '[' && in[p + 3] == 'C' && in[p + 4] == 'D'
			&& in[p + 5] == 'A' && in[p + 6] == 'T' && in[p + 7] == 'A' && in[p + 8] == '[') {
			size_t q = p + 9, end = size;
			for (size_t i = q; i + 2 < size; i++) {
				if (in[i] == ']' && in[i + 1] == ']' && in[i + 2] == '>') {
					end = i;
					break;
				}
			}
			while (q < end && !s.stop)
				Text(s, DecodeByte(s.charset, in, end, q));
			p = end < size ? end + 3 : size;
			return true;
		}
		p = AfterTag(s, p);
		return true;
	}
	if (c == '?') {
		p = AfterTag(s, p);
		return true;
	}
	if (!Alpha(c) && !(c == '/' && p + 2 < size && Alpha(in[p + 2])))
		return false;
	Page& g = *s.page;
	const uint32_t mark = g.stringCount;
	Tag t;
	if (!ParseTag(s, p, t)) {
		if (!s.stop)
			g.stringCount = mark;
		return true;
	}
	s.keep = false;
	if (s.select != kNone) {
		// Inside <select> only its own tags count; a control or a form
		// boundary ends a select that was left open.
		const bool own = t.id == tOption || t.id == tOptgroup || t.id == tSelect;
		const bool ends = t.id == tForm || t.id == tTable
			|| (!t.closing && (t.id == tInput || t.id == tTextarea || t.id == tButton));
		if (!own && !ends) {
			g.stringCount = mark;
			return true;
		}
		// The tag's attribute strings were appended after the option label:
		// close the option on the label alone.
		if (s.option != kNone) {
			const uint32_t all = g.stringCount;
			g.stringCount = mark;
			CloseOption(s);
			g.stringCount = all;
		}
		if (ends)
			CloseSelect(s);
	}
	if (t.closing)
		EndTag(s, t);
	else
		StartTag(s, t, p);
	if (!s.keep && !s.stop && g.stringCount >= mark)
		g.stringCount = mark;
	return true;
}

inline Charset CharsetFromNameN(const char* name, size_t length)
{
	static constexpr struct { char name[16]; Charset charset; } kNames[] = {
		{"utf-8", Charset::Utf8}, {"utf8", Charset::Utf8},
		{"iso-8859-2", Charset::Latin2}, {"iso8859-2", Charset::Latin2},
		{"iso_8859-2", Charset::Latin2}, {"latin2", Charset::Latin2},
		{"l2", Charset::Latin2}, {"windows-1250", Charset::Windows1250},
		{"cp1250", Charset::Windows1250}, {"x-cp1250", Charset::Windows1250},
		{"windows-1252", Charset::Windows1252}, {"cp1252", Charset::Windows1252},
		{"x-cp1252", Charset::Windows1252},
		// As every browser does: Latin-1 and ASCII labels mean windows-1252.
		{"iso-8859-1", Charset::Windows1252}, {"iso8859-1", Charset::Windows1252},
		{"iso_8859-1", Charset::Windows1252}, {"latin1", Charset::Windows1252},
		{"l1", Charset::Windows1252}, {"us-ascii", Charset::Windows1252},
		{"ascii", Charset::Windows1252},
	};
	for (const auto& k : kNames)
		if (Same(name, length, k.name))
			return k.charset;
	return Charset::Auto;
}

// <meta charset=...> / <meta http-equiv content="...; charset=..."> near the
// start of the document.
inline Charset SniffCharset(const uint8_t* in, size_t size)
{
	const size_t limit = size < 8192 ? size : 8192;
	for (size_t i = 0; i + 5 < limit; i++) {
		if (in[i] != '<' || !Same(reinterpret_cast<const char*>(in + i + 1), 4, "meta"))
			continue;
		size_t end = i + 5;
		while (end < limit && in[end] != '>')
			end++;
		for (size_t k = i + 5; k + 7 <= end; k++) {
			if (!Same(reinterpret_cast<const char*>(in + k), 7, "charset"))
				continue;
			size_t q = k + 7;
			while (q < end && Space(in[q])) q++;
			if (q >= end || in[q] != '=')
				continue;
			q++;
			while (q < end && (Space(in[q]) || in[q] == '"' || in[q] == '\'')) q++;
			size_t e = q;
			while (e < end && !Space(in[e]) && in[e] != '"' && in[e] != '\'' && in[e] != ';'
				&& in[e] != '/')
				e++;
			const Charset found = CharsetFromNameN(reinterpret_cast<const char*>(in + q), e - q);
			if (found != Charset::Auto)
				return found;
		}
		i = end;
	}
	return Charset::Auto;
}

inline bool Prepare(State& s, const uint8_t* bytes, size_t size, Charset hint,
	Page& page, const Options& options, size_t& p, bool sniff)
{
	memset(&s, 0, sizeof s);
	page.textCount = page.lineCount = page.linkCount = page.stringCount = 0;
	page.formCount = page.fieldCount = page.anchorCount = 0;
	page.title[0] = 0;
	page.titleLength = 0;
	page.baseStart = page.baseLength = 0;
	page.truncated = false;
	page.overflow = 0;
	page.charset = Charset::Utf8;
	if (!page.text) page.textCapacity = 0;
	if (!page.lines) page.lineCapacity = 0;
	if (!page.links) page.linkCapacity = 0;
	if (!page.strings) page.stringCapacity = 0;
	if (!page.forms) page.formCapacity = 0;
	if (!page.fields) page.fieldCapacity = 0;
	if (!page.anchors) page.anchorCapacity = 0;
	s.in = bytes;
	s.size = bytes ? size : 0;
	s.page = &page;
	s.options = &options;
	s.width = options.width && options.width < 16 ? 16 : options.width;
	s.blockFirst = true;
	s.openLink = s.field = s.form = s.select = s.option = kNone;
	p = 0;
	Charset charset = hint;
	if (s.size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
		charset = Charset::Utf8;
		p = 3;
	}
	if (charset == Charset::Auto && sniff)
		charset = SniffCharset(bytes, s.size);
	if (charset == Charset::Auto)
		charset = ValidUtf8(bytes, s.size) ? Charset::Utf8
			: options.fallback == Charset::Auto ? Charset::Windows1252 : options.fallback;
	s.charset = page.charset = charset;
	return true;
}

inline void Finish(State& s)
{
	Page& g = *s.page;
	if (!s.stop) {
		CloseSelect(s);
		CloseLink(s);
		FinishLine(s);
	} else {
		if (s.openLink != kNone && !s.linkPending) {
			Link& l = g.links[s.openLink];
			l.textLength = g.textCount > l.textStart ? g.textCount - l.textStart : 0;
		}
	}
	// Links whose line could not be stored are not part of the page.
	if (g.linkCount > s.nextLink)
		g.linkCount = s.nextLink;
	for (uint32_t i = 0; i < g.linkCount; i++) {
		Link& l = g.links[i];
		if (l.textStart > g.textCount) l.textStart = g.textCount;
		if (l.textLength > g.textCount - l.textStart) l.textLength = g.textCount - l.textStart;
	}
	for (uint32_t i = 0; i < g.anchorCount; i++)
		if (g.anchors[i].position > g.textCount)
			g.anchors[i].position = g.textCount;
}

inline char16_t Fold(char16_t c)
{
	if (c < 0x80)
		return c >= 'A' && c <= 'Z' ? char16_t(c + 32) : c == u'\n' ? u' ' : c;
	if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return char16_t(c + 32);
	if (c >= 0x100 && c <= 0x17F) {
		if (c == 0x178) return 0xFF;
		if (c == 0x130 || c == 0x131 || c == 0x138 || c == 0x149 || c == 0x17F) return c;
		if (c <= 0x137 || (c >= 0x14A && c <= 0x177)) return char16_t(c | 1);
		return (c & 1) ? char16_t(c + 1) : c;  // 0x139-0x148, 0x179-0x17E
	}
	if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return char16_t(c + 32);
	if (c >= 0x410 && c <= 0x42F) return char16_t(c + 32);
	if (c >= 0x400 && c <= 0x40F) return char16_t(c + 80);
	return c;
}

struct Url {
	bool hasScheme, hasAuthority, hasQuery, hasFragment;
	size_t schemeEnd;              // length of the scheme
	size_t authStart, authEnd;
	size_t pathStart, pathEnd;
	size_t queryStart, queryEnd;
	size_t fragmentStart, end;
};

inline void ParseUrl(const char* u, size_t n, Url& r)
{
	memset(&r, 0, sizeof r);
	size_t p = 0;
	if (n && Alpha(uint8_t(u[0]))) {
		size_t i = 1;
		while (i < n && (Alpha(uint8_t(u[i])) || Digit(uint8_t(u[i])) || u[i] == '+'
			|| u[i] == '-' || u[i] == '.'))
			i++;
		if (i < n && u[i] == ':') {
			r.hasScheme = true;
			r.schemeEnd = i;
			p = i + 1;
		}
	}
	if (n - p >= 2 && u[p] == '/' && u[p + 1] == '/') {
		r.hasAuthority = true;
		p += 2;
		r.authStart = p;
		while (p < n && u[p] != '/' && u[p] != '?' && u[p] != '#')
			p++;
		r.authEnd = p;
	}
	r.pathStart = p;
	while (p < n && u[p] != '?' && u[p] != '#')
		p++;
	r.pathEnd = p;
	if (p < n && u[p] == '?') {
		r.hasQuery = true;
		r.queryStart = ++p;
		while (p < n && u[p] != '#')
			p++;
		r.queryEnd = p;
	}
	if (p < n && u[p] == '#') {
		r.hasFragment = true;
		r.fragmentStart = p + 1;
	}
	r.end = n;
}

struct Writer {
	char* out;
	size_t capacity, count;
	bool ok;
	void Raw(char c)
	{
		if (count + 1 < capacity)
			out[count++] = c;
		else
			ok = false;
	}
	// URL text: spaces, controls, quotes and non-ASCII bytes become %XX;
	// tabs and line breaks inside a URL are dropped.
	void Encode(char c)
	{
		const uint8_t b = uint8_t(c);
		if (b == '\t' || b == '\n' || b == '\r')
			return;
		if (b <= 0x20 || b >= 0x7F || b == '"' || b == '<' || b == '>' || b == '`') {
			static constexpr char kHex[] = "0123456789ABCDEF";
			Raw('%');
			Raw(kHex[b >> 4]);
			Raw(kHex[b & 15]);
		} else {
			Raw(c);
		}
	}
	void Encode(const char* text, size_t from, size_t to)
	{
		for (size_t i = from; i < to; i++)
			Encode(text[i]);
	}
};

// RFC 3986 5.2.4 on out[start, end), in place. Returns the new end.
inline size_t RemoveDots(char* out, size_t start, size_t end)
{
	size_t i = start, o = start;
	auto pop = [&]() {
		while (o > start) {
			if (out[--o] == '/')
				break;
		}
	};
	while (i < end) {
		const size_t left = end - i;
		if (left >= 3 && out[i] == '.' && out[i + 1] == '.' && out[i + 2] == '/') i += 3;
		else if (left >= 2 && out[i] == '.' && out[i + 1] == '/') i += 2;
		else if (left >= 3 && out[i] == '/' && out[i + 1] == '.' && out[i + 2] == '/') i += 2;
		else if (left == 2 && out[i] == '/' && out[i + 1] == '.') { out[++i] = '/'; }
		else if (left >= 4 && out[i] == '/' && out[i + 1] == '.' && out[i + 2] == '.'
			&& out[i + 3] == '/') { i += 3; pop(); }
		else if (left == 3 && out[i] == '/' && out[i + 1] == '.' && out[i + 2] == '.') {
			i += 2;
			out[i] = '/';
			pop();
		}
		else if ((left == 1 && out[i] == '.') || (left == 2 && out[i] == '.' && out[i + 1] == '.'))
			i = end;
		else {
			if (out[i] == '/')
				out[o++] = out[i++];
			while (i < end && out[i] != '/')
				out[o++] = out[i++];
		}
	}
	return o;
}

// application/x-www-form-urlencoded, UTF-8 bytes.
inline void FormEncode(Writer& w, const char* text, size_t length)
{
	static constexpr char kHex[] = "0123456789ABCDEF";
	for (size_t i = 0; i < length; i++) {
		const uint8_t b = uint8_t(text[i]);
		if (Alpha(b) || Digit(b) || b == '*' || b == '-' || b == '.' || b == '_') {
			w.Raw(char(b));
		} else if (b == ' ') {
			w.Raw('+');
		} else if (b == '\n' && !(i && text[i - 1] == '\r')) {
			w.Raw('%'); w.Raw('0'); w.Raw('D'); w.Raw('%'); w.Raw('0'); w.Raw('A');
		} else {
			w.Raw('%');
			w.Raw(kHex[b >> 4]);
			w.Raw(kHex[b & 15]);
		}
	}
}

inline void FormPair(Writer& w, bool& first, const char* name, size_t nameLength,
	const char* value, size_t valueLength)
{
	w.Raw(first ? '?' : '&');
	first = false;
	FormEncode(w, name, nameLength);
	w.Raw('=');
	FormEncode(w, value, valueLength);
}

inline bool HttpScheme(const char* u, size_t schemeLength)
{
	return Same(u, schemeLength, "http") || Same(u, schemeLength, "https");
}

} // namespace detail

// ---- rendering ---------------------------------------------------------

// Charset of a label such as the one in "Content-Type: text/html;
// charset=ISO-8859-2". Unknown labels give Charset::Auto.
inline Charset CharsetFromName(const char* name, size_t length)
{
	return name ? detail::CharsetFromNameN(name, length) : Charset::Auto;
}

// Renders an HTML document. `hint` is the charset of the HTTP header, or
// Charset::Auto. Order: UTF-8 byte order mark, hint, <meta>, valid UTF-8,
// Options::fallback. Returns true when the whole document fitted; false means
// page.truncated (the page holds everything up to the overflow and is usable).
inline bool Render(const uint8_t* html, size_t size, Charset hint, Page& page,
	const Options& options = Options())
{
	detail::State s;
	size_t p;
	detail::Prepare(s, html, size, hint, page, options, p, true);
	while (p < s.size && !s.stop) {
		if (s.in[p] == '<') {
			if (detail::Markup(s, p))
				continue;
			detail::Text(s, '<');
			p++;
			continue;
		}
		detail::Text(s, detail::NextText(s, p, s.size, false));
	}
	detail::Finish(s);
	return !page.truncated;
}

// Renders a text/plain response: one paragraph line per source line, wrapped
// at the width; runs of spaces collapse, empty lines only set lineFirst on
// the line that follows.
inline bool RenderPlain(const uint8_t* text, size_t size, Charset hint, Page& page,
	const Options& options = Options())
{
	detail::State s;
	size_t p;
	detail::Prepare(s, text, size, hint, page, options, p, false);
	s.plain = true;
	while (p < s.size && !s.stop) {
		const uint32_t cp = detail::DecodeByte(s.charset, s.in, s.size, p);
		if (cp == '\n' || cp == '\f' || cp == 0x2028 || cp == 0x2029) {
			if (page.textCount == s.lineStart)
				s.blockFirst = true;
			detail::FinishLine(s);
		} else {
			detail::Flow(s, cp);
		}
	}
	detail::Finish(s);
	return !page.truncated;
}

// ---- navigation --------------------------------------------------------
// In all Find functions `fromLine` is the first line looked at and
// `direction` is +1 (down) or -1 (up); pass current line +/- 1 for "next" and
// "previous". The result is a line index or kNone.

// Line that contains text pool offset `position` (the next line when the
// offset is between lines, the last line when it is past the end).
inline uint32_t LineAt(const Page& page, uint32_t position)
{
	if (!page.lineCount)
		return kNone;
	uint32_t low = 0, high = page.lineCount;
	while (low < high) {
		const uint32_t mid = low + (high - low) / 2;
		if (page.lines[mid].start + page.lines[mid].length > position)
			high = mid;
		else
			low = mid + 1;
	}
	// Empty lines never satisfy "end > position": step back onto one that
	// starts exactly here.
	if (low && page.lines[low - 1].length == 0 && page.lines[low - 1].start == position)
		low--;
	return low < page.lineCount ? low : page.lineCount - 1;
}

template<typename Match>
inline uint32_t FindLine(const Page& page, uint32_t fromLine, int direction, Match match)
{
	if (fromLine >= page.lineCount)
		return kNone;
	for (uint32_t i = fromLine;; direction < 0 ? i-- : i++) {
		if (i >= page.lineCount)
			return kNone;
		if (match(page.lines[i]))
			return i;
		if (direction < 0 && !i)
			return kNone;
	}
}

// First line of a heading; `level` 0 = any, 1-6 = exactly that level.
inline uint32_t FindNextHeading(const Page& page, uint32_t fromLine, int direction,
	uint32_t level = 0)
{
	return FindLine(page, fromLine, direction, [level](const Line& l) {
		return l.kind == kindHeading && !(l.flags & lineContinues)
			&& (!level || l.level == level);
	});
}

// Line on which at least one link starts.
inline uint32_t FindNextLink(const Page& page, uint32_t fromLine, int direction)
{
	return FindLine(page, fromLine, direction, [](const Line& l) { return l.linkCount != 0; });
}

// First line of a form control.
inline uint32_t FindNextField(const Page& page, uint32_t fromLine, int direction)
{
	return FindLine(page, fromLine, direction, [](const Line& l) {
		return l.kind == kindForm && !(l.flags & lineContinues);
	});
}

// First line of a block of the given kind (kindListItem, kindTableRow, ...).
inline uint32_t FindNextKind(const Page& page, uint32_t fromLine, int direction, uint8_t kind)
{
	return FindLine(page, fromLine, direction, [kind](const Line& l) {
		return l.kind == kind && !(l.flags & lineContinues);
	});
}

// Searches the page text, ignoring case for ASCII, Latin-1, Latin Extended-A
// (Polish, Czech, ...), Greek and Cyrillic letters. A space in the needle
// also matches the place where a line was wrapped or ended, so a phrase is
// found even when it spans lines. Down: the first match that starts at or
// after the start of fromLine. Up: the last match that starts at or before
// the end of fromLine. Returns the line the match starts on; `column`
// (optional) receives the offset in that line.
inline uint32_t FindText(const Page& page, const char16_t* needle, size_t needleLength,
	uint32_t fromLine, int direction, uint32_t* column = nullptr)
{
	if (!needle || !needleLength || fromLine >= page.lineCount)
		return kNone;
	const Line& bottom = page.lines[page.lineCount - 1];
	const uint32_t textEnd = bottom.start + bottom.length;
	if (needleLength > textEnd)
		return kNone;
	const uint32_t n = uint32_t(needleLength);
	const uint32_t last = textEnd - n;  // last possible start
	const Line& from = page.lines[fromLine];
	uint32_t at = direction < 0 ? from.start + from.length : from.start;
	if (direction < 0 && at > last)
		at = last;
	const char16_t first = detail::Fold(needle[0]);
	for (;; direction < 0 ? at-- : at++) {
		if (at > last)
			return kNone;
		if (detail::Fold(page.text[at]) == first) {
			uint32_t k = 1;
			while (k < n && detail::Fold(page.text[at + k]) == detail::Fold(needle[k]))
				k++;
			if (k == n) {
				const uint32_t line = LineAt(page, at);
				if (line != kNone && column) {
					const Line& l = page.lines[line];
					*column = at > l.start ? at - l.start : 0;
				}
				return line;
			}
		}
		if (direction < 0 && !at)
			return kNone;
	}
}

// Line of the element with the given id / anchor name ("#name" without '#').
// %XX escapes in `name` are decoded before comparing. kNone when not found.
inline uint32_t FindAnchor(const Page& page, const char* name, size_t length)
{
	auto hex = [](char c) -> int {
		if (c >= '0' && c <= '9') return c - '0';
		c = char(c | 0x20);
		return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
	};
	if (!name || !length)
		return kNone;
	for (uint32_t i = 0; i < page.anchorCount; i++) {
		const Anchor& a = page.anchors[i];
		const char* have = page.strings + a.nameStart;
		size_t p = 0, k = 0;
		for (; p < length && k < a.nameLength; k++) {
			char c = name[p++];
			if (c == '%' && length - p >= 2 && hex(name[p]) >= 0 && hex(name[p + 1]) >= 0) {
				c = char(hex(name[p]) * 16 + hex(name[p + 1]));
				p += 2;
			}
			if (c != have[k])
				break;
		}
		if (p == length && k == a.nameLength)
			return LineAt(page, a.position);
	}
	return kNone;
}

// ---- URLs --------------------------------------------------------------

// RFC 3986 section 5 reference resolution. `out` receives the target URL
// without the fragment, NUL ended (outLength excludes the NUL); spaces,
// control characters, quotes and non-ASCII bytes are percent-encoded, the
// scheme is lower-cased, and an http/https URL without a path gets "/".
// `fragment` (optional) receives the reference's fragment without '#', NUL
// ended, percent-encoded the same way. Returns false when `out` or
// `fragment` is too small, or when the reference is relative and the base
// is not an absolute URL; `out` is then an empty string. `out` must have room
// for the URL as it is before "./" and "../" are removed (3 * (base length +
// reference length) + 4 is always enough) and must not overlap the inputs.
inline bool ResolveUrl(const char* base, size_t baseLength, const char* reference,
	size_t referenceLength, char* out, size_t capacity, size_t* outLength = nullptr,
	char* fragment = nullptr, size_t fragmentCapacity = 0, size_t* fragmentLength = nullptr)
{
	using namespace detail;
	if (outLength) *outLength = 0;
	if (fragmentLength) *fragmentLength = 0;
	if (fragment && fragmentCapacity) fragment[0] = 0;
	if (!out || !capacity)
		return false;
	out[0] = 0;
	if (!base) baseLength = 0;
	if (!reference) referenceLength = 0;
	while (referenceLength && uint8_t(reference[0]) <= 0x20) { reference++; referenceLength--; }
	while (referenceLength && uint8_t(reference[referenceLength - 1]) <= 0x20) referenceLength--;
	while (baseLength && uint8_t(base[0]) <= 0x20) { base++; baseLength--; }
	while (baseLength && uint8_t(base[baseLength - 1]) <= 0x20) baseLength--;
	Url r, b;
	ParseUrl(reference, referenceLength, r);
	ParseUrl(base, baseLength, b);
	if (!r.hasScheme && !b.hasScheme)
		return false;
	Writer w{out, capacity, 0, true};
	const char* schemeFrom = r.hasScheme ? reference : base;
	const size_t schemeLength = r.hasScheme ? r.schemeEnd : b.schemeEnd;
	for (size_t i = 0; i < schemeLength; i++)
		w.Raw(Lower(schemeFrom[i]));
	w.Raw(':');
	bool hasAuthority, hasQuery = r.hasQuery, dots = true;
	const char* queryFrom = reference;
	size_t queryStart = r.queryStart, queryEnd = r.queryEnd;
	size_t pathAt;
	if (r.hasScheme || r.hasAuthority) {
		hasAuthority = r.hasAuthority;
		if (hasAuthority) {
			w.Raw('/'); w.Raw('/');
			w.Encode(reference, r.authStart, r.authEnd);
		}
		pathAt = w.count;
		w.Encode(reference, r.pathStart, r.pathEnd);
		dots = r.pathEnd > r.pathStart && reference[r.pathStart] == '/';
	} else {
		hasAuthority = b.hasAuthority;
		if (hasAuthority) {
			w.Raw('/'); w.Raw('/');
			w.Encode(base, b.authStart, b.authEnd);
		}
		pathAt = w.count;
		if (r.pathEnd == r.pathStart) {
			w.Encode(base, b.pathStart, b.pathEnd);
			dots = false;
			if (!r.hasQuery) {
				hasQuery = b.hasQuery;
				queryFrom = base;
				queryStart = b.queryStart;
				queryEnd = b.queryEnd;
			}
		} else if (reference[r.pathStart] == '/') {
			w.Encode(reference, r.pathStart, r.pathEnd);
		} else {
			if (b.hasAuthority && b.pathEnd == b.pathStart) {
				w.Raw('/');
			} else {
				size_t slash = b.pathEnd;
				while (slash > b.pathStart && base[slash - 1] != '/')
					slash--;
				w.Encode(base, b.pathStart, slash);
			}
			w.Encode(reference, r.pathStart, r.pathEnd);
		}
	}
	if (w.ok && dots)
		w.count = RemoveDots(out, pathAt, w.count);
	if (w.count == pathAt && hasAuthority && HttpScheme(out, schemeLength))
		w.Raw('/');
	if (hasQuery) {
		w.Raw('?');
		w.Encode(queryFrom, queryStart, queryEnd);
	}
	if (!w.ok) {
		out[0] = 0;
		return false;
	}
	out[w.count] = 0;
	if (fragment && r.hasFragment) {
		Writer f{fragment, fragmentCapacity, 0, fragmentCapacity != 0};
		f.Encode(reference, r.fragmentStart, r.end);
		if (!f.ok) {
			if (fragmentCapacity) fragment[0] = 0;
			out[0] = 0;
			return false;
		}
		fragment[f.count] = 0;
		if (fragmentLength) *fragmentLength = f.count;
	}
	if (outLength) *outLength = w.count;
	return true;
}

// True for an absolute http:// or https:// URL (what the browser can fetch).
inline bool IsHttpUrl(const char* url, size_t length)
{
	detail::Url u;
	if (!url)
		return false;
	detail::ParseUrl(url, length, u);
	return u.hasScheme && u.hasAuthority && u.authEnd > u.authStart
		&& detail::HttpScheme(url, u.schemeEnd);
}

// The URL relative references of the page resolve against: the document URL,
// or <base href> (itself resolved against the document URL) when present.
inline bool PageBaseUrl(const Page& page, const char* documentUrl, size_t documentLength,
	char* out, size_t capacity, size_t* outLength = nullptr)
{
	return ResolveUrl(documentUrl, documentLength, page.strings + page.baseStart,
		page.baseLength, out, capacity, outLength);
}

// Target of link `link`; baseUrl is the result of PageBaseUrl().
inline bool ResolveLink(const Page& page, uint32_t link, const char* baseUrl,
	size_t baseLength, char* out, size_t capacity, size_t* outLength = nullptr,
	char* fragment = nullptr, size_t fragmentCapacity = 0, size_t* fragmentLength = nullptr)
{
	if (link >= page.linkCount) {
		if (out && capacity) out[0] = 0;
		return false;
	}
	const Link& l = page.links[link];
	return ResolveUrl(baseUrl, baseLength, page.strings + l.urlStart, l.urlLength, out,
		capacity, outLength, fragment, fragmentCapacity, fragmentLength);
}

struct Pair {
	const char* name; size_t nameLength;
	const char* value; size_t valueLength;  // UTF-8
};

// "<action without query and fragment>?name=value&..." with the pairs encoded
// as application/x-www-form-urlencoded (UTF-8). `action` must already be
// absolute. Returns false when `out` is too small.
inline bool BuildGetUrl(const char* action, size_t actionLength, const Pair* pairs,
	size_t pairCount, char* out, size_t capacity, size_t* outLength = nullptr)
{
	if (outLength) *outLength = 0;
	if (!out || !capacity)
		return false;
	detail::Writer w{out, capacity, 0, true};
	for (size_t i = 0; action && i < actionLength && action[i] != '?' && action[i] != '#'; i++)
		w.Encode(action[i]);
	bool first = true;
	for (size_t i = 0; pairs && i < pairCount; i++)
		detail::FormPair(w, first, pairs[i].name, pairs[i].name ? pairs[i].nameLength : 0,
			pairs[i].value, pairs[i].value ? pairs[i].valueLength : 0);
	if (!w.ok) {
		out[0] = 0;
		return false;
	}
	out[w.count] = 0;
	if (outLength) *outLength = w.count;
	return true;
}

// A value typed by the user for Page::fields[i]; value == nullptr keeps the
// field's default.
struct FieldValue {
	const char* value; size_t length;  // UTF-8
};

// The URL a GET submission of form `form` requests. baseUrl: PageBaseUrl().
// `values` is null or an array parallel to Page::fields. `submitField` is the
// index of the submit button that was activated (its name=value is sent) or
// kNone. Sent: named, enabled text/password/hidden/textarea/select fields,
// checked check boxes and radio buttons ("on" without a value). Returns false
// for a bad index, a POST form (the URL is still built: the caller decides)
// or when `out` is too small.
inline bool BuildFormGetUrl(const Page& page, uint32_t form, const char* baseUrl,
	size_t baseLength, const FieldValue* values, uint32_t submitField, char* out,
	size_t capacity, size_t* outLength = nullptr)
{
	if (outLength) *outLength = 0;
	if (out && capacity) out[0] = 0;
	if (form >= page.formCount || !out || !capacity)
		return false;
	const Form& f = page.forms[form];
	size_t length = 0;
	if (!ResolveUrl(baseUrl, baseLength, page.strings + f.actionStart, f.actionLength, out,
			capacity, &length))
		return false;
	for (size_t i = 0; i < length; i++)
		if (out[i] == '?') { length = i; break; }
	detail::Writer w{out, capacity, length, true};
	bool first = true;
	for (uint32_t i = f.firstField; i < f.firstField + f.fieldCount && i < page.fieldCount; i++) {
		const Field& field = page.fields[i];
		if (field.form != form || !field.nameLength || (field.flags & fieldDisabled))
			continue;
		const char* value = page.strings + field.valueStart;
		size_t valueLength = field.valueLength;
		switch (field.type) {
		case fieldOption: case fieldButton: case fieldReset: case fieldFile:
			continue;
		case fieldSubmit:
			if (i != submitField)
				continue;
			break;
		case fieldCheckbox: case fieldRadio:
			if (!(field.flags & fieldChecked))
				continue;
			if (!(field.flags & fieldHasValue)) { value = "on"; valueLength = 2; }
			break;
		default:
			break;
		}
		if (values && values[i].value && field.type != fieldSubmit) {
			value = values[i].value;
			valueLength = values[i].length;
		}
		detail::FormPair(w, first, page.strings + field.nameStart, field.nameLength, value,
			valueLength);
	}
	if (!w.ok) {
		out[0] = 0;
		return false;
	}
	out[w.count] = 0;
	if (outLength) *outLength = w.count;
	return f.method == 0;
}

// Not covered: UTF-16/UTF-32 documents and charsets other than the five
// above; CSS and the hidden attribute (hidden content is read); <label for>
// association; colspan/rowspan; multiple selects (one value); POST bodies and
// multipart; legacy-charset form submission (always UTF-8); <input type=image>
// coordinates; javascript: and data: links are stored like any other link
// (IsHttpUrl() tells them apart after ResolveLink()).

} // namespace hakefi::html
