// SPDX-License-Identifier: GPL-2.0-or-later
//
// pc_winread - text and control recognition for classic Windows screens.
// The algorithm is described in WINREAD.txt.  No glyph data lives here: the
// fonts come from the user's Windows media at run time (AddFon).

#include "pc_winread.h"

#include <stdlib.h>
#include <string.h>

#ifdef PCWIN_DEBUG
#include <stdio.h>
#define PCWIN_LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define PCWIN_LOG(...) ((void)0)
#endif

namespace pcwin {

namespace {

typedef uint32_t Colour;
const Colour kNone = 0xFFFFFFFFu;
const uint32_t kInf = 0x7FFFFFFFu;

enum {
	kMaxW = 1024,
	kMaxH = 768,
	kMaxPixels = kMaxW * kMaxH,
	kMaxFonts = 96,
	kMaxGlyphs = 24000,
	kMaxCols = 360000,
	kHash = 128,
	kMaxFontHeight = 48,
	kMaxRuns = 1 << 18,
	kMaxComps = 60000,
	kMaxBoxes = 3072,
	kMaxActive = 384,
	kMaxSegs = 4096,
	kMaxLattice = 16384,
	kMaxEdges = 512,
	kMaxPanels = 32,
	kMaxMenus = 8,
	kMaxMenuRows = 64,
	kMaxFocus = 16,
	kMaxLines = 2048,
	kMaxTLines = 1024,
	kMaxItems = 384,
	kMaxDiscs = 64,
	kMaxExcl = 384,
	kMaxCarets = 8,
	kMaxUnread = 96,
	kPool = 65536,
	kInkRun = 20,          // longer horizontal runs of one colour are never glyph ink
	kPad = 16,
	kDP = kMaxW + 192,
	kMaxSteps = 1200
};

enum { kSideT = 0, kSideL, kSideB, kSideR };
enum { kFlat = 0, kRaised, kSunken, kEtched };

struct Glyph {
	uint32_t cols;         // index of the first column mask
	uint16_t cp;
	uint8_t  width;        // advance = cell width
	uint8_t  left;         // first inked column
	uint8_t  rank;         // lower = preferred when two parses are pixel-identical
	uint8_t  pad;
	int16_t  next;         // hash chain, index relative to the font's first glyph
};

struct Font {
	uint16_t first, count;
	uint8_t  height, ascent, space, bold, points, maxLeft, maxWidth, pad;
	int16_t  hash[kHash];
};

struct Seg {
	int16_t y, x0, x1;
	uint8_t dither, pad;
};

struct Lattice {
	int16_t y, x0, x1, pad;
	Colour  fill;
};

// A piece of the right border of a raised frame: a column that is darker
// than the surface left of it and lighter than the column right of it.
struct Edge {
	int16_t x, y0, y1;
	int16_t leftTop, leftBottom;   // where the top / bottom border ends, -1 = none
};

// A raised frame (pop-up menu, window): outer rectangle.
struct Panel {
	int16_t x0, y0, x1, y1;
	Colour  face, light, shade;
	uint8_t used, pad[3];
};

struct Box {
	int16_t x0, y0, x1, y1;
	Colour  colour;
	Colour  e[4][4];       // dominant colour beside each side at distance 1..4
	uint8_t kind, used, role, pad;
};

struct Active {
	int16_t x0, x1, y0, lastFull, lastGood;
	int16_t seen;
	uint8_t weak, pad;
	Colour  colour;
};

struct Run {
	int16_t  x0, x1, y;
	uint16_t comp;
	uint32_t parent;
};

struct Comp {
	int16_t  x0, y0, x1, y1;
	uint32_t count;
	Colour   colour, bg;
	uint8_t  state;        // 0 = unknown, 1 = glyph candidate, 2 = dead
	uint8_t  shadow;
	int16_t  line;
};

struct Line {
	int16_t x0, y0, x1, y1;
	Colour  fg, bg;
	uint16_t comps;
	uint8_t shadow, big;
};

struct TLine {
	int16_t x0, y0, x1, y1;    // ink box (grows when lines are joined)
	int16_t fy0, fy1;          // first line of a paragraph
	int16_t cellX, cellY;      // cell origin of the first line
	int16_t lastCellY;         // cell top of the last joined line
	int16_t lines;
	Colour  fg, bg;
	int32_t text;
	uint16_t accel;
	uint8_t font;
	uint8_t used, disabled, damaged, bold, selection;
};

struct Disc {
	int16_t x0, y0, x1, y1;
	uint8_t checked, pad;
};

struct RectI {
	int16_t x0, y0, x1, y1;
};

struct Caret {
	int16_t x, y0, y1, width;
	Colour  paper;
};

struct Item {
	uint8_t  role;
	int8_t   percent;
	uint16_t flags;
	int16_t  x0, y0, x1, y1;
	int16_t  cy;               // reading-order row
	int16_t  parent;
	int32_t  text, label;
	uint16_t accel;
	uint8_t  pos, cnt;
	Colour   colour;
};

struct Unread {
	int16_t x0, y0, x1, y1;
	Colour  fg, bg;
	uint8_t lines, used;
};

struct Back {
	int16_t prev;
	int16_t glyph;             // >= 0 glyph, -1 space, -2 blank column, -3 junk column
	uint8_t prevState, underlined;
};

struct Parse {
	int      font, cellX, cellTop;
	unsigned glyphs, junk, letters;
	uint32_t cost;
	uint16_t accel;
	unsigned len;
	char     text[512];
};

struct Sticky {
	bool    valid;
	int16_t x, y, w, h;
};

}	// namespace

struct Context {
	// fonts
	unsigned fontCount, glyphCount, colCount, preferred;
	uint8_t  mru[kMaxFonts];
	Font     fonts[kMaxFonts];
	Glyph    glyphs[kMaxGlyphs];
	uint64_t cols[kMaxCols];

	// frame
	int w, h;
	Colour work[kMaxPixels];

	unsigned segCount, focusCount, boxCount, runCount, compCount, lineCount,
		tlineCount, itemCount, discCount, exclCount, caretCount, poolUsed;
	Seg    segs[kMaxSegs];
	Lattice lattice[kMaxLattice];
	unsigned panelCount;
	Edge   edges[kMaxEdges];
	Panel  panels[kMaxPanels];
	int16_t edgeStart[kMaxW];
	RectI  focus[kMaxFocus];
	Box    boxes[kMaxBoxes];
	Active active[kMaxActive];
	Run    runs[kMaxRuns];
	uint32_t rowStart[kMaxH + 1];
	Comp   comps[kMaxComps];
	uint16_t order[kMaxComps];
	Line   lines[kMaxLines];
	TLine  tlines[kMaxTLines];
	uint16_t torder[kMaxTLines];
	Item   items[kMaxItems];
	uint16_t iorder[kMaxItems];
	Disc   discs[kMaxDiscs];
	RectI  excl[kMaxExcl];
	Caret  carets[kMaxCarets];
	char   pool[kPool];
	unsigned unreadCount;
	Unread unread[kMaxUnread];

	// parser work area
	uint64_t raw[kDP], cur[kDP];
	uint32_t cost[2][kDP];
	Back     back[2][kDP];
	int16_t  steps[kMaxSteps];
	uint8_t  stepUnder[kMaxSteps];

	Sticky sticky;
};

namespace {

// ---------------------------------------------------------------------------
// small helpers

inline int Lum(Colour c)
{
	return (int)((((c >> 16) & 255) * 77 + ((c >> 8) & 255) * 150 + (c & 255) * 29) >> 8);
}

inline int Sat(Colour c)
{
	int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
	int mx = r > g ? r : g, mn = r < g ? r : g;
	if (b > mx) mx = b;
	if (b < mn) mn = b;
	return mx - mn;
}

inline Colour P(const Context* c, int x, int y)
{
	return c->work[y * c->w + x];
}

inline Colour PS(const Context* c, int x, int y)
{
	if (x < 0 || y < 0 || x >= c->w || y >= c->h)
		return kNone;
	return c->work[y * c->w + x];
}

inline int Min(int a, int b) { return a < b ? a : b; }
inline int Max(int a, int b) { return a > b ? a : b; }
inline int Abs(int a) { return a < 0 ? -a : a; }

int PoolAdd(Context* c, const char* text, size_t length)
{
	if (length == 0)
		return 0;
	if (c->poolUsed + length + 1 > kPool)
		return 0;
	int at = (int)c->poolUsed;
	memcpy(c->pool + at, text, length);
	c->pool[at + length] = 0;
	c->poolUsed += (unsigned)length + 1;
	return at;
}

int PoolJoin(Context* c, int a, int b, const char* separator)
{
	if (a == 0)
		return b;
	if (b == 0)
		return a;
	size_t la = strlen(c->pool + a), lb = strlen(c->pool + b), ls = strlen(separator);
	if (c->poolUsed + la + lb + ls + 1 > kPool)
		return a;
	int at = (int)c->poolUsed;
	memcpy(c->pool + at, c->pool + a, la);
	memcpy(c->pool + at + la, separator, ls);
	memcpy(c->pool + at + la + ls, c->pool + b, lb);
	c->pool[at + la + ls + lb] = 0;
	c->poolUsed += (unsigned)(la + ls + lb) + 1;
	return at;
}

// ---------------------------------------------------------------------------
// fonts

inline unsigned Rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }
inline uint32_t Rd32(const uint8_t* p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

const uint16_t k1252[32] = {
	0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
	0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178
};

const uint16_t k1250[128] = {
	0x20AC, 0, 0x201A, 0, 0x201E, 0x2026, 0x2020, 0x2021,
	0, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
	0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
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
	0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9
};

uint16_t MapChar(unsigned charset, unsigned code)
{
	if (code < 0x20)
		return 0;
	if (code < 0x7F)
		return (uint16_t)code;
	if (code < 0x80)
		return 0;
	if (charset == 0)
		return code >= 0xA0 ? (uint16_t)code : k1252[code - 0x80];
	if (charset == 238)
		return k1250[code - 0x80];
	return 0;       // OEM, symbol and other national sets: ASCII part only
}

uint8_t Rank(uint16_t cp)
{
	if ((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'))
		return 0;
	if (cp < 0x80) {
		switch (cp) {
			case '.': case ',': case ':': case '-': case '(': case ')':
			case '\'': case '"': case '!': case '?': case '/': case '%':
			case '&': case ';':
				return 1;
			case '|': case '`': case '^': case '~': case '_':
				return 4;
			default:
				return 2;
		}
	}
	if (cp >= 0xC0 && cp < 0x250 && cp != 0xD7 && cp != 0xF7)
		return 3;
	return 5;
}

inline unsigned HashKey(unsigned left, uint64_t mask)
{
	mask ^= mask >> 29;
	mask *= 0xBF58476D1CE4E5B9ull;
	mask ^= mask >> 32;
	return (unsigned)((mask + left * 31) & (kHash - 1));
}

unsigned ReadGlyph(const uint8_t* f, size_t size, unsigned entry, bool v3,
	unsigned height, uint64_t* out)
{
	unsigned width = Rd16(f + entry);
	uint32_t offset = v3 ? Rd32(f + entry + 2) : Rd16(f + entry + 2);
	if (width == 0 || width > 62)
		return 0;
	unsigned byteCols = (width + 7) / 8;
	if ((uint64_t)offset + (uint64_t)byteCols * height > size)
		return 0;
	for (unsigned x = 0; x < width; x++) {
		const uint8_t* column = f + offset + (x / 8) * height;
		uint64_t mask = 0;
		uint8_t bit = (uint8_t)(0x80 >> (x % 8));
		for (unsigned r = 0; r < height; r++) {
			if (column[r] & bit)
				mask |= 1ull << r;
		}
		out[x] = mask;
	}
	return width;
}

bool AddFnt(Context* c, const uint8_t* f, size_t size)
{
	if (size < 118)
		return false;
	unsigned version = Rd16(f);
	if (version != 0x200 && version != 0x300)
		return false;
	if (Rd16(f + 66) & 1)
		return false;       // vector font
	bool v3 = version == 0x300;
	unsigned points = Rd16(f + 68), ascent = Rd16(f + 74), weight = Rd16(f + 83);
	unsigned charset = f[85], height = Rd16(f + 88);
	unsigned first = f[95], last = f[96], deflt = f[97];
	if (height == 0 || height > kMaxFontHeight || last < first || ascent >= height + 1)
		return false;
	unsigned entrySize = v3 ? 6 : 4, table = v3 ? 148 : 118;
	unsigned n = last - first + 1;
	if (table + (size_t)n * entrySize > size)
		return false;

	uint64_t def[64], g[64], b[64];
	unsigned defWidth = 0;
	if (deflt < n)
		defWidth = ReadGlyph(f, size, table + deflt * entrySize, v3, height, def);

	bool any = false;
	for (unsigned bold = 0; bold < 2; bold++) {
		if (bold && weight >= 600)
			break;
		if (c->fontCount >= kMaxFonts)
			break;
		unsigned saveGlyphs = c->glyphCount, saveCols = c->colCount;
		bool full = false;
		Font& font = c->fonts[c->fontCount];
		memset(&font, 0, sizeof(font));
		for (unsigned i = 0; i < kHash; i++)
			font.hash[i] = -1;
		font.first = (uint16_t)c->glyphCount;
		font.height = (uint8_t)height;
		font.ascent = (uint8_t)ascent;
		font.bold = (uint8_t)(bold || weight >= 600);
		font.points = (uint8_t)(points > 255 ? 255 : points);

		for (unsigned code = first; code <= last; code++) {
			unsigned width = ReadGlyph(f, size, table + (code - first) * entrySize, v3, height, g);
			if (width == 0)
				continue;
			bool blank = true;
			for (unsigned x = 0; x < width; x++) {
				if (g[x]) { blank = false; break; }
			}
			if (code == 32 && blank) {
				font.space = (uint8_t)(width + bold);
				continue;
			}
			if (blank)
				continue;
			if (code - first != deflt && width == defWidth
				&& memcmp(g, def, width * sizeof(uint64_t)) == 0)
				continue;       // not defined in this font
			if (code - first == deflt && (code < 0x21 || code > 0x7E))
				continue;
			uint16_t cp = MapChar(charset, code);
			if (cp == 0 || cp == 0xA0)
				continue;
			const uint64_t* source = g;
			if (bold) {
				for (unsigned x = 0; x <= width; x++)
					b[x] = (x < width ? g[x] : 0) | (x > 0 ? g[x - 1] : 0);
				width++;
				source = b;
			}
			unsigned left = 0;
			while (left < width && source[left] == 0)
				left++;
			uint8_t rank = Rank(cp);
			bool duplicate = false;
			for (unsigned k = font.first; k < c->glyphCount; k++) {
				Glyph& other = c->glyphs[k];
				if (other.width == width
					&& memcmp(c->cols + other.cols, source, width * sizeof(uint64_t)) == 0) {
					if ((cp == 'l' && other.cp == 'I') || (cp == 'I' && other.cp == 'l')) {
						other.cp = 'I';
						other.pad = 1;      // same picture: decided from the context
					} else if (rank < other.rank) {
						other.rank = rank;
						other.cp = cp;
					}
					duplicate = true;
					break;
				}
			}
			if (duplicate)
				continue;
			if (left > 12)
				continue;
			if (c->glyphCount >= kMaxGlyphs || c->colCount + width > kMaxCols) {
				full = true;
				break;
			}
			Glyph& glyph = c->glyphs[c->glyphCount];
			glyph.cols = c->colCount;
			glyph.cp = cp;
			glyph.width = (uint8_t)width;
			glyph.left = (uint8_t)left;
			glyph.rank = rank;
			glyph.pad = 0;
			memcpy(c->cols + c->colCount, source, width * sizeof(uint64_t));
			c->colCount += width;
			unsigned key = HashKey(left, source[left]);
			glyph.next = font.hash[key];
			font.hash[key] = (int16_t)(c->glyphCount - font.first);
			c->glyphCount++;
			if (left > font.maxLeft)
				font.maxLeft = (uint8_t)left;
			if (width > font.maxWidth)
				font.maxWidth = (uint8_t)width;
		}
		font.count = (uint16_t)(c->glyphCount - font.first);
		if (font.count < 20 || full) {
			c->glyphCount = saveGlyphs;
			c->colCount = saveCols;
			continue;
		}
		if (font.space == 0)
			font.space = (uint8_t)(Rd16(f + 91) ? Rd16(f + 91) : 4);
		// The dialog fonts are tried first whatever the order of the files.
		uint32_t face = Rd32(f + 105);
		bool dialogFont = face < size && size - face >= 14
			&& memcmp(f + face, "MS Sans Serif", 14) == 0;
		unsigned at = c->fontCount;
		if (dialogFont) {
			for (; at > c->preferred; at--)
				c->mru[at] = c->mru[at - 1];
			c->preferred++;
		}
		c->mru[at] = (uint8_t)c->fontCount;
		c->fontCount++;
		any = true;
	}
	return any;
}

// ---------------------------------------------------------------------------
// text line parser: exact cover of the column masks by font cells

void Utf8(char* out, unsigned& length, unsigned capacity, unsigned cp)
{
	if (cp < 0x80) {
		if (length + 1 < capacity)
			out[length++] = (char)cp;
	} else if (cp < 0x800) {
		if (length + 2 < capacity) {
			out[length++] = (char)(0xC0 | (cp >> 6));
			out[length++] = (char)(0x80 | (cp & 63));
		}
	} else if (length + 3 < capacity) {
		out[length++] = (char)(0xE0 | (cp >> 12));
		out[length++] = (char)(0x80 | ((cp >> 6) & 63));
		out[length++] = (char)(0x80 | (cp & 63));
	}
}

inline bool IsLower(char ch) { return (ch >= 'a' && ch <= 'z') || (ch & 0x80); }
inline bool IsLetter(char ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch & 0x80) || ch == 1;
}

// Capital I and small l have the same picture in some fonts (marked 0x01).
void ResolveIl(char* text)
{
	for (char* word = text; *word;) {
		char* end = word;
		bool lower = false;
		while (*end && *end != ' ') {
			if (IsLower(*end))
				lower = true;
			end++;
		}
		for (char* p = word; p < end; p++) {
			if (*p != 1)
				continue;
			if (!lower)
				*p = 'I';
			else if (p > word && IsLetter(p[-1]))
				*p = 'l';
			else if (p + 1 < end && p[1] == 1)
				*p = 'I';
			else if (p + 1 < end && strchr("aeiouy", p[1]) != nullptr)
				*p = 'l';
			else
				*p = 'I';
		}
		word = end;
		while (*word == ' ')
			word++;
	}
}

inline void Relax(Context* c, int state, int at, uint32_t cost, int prev,
	int prevState, int glyph, bool underlined)
{
	if (at >= kDP)
		return;
	if (cost < c->cost[state][at]) {
		c->cost[state][at] = cost;
		Back& back = c->back[state][at];
		back.prev = (int16_t)prev;
		back.glyph = (int16_t)glyph;
		back.prevState = (uint8_t)prevState;
		back.underlined = underlined;
	}
}

void TryKey(Context* c, const Font& font, int state, int i, unsigned left,
	uint64_t key, uint64_t under, uint32_t base)
{
	const uint64_t* m = c->cur;
	for (int g = font.hash[HashKey(left, key)]; g >= 0;) {
		const Glyph& glyph = c->glyphs[font.first + g];
		int next = glyph.next;
		if (glyph.left == left) {
			const uint64_t* cols = c->cols + glyph.cols;
			bool ok = true, allUnder = under != 0, extra = false;
			for (unsigned k = 0; k < glyph.width; k++) {
				uint64_t s = m[i + k];
				if ((s & ~under) != (cols[k] & ~under)) { ok = false; break; }
				if (!(s & under))
					allUnder = false;
				if ((s ^ cols[k]) & under)
					extra = true;
			}
			if (ok && (!extra || allUnder)) {
				Relax(c, 0, i + glyph.width, base + 1000 + glyph.rank, i, state, g,
					extra && allUnder);
			}
		}
		g = next;
	}
}

// Runs the parser over c->cur[0 .. kPad + n + pad).  Returns false when no
// parse covers every inked column.
bool RunParser(Context* c, const Font& font, int n, bool fuzzy, Parse* out)
{
	const uint64_t under = font.ascent + 1 < font.height ? 1ull << (font.ascent + 1) : 0;
	const uint64_t* m = c->cur;
	int end = kPad + n + font.maxWidth + 2;
	if (end > kDP)
		end = kDP;
	for (int i = 0; i < end; i++)
		c->cost[0][i] = c->cost[1][i] = kInf;
	for (int i = kPad - font.maxLeft; i <= kPad; i++)
		c->cost[0][i] = 0, c->back[0][i].prev = -1, c->back[0][i].glyph = -4;

	int last = kPad + n - 1;
	for (int i = kPad - font.maxLeft; i <= last; i++) {
		for (int state = 0; state < 2; state++) {
			uint32_t base = c->cost[state][i];
			if (base == kInf)
				continue;
			for (unsigned left = 0; left <= font.maxLeft; left++) {
				uint64_t s = m[i + left];
				if (s == 0)
					continue;
				TryKey(c, font, state, i, left, s, under, base);
				if ((s & under) && (s & ~under))
					TryKey(c, font, state, i, left, s & ~under, under, base);
				if (s & ~under)
					break;      // an underline alone may also lie under a left bearing
			}
			bool blank = true;
			for (unsigned k = 0; k < font.space; k++) {
				if (m[i + k]) { blank = false; break; }
			}
			if (blank)
				Relax(c, 1, i + font.space, base + 1000, i, state, -1, false);
			if (state == 1 && m[i] == 0)
				Relax(c, 1, i + 1, base + 1, i, state, -2, false);
			if (fuzzy)
				Relax(c, 0, i + 1, base + 3000, i, state, -3, false);
		}
	}
	int best = -1;
	for (int i = last + 1; i < end; i++) {
		if (c->cost[0][i] != kInf && (best < 0 || c->cost[0][i] < c->cost[0][best]))
			best = i;
	}
	if (best < 0)
		return false;

	unsigned count = 0;
	int at = best, state = 0;
	while (at >= 0) {
		const Back& back = c->back[state][at];
		if (back.prev < 0)
			break;
		if (count < kMaxSteps) {
			c->steps[count] = back.glyph;
			c->stepUnder[count] = back.underlined;
			count++;
		}
		int prev = back.prev;
		state = back.prevState;
		at = prev;
	}
	out->cellX = at - kPad;
	out->cost = c->cost[0][best];
	out->glyphs = out->junk = out->letters = 0;
	out->accel = 0;
	out->len = 0;
	unsigned underlinedCount = 0;
	bool space = false;
	for (unsigned k = count; k-- > 0;) {
		int g = c->steps[k];
		if (g >= 0) {
			const Glyph& glyph = c->glyphs[font.first + g];
			if (space && out->len > 0)
				Utf8(out->text, out->len, sizeof(out->text), ' ');
			space = false;
			Utf8(out->text, out->len, sizeof(out->text), glyph.pad ? 1 : glyph.cp);
			out->glyphs++;
			if (glyph.rank == 0 || glyph.rank == 3)
				out->letters++;
			if (c->stepUnder[k]) {
				underlinedCount++;
				out->accel = glyph.cp;
			}
		} else if (g == -1) {
			space = true;
		} else if (g == -3) {
			out->junk++;
		}
	}
	out->text[out->len] = 0;
	ResolveIl(out->text);
	if (underlinedCount != 1)
		out->accel = 0;
	return true;
}

// Recognises the text whose column masks are in c->raw[kPad .. kPad + n).
// Bit r of a mask is row "top + r".  The masks are destroyed.
bool ParseColumns(Context* c, int n, int top, bool allowFuzzy, Parse* out)
{
	if (n <= 0 || n > kMaxW || c->fontCount == 0)
		return false;
	while (n > 0 && c->raw[kPad + n - 1] == 0)
		n--;
	if (n == 0)
		return false;
	uint64_t all = 0;
	for (int i = 0; i < n; i++)
		all |= c->raw[kPad + i];
	int low = __builtin_ctzll(all), high = 63 - __builtin_clzll(all);
	int inkHeight = high - low + 1;
	for (int i = 0; i < n; i++)
		c->raw[kPad + i] >>= low;
	memset(c->cur, 0, sizeof(uint64_t) * kPad);
	int tail = kPad + n;
	int clear = Min(kDP, tail + 80);

	bool found = false;
	Parse trial;
	for (int pass = 0; pass < (allowFuzzy ? 2 : 1) && !found; pass++) {
		bool fuzzy = pass == 1;
		// The tolerant pass is for text partly covered by the mouse pointer:
		// that text is in one of the fonts read a moment ago.  (Trying all
		// fonts on every TrueType row would cost most of the frame's time.)
		unsigned tried = fuzzy && c->fontCount > 6 ? 6 : c->fontCount;
		for (unsigned order = 0; order < tried; order++) {
			unsigned index = c->mru[order];
			const Font& font = c->fonts[index];
			if (inkHeight > font.height)
				continue;
			for (int offset = 0; offset + inkHeight <= font.height; offset++) {
				for (int i = 0; i < n; i++)
					c->cur[kPad + i] = c->raw[kPad + i] << offset;
				memset(c->cur + tail, 0, sizeof(uint64_t) * (clear - tail));
				if (!RunParser(c, font, n, fuzzy, &trial))
					continue;
				if (fuzzy) {
					if (trial.letters < 3 || trial.junk * 4 > (unsigned)n
						|| trial.junk > trial.glyphs * 2)
						continue;
				}
				if (!found || trial.cost < out->cost) {
					*out = trial;
					out->font = (int)index;
					out->cellTop = top + low - offset;
					found = true;
				}
			}
			if (found && !fuzzy)
				break;
		}
	}
	if (!found)
		return false;
	if (out->glyphs >= 5 && out->junk == 0) {
		unsigned at = 0;
		while (at < c->fontCount && c->mru[at] != out->font)
			at++;
		for (; at > 0 && at < c->fontCount; at--)
			c->mru[at] = c->mru[at - 1];
		c->mru[0] = (uint8_t)out->font;
	}
	return true;
}

// ---------------------------------------------------------------------------
// pass 0: dotted focus rectangles and dithered areas

// Is row "y" at x a checkerboard continuation of row "from"?  (The opposite
// phase: letters such as "will" alternate too, but in the same phase.)
bool DitherAt(const Context* c, int x, int y, int from)
{
	if (y < 0 || y >= c->h || x < 0 || x + 3 >= c->w)
		return false;
	const Colour* r = c->work + y * c->w + x;
	const Colour* o = c->work + from * c->w + x;
	return r[0] != r[1] && r[0] == r[2] && r[1] == r[3] && r[0] == o[1] && r[1] == o[0];
}

// Is row "other" the next row of a checkerboard whose row has colour "k" at
// x0, x0 + 2, ... x1?  It must have k on the other columns and something else
// on these.
bool LatticeNeighbour(const Context* c, int other, int x0, int x1, Colour k)
{
	if (other < 0 || other >= c->h)
		return false;
	const Colour* r = c->work + other * c->w;
	int total = 0, on = 0, off = 0;
	for (int x = x0; x < x1; x += 2) {
		total++;
		if (r[x + 1] == k) on++;
		if (r[x] != k) off++;
	}
	return on * 10 >= total * 9 && off * 2 >= total;
}

// Checkerboards: every other pixel of a row has one colour and the rows next
// to it have that colour on the other columns.  The other half may be one
// colour (a scroll bar track, a patterned desktop) or a picture (the desktop
// dimmed behind "Shut Down Windows").  Such areas are painted in a colour of
// their own before anything else looks at the frame.
void PaintLattices(Context* c)
{
	unsigned count = 0;
	for (int y = 0; y < c->h; y++) {
		const Colour* r = c->work + y * c->w;
		for (int parity = 0; parity < 2; parity++) {
			int x = parity;
			while (x + 2 < c->w) {
				Colour k = r[x];
				if (r[x + 2] != k || r[x + 1] == k) {
					x += 2;
					continue;
				}
				// 16 plain pixels end it: that is the face of something
				int end = x + 2, plain = 0;
				while (end + 2 < c->w && r[end + 2] == k) {
					plain = r[end + 1] == k ? plain + 1 : 0;
					if (plain == 8)
						break;
					end += 2;
				}
				int next = end + 2;
				// Its ends are where a neighbouring row still has the colour on
				// the other columns (a plain run or the edge of a frame does not).
				int start = x;
				const Colour* up = y > 0 ? r - c->w : r + c->w;
				const Colour* down = y + 1 < c->h ? r + c->w : r - c->w;
				while (end > start && (r[end - 1] == k
						|| (up[end - 1] != k && down[end - 1] != k)))
					end -= 2;
				while (end > start && (r[start + 1] == k
						|| (up[start + 1] != k && down[start + 1] != k)))
					start += 2;
				if (end - start >= 16 && count < kMaxLattice) {
					int off = 0, total = 0;
					for (int i = start; i < end; i += 2) {
						total++;
						if (r[i + 1] != k) off++;
					}
					if (off * 2 >= total
						&& (LatticeNeighbour(c, y - 1, start, end, k)
							|| LatticeNeighbour(c, y + 1, start, end, k))) {
						Lattice& seg = c->lattice[count++];
						seg.y = (int16_t)y;
						// an end pixel whose column does not alternate is the
						// plain surface beside the area
						seg.x0 = (int16_t)(up[start] == k && down[start] == k
							? start + 1 : start);
						seg.x1 = (int16_t)(up[end] == k && down[end] == k ? end - 1 : end);
						seg.pad = 0;
						Colour other = r[start + 1];
						seg.fill = 0x01000000u | (k < other ? k : other);
					}
				}
				x = next;
			}
		}
	}
	// painted afterwards: the rows are compared with their neighbours above
	for (unsigned i = 0; i < count; i++) {
		const Lattice& seg = c->lattice[i];
		Colour* r = c->work + seg.y * c->w;
		for (int x = seg.x0; x <= seg.x1; x++)
			r[x] = seg.fill;
	}
}

bool AltColumn(const Context* c, int x, int ya, int yb)
{
	if (x < 0 || x >= c->w)
		return false;
	int good = 0, total = 0;
	for (int y = ya + 2; y <= yb; y++) {
		total++;
		Colour p = P(c, x, y);
		if (p == P(c, x, y - 2) && p != P(c, x, y - 1))
			good++;
	}
	return total >= 3 && good * 100 >= total * 75;
}

void FindSegments(Context* c)
{
	c->segCount = 0;
	for (int y = 0; y < c->h; y++) {
		const Colour* r = c->work + y * c->w;
		int start = 0;
		for (int x = 2; x <= c->w; x++) {
			bool alt = x < c->w && r[x] == r[x - 2] && r[x] != r[x - 1];
			if (alt)
				continue;
			// the first and last pixel may be the end of a plain run beside it
			int from = start, to = x - 1;
			if (from > 0 && r[from - 1] == r[from])
				from++;
			if (to + 1 < c->w && r[to + 1] == r[to])
				to--;
			if (to - from + 1 >= 12 && c->segCount < kMaxSegs) {
				Seg& seg = c->segs[c->segCount++];
				seg.y = (int16_t)y;
				seg.x0 = (int16_t)from;
				seg.x1 = (int16_t)to;
				seg.dither = 0;
			}
			start = x - 1;
		}
	}
	// pieces of one dotted row (descenders and the pointer interrupt it)
	for (unsigned i = 0; i + 1 < c->segCount;) {
		Seg& a = c->segs[i];
		const Seg& b = c->segs[i + 1];
		if (b.y == a.y && b.x0 - a.x1 <= 8
			&& P(c, b.x0, b.y) == P(c, a.x0 + ((b.x0 - a.x0) & 1), a.y)
			&& P(c, b.x0 + 1, b.y) == P(c, a.x0 + ((b.x0 + 1 - a.x0) & 1), a.y)) {
			a.x1 = b.x1;
			for (unsigned k = i + 1; k + 1 < c->segCount; k++)
				c->segs[k] = c->segs[k + 1];
			c->segCount--;
		} else {
			i++;
		}
	}
	for (unsigned i = 0; i < c->segCount; i++) {
		Seg& seg = c->segs[i];
		int mid = (seg.x0 + seg.x1) / 2 - 1;
		bool a = DitherAt(c, seg.x0 + 2, seg.y - 1, seg.y)
			|| DitherAt(c, seg.x0 + 2, seg.y + 1, seg.y);
		bool b = DitherAt(c, mid, seg.y - 1, seg.y) || DitherAt(c, mid, seg.y + 1, seg.y);
		seg.dither = a && b;
	}
}

void PatchFocus(Context* c, const RectI& f)
{
	Colour p = P(c, f.x0 + 4, f.y0), q = P(c, f.x0 + 5, f.y0);
	int np = 0, nq = 0;
	for (int pass = 0; pass < 2 && np + nq == 0; pass++) {
		int y = pass == 0 ? f.y0 + 1 : f.y0 - 1;
		if (y < 0 || y >= c->h)
			continue;
		for (int x = f.x0 + 1; x < f.x1; x++) {
			Colour v = P(c, x, y);
			if (v == p) np++;
			else if (v == q) nq++;
		}
	}
	Colour bg = np >= nq ? p : q, dot = np >= nq ? q : p;
	for (int x = f.x0; x <= f.x1; x++) {
		if (P(c, x, f.y0) == dot) c->work[f.y0 * c->w + x] = bg;
		if (P(c, x, f.y1) == dot) c->work[f.y1 * c->w + x] = bg;
	}
	for (int y = f.y0; y <= f.y1; y++) {
		if (P(c, f.x0, y) == dot) c->work[y * c->w + f.x0] = bg;
		if (P(c, f.x1, y) == dot) c->work[y * c->w + f.x1] = bg;
	}
}

void FindFocusRects(Context* c)
{
	c->focusCount = 0;
	for (unsigned i = 0; i < c->segCount; i++) {
		const Seg& top = c->segs[i];
		if (top.dither)
			continue;
		for (unsigned j = i + 1; j < c->segCount; j++) {
			const Seg& bottom = c->segs[j];
			int dy = bottom.y - top.y;
			if (dy > 400)
				break;
			if (bottom.dither || dy < 5)
				continue;
			// one end may be shortened by a descender or the pointer
			int d0 = Abs(bottom.x0 - top.x0), d1 = Abs(bottom.x1 - top.x1);
			if (!((d0 <= 1 && d1 <= 10) || (d1 <= 1 && d0 <= 10)))
				continue;
			int left = -1, right = -1;
			for (int x = Min(top.x0, bottom.x0) - 2; x <= Min(top.x0, bottom.x0) + 1; x++) {
				if (AltColumn(c, x, top.y, bottom.y)) { left = x; break; }
			}
			for (int x = Max(top.x1, bottom.x1) + 2; x >= Max(top.x1, bottom.x1) - 1; x--) {
				if (AltColumn(c, x, top.y, bottom.y)) { right = x; break; }
			}
			if (left < 0 || right < 0 || c->focusCount >= kMaxFocus)
				continue;
			RectI& f = c->focus[c->focusCount++];
			f.x0 = (int16_t)left;
			f.x1 = (int16_t)right;
			f.y0 = top.y;
			f.y1 = bottom.y;
			break;
		}
	}
	// patch after detection so that neighbouring tests see the real picture
	for (unsigned i = 0; i < c->focusCount; i++)
		PatchFocus(c, c->focus[i]);
	// A checkerboard starts one pixel later on every other row: give all rows
	// of one area the same ends.
	for (int y = 0; y <= c->h; y++)
		c->rowStart[y] = c->segCount;
	for (unsigned i = c->segCount; i-- > 0;)
		c->rowStart[c->segs[i].y] = i;
	for (int y = c->h - 1; y >= 0; y--) {
		if (c->rowStart[y] > c->rowStart[y + 1])
			c->rowStart[y] = c->rowStart[y + 1];
	}
	for (unsigned i = 0; i < c->segCount; i++) {
		Seg& seg = c->segs[i];
		seg.pad = 0;
		if (!seg.dither)
			continue;
		for (int dy = -1; dy <= 1; dy += 2) {
			int y = seg.y + dy;
			if (y < 0 || y >= c->h)
				continue;
			for (unsigned k = c->rowStart[y]; k < c->rowStart[y + 1]; k++) {
				const Seg& other = c->segs[k];
				if (!other.dither)
					continue;
				if (other.x0 == seg.x0 - 1)
					seg.pad |= 1;
				if (other.x1 == seg.x1 + 1)
					seg.pad |= 2;
			}
		}
	}
	for (unsigned i = 0; i < c->segCount; i++) {
		Seg seg = c->segs[i];
		if (!seg.dither)
			continue;
		Colour* r = c->work + seg.y * c->w;
		Colour a = r[seg.x0], b = r[seg.x0 + 1];
		if (seg.pad & 1)
			seg.x0--;
		if (seg.pad & 2)
			seg.x1++;
		// a colour of its own, so that the area joins nothing around it
		Colour fill = 0x01000000u | (a < b ? a : b);
		for (int x = seg.x0; x <= seg.x1; x++)
			r[x] = fill;
	}
}

// ---------------------------------------------------------------------------
// pass 1: boxes (rectangular areas of one colour with straight left and right
// borders; text inside does not matter)

Colour DominantRow(const Context* c, int y, int xa, int xb)
{
	if (y < 0 || y >= c->h)
		return kNone;
	xa = Max(xa, 0);
	xb = Min(xb, c->w - 1);
	if (xb < xa)
		return kNone;
	Colour candidate = 0;
	int votes = 0;
	for (int x = xa; x <= xb; x++) {
		Colour p = P(c, x, y);
		if (votes == 0) candidate = p, votes = 1;
		else if (p == candidate) votes++;
		else votes--;
	}
	int count = 0;
	for (int x = xa; x <= xb; x++) {
		if (P(c, x, y) == candidate)
			count++;
	}
	return count * 4 >= (xb - xa + 1) * 3 ? candidate : kNone;
}

Colour DominantColumn(const Context* c, int x, int ya, int yb)
{
	if (x < 0 || x >= c->w)
		return kNone;
	ya = Max(ya, 0);
	yb = Min(yb, c->h - 1);
	if (yb < ya)
		return kNone;
	Colour candidate = 0;
	int votes = 0;
	for (int y = ya; y <= yb; y++) {
		Colour p = P(c, x, y);
		if (votes == 0) candidate = p, votes = 1;
		else if (p == candidate) votes++;
		else votes--;
	}
	int count = 0;
	for (int y = ya; y <= yb; y++) {
		if (P(c, x, y) == candidate)
			count++;
	}
	return count * 4 >= (yb - ya + 1) * 3 ? candidate : kNone;
}

void CloseBox(Context* c, const Active& a)
{
	// rows above the first complete one may be interrupted (mouse pointer, caret)
	int top = a.y0;
	while (top > 0 && P(c, a.x0, top - 1) == a.colour && P(c, a.x1, top - 1) == a.colour
		&& PS(c, a.x0 - 1, top - 1) != a.colour && PS(c, a.x1 + 1, top - 1) != a.colour)
		top--;
	int height = a.lastGood - top + 1;
	if (height < 5 || c->boxCount >= kMaxBoxes)
		return;
	for (unsigned i = 0; i < c->boxCount; i++) {
		Box& other = c->boxes[i];
		if (other.x0 == a.x0 && other.x1 == a.x1 && other.colour == a.colour
			&& other.y1 >= top - 1 && other.y0 <= a.lastGood + 1) {
			other.y0 = (int16_t)Min(other.y0, top);
			other.y1 = (int16_t)Max(other.y1, a.lastGood);
			return;
		}
	}
	Box& box = c->boxes[c->boxCount++];
	memset(&box, 0, sizeof(box));
	box.x0 = a.x0;
	box.x1 = a.x1;
	box.y0 = (int16_t)top;
	box.y1 = a.lastGood;
	box.colour = a.colour;
}

void FindBoxes(Context* c)
{
	c->boxCount = 0;
	unsigned active = 0;
	for (int y = 0; y <= c->h; y++) {
		if (y < c->h) {
			const Colour* r = c->work + y * c->w;
			int a = 0;
			while (a < c->w) {
				int b = a;
				Colour colour = r[a];
				while (b + 1 < c->w && r[b + 1] == colour)
					b++;
				if (b - a + 1 >= 6) {
					unsigned k = 0;
					for (; k < active; k++) {
						Active& box = c->active[k];
						if (box.x0 == a && box.x1 == b && box.colour == colour)
							break;
					}
					if (k < active) {
						c->active[k].lastFull = c->active[k].lastGood = (int16_t)y;
						c->active[k].seen = (int16_t)y;
						c->active[k].weak = 0;
					} else if (active < kMaxActive) {
						Active& box = c->active[active++];
						box.x0 = (int16_t)a;
						box.x1 = (int16_t)b;
						box.y0 = box.lastFull = box.lastGood = box.seen = (int16_t)y;
						box.weak = 0;
						box.colour = colour;
					}
				}
				a = b + 1;
			}
		}
		for (unsigned k = 0; k < active;) {
			Active& box = c->active[k];
			bool keep = false;
			if (y < c->h) {
				if (box.seen == y) {
					keep = true;
				} else {
					bool left = P(c, box.x0, y) == box.colour
						&& PS(c, box.x0 - 1, y) != box.colour;
					bool right = P(c, box.x1, y) == box.colour
						&& PS(c, box.x1 + 1, y) != box.colour;
					if (left && right) {
						keep = true;
						if (box.weak == 0)
							box.lastGood = (int16_t)y;
					} else if ((left || right) && box.weak < 20) {
						box.weak++;
						keep = true;
					} else if (box.weak < 20 && PS(c, box.x0 - 1, y) != box.colour
						&& PS(c, box.x1 + 1, y) != box.colour) {
						// letters touch both ends (a selection as wide as its text)
						int same = 0;
						for (int x = box.x0; x <= box.x1; x++) {
							if (P(c, x, y) == box.colour)
								same++;
						}
						if (same * 2 > box.x1 - box.x0 + 1) {
							box.weak++;
							keep = true;
						}
					}
				}
			}
			if (keep) {
				k++;
				continue;
			}
			CloseBox(c, box);
			c->active[k] = c->active[--active];
		}
	}

	// One field whose right edge moves by a pixel or two (a scroll bar whose
	// buttons and track start in different columns): one box.
	for (unsigned i = 0; i < c->boxCount; i++) {
		Box& a = c->boxes[i];
		for (unsigned j = 0; j < c->boxCount;) {
			Box& b = c->boxes[j];
			if (j != i && b.colour == a.colour && b.x0 == a.x0 && Abs(b.x1 - a.x1) <= 2
				&& b.x1 != a.x1 && b.y0 <= a.y1 + 2 && b.y1 >= a.y0 - 2) {
				if (b.y1 - b.y0 > a.y1 - a.y0)
					a.x1 = b.x1;
				a.y0 = (int16_t)Min(a.y0, b.y0);
				a.y1 = (int16_t)Max(a.y1, b.y1);
				c->boxes[j] = c->boxes[--c->boxCount];
				if (i == c->boxCount) {
					// "a" was the last one and has just been moved to j
					i = j;
				}
				j = 0;
				continue;
			}
			j++;
		}
	}
	for (unsigned k = 0; k < c->focusCount && c->boxCount < kMaxBoxes; k++) {
		const RectI& f = c->focus[k];
		Colour inside = DominantRow(c, f.y0, f.x0, f.x1);
		if (inside == kNone || f.y0 == 0 || inside == DominantRow(c, f.y0 - 1, f.x0, f.x1))
			continue;
		bool known = false;
		for (unsigned i = 0; i < c->boxCount; i++) {
			const Box& box = c->boxes[i];
			if (box.x0 == f.x0 && box.x1 == f.x1 && box.colour == inside
				&& box.y0 <= f.y0 + 2 && box.y1 >= f.y1 - 2)
				known = true;
		}
		if (known)
			continue;
		Box& box = c->boxes[c->boxCount++];
		memset(&box, 0, sizeof(box));
		box.x0 = f.x0;
		box.x1 = f.x1;
		box.y0 = f.y0;
		box.y1 = f.y1;
		box.colour = inside;
	}
	for (unsigned i = 0; i < c->boxCount; i++) {
		Box& box = c->boxes[i];
		int inset = box.x1 - box.x0 >= 8 ? 1 : 0;
		int insetY = box.y1 - box.y0 >= 8 ? 1 : 0;
		for (int d = 1; d <= 4; d++) {
			box.e[kSideT][d - 1] = DominantRow(c, box.y0 - d, box.x0 + inset, box.x1 - inset);
			box.e[kSideB][d - 1] = DominantRow(c, box.y1 + d, box.x0 + inset, box.x1 - inset);
			box.e[kSideL][d - 1] = DominantColumn(c, box.x0 - d, box.y0 + insetY, box.y1 - insetY);
			box.e[kSideR][d - 1] = DominantColumn(c, box.x1 + d, box.y0 + insetY, box.y1 - insetY);
		}
		// The first ring that differs between top and bottom tells the relief
		// (Windows 3.x fields have a one-colour outline first).
		// Rings in the box's own colour belong to the field (the light inner
		// edge of a grey field, the top of a scroll bar inside a frame).
		for (int side = 0; side < 4; side++) {
			int skip = 0;
			while (skip < 2 && box.e[side][skip] == box.colour)
				skip++;
			if (skip > 0 && box.e[side][skip] != box.colour) {
				for (int d = 0; d + skip < 4; d++)
					box.e[side][d] = box.e[side][d + skip];
				for (int d = 4 - skip; d < 4; d++)
					box.e[side][d] = kNone;
			}
		}
		box.kind = kFlat;
		if (box.colour & 0xFF000000u)
			continue;       // a dithered area is never a control
		{
			// An etched or one-line frame (group box).  The top side is left
			// out of the test: the caption interrupts it.
			const Colour* l = box.e[kSideL];
			const Colour* b = box.e[kSideB];
			const Colour* r = box.e[kSideR];
			bool one = l[0] != kNone && l[0] != box.colour && l[0] == r[0] && l[0] == b[0]
				&& l[1] == box.colour && r[1] == box.colour && b[1] == box.colour;
			bool two = l[0] != kNone && l[1] != kNone && l[0] != l[1] && l[0] != box.colour
				&& l[1] != box.colour && r[0] == l[1] && r[1] == l[0] && b[0] == l[1]
				&& b[1] == l[0] && l[2] == box.colour;
			if ((one || two) && box.x1 - box.x0 >= 40 && box.y1 - box.y0 >= 16) {
				box.kind = kEtched;
				continue;
			}
		}
		for (int d = 0; d < 3; d++) {
			Colour t = box.e[kSideT][d], l = box.e[kSideL][d], b = box.e[kSideB][d],
				r = box.e[kSideR][d];
			if (t == kNone || b == kNone || t != l)
				break;
			if (t == b) {
				if (t != r || t == box.colour)
					break;
				continue;
			}
			if (Lum(t) < Lum(b) && Lum(box.e[kSideT][0]) <= Lum(box.colour)
				&& (d > 0 || b != box.colour))
				box.kind = kSunken;
			else if (d == 0 && b == r && Lum(t) > Lum(b) && b != box.colour
				&& t != box.colour)
				box.kind = kRaised;
			break;
		}
	}
}

void AddExclusion(Context* c, int x0, int y0, int x1, int y1)
{
	if (c->exclCount >= kMaxExcl)
		return;
	RectI& r = c->excl[c->exclCount++];
	r.x0 = (int16_t)x0;
	r.y0 = (int16_t)y0;
	r.x1 = (int16_t)x1;
	r.y1 = (int16_t)y1;
}

bool IsCheckBox(const Box& box)
{
	int w = box.x1 - box.x0 + 1, h = box.y1 - box.y0 + 1;
	if (box.kind != kSunken || w < 7 || w > 13 || h < 7 || h > 13 || Abs(w - h) > 1)
		return false;
	// a complete two-ring frame (the gap between two letters can look sunken)
	const Colour* t = box.e[kSideT];
	const Colour* l = box.e[kSideL];
	const Colour* b = box.e[kSideB];
	const Colour* r = box.e[kSideR];
	return t[1] != kNone && t[1] == l[1] && t[1] != box.colour && t[1] != t[0]
		&& b[0] != kNone && b[0] == r[0] && b[1] != kNone && b[1] == r[1];
}

// Finds the caret (a thin vertical bar taller than any glyph) in sunken boxes
// and paints it over.
void FindCarets(Context* c)
{
	c->caretCount = 0;
	for (unsigned i = 0; i < c->boxCount && c->caretCount < kMaxCarets; i++) {
		const Box& box = c->boxes[i];
		if (box.kind != kSunken || box.y1 - box.y0 < 11 || IsCheckBox(box))
			continue;
		bool found = false;
		for (int x = box.x0; x <= box.x1 && !found; x++) {
			int y = box.y0;
			while (y <= box.y1) {
				Colour p = P(c, x, y);
				if (p == box.colour) { y++; continue; }
				int end = y;
				while (end + 1 <= box.y1 && P(c, x, end + 1) == p)
					end++;
				int length = end - y + 1;
				// one or two pixels wide
				int wide = 1;
				if (x + 1 <= box.x1 && P(c, x + 1, y) == p && P(c, x + 1, end) == p)
					wide = 2;
				// (it stands on the paper of the field: a window lying on the
				// field has its own colours around its letters and lines)
				if (length >= 12 && length <= 28 && (y > box.y0 || end < box.y1)
					&& PS(c, x - 1, y) != p && PS(c, x + wide, y) != p
					&& PS(c, x - 1, end) != p && PS(c, x + wide, end) != p
					&& (PS(c, x - 1, y) == box.colour || PS(c, x + wide, y) == box.colour)
					&& (PS(c, x - 1, end) == box.colour
						|| PS(c, x + wide, end) == box.colour)) {
					// bare on both sides for the whole height (a letter stem
					// is not, except "l" and "I", which are shorter than a line)
					// (a letter may touch it in between: only the ends are bare)
					bool bare = true;
					for (int yy = y; yy <= end; yy++) {
						if (P(c, x + wide - 1, yy) != p) { bare = false; break; }
					}
					if (bare) {
						Caret& caret = c->carets[c->caretCount++];
						caret.x = (int16_t)x;
						caret.y0 = (int16_t)y;
						caret.y1 = (int16_t)end;
						caret.width = (int16_t)wide;
						caret.paper = box.colour;
						found = true;
						break;
					}
				}
				y = end + 1;
			}
		}
	}
	for (unsigned i = 0; i < c->caretCount; i++) {
		const Caret& caret = c->carets[i];
		for (int y = caret.y0; y <= caret.y1; y++) {
			for (int k = 0; k < caret.width; k++)
				c->work[y * c->w + caret.x + k] = caret.paper;
		}
	}
}

void SortU16(uint16_t* order, unsigned count, const int32_t* keys);

// ---------------------------------------------------------------------------
// raised frames (pop-up menus, windows)

void AddEdge(Context* c, unsigned* count, int x, int y0, int y1)
{
	if (*count >= kMaxEdges)
		return;
	Edge& edge = c->edges[(*count)++];
	edge.x = (int16_t)x;
	edge.y0 = (int16_t)y0;
	edge.y1 = (int16_t)y1;
	edge.leftTop = edge.leftBottom = -1;
	Colour face = P(c, x - 1, y0), shade = P(c, x, y0), dark = P(c, x + 1, y0);
	// bottom border: the same two colours, lying
	if (y1 + 2 < c->h) {
		int left = x;
		while (left > 0 && P(c, left - 1, y1 + 1) == shade && P(c, left - 1, y1 + 2) == dark)
			left--;
		if (x - left >= 30 && P(c, x, y1 + 1) == shade)
			edge.leftBottom = (int16_t)left;
	}
	// top border: a light line
	if (y0 >= 1) {
		Colour light = P(c, x - 1, y0 - 1);
		if (light != face && Lum(light) > Lum(face)) {
			int left = x - 1;
			while (left > 0 && P(c, left - 1, y0 - 1) == light)
				left--;
			if (x - 1 - left >= 30)
				edge.leftTop = (int16_t)left;
		}
	}
}

void FindPanels(Context* c, int32_t* keys)
{
	c->panelCount = 0;
	unsigned count = 0;
	for (int x = 0; x < c->w; x++)
		c->edgeStart[x] = -1;
	for (int y = 0; y <= c->h; y++) {
		const Colour* r = y < c->h ? c->work + y * c->w : nullptr;
		for (int x = 1; x + 1 < c->w; x++) {
			bool valid = false;
			if (r != nullptr) {
				Colour a = r[x - 1], b = r[x], d = r[x + 1];
				if (a != b && b != d && !((a | b | d) & 0xFF000000u)) {
					int lum = Lum(b);
					valid = Lum(d) < lum && lum < Lum(a);
				}
			}
			int start = c->edgeStart[x];
			if (start < 0) {
				if (valid)
					c->edgeStart[x] = (int16_t)y;
				continue;
			}
			if (valid) {
				const Colour* o = c->work + start * c->w + x;
				if (o[-1] == r[x - 1] && o[0] == r[x] && o[1] == r[x + 1])
					continue;
			}
			if (y - start >= 12)
				AddEdge(c, &count, x, start, y - 1);
			c->edgeStart[x] = (int16_t)(valid ? y : -1);
		}
	}
	for (unsigned i = 0; i < count; i++) {
		keys[i] = c->edges[i].x * 1024 + c->edges[i].y0;
		c->order[i] = (uint16_t)i;
	}
	SortU16(c->order, count, keys);
	for (unsigned i = 0; i < count;) {
		const Edge& first = c->edges[c->order[i]];
		Colour face = P(c, first.x - 1, first.y0), shade = P(c, first.x, first.y0),
			dark = P(c, first.x + 1, first.y0);
		// pieces of one border below each other (something covers the gaps)
		unsigned j = i;
		while (j + 1 < count && c->edges[c->order[j]].leftBottom < 0) {
			const Edge& next = c->edges[c->order[j + 1]];
			if (next.x != first.x || next.leftTop >= 0 || P(c, next.x - 1, next.y0) != face
				|| P(c, next.x, next.y0) != shade || P(c, next.x + 1, next.y0) != dark)
				break;
			j++;
		}
		const Edge& last = c->edges[c->order[j]];
		i = j + 1;
		int left = last.leftBottom >= 0 ? last.leftBottom : first.leftTop;
		if (left < 2)
			continue;
		// "left" is the column of the light inner line
		int top = first.y0 - 2, bottom = last.y1 + 2;
		int probe = last.leftBottom >= 0 ? last.y1 : first.y0 - 1;
		Colour light = P(c, left, probe);
		if (light == face || Lum(light) <= Lum(face))
			continue;
		if (first.leftTop < 0) {
			int y = probe;
			while (y > 0 && P(c, left, y - 1) == light)
				y--;
			top = Min(top, y - 1);
		}
		if (last.leftBottom < 0) {
			int y = probe;
			while (y + 1 < c->h && P(c, left, y + 1) == light)
				y++;
			bottom = Max(bottom, y + 2);
		}
		if (top < 0 || bottom >= c->h || c->panelCount >= kMaxPanels)
			continue;
		Panel& panel = c->panels[c->panelCount++];
		panel.x0 = (int16_t)(left - 1);
		panel.x1 = (int16_t)(first.x + 1);
		panel.y0 = (int16_t)top;
		panel.y1 = (int16_t)bottom;
		panel.face = face;
		panel.light = light;
		panel.shade = shade;
		panel.used = 0;
		PCWIN_LOG("panel (%d,%d)-(%d,%d) face=%06X\n", panel.x0, panel.y0, panel.x1,
			panel.y1, face);
	}
}

// ---------------------------------------------------------------------------
// pass 2: connected components of short runs (glyph candidates)

uint32_t Find(Context* c, uint32_t i)
{
	while (c->runs[i].parent != i) {
		c->runs[i].parent = c->runs[c->runs[i].parent].parent;
		i = c->runs[i].parent;
	}
	return i;
}

void FindComponents(Context* c)
{
	c->runCount = 0;
	c->compCount = 0;
	for (int y = 0; y < c->h; y++) {
		c->rowStart[y] = c->runCount;
		const Colour* r = c->work + y * c->w;
		uint32_t prev = y > 0 ? c->rowStart[y - 1] : 0;
		uint32_t prevEnd = c->rowStart[y];
		int a = 0;
		while (a < c->w) {
			int b = a;
			Colour colour = r[a];
			while (b + 1 < c->w && r[b + 1] == colour)
				b++;
			if (b - a + 1 <= kInkRun && c->runCount < kMaxRuns) {
				uint32_t index = c->runCount++;
				Run& run = c->runs[index];
				run.x0 = (int16_t)a;
				run.x1 = (int16_t)b;
				run.y = (int16_t)y;
				run.comp = 0xFFFF;
				run.parent = index;
				if (y > 0) {
					while (prev < prevEnd && c->runs[prev].x1 < a - 1)
						prev++;
					const Colour* above = r - c->w;
					for (uint32_t q = prev; q < prevEnd && c->runs[q].x0 <= b + 1; q++) {
						if (above[c->runs[q].x0] != colour)
							continue;
						uint32_t ra = Find(c, q), rb = Find(c, index);
						if (ra != rb)
							c->runs[rb].parent = ra;
					}
				}
			}
			a = b + 1;
		}
	}
	c->rowStart[c->h] = c->runCount;
	for (uint32_t i = 0; i < c->runCount; i++) {
		uint32_t root = Find(c, i);
		Run& top = c->runs[root];
		const Run& run = c->runs[i];
		if (top.comp == 0xFFFF) {
			if (c->compCount >= kMaxComps)
				continue;
			top.comp = (uint16_t)c->compCount;
			Comp& comp = c->comps[c->compCount++];
			comp.x0 = run.x0;
			comp.x1 = run.x1;
			comp.y0 = comp.y1 = run.y;
			comp.count = 0;
			comp.colour = P(c, run.x0, run.y);
			comp.bg = kNone;
			comp.state = 0;
			comp.shadow = 0;
			comp.line = -1;
		}
		Comp& comp = c->comps[top.comp];
		if (run.x0 < comp.x0) comp.x0 = run.x0;
		if (run.x1 > comp.x1) comp.x1 = run.x1;
		if (run.y > comp.y1) comp.y1 = run.y;
		comp.count += (uint32_t)(run.x1 - run.x0 + 1);
	}
}

// Radio button: a small filled octagon inside a two-tone ring.
bool IsDisc(const Context* c, const Comp& whole, Disc* disc)
{
	if (whole.x1 - whole.x0 > 14 || whole.y1 - whole.y0 > 14 || whole.y1 - whole.y0 < 5)
		return false;
	// The disc starts with the component's top row; its size follows from the
	// length of that row (the highlight arc may hang on at the bottom right).
	Colour f = whole.colour;
	int a = whole.x0;
	while (a <= whole.x1 && P(c, a, whole.y0) != f)
		a++;
	int b = a;
	while (b + 1 < c->w && P(c, b + 1, whole.y0) == f)
		b++;
	int top = b - a + 1;
	if (top < 2 || top > 7)
		return false;
	Comp comp = whole;
	comp.x0 = (int16_t)(a - 2);
	comp.x1 = (int16_t)(b + 2);
	comp.y1 = (int16_t)(comp.y0 + top + 3);
	int w = comp.x1 - comp.x0 + 1, h = comp.y1 - comp.y0 + 1;
	if (comp.x0 < 8 || comp.y0 < 3 || comp.x1 + 8 >= c->w || comp.y1 + 3 >= c->h)
		return false;
	int cx = comp.x0 + w / 2, cy = comp.y0 + h / 2;
	if (P(c, comp.x0, comp.y0) == f || P(c, comp.x1, comp.y0) == f
		|| P(c, comp.x0, comp.y1) == f || P(c, comp.x1, comp.y1) == f)
		return false;
	if (P(c, cx, comp.y0) != f || P(c, cx, comp.y1) != f || P(c, comp.x0, cy) != f
		|| P(c, comp.x1, cy) != f)
		return false;
	Colour centre = P(c, cx, cy);
	int fill = 0;
	for (int y = comp.y0; y <= comp.y1; y++) {
		for (int x = comp.x0; x <= comp.x1; x++) {
			Colour p = P(c, x, y);
			if (p == f || (centre != f && p == centre))
				fill++;
		}
	}
	if (fill * 100 < w * h * 70)
		return false;
	Colour rl = P(c, comp.x0 - 1, cy), rr = P(c, comp.x1 + 1, cy);
	Colour rt = P(c, cx, comp.y0 - 1), rb = P(c, cx, comp.y1 + 1);
	if (rl == f || rr == f || rt == f || rb == f || rl != rt || rr != rb)
		return false;
	if (rl == rr) {
		// one-colour ring: a letter's hole unless the outside differs from the inside
		int x = comp.x0 - 1;
		while (x > comp.x0 - 8 && P(c, x, cy) == rl)
			x--;
		if (P(c, x, cy) == f || P(c, x, cy) == rl)
			return false;
	}
	disc->x0 = (int16_t)(comp.x0 - 2);
	disc->y0 = (int16_t)(comp.y0 - 2);
	disc->x1 = (int16_t)(comp.x1 + 2);
	disc->y1 = (int16_t)(comp.y1 + 2);
	disc->checked = centre != f;
	return true;
}

int RunLength(const Context* c, int x, int y, int limit)
{
	const Colour* r = c->work + y * c->w;
	Colour p = r[x];
	int length = 1;
	for (int k = x - 1; k >= 0 && r[k] == p && length <= limit; k--)
		length++;
	for (int k = x + 1; k < c->w && r[k] == p && length <= limit; k++)
		length++;
	return length;
}

// Is the colour around a component a real background (it reaches a long run)
// and not the stroke of a glyph whose hole we are looking at?
bool TrueBackground(const Context* c, int x, int y, Colour bg)
{
	for (int direction = -1; direction <= 1; direction += 2) {
		int yy = y;
		for (int step = 0; step < 48 && yy >= 0 && yy < c->h; step++, yy += direction) {
			if (P(c, x, yy) != bg)
				break;
			if (RunLength(c, x, yy, kInkRun) > kInkRun)
				return true;
		}
	}
	return false;
}

void ValidateComp(Context* c, Comp& comp)
{
	comp.state = 2;
	int w = comp.x1 - comp.x0 + 1, h = comp.y1 - comp.y0 + 1;
	if (h > 40 || w > 400)
		return;
	for (unsigned i = 0; i < c->exclCount; i++) {
		const RectI& r = c->excl[i];
		if (comp.x0 >= r.x0 && comp.x1 <= r.x1 && comp.y0 >= r.y0 && comp.y1 <= r.y1)
			return;
	}
	const Colour f = comp.colour;
	int ex0 = Max(comp.x0 - 1, 0), ey0 = Max(comp.y0 - 1, 0);
	int ex1 = Min(comp.x1 + 1, c->w - 1), ey1 = Min(comp.y1 + 1, c->h - 1);

	Colour other[4];
	int count[4];
	int others = 0;
	for (int y = ey0; y <= ey1; y++) {
		const Colour* r = c->work + y * c->w;
		for (int x = ex0; x <= ex1; x++) {
			Colour p = r[x];
			if (p == f) {
				// part of a long run of our own colour: we are background
				if ((x == ex0 || r[x - 1] != f) && RunLength(c, x, y, kInkRun) > kInkRun)
					return;
				continue;
			}
			int k = 0;
			while (k < others && other[k] != p)
				k++;
			if (k == others) {
				if (others == 4)
					return;
				other[others] = p;
				count[others++] = 0;
			}
			count[k]++;
		}
	}
	if (others == 0)
		return;
	int best = 0;
	for (int k = 1; k < others; k++) {
		if (count[k] > count[best])
			best = k;
	}
	Colour bg = other[best];

	// a shadow one pixel right and below (disabled text of Windows 95)
	Colour shadow = kNone;
	for (int k = 0; k < others && shadow == kNone; k++) {
		if (k == best)
			continue;
		bool all = true;
		for (int y = ey0; y <= ey1 && all; y++) {
			for (int x = ex0; x <= ex1; x++) {
				if (P(c, x, y) == other[k] && PS(c, x - 1, y - 1) != f) { all = false; break; }
			}
		}
		if (all)
			shadow = other[k];
	}
	// are we such a shadow ourselves?
	{
		Colour source = kNone;
		bool all = true;
		for (int y = comp.y0; y <= comp.y1 && all; y++) {
			for (int x = comp.x0; x <= comp.x1; x++) {
				if (P(c, x, y) != f)
					continue;
				Colour q = PS(c, x - 1, y - 1);
				if (q == f)
					continue;
				if (q == bg || q == kNone || (source != kNone && q != source)) { all = false; break; }
				source = q;
			}
		}
		if (all && source != kNone)
			return;
	}

	// inside the box: only ink, background and shadow
	for (int y = comp.y0; y <= comp.y1; y++) {
		const Colour* r = c->work + y * c->w;
		for (int x = comp.x0; x <= comp.x1; x++) {
			if (r[x] != f && r[x] != bg && r[x] != shadow)
				return;
		}
	}
	// the ring around it: background, or a straight border of one colour
	int borders = 0;
	for (int side = 0; side < 4; side++) {
		bool horizontal = side < 2;
		int fixed = side == 0 ? comp.y0 - 1 : side == 1 ? comp.y1 + 1
			: side == 2 ? comp.x0 - 1 : comp.x1 + 1;
		if (fixed < 0 || fixed >= (horizontal ? c->h : c->w))
			continue;
		int from = horizontal ? comp.x0 : comp.y0, to = horizontal ? comp.x1 : comp.y1;
		bool clean = true, uniform = true;
		Colour first = horizontal ? P(c, from, fixed) : P(c, fixed, from);
		for (int k = from; k <= to; k++) {
			Colour p = horizontal ? P(c, k, fixed) : P(c, fixed, k);
			if (p != bg && p != f && p != shadow)
				clean = false;
			if (p != first)
				uniform = false;
		}
		if (clean)
			continue;
		if (!uniform)
			return;
		borders++;
	}
	if (borders > 2)
		return;

	// find a background pixel next to the ink and make sure it is background
	bool real = false, probed = false;
	for (int y = ey0; y <= ey1 && !probed; y++) {
		for (int x = ex0; x <= ex1; x++) {
			if (P(c, x, y) == bg) {
				real = TrueBackground(c, x, y, bg);
				probed = true;
				break;
			}
		}
	}
	if (!real) {
		// second chance from the other end
		for (int y = ey1; y >= ey0 && !real; y--) {
			for (int x = ex1; x >= ex0; x--) {
				if (P(c, x, y) == bg) {
					real = TrueBackground(c, x, y, bg);
					y = -1;
					break;
				}
			}
		}
	}
	if (!real)
		return;
	comp.bg = bg;
	comp.shadow = shadow != kNone;
	comp.state = 1;
}

void SortU16(uint16_t* order, unsigned count, const int32_t* keys)
{
	unsigned gap = 1;
	while (gap < count / 3)
		gap = gap * 3 + 1;
	for (; gap > 0; gap /= 3) {
		for (unsigned i = gap; i < count; i++) {
			uint16_t value = order[i];
			unsigned j = i;
			while (j >= gap && keys[order[j - gap]] > keys[value]) {
				order[j] = order[j - gap];
				j -= gap;
			}
			order[j] = value;
		}
	}
}

void AddToLine(Context* c, int lineIndex, Comp& comp, int compIndex)
{
	(void)compIndex;
	Line& line = c->lines[lineIndex];
	if (comp.x0 < line.x0) line.x0 = comp.x0;
	if (comp.x1 > line.x1) line.x1 = comp.x1;
	if (comp.y0 < line.y0) line.y0 = comp.y0;
	if (comp.y1 > line.y1) line.y1 = comp.y1;
	line.comps++;
	if (comp.shadow)
		line.shadow = 1;
	comp.line = (int16_t)lineIndex;
}

int NewLine(Context* c, Comp& comp, int compIndex, bool big)
{
	if (c->lineCount >= kMaxLines)
		return -1;
	int index = (int)c->lineCount++;
	Line& line = c->lines[index];
	line.x0 = comp.x0;
	line.x1 = comp.x1;
	line.y0 = comp.y0;
	line.y1 = comp.y1;
	line.fg = comp.colour;
	line.bg = comp.bg;
	line.comps = 0;
	line.shadow = 0;
	line.big = big;
	AddToLine(c, index, comp, compIndex);
	return index;
}

void GroupLines(Context* c, int32_t* keys)
{
	c->lineCount = 0;
	unsigned n = 0;
	for (unsigned i = 0; i < c->compCount; i++) {
		if (c->comps[i].state == 1) {
			keys[i] = c->comps[i].x0;
			c->order[n++] = (uint16_t)i;
		}
	}
	SortU16(c->order, n, keys);

	// letters first
	for (unsigned k = 0; k < n; k++) {
		Comp& comp = c->comps[c->order[k]];
		if (comp.y1 - comp.y0 + 1 < 5)
			continue;
		int best = -1, bestGap = 1 << 20;
		for (int l = (int)c->lineCount - 1; l >= 0; l--) {
			const Line& line = c->lines[l];
			if (line.fg != comp.colour || line.bg != comp.bg)
				continue;
			int gap = comp.x0 - line.x1 - 1;
			int limit = Max(9, line.y1 - line.y0 + 1);
			if (gap > limit)
				continue;
			if (comp.y0 > line.y1 || comp.y1 < line.y0)
				continue;
			// the side of a dotted focus rectangle stood between them: two
			// captions (icons of the desktop stand close together)
			bool split = false;
			for (unsigned f = 0; f < c->focusCount && gap >= 2; f++) {
				const RectI& r = c->focus[f];
				if (comp.y0 < r.y0 || comp.y1 > r.y1)
					continue;
				if ((r.x0 > line.x1 && r.x0 < comp.x0) || (r.x1 > line.x1 && r.x1 < comp.x0))
					split = true;
			}
			if (split)
				continue;
			if (gap < bestGap) {
				bestGap = gap;
				best = l;
			}
		}
		if (best >= 0)
			AddToLine(c, best, comp, c->order[k]);
		else
			NewLine(c, comp, c->order[k], true);
	}
	// dots, commas, dashes, quotes, underlines
	unsigned letterLines = c->lineCount;
	for (unsigned k = 0; k < n; k++) {
		Comp& comp = c->comps[c->order[k]];
		if (comp.line >= 0)
			continue;
		int best = -1, bestDistance = 1 << 20;
		for (unsigned l = 0; l < letterLines; l++) {
			const Line& line = c->lines[l];
			if (line.fg != comp.colour || line.bg != comp.bg)
				continue;
			if (comp.x1 < line.x0 - 8 || comp.x0 > line.x1 + 8)
				continue;
			int distance = comp.y0 > line.y1 ? comp.y0 - line.y1
				: comp.y1 < line.y0 ? line.y0 - comp.y1 : 0;
			if (distance > 4)
				continue;
			if (distance < bestDistance) {
				bestDistance = distance;
				best = (int)l;
			}
		}
		if (best >= 0)
			AddToLine(c, best, comp, c->order[k]);
	}
	// what is left: rows made only of small marks ("...", "-----")
	for (unsigned k = 0; k < n; k++) {
		Comp& comp = c->comps[c->order[k]];
		if (comp.line >= 0)
			continue;
		int best = -1;
		for (int l = (int)c->lineCount - 1; l >= (int)letterLines; l--) {
			const Line& line = c->lines[l];
			if (line.fg != comp.colour || line.bg != comp.bg)
				continue;
			int gap = comp.x0 - line.x1 - 1;
			if (gap > 8 || gap < -3)
				continue;
			if (comp.y0 > line.y1 + 2 || comp.y1 < line.y0 - 2)
				continue;
			best = l;
			break;
		}
		if (best >= 0)
			AddToLine(c, best, comp, c->order[k]);
		else
			NewLine(c, comp, c->order[k], false);
	}
}

int AddTLine(Context* c, const Parse& parse, int x0, int y0, int x1, int y1,
	Colour fg, Colour bg, bool shadow)
{
	if (c->tlineCount >= kMaxTLines)
		return -1;
	int index = (int)c->tlineCount++;
	TLine& t = c->tlines[index];
	memset(&t, 0, sizeof(t));
	t.x0 = (int16_t)x0;
	t.y0 = t.fy0 = (int16_t)y0;
	t.x1 = (int16_t)x1;
	t.y1 = t.fy1 = (int16_t)y1;
	t.cellX = (int16_t)parse.cellX;
	t.cellY = t.lastCellY = (int16_t)parse.cellTop;
	t.lines = 1;
	t.fg = fg;
	t.bg = bg;
	t.text = PoolAdd(c, parse.text, parse.len);
	t.accel = parse.accel;
	t.font = (uint8_t)parse.font;
	t.bold = c->fonts[parse.font].bold;
	t.damaged = parse.junk > 0;
	t.disabled = shadow || Abs(Lum(fg) - Lum(bg)) < 90;
	return index;
}

void ReadLines(Context* c)
{
	c->tlineCount = 0;
	c->unreadCount = 0;
	for (unsigned l = 0; l < c->lineCount; l++) {
		const Line& line = c->lines[l];
		int width = line.x1 - line.x0 + 1, height = line.y1 - line.y0 + 1;
		if (height > 40 || width > kMaxW)
			continue;
		if (!line.big && line.comps < 3)
			continue;
		for (int x = 0; x < width; x++)
			c->raw[kPad + x] = 0;
		for (int y = line.y0; y <= line.y1; y++) {
			const Colour* r = c->work + y * c->w + line.x0;
			uint64_t bit = 1ull << (y - line.y0);
			for (int x = 0; x < width; x++) {
				if (r[x] == line.fg)
					c->raw[kPad + x] |= bit;
			}
		}
		Parse parse;
		if (!ParseColumns(c, width, line.y0, line.comps >= 5, &parse)) {
			if (line.big && line.comps >= 5 && height >= 6 && c->unreadCount < kMaxUnread) {
				Unread& u = c->unread[c->unreadCount++];
				u.x0 = line.x0;
				u.y0 = line.y0;
				u.x1 = line.x1;
				u.y1 = line.y1;
				u.fg = line.fg;
				u.bg = line.bg;
				u.lines = 1;
				u.used = 0;
			}
			PCWIN_LOG("unread line (%d,%d)-(%d,%d) fg=%06X bg=%06X comps=%u\n", line.x0,
				line.y0, line.x1, line.y1, line.fg, line.bg, line.comps);
			continue;
		}
		PCWIN_LOG("line (%d,%d)-(%d,%d) font=%d junk=%u [%s]\n", line.x0, line.y0, line.x1,
			line.y1, parse.font, parse.junk, parse.text);
		if (!line.big) {
			// specks or a dotted pattern are not a row of dashes
			bool mark = false;
			for (const char* q = parse.text; *q; q++) {
				if (*q == ' ') { mark = false; break; }
				if (*q != '.' && *q != ',' && *q != '\'' && *q != '`')
					mark = true;
			}
			if (!mark)
				continue;
		}
		parse.cellX += line.x0;
		AddTLine(c, parse, line.x0, line.y0, line.x1, line.y1, line.fg, line.bg,
			line.shadow);
	}
}

// Is there a tall straight edge (a frame) between two pieces of text?  The
// mouse pointer is not one: it is shorter than a control.
bool WallBetween(const Context* c, int xa, int xb, int top, int bottom, Colour bg)
{
	int middle = (top + bottom) / 2;
	if (top - 6 < 0 || bottom + 6 >= c->h)
		return true;
	for (int x = xa; x <= xb; x++) {
		Colour p = P(c, x, middle);
		if (p == bg)
			continue;
		if (P(c, x, top - 6) == p && P(c, x, bottom + 6) == p)
			return true;
	}
	return false;
}

bool CoveredBetween(const Context* c, int xa, int xb, int top, int bottom, Colour bg,
	Colour fg)
{
	int foreign = 0;
	for (int y = top; y <= bottom; y++) {
		for (int x = xa; x <= xb; x++) {
			Colour p = P(c, x, y);
			if (p != bg && p != fg)
				foreign++;
		}
	}
	return foreign >= 4;
}

// Joins pieces of one row that were split by a wide gap or by something that
// covered a letter.
void MergeRows(Context* c)
{
	for (unsigned i = 0; i < c->tlineCount; i++) {
		TLine& a = c->tlines[i];
		if (a.used)
			continue;
		bool again = true;
		while (again) {
			again = false;
			int best = -1;
			for (unsigned j = 0; j < c->tlineCount; j++) {
				const TLine& b = c->tlines[j];
				if (j == i || b.used || b.font != a.font || b.cellY != a.cellY
					|| b.fg != a.fg || b.bg != a.bg)
					continue;
				int gap = b.x0 - a.x1;
				if (gap < 0 || gap > 60)
					continue;
				// A wide gap is one text only when something lies on it (the
				// mouse pointer) and no frame stands between.
				if (gap > 14 && (WallBetween(c, a.x1 + 1, b.x0 - 1, Min(a.y0, b.y0),
						Max(a.y1, b.y1), a.bg)
						|| !CoveredBetween(c, a.x1 + 1, b.x0 - 1, Min(a.y0, b.y0),
							Max(a.y1, b.y1), a.bg, a.fg)))
					continue;
				if (best < 0 || b.x0 < c->tlines[best].x0)
					best = (int)j;
			}
			if (best >= 0) {
				TLine& b = c->tlines[best];
				a.text = PoolJoin(c, a.text, b.text, " ");
				a.y0 = a.fy0 = (int16_t)Min(a.y0, b.y0);
				a.y1 = a.fy1 = (int16_t)Max(a.y1, b.y1);
				if (!a.accel)
					a.accel = b.accel;
				a.damaged |= b.damaged;
				if (b.x0 - a.x1 > 12)
					a.damaged = 1;      // something covers a letter or two
				a.x1 = b.x1;
				b.used = 1;
				again = true;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// assembly

int AddItem(Context* c, unsigned role, int x0, int y0, int x1, int y1)
{
	if (c->itemCount >= kMaxItems)
		return -1;
	int index = (int)c->itemCount++;
	Item& item = c->items[index];
	memset(&item, 0, sizeof(item));
	item.role = (uint8_t)role;
	item.percent = -1;
	item.x0 = (int16_t)x0;
	item.y0 = (int16_t)y0;
	item.x1 = (int16_t)x1;
	item.y1 = (int16_t)y1;
	item.cy = (int16_t)((y0 + y1) / 2);
	item.parent = -1;
	return index;
}

inline bool LineInside(const TLine& t, int x0, int y0, int x1, int y1)
{
	int cx = (t.x0 + t.x1) / 2, cy = (t.fy0 + t.fy1) / 2;
	return cx >= x0 && cx <= x1 && cy >= y0 && cy <= y1;
}

void SortLines(Context* c, int32_t* keys)
{
	unsigned n = 0;
	for (unsigned i = 0; i < c->tlineCount; i++) {
		keys[i] = c->tlines[i].cellY * 2048 + c->tlines[i].x0;
		c->torder[n++] = (uint16_t)i;
	}
	SortU16(c->torder, n, keys);
}

// Collects the text lines inside a rectangle, in reading order; rows are
// joined with "separator".  Returns the pool offset.
int TakeText(Context* c, int x0, int y0, int x1, int y1, bool* disabled,
	uint16_t* accel, bool* damaged)
{
	int text = 0, lastY = -1000, lastX = 0;
	bool allDisabled = true, any = false;
	for (unsigned k = 0; k < c->tlineCount; k++) {
		TLine& t = c->tlines[c->torder[k]];
		if (t.used || !LineInside(t, x0, y0, x1, y1))
			continue;
		// two pieces of one word (the selection ends inside it)
		bool glued = t.cellY == lastY && t.x0 - lastX < 4;
		text = PoolJoin(c, text, t.text, glued ? "" : " ");
		lastY = t.cellY;
		lastX = t.x1;
		t.used = 1;
		any = true;
		if (!t.disabled)
			allDisabled = false;
		if (accel && !*accel)
			*accel = t.accel;
		if (damaged && t.damaged)
			*damaged = true;
	}
	if (disabled)
		*disabled = any && allDisabled;
	return text;
}

bool IsPercent(const char* text, int* value)
{
	int v = 0, digits = 0;
	const char* p = text;
	while (*p >= '0' && *p <= '9' && digits < 4) {
		v = v * 10 + (*p - '0');
		digits++;
		p++;
	}
	while (*p == ' ')
		p++;
	if (digits == 0 || *p != '%' || p[1] != 0 || v > 100)
		return false;
	*value = v;
	return true;
}

// Text drawn across the border between the filled and the empty part of a
// progress bar: ink is whatever differs from the bar colour at that column.
bool ReadSplitText(Context* c, int x0, int y0, int x1, int y1, int split,
	Colour leftColour, Colour rightColour, Parse* parse, bool fuzzy = false)
{
	int width = x1 - x0 + 1, height = y1 - y0 + 1;
	if (width > kMaxW || height > 40 || width < 3)
		return false;
	for (int x = 0; x < width; x++)
		c->raw[kPad + x] = 0;
	int first = width, last = -1;
	for (int y = y0; y <= y1; y++) {
		const Colour* r = c->work + y * c->w + x0;
		uint64_t bit = 1ull << (y - y0);
		for (int x = 0; x < width; x++) {
			Colour bg = x0 + x < split ? leftColour : rightColour;
			if (r[x] != bg) {
				c->raw[kPad + x] |= bit;
				if (x < first) first = x;
				if (x > last) last = x;
			}
		}
	}
	if (last < first)
		return false;
	int n = last - first + 1;
	for (int x = 0; x < n; x++)
		c->raw[kPad + x] = c->raw[kPad + first + x];
	if (!ParseColumns(c, n, y0, fuzzy, parse))
		return false;
	parse->cellX += x0 + first;
	return true;
}

void MarkUsedInside(Context* c, int x0, int y0, int x1, int y1);
int AddTLine(Context* c, const Parse& parse, int x0, int y0, int x1, int y1,
	Colour fg, Colour bg, bool shadow);

// A selection bar: its text has the colour of the field around it, so the
// ordinary reading (ink on a background) sees only pieces of it.
void ReadSelection(Context* c, const Box& bar)
{
	Parse parse;
	if (!ReadSplitText(c, bar.x0, bar.y0, bar.x1, bar.y1, bar.x1 + 1, bar.colour,
			bar.colour, &parse, true))
		return;
	MarkUsedInside(c, bar.x0, bar.y0, bar.x1, bar.y1);
	Colour fg = kNone;
	for (int x = bar.x0; x <= bar.x1 && fg == kNone; x++) {
		for (int y = bar.y0; y <= bar.y1; y++) {
			if (P(c, x, y) != bar.colour) { fg = P(c, x, y); break; }
		}
	}
	int index = AddTLine(c, parse, bar.x0, bar.y0, bar.x1, bar.y1, fg, bar.colour, false);
	if (index >= 0) {
		c->tlines[index].x0 = (int16_t)parse.cellX;
		c->tlines[index].selection = 1;
		c->tlines[index].disabled = 0;
	}
}

void MarkUsedInside(Context* c, int x0, int y0, int x1, int y1)
{
	for (unsigned i = 0; i < c->tlineCount; i++) {
		TLine& t = c->tlines[i];
		if (!t.used && LineInside(t, x0, y0, x1, y1))
			t.used = 1;
	}
}

bool BoxInsideSunken(const Context* c, unsigned index)
{
	const Box& box = c->boxes[index];
	for (unsigned i = 0; i < c->boxCount; i++) {
		const Box& outer = c->boxes[i];
		if (i == index || outer.kind != kSunken)
			continue;
		if (box.x0 >= outer.x0 && box.x1 <= outer.x1 && box.y0 >= outer.y0
			&& box.y1 <= outer.y1)
			return true;
	}
	return false;
}

struct Assembly {
	int titleBox;
	RectI dialog;
	bool hasDialog;
};

void BuildProgress(Context* c)
{
	// (a) two flat boxes side by side: filled part | empty part
	for (unsigned i = 0; i < c->boxCount; i++) {
		Box& a = c->boxes[i];
		if (a.used || a.kind == kRaised)
			continue;
		int height = a.y1 - a.y0 + 1;
		if (height < 8 || height > 30)
			continue;
		for (unsigned j = 0; j < c->boxCount; j++) {
			Box& b = c->boxes[j];
			if (j == i || b.used || b.kind == kRaised || b.colour == a.colour)
				continue;
			if (b.x0 != a.x1 + 1 || Abs(b.y0 - a.y0) > 1 || Abs(b.y1 - a.y1) > 1)
				continue;
			int total = b.x1 - a.x0 + 1;
			if (total < 40)
				continue;
			// the same thing above, below and at both ends
			if (a.e[kSideT][0] != b.e[kSideT][0] || a.e[kSideB][0] != b.e[kSideB][0]
				|| a.e[kSideL][0] != b.e[kSideR][0] || a.e[kSideL][0] == kNone)
				continue;
			// the filled part is the darker one
			if (Lum(a.colour) >= Lum(b.colour) || ((a.colour | b.colour) & 0xFF000000u))
				continue;
			// The black outline of a default button with the dialog's face
			// right of it looks the same: but a button face lies in it.
			bool face = false;
			for (unsigned k = 0; k < c->boxCount && !face; k++) {
				const Box& inner = c->boxes[k];
				face = k != i && k != j && inner.kind == kRaised && inner.x0 >= a.x0
					&& inner.x1 <= b.x1 && inner.y0 >= a.y0 && inner.y1 <= a.y1
					&& inner.x1 - inner.x0 >= 14;
			}
			if (face)
				continue;
			int item = AddItem(c, kRoleProgress, a.x0 - 1, a.y0 - 1, b.x1 + 1, a.y1 + 1);
			if (item < 0)
				return;
			int percent = ((a.x1 - a.x0 + 1) * 100 + total / 2) / total;
			Parse parse;
			int value;
			if (ReadSplitText(c, a.x0, Max(a.y0, b.y0), b.x1, Min(a.y1, b.y1), b.x0,
					a.colour, b.colour, &parse)
				&& IsPercent(parse.text, &value))
				percent = value;
			c->items[item].percent = (int8_t)percent;
			MarkUsedInside(c, a.x0, a.y0 - 1, b.x1, a.y1 + 1);
			a.used = b.used = 1;
			a.role = b.role = kRoleProgress;
			break;
		}
	}
	// (b) one box holding only "NN%" (empty or full bar)
	for (unsigned i = 0; i < c->boxCount; i++) {
		Box& a = c->boxes[i];
		int height = a.y1 - a.y0 + 1, width = a.x1 - a.x0 + 1;
		if (a.used || a.kind == kRaised || height < 8 || height > 30 || width < 40)
			continue;
		int found = -1, others = 0, value = 0;
		for (unsigned k = 0; k < c->tlineCount; k++) {
			const TLine& t = c->tlines[k];
			if (t.used || !LineInside(t, a.x0, a.y0, a.x1, a.y1))
				continue;
			if (found < 0 && IsPercent(c->pool + t.text, &value))
				found = (int)k;
			else
				others++;
		}
		if (found < 0 || others > 0)
			continue;
		int item = AddItem(c, kRoleProgress, a.x0 - 1, a.y0 - 1, a.x1 + 1, a.y1 + 1);
		if (item < 0)
			return;
		c->items[item].percent = (int8_t)value;
		c->tlines[found].used = 1;
		a.used = 1;
		a.role = kRoleProgress;
	}
	// (c) common-control bar: thin sunken frame with blocks from the left
	for (unsigned i = 0; i < c->boxCount; i++) {
		Box& a = c->boxes[i];
		int height = a.y1 - a.y0 + 1, width = a.x1 - a.x0 + 1;
		if (a.used || a.kind != kSunken || height < 8 || height > 30 || width < 50)
			continue;
		// a thin frame: the second ring is already the surroundings
		if (a.e[kSideT][1] == kNone || a.e[kSideT][1] != a.e[kSideB][1]
			|| a.e[kSideT][1] == a.e[kSideT][0])
			continue;
		if (Lum(a.colour) > 235)
			continue;       // a white field is an edit
		bool text = false;
		for (unsigned k = 0; k < c->tlineCount; k++) {
			if (!c->tlines[k].used && LineInside(c->tlines[k], a.x0, a.y0, a.x1, a.y1))
				text = true;
		}
		if (text)
			continue;
		int right = a.x0 - 1, blocks = 0;
		Colour blockColour = kNone;
		bool bad = false;
		for (unsigned j = 0; j < c->boxCount; j++) {
			Box& b = c->boxes[j];
			if (j == i || b.x0 < a.x0 || b.x1 > a.x1 || b.y0 < a.y0 || b.y1 > a.y1
				|| b.colour == a.colour)
				continue;
			if (b.y1 - b.y0 + 1 < height - 6) { bad = true; break; }
			if (blockColour == kNone)
				blockColour = b.colour;
			if (b.colour != blockColour) { bad = true; break; }
			blocks++;
			if (b.x1 > right)
				right = b.x1;
		}
		if (bad)
			continue;
		if (blocks == 0) {
			// the interior of an empty bar is one box; a filled bar's blocks
			// are narrower than kInkRun, so look at the pixels
			int y = (a.y0 + a.y1) / 2;
			for (int x = a.x0; x <= a.x1; x++) {
				Colour p = P(c, x, y);
				if (p == a.colour)
					continue;
				if (blockColour == kNone)
					blockColour = p;
				if (p != blockColour) { bad = true; break; }
				right = x;
			}
			if (bad)
				continue;
		}
		int item = AddItem(c, kRoleProgress, a.x0 - 1, a.y0 - 1, a.x1 + 1, a.y1 + 1);
		if (item < 0)
			return;
		// the gap after the last block belongs to it
		int filled = right >= a.x0 ? right - a.x0 + 3 : 0;
		int percent = (filled * 100 + width / 2) / width;
		c->items[item].percent = (int8_t)Min(100, Max(0, percent));
		AddExclusion(c, a.x0 - 1, a.y0 - 1, a.x1 + 1, a.y1 + 1);
		a.used = 1;
		a.role = kRoleProgress;
	}
}

void BuildTitle(Context* c, Assembly* as, char* title)
{
	as->titleBox = -1;
	as->hasDialog = false;
	title[0] = 0;
	int bestScore = -1, bestLine = -1;
	for (unsigned i = 0; i < c->boxCount; i++) {
		const Box& box = c->boxes[i];
		int height = box.y1 - box.y0 + 1, width = box.x1 - box.x0 + 1;
		if (box.used || box.kind != kFlat || height < 12 || height > 28 || width < 80)
			continue;
		int found = -1, count = 0;
		for (unsigned k = 0; k < c->tlineCount; k++) {
			const TLine& t = c->tlines[k];
			if (t.used || t.bg != box.colour || !LineInside(t, box.x0, box.y0, box.x1, box.y1))
				continue;
			found = (int)k;
			count++;
		}
		if (count != 1 || !c->tlines[found].bold)
			continue;
		if (BoxInsideSunken(c, i))
			continue;
		int score = Sat(box.colour) * 4096 + (4095 - Min(4095, width));
		if (score > bestScore) {
			bestScore = score;
			as->titleBox = (int)i;
			bestLine = found;
		}
	}
	if (as->titleBox < 0)
		return;
	const Box& bar = c->boxes[as->titleBox];
	TLine& t = c->tlines[bestLine];
	t.used = 1;
	size_t length = strlen(c->pool + t.text);
	if (length >= kTitleBytes)
		length = kTitleBytes - 1;
	memcpy(title, c->pool + t.text, length);
	title[length] = 0;

	as->dialog.x0 = bar.x0;
	as->dialog.x1 = bar.x1;
	as->dialog.y0 = bar.y0;
	as->dialog.y1 = bar.y1;
	as->hasDialog = true;
	// the client area: boxes of one colour right below the bar
	int bottom = bar.y1;
	bool grown = true;
	while (grown) {
		grown = false;
		for (unsigned i = 0; i < c->boxCount; i++) {
			const Box& box = c->boxes[i];
			if (box.y0 <= bottom || box.y0 > bottom + 8)
				continue;
			if (box.x0 > bar.x0 + 6 || box.x1 < bar.x1 - 6 || box.x0 < bar.x0 - 12
				|| box.x1 > bar.x1 + 12)
				continue;
			if (box.y1 > bottom) {
				bottom = box.y1;
				grown = true;
				as->dialog.x0 = (int16_t)Min(as->dialog.x0, box.x0);
				as->dialog.x1 = (int16_t)Max(as->dialog.x1, box.x1);
			}
		}
	}
	if (bottom == bar.y1) {
		// no box: follow the colour under the bar's left end
		int x = bar.x0 + 1, y = bar.y1 + 3;
		if (y < c->h) {
			Colour face = P(c, x, y);
			while (y + 1 < c->h && P(c, x, y + 1) == face)
				y++;
			bottom = y;
		}
	}
	as->dialog.y1 = (int16_t)bottom;
}

void BuildContainers(Context* c, int32_t* keys)
{
	for (unsigned i = 0; i < c->boxCount; i++) {
		Box& box = c->boxes[i];
		if (box.used || box.kind != kSunken)
			continue;
		bool taken = false;
		for (unsigned k = 0; k < c->itemCount && !taken; k++) {
			const Item& item = c->items[k];
			int cx = (box.x0 + box.x1) / 2, cy = (box.y0 + box.y1) / 2;
			taken = cx >= item.x0 && cx <= item.x1 && cy >= item.y0 && cy <= item.y1;
		}
		if (taken)
			continue;       // part of a progress bar or of a field already built
		if (IsCheckBox(box)) {
			int marks = 0;
			for (int y = box.y0; y <= box.y1; y++) {
				for (int x = box.x0; x <= box.x1; x++) {
					if (P(c, x, y) != box.colour)
						marks++;
				}
			}
			int item = AddItem(c, kRoleCheckBox, box.x0 - 2, box.y0 - 2, box.x1 + 2, box.y1 + 2);
			if (item < 0)
				return;
			if (marks >= 3)
				c->items[item].flags |= kFlagChecked;
			box.used = 1;
			box.role = kRoleCheckBox;
			continue;
		}
		int width = box.x1 - box.x0 + 1;
		if (width < 16)
			continue;
		// stack: the part above a selection bar, the bar, the part below
		int x0 = box.x0, x1 = box.x1, y0 = box.y0, y1 = box.y1;
		int areaMain = (box.y1 - box.y0 + 1);
		Colour main = box.colour;
		box.used = 1;
		bool grown = true;
		while (grown) {
			grown = false;
			for (unsigned j = 0; j < c->boxCount; j++) {
				Box& below = c->boxes[j];
				if (below.used || below.x0 != x0 || below.x1 != x1 || below.y0 != y1 + 1)
					continue;
				int h = below.y1 - below.y0 + 1;
				if (below.colour != main && h > areaMain) {
					main = below.colour;
					areaMain = h;
				} else if (below.colour == main) {
					areaMain += h;
				}
				y1 = below.y1;
				below.used = 1;
				below.role = kRoleList;
				grown = true;
				break;
			}
		}
		int height = y1 - y0 + 1;
		if (height < 10)
			continue;
		bool single = height <= 26;
		for (unsigned j = 0; j < c->boxCount; j++) {
			const Box& bar = c->boxes[j];
			int h = bar.y1 - bar.y0 + 1;
			if (j == i || bar.colour == main || h < 8 || h > 26 || bar.x0 < x0
				|| bar.x1 > x1 || bar.y0 < y0 || bar.y1 > y1 || bar.kind == kRaised)
				continue;
			bool part = false;
			for (unsigned k = 0; k < c->boxCount && !part; k++) {
				const Box& whole = c->boxes[k];
				part = k != j && whole.colour == bar.colour && whole.x0 <= bar.x0
					&& whole.x1 >= bar.x1 && whole.y0 <= bar.y0 && whole.y1 >= bar.y1
					&& whole.y1 - whole.y0 <= 26
					&& (whole.x1 - whole.x0 > bar.x1 - bar.x0
						|| whole.y1 - whole.y0 > bar.y1 - bar.y0 || k < j);
			}
			if (part)
				continue;
			ReadSelection(c, bar);
			SortLines(c, keys);
		}
		// a drop-down button right of a single-line field
		bool combo = false;
		if (single) {
			for (unsigned j = 0; j < c->boxCount; j++) {
				const Box& button = c->boxes[j];
				if (button.kind != kRaised || button.x0 < x1 + 1 || button.x0 > x1 + 6)
					continue;
				if (button.y0 < y0 - 3 || button.y1 > y1 + 3 || button.x1 - button.x0 > 22)
					continue;
				combo = true;
			}
		}
		// rows
		int selected = -1;
		bool anySelection = false;
		for (unsigned k = 0; k < c->tlineCount; k++) {
			const TLine& t = c->tlines[c->torder[k]];
			if (t.used || !LineInside(t, x0, y0, x1, y1))
				continue;
			if (t.bg != main && selected < 0)
				selected = c->torder[k];
			if (t.selection)
				anySelection = true;
		}
		unsigned role = single ? (combo ? kRoleCombo : kRoleEdit)
			: (selected >= 0 ? kRoleList : kRoleEdit);
		int item = AddItem(c, role, x0 - 2, y0 - 2, x1 + 2, y1 + 2);
		if (item < 0)
			return;
		box.role = (uint8_t)role;
		for (unsigned k = 0; k < c->caretCount; k++) {
			const Caret& caret = c->carets[k];
			if (caret.x >= x0 && caret.x <= x1 && caret.y0 >= y0 && caret.y1 <= y1)
				c->items[item].flags |= kFlagCaret;
		}
		if (role == kRoleList) {
			c->items[item].text = c->tlines[selected].text;
			unsigned visible = 0;
			for (unsigned k = 0; k < c->tlineCount; k++) {
				TLine& t = c->tlines[c->torder[k]];
				if (t.used || !LineInside(t, x0, y0, x1, y1))
					continue;
				t.used = 1;
				int row = AddItem(c, kRoleListItem, x0, t.cellY, x1,
					t.cellY + c->fonts[t.font].height - 1);
				if (row < 0)
					return;
				c->items[row].parent = (int16_t)item;
				c->items[row].text = t.text;
				c->items[row].flags |= kFlagChild;
				visible++;
				if (t.bg != main) {
					c->items[row].flags |= kFlagSelected;
					c->items[item].pos = (uint8_t)visible;
				}
			}
			c->items[item].cnt = (uint8_t)visible;
			for (unsigned k = 0, n = 0; k < c->itemCount; k++) {
				if (c->items[k].parent == item) {
					c->items[k].pos = (uint8_t)++n;
					c->items[k].cnt = (uint8_t)visible;
				}
			}
		} else {
			bool disabled = false, damaged = false;
			if (selected >= 0 || anySelection)
				c->items[item].flags |= kFlagSelected;
			c->items[item].text = TakeText(c, x0, y0, x1, y1, &disabled, 0, &damaged);
			if (disabled)
				c->items[item].flags |= kFlagDisabled;
			if (damaged)
				c->items[item].flags |= kFlagDamaged;
			if (!single)
				c->items[item].flags |= kFlagMultiline;
		}
	}
}

void BuildButtons(Context* c)
{
	for (unsigned i = 0; i < c->boxCount; i++) {
		Box& box = c->boxes[i];
		if (box.used || box.kind != kRaised)
			continue;
		int width = box.x1 - box.x0 + 1, height = box.y1 - box.y0 + 1;
		if (width < 14 || height < 12 || height > 60)
			continue;
		bool any = false;
		for (unsigned k = 0; k < c->tlineCount; k++) {
			if (!c->tlines[k].used && LineInside(c->tlines[k], box.x0, box.y0, box.x1, box.y1))
				any = true;
		}
		if (!any)
			continue;
		int item = AddItem(c, kRoleButton, box.x0 - 3, box.y0 - 3, box.x1 + 3, box.y1 + 3);
		if (item < 0)
			return;
		bool disabled = false, damaged = false;
		uint16_t accel = 0;
		c->items[item].text = TakeText(c, box.x0, box.y0, box.x1, box.y1, &disabled,
			&accel, &damaged);
		c->items[item].accel = accel;
		if (disabled)
			c->items[item].flags |= kFlagDisabled;
		if (damaged)
			c->items[item].flags |= kFlagDamaged;
		// default button: one more dark outline than its neighbours have
		const Colour* t = box.e[kSideT];
		const Colour* b = box.e[kSideB];
		bool isDefault;
		if (t[1] == t[0])           // two-pixel bevel (Windows 3.x look)
			isDefault = t[2] != kNone && t[3] == t[2] && b[3] == b[2];
		else                        // Windows 95 look
			isDefault = t[1] != kNone && t[1] == b[1] && Lum(t[1]) < Lum(box.colour);
		if (isDefault)
			c->items[item].flags |= kFlagDefault;
		box.used = 1;
		box.role = kRoleButton;
	}
}

// A small mark of one colour beside a menu caption (check mark, bullet,
// submenu arrow): everything else in the rectangle must be the background.
bool MenuMark(const Context* c, int x0, int y0, int x1, int y1, Colour fg, Colour bg,
	int* width, int* height)
{
	x0 = Max(x0, 0);
	y0 = Max(y0, 0);
	x1 = Min(x1, c->w - 1);
	y1 = Min(y1, c->h - 1);
	int ax = c->w, bx = -1, ay = c->h, by = -1;
	for (int y = y0; y <= y1; y++) {
		for (int x = x0; x <= x1; x++) {
			Colour p = P(c, x, y);
			if (p == bg)
				continue;
			if (p != fg)
				return false;
			if (x < ax) ax = x;
			if (x > bx) bx = x;
			if (y < ay) ay = y;
			if (y > by) by = y;
		}
	}
	if (bx < 0)
		return false;
	*width = bx - ax + 1;
	*height = by - ay + 1;
	return true;
}

// Does something lie on panel "a" where panel "b" is?  (The newer pop-up
// menu covers the border of the one it was opened from.)
bool PanelCovered(const Context* c, const Panel& a, const Panel& b)
{
	int y0 = Max(a.y0, b.y0) + 2, y1 = Min(a.y1, b.y1) - 2;
	if (y1 - y0 < 4)
		return false;
	for (int side = 0; side < 2; side++) {
		int x = side == 0 ? a.x1 - 1 : a.x0 + 1;
		Colour colour = side == 0 ? a.shade : a.light;
		if (x < b.x0 || x > b.x1)
			continue;
		int other = 0;
		for (int y = y0; y <= y1; y++) {
			if (P(c, x, y) != colour)
				other++;
		}
		if (other * 2 > y1 - y0 + 1)
			return true;
	}
	return false;
}

struct MenuRow {
	int16_t line;              // caption
	int16_t more[3];           // further texts of the row (shortcut), -1 = none
	int16_t cy;
	uint8_t aligned, pad;
};

// Pop-up menus: a raised frame without controls whose texts stand in one
// column, further apart than the lines of a paragraph.
void BuildMenus(Context* c, int32_t* keys)
{
	int menuItem[kMaxMenus], menuPanel[kMaxMenus], menuBar[kMaxMenus];
	unsigned menus = 0;
	// small frames first: a menu lies inside the frame of its window
	unsigned order[kMaxPanels];
	for (unsigned i = 0; i < c->panelCount; i++)
		order[i] = i;
	for (unsigned i = 1; i < c->panelCount; i++) {
		unsigned value = order[i];
		const Panel& p = c->panels[value];
		int area = (p.x1 - p.x0) * (p.y1 - p.y0);
		unsigned j = i;
		while (j > 0) {
			const Panel& q = c->panels[order[j - 1]];
			if ((q.x1 - q.x0) * (q.y1 - q.y0) <= area)
				break;
			order[j] = order[j - 1];
			j--;
		}
		order[j] = value;
	}
	for (unsigned n = 0; n < c->panelCount && menus < kMaxMenus; n++) {
		const Panel& panel = c->panels[order[n]];
		int x0 = panel.x0 + 2, x1 = panel.x1 - 2, y0 = panel.y0 + 2, y1 = panel.y1 - 2;
		if (x1 - x0 < 36 || y1 - y0 < 14)
			continue;
		// the whole frame is there (a picture of a menu in a dialog is cut)
		int lit = 0;
		for (int x = panel.x0 + 1; x <= panel.x1 - 2; x++) {
			if (P(c, x, panel.y0 + 1) == panel.light)
				lit++;
		}
		if (lit * 10 < (panel.x1 - panel.x0 - 2) * 7)
			continue;
		// A window has controls (fields, buttons) or at least the buttons of
		// its title bar; the icons of a menu are smaller than a control.
		bool reject = false;
		for (unsigned k = 0; k < c->boxCount && !reject; k++) {
			const Box& box = c->boxes[k];
			if ((box.kind != kRaised && box.kind != kSunken) || box.x0 < x0 || box.x1 > x1
				|| box.y0 < y0 || box.y1 > y1)
				continue;
			int bw = box.x1 - box.x0 + 1, bh = box.y1 - box.y0 + 1;
			if (box.kind == kRaised && box.colour == panel.face && bw >= x1 - x0 - 3)
				continue;       // the face of the frame itself
			reject = (bw >= 36 && bh >= 10)
				|| (box.kind == kRaised && bw <= 22 && bh <= 26 && bw >= 8 && bh >= 8
					&& box.y0 <= y0 + 26 && box.x0 > (x0 + x1) / 2)
				|| IsCheckBox(box);
		}
		if (reject)
			continue;

		MenuRow rows[kMaxMenuRows];
		unsigned rowCount = 0;
		bool overflow = false;
		for (unsigned k = 0; k < c->tlineCount; k++) {
			int index = c->torder[k];
			const TLine& t = c->tlines[index];
			if (t.used || t.lines != 1 || !LineInside(t, x0, y0, x1, y1))
				continue;
			int cy = t.cellY + c->fonts[t.font].height / 2;
			unsigned row = 0;
			while (row < rowCount && Abs(rows[row].cy - cy) > 3)
				row++;
			if (row == rowCount) {
				if (rowCount >= kMaxMenuRows) {
					overflow = true;
					break;
				}
				rows[rowCount].line = (int16_t)index;
				rows[rowCount].more[0] = rows[rowCount].more[1] = rows[rowCount].more[2] = -1;
				rows[rowCount].cy = (int16_t)cy;
				rows[rowCount].aligned = 0;
				rowCount++;
				continue;
			}
			MenuRow& r = rows[row];
			int16_t add = (int16_t)index;
			if (t.x0 < c->tlines[r.line].x0) {
				add = r.line;
				r.line = (int16_t)index;
			}
			for (int m = 0; m < 3; m++) {
				if (r.more[m] < 0) { r.more[m] = add; break; }
				if (c->tlines[add].x0 < c->tlines[r.more[m]].x0) {
					int16_t swap = r.more[m];
					r.more[m] = add;
					add = swap;
				}
			}
		}
		if (overflow || rowCount == 0)
			continue;
		// the captions start in one column
		unsigned best = 0;
		int column = 0;
		for (unsigned i = 0; i < rowCount; i++) {
			unsigned same = 0;
			for (unsigned j = 0; j < rowCount; j++) {
				if (Abs(c->tlines[rows[j].line].cellX - c->tlines[rows[i].line].cellX) <= 1)
					same++;
			}
			if (same > best) {
				best = same;
				column = c->tlines[rows[i].line].cellX;
			}
		}
		// A check mark or bullet stands so close to its caption that the two
		// were taken for one unreadable text: read the part in the column.
		for (unsigned k = 0; k < c->unreadCount && rowCount < kMaxMenuRows; k++) {
			Unread& u = c->unread[k];
			int width = u.x1 - column + 1, height = u.y1 - u.y0 + 1;
			if (u.used || u.x0 >= column - 2 || width < 3 || height > 20
				|| (u.y0 + u.y1) / 2 < y0 || (u.y0 + u.y1) / 2 > y1 || u.x0 < x0 || u.x1 > x1)
				continue;
			for (int x = 0; x < width; x++)
				c->raw[kPad + x] = 0;
			for (int y = u.y0; y <= u.y1; y++) {
				const Colour* r = c->work + y * c->w + column;
				uint64_t bit = 1ull << (y - u.y0);
				for (int x = 0; x < width; x++) {
					if (r[x] == u.fg)
						c->raw[kPad + x] |= bit;
				}
			}
			int skip = 0;
			while (skip < width && c->raw[kPad + skip] == 0)
				skip++;
			if (skip == width)
				continue;
			for (int x = skip; x < width; x++)
				c->raw[kPad + x - skip] = c->raw[kPad + x];
			Parse parse;
			if (!ParseColumns(c, width - skip, u.y0, false, &parse))
				continue;
			parse.cellX += column + skip;
			if (Abs(parse.cellX - column) > 1)
				continue;
			int index = AddTLine(c, parse, column + skip, u.y0, u.x1, u.y1, u.fg, u.bg, false);
			if (index < 0)
				break;
			u.used = 1;
			MenuRow& row = rows[rowCount++];
			row.line = (int16_t)index;
			row.more[0] = row.more[1] = row.more[2] = -1;
			row.cy = (int16_t)(c->tlines[index].cellY + c->fonts[parse.font].height / 2);
			row.aligned = 0;
			best++;
			SortLines(c, keys);
		}
		// rows top to bottom
		for (unsigned i = 1; i < rowCount; i++) {
			MenuRow value = rows[i];
			unsigned j = i;
			while (j > 0 && rows[j - 1].cy > value.cy) {
				rows[j] = rows[j - 1];
				j--;
			}
			rows[j] = value;
		}
		bool anyBar = false, pitchOk = true;
		int previous = -1;
		for (unsigned i = 0; i < rowCount; i++) {
			const TLine& t = c->tlines[rows[i].line];
			// (a check mark read as covered letters before the caption)
			rows[i].aligned = Abs(t.cellX - column) <= 1
				|| (t.damaged && t.x0 < column - 2 && t.x0 >= x0 && t.x1 > column + 4);
			if (!rows[i].aligned)
				continue;
			if (t.bg != panel.face)
				anyBar = true;
			if (previous >= 0) {
				const TLine& above = c->tlines[rows[previous].line];
				if (t.cellY - above.cellY < c->fonts[above.font].height + 2)
					pitchOk = false;
			}
			previous = (int)i;
		}
		best = 0;
		for (unsigned i = 0; i < rowCount; i++) {
			if (rows[i].aligned)
				best++;
		}
		if (!pitchOk || best * 5 < rowCount * 4 || (best < 2 && !(rowCount == 1 && anyBar)))
			continue;

		int menu = AddItem(c, kRoleMenu, panel.x0, panel.y0, panel.x1, panel.y1);
		if (menu < 0)
			return;
		c->items[menu].cy = panel.y0;
		c->items[menu].cnt = (uint8_t)best;
		c->panels[order[n]].used = 1;
		menuItem[menus] = menu;
		menuPanel[menus] = (int)order[n];
		menuBar[menus] = -1;
		unsigned position = 0;
		for (unsigned i = 0; i < rowCount; i++) {
			TLine& t = c->tlines[rows[i].line];
			t.used = 1;
			for (int m = 0; m < 3; m++) {
				if (rows[i].more[m] >= 0)
					c->tlines[rows[i].more[m]].used = 1;
			}
			if (!rows[i].aligned)
				continue;
			const Font& font = c->fonts[t.font];
			int top = t.cellY - 1, bottom = t.cellY + font.height;
			int row = AddItem(c, kRoleMenuItem, x0 + 1, top, x1 - 1, bottom);
			if (row < 0)
				return;
			Item& item = c->items[row];
			item.parent = (int16_t)menu;
			item.text = t.text;
			item.accel = t.accel;
			item.pos = (uint8_t)++position;
			item.cnt = (uint8_t)best;
			int right = t.x1;
			for (int m = 0; m < 3; m++) {
				if (rows[i].more[m] < 0)
					continue;
				// the shortcut; a text in other colours lies on the menu (tool tip)
				const TLine& more = c->tlines[rows[i].more[m]];
				if (more.fg != t.fg || more.bg != t.bg)
					continue;
				item.text = PoolJoin(c, item.text, more.text, " ");
				right = Max(right, more.x1);
			}
			bool bar = t.bg != panel.face;
			if (bar) {
				item.flags |= kFlagSelected;
				if (menuBar[menus] < 0)
					menuBar[menus] = row;
			}
			bool marked = t.x0 < column - 2;
			if (t.damaged && !marked)
				item.flags |= kFlagDamaged;
			if (t.disabled || (bar && Abs(Lum(t.fg) - Lum(t.bg)) < 150))
				item.flags |= kFlagDisabled;
			int mw, mh;
			int textLeft = marked ? column : t.x0;
			if (textLeft - 3 - (x0 + 1) >= 5
				&& MenuMark(c, x0 + 1, t.cellY, textLeft - 3, bottom - 1, t.fg, t.bg, &mw, &mh)
				&& mw >= 4 && mw <= 10 && mh >= 4 && mh <= 10)
				item.flags |= kFlagChecked;
			// (the last columns may lie under the submenu)
			if (MenuMark(c, Max(right + 4, x1 - 14), t.cellY, x1 - 5, bottom - 1, t.fg, t.bg,
					&mw, &mh)
				&& mw >= 3 && mw <= 5 && mh >= 5 && mh <= 9)
				item.flags |= kFlagSubmenu;
			// something lies on the rest of the row: the caption may be cut
			for (int x = right + 2; x <= x1 - 16 && !(item.flags & kFlagDamaged); x++) {
				for (int y = t.cellY; y < bottom; y++) {
					Colour p = P(c, x, y);
					if (p != t.fg && p != t.bg) {
						item.flags |= kFlagDamaged;
						break;
					}
				}
			}
		}
		// whatever else looks like text in it (the banner of the Start menu)
		for (unsigned k = 0; k < c->unreadCount; k++) {
			Unread& u = c->unread[k];
			int cx = (u.x0 + u.x1) / 2, cy = (u.y0 + u.y1) / 2;
			if (cx >= panel.x0 && cx <= panel.x1 && cy >= panel.y0 && cy <= panel.y1)
				u.used = 1;
		}
		menus++;
	}
	if (menus == 0)
		return;
	// The keyboard is in the newest menu that has a bar: the one no other
	// such menu lies on.
	int focused = -1;
	for (unsigned a = 0; a < menus; a++) {
		bool covered = false, coveredByBar = false;
		for (unsigned b = 0; b < menus; b++) {
			if (b == a || !PanelCovered(c, c->panels[menuPanel[a]], c->panels[menuPanel[b]]))
				continue;
			covered = true;
			if (menuBar[b] >= 0)
				coveredByBar = true;
		}
		if (menuBar[a] < 0)
			continue;
		if (covered)
			c->items[menuBar[a]].flags |= kFlagSubmenu;
		if (!coveredByBar)
			focused = menuBar[a];
	}
	if (focused >= 0)
		c->items[focused].flags |= kFlagFocused;
}

// Tabs of a property sheet: flat boxes with a light top edge and one caption,
// standing on the box of the page.  The open one is taller.
void BuildTabs(Context* c)
{
	int tabs[16];
	unsigned count = 0;
	for (unsigned i = 0; i < c->boxCount && count < 16; i++) {
		Box& box = c->boxes[i];
		int width = box.x1 - box.x0 + 1, height = box.y1 - box.y0 + 1;
		if (box.used || box.kind == kSunken || box.kind == kEtched || width < 16
			|| width > 200 || height < 13 || height > 24 || (box.colour & 0xFF000000u))
			continue;
		Colour top = box.e[kSideT][0];
		if (top == kNone || top == box.colour || Lum(top) <= Lum(box.colour))
			continue;
		bool page = false;
		for (unsigned j = 0; j < c->boxCount && !page; j++) {
			const Box& below = c->boxes[j];
			page = j != i && below.colour == box.colour && below.y0 > box.y1
				&& below.y0 <= box.y1 + 5 && below.x0 <= box.x0 + 2 && below.x1 >= box.x1 - 2
				&& below.x1 - below.x0 >= 100 && below.x1 - below.x0 > width + 8
				&& below.y1 - below.y0 >= 40;
		}
		if (!page)
			continue;
		int found = -1, lines = 0;
		for (unsigned k = 0; k < c->tlineCount; k++) {
			const TLine& t = c->tlines[k];
			if (t.used || !LineInside(t, box.x0, box.y0, box.x1, box.y1))
				continue;
			found = (int)k;
			lines++;
		}
		if (lines != 1 || c->tlines[found].x0 < box.x0 || c->tlines[found].x1 > box.x1)
			continue;
		int item = AddItem(c, kRoleTab, box.x0 - 2, box.y0 - 2, box.x1 + 2, box.y1);
		if (item < 0)
			return;
		TLine& t = c->tlines[found];
		t.used = 1;
		c->items[item].text = t.text;
		c->items[item].accel = t.accel;
		if (t.disabled)
			c->items[item].flags |= kFlagDisabled;
		box.used = 1;
		box.role = kRoleTab;
		tabs[count++] = item;
	}
	if (count == 0)
		return;
	// left to right; the open one starts higher than the others
	for (unsigned i = 1; i < count; i++) {
		int value = tabs[i];
		unsigned j = i;
		while (j > 0 && c->items[tabs[j - 1]].x0 > c->items[value].x0) {
			tabs[j] = tabs[j - 1];
			j--;
		}
		tabs[j] = value;
	}
	int highest = 0, others = 1 << 20;
	for (unsigned i = 1; i < count; i++) {
		if (c->items[tabs[i]].y0 < c->items[tabs[highest]].y0)
			highest = (int)i;
	}
	for (unsigned i = 0; i < count; i++) {
		if ((int)i != highest)
			others = Min(others, c->items[tabs[i]].y0);
	}
	if (count == 1 || others > c->items[tabs[highest]].y0)
		c->items[tabs[highest]].flags |= kFlagSelected;
	for (unsigned i = 0; i < count; i++) {
		Item& item = c->items[tabs[i]];
		item.pos = (uint8_t)(i + 1);
		item.cnt = (uint8_t)count;
		item.cy = (int16_t)(c->items[tabs[0]].cy);
	}
}

// Wrapped lines of one paragraph: same font and colours, same left edge (or
// same centre), line pitch of the font.
void JoinParagraphs(Context* c)
{
	for (unsigned k = 0; k < c->tlineCount; k++) {
		TLine& a = c->tlines[c->torder[k]];
		if (a.used)
			continue;
		const Font& font = c->fonts[a.font];
		int centre = a.x0 + a.x1;
		for (unsigned m = k + 1; m < c->tlineCount; m++) {
			TLine& b = c->tlines[c->torder[m]];
			int pitch = b.cellY - a.lastCellY;
			if (pitch > font.height + 1)
				break;
			if (b.used || pitch < font.height || b.font != a.font || b.fg != a.fg
				|| b.bg != a.bg)
				continue;
			bool sameLeft = b.cellX == a.cellX
				|| (a.lines == 1 && a.cellX > b.cellX && a.cellX - b.cellX <= 12);
			bool sameCentre = Abs(b.x0 + b.x1 - centre) <= 3 && b.cellX != a.cellX;
			if (!sameLeft && !sameCentre)
				continue;
			if (b.x0 > a.x1 || b.x1 < a.x0)
				continue;
			a.text = PoolJoin(c, a.text, b.text, " ");
			a.x0 = (int16_t)Min(a.x0, b.x0);
			a.x1 = (int16_t)Max(a.x1, b.x1);
			a.y1 = b.y1;
			a.lastCellY = b.cellY;
			a.lines++;
			a.damaged |= b.damaged;
			if (!a.accel)
				a.accel = b.accel;
			b.used = 1;
		}
	}
}

// Group box: an etched frame whose top line passes through a caption.
void BuildGroups(Context* c)
{
	for (unsigned i = 0; i < c->boxCount; i++) {
		Box& box = c->boxes[i];
		if (box.used || box.kind != kEtched)
			continue;
		int best = -1;
		for (unsigned k = 0; k < c->tlineCount; k++) {
			const TLine& t = c->tlines[k];
			if (t.used || t.lines != 1 || t.x0 < box.x0 || t.x0 > box.x0 + 30
				|| t.x1 > box.x1)
				continue;
			// the frame's top line is one to three rows above the box
			if (t.fy0 > box.y0 - 1 || t.fy1 < box.y0 - 3)
				continue;
			if (best < 0 || t.x0 < c->tlines[best].x0)
				best = (int)k;
		}
		if (best < 0)
			continue;
		TLine& t = c->tlines[best];
		int item = AddItem(c, kRoleGroup, box.x0 - 2, t.fy0, box.x1 + 2, box.y1 + 2);
		if (item < 0)
			return;
		c->items[item].cy = (int16_t)((t.fy0 + t.fy1) / 2);
		c->items[item].text = t.text;
		c->items[item].accel = t.accel;
		t.used = 1;
		box.used = 1;
		box.role = kRoleGroup;
	}
}

void BuildMarks(Context* c)
{
	for (unsigned i = 0; i < c->discCount; i++) {
		const Disc& disc = c->discs[i];
		int item = AddItem(c, kRoleRadio, disc.x0, disc.y0, disc.x1, disc.y1);
		if (item < 0)
			return;
		if (disc.checked)
			c->items[item].flags |= kFlagChecked;
	}
	for (unsigned i = 0; i < c->itemCount; i++) {
		Item& item = c->items[i];
		if (item.role != kRoleRadio && item.role != kRoleCheckBox)
			continue;
		int cy = (item.y0 + item.y1) / 2;
		int best = -1;
		for (unsigned k = 0; k < c->tlineCount; k++) {
			const TLine& t = c->tlines[k];
			if (t.used)
				continue;
			int gap = t.x0 - item.x1;
			if (gap < 0 || gap > 18 || t.fy0 > cy + 3 || t.fy1 < cy - 3)
				continue;
			if (best < 0 || t.x0 < c->tlines[best].x0)
				best = (int)k;
		}
		if (best < 0)
			continue;
		TLine& t = c->tlines[best];
		t.used = 1;
		item.text = t.text;
		item.accel = t.accel;
		if (t.disabled)
			item.flags |= kFlagDisabled;
		if (t.damaged)
			item.flags |= kFlagDamaged;
		item.x1 = (int16_t)Max(item.x1, t.x1 + 2);
		item.y0 = (int16_t)Min(item.y0, t.y0 - 2);
		item.y1 = (int16_t)Max(item.y1, t.y1 + 2);
	}
}

bool EndsWithColon(const char* text)
{
	size_t length = strlen(text);
	return length > 0 && text[length - 1] == ':';
}

void BuildLabels(Context* c)
{
	for (unsigned i = 0; i < c->itemCount; i++) {
		Item& item = c->items[i];
		if (item.role != kRoleEdit && item.role != kRoleList && item.role != kRoleCombo
			&& item.role != kRoleProgress)
			continue;
		int best = -1, bestDistance = 1 << 20;
		for (unsigned k = 0; k < c->tlineCount; k++) {
			const TLine& t = c->tlines[k];
			if (t.used)
				continue;
			const char* text = c->pool + t.text;
			bool marked = EndsWithColon(text) || t.accel != 0;
			int distance = -1;
			// above
			if (t.y1 < item.y0 + 1 && item.y0 - t.y1 <= 14 && t.x0 >= item.x0 - 16
				&& t.x0 <= item.x0 + 24 && t.lines == 1) {
				if (marked || item.role == kRoleProgress)
					distance = item.y0 - t.y1;
			}
			// to the left
			if (distance < 0 && marked && t.x1 < item.x0 && item.x0 - t.x1 <= 80
				&& t.fy0 <= (item.y0 + item.y1) / 2 + 2 && t.fy1 >= (item.y0 + item.y1) / 2 - 2
				&& t.lines == 1 && item.role != kRoleProgress)
				distance = 20 + item.x0 - t.x1;
			if (distance >= 0 && distance < bestDistance) {
				bestDistance = distance;
				best = (int)k;
			}
		}
		if (best < 0)
			continue;
		TLine& t = c->tlines[best];
		t.used = 1;
		item.label = t.text;
		item.accel = t.accel;
		if (t.disabled && item.role != kRoleProgress)
			item.flags |= kFlagDisabled;
	}
}

// Text-like rows that no loaded font explains (TrueType text): kept as empty
// statics so that the caller can read them some other way.
void BuildUnread(Context* c)
{
	for (unsigned i = 0; i < c->unreadCount; i++) {
		Unread& a = c->unread[i];
		if (a.used)
			continue;
		// inside a control that was understood: not interesting
		bool inside = false;
		for (unsigned k = 0; k < c->itemCount && !inside; k++) {
			const Item& item = c->items[k];
			int cx = (a.x0 + a.x1) / 2, cy = (a.y0 + a.y1) / 2;
			if (item.role != kRoleText && cx >= item.x0 && cx <= item.x1 && cy >= item.y0
				&& cy <= item.y1)
				inside = true;
		}
		if (inside)
			continue;
		int height = a.y1 - a.y0 + 1;
		bool grown = true;
		while (grown) {
			grown = false;
			for (unsigned j = 0; j < c->unreadCount; j++) {
				Unread& b = c->unread[j];
				if (j == i || b.used || b.fg != a.fg || b.bg != a.bg)
					continue;
				int gap = b.y0 - a.y1;
				if (gap < 1 || gap > 8 || Abs(b.x0 - a.x0) > 2
					|| Abs((b.y1 - b.y0 + 1) - height) > 4)
					continue;
				a.y1 = b.y1;
				a.x1 = (int16_t)Max(a.x1, b.x1);
				a.lines++;
				b.used = 1;
				grown = true;
			}
		}
		int item = AddItem(c, kRoleText, a.x0, a.y0, a.x1, a.y1);
		if (item < 0)
			return;
		Item& it = c->items[item];
		it.cy = (int16_t)(a.y0 + height / 2);
		it.colour = a.fg;
		it.flags |= kFlagUnread;
		it.cnt = a.lines;
		if (a.lines > 1)
			it.flags |= kFlagMultiline;
		a.used = 1;
	}
}

void BuildStatics(Context* c)
{
	for (unsigned k = 0; k < c->tlineCount; k++) {
		TLine& t = c->tlines[c->torder[k]];
		if (t.used)
			continue;
		int item = AddItem(c, kRoleText, t.x0, t.y0, t.x1, t.y1);
		if (item < 0)
			return;
		Item& it = c->items[item];
		it.cy = (int16_t)((t.fy0 + t.fy1) / 2);
		it.text = t.text;
		it.colour = t.fg;
		it.accel = t.accel;
		if (t.bold)
			it.flags |= kFlagBold;
		if (t.disabled)
			it.flags |= kFlagDisabled;
		if (t.damaged)
			it.flags |= kFlagDamaged;
		t.used = 1;
	}
}

// Is there a picture (an icon) right above a text?
bool IconAbove(const Context* c, const Item& item)
{
	int cx = (item.x0 + item.x1) / 2;
	int x0 = Max(cx - 16, 0), x1 = Min(cx + 15, c->w - 1);
	int y0 = Max(item.y0 - 40, 0), y1 = item.y0 - 5;
	if (y1 - y0 < 20)
		return false;
	Colour surface = P(c, cx, item.y0 - 3);
	int other = 0;
	for (int y = y0; y <= y1; y++) {
		for (int x = x0; x <= x1; x++) {
			if (P(c, x, y) != surface)
				other++;
		}
	}
	return other >= 300;
}

int Overlap(const RectI& f, const Item& item)
{
	int x0 = Max(f.x0, item.x0), x1 = Min(f.x1, item.x1);
	int y0 = Max(f.y0, item.y0), y1 = Min(f.y1, item.y1);
	if (x1 < x0 || y1 < y0)
		return 0;
	return (x1 - x0 + 1) * (y1 - y0 + 1);
}

void AssignFocus(Context* c)
{
	// An open menu has the keyboard (BuildMenus marked the item under the
	// bar, if there is one); what was focused under it is remembered.
	for (unsigned i = 0; i < c->itemCount; i++) {
		if (c->items[i].role == kRoleMenu)
			return;
	}
	int focused = -1;
	bool byCaret = false, byIcon = false;
	int bestArea = 1 << 30;
	for (unsigned k = 0; k < c->focusCount; k++) {
		const RectI& f = c->focus[k];
		int area = (f.x1 - f.x0 + 1) * (f.y1 - f.y0 + 1);
		for (unsigned i = 0; i < c->itemCount; i++) {
			const Item& item = c->items[i];
			if (item.role == kRoleText || item.role == kRoleListItem
				|| item.role == kRoleProgress || item.role == kRoleGroup)
				continue;
			if (Overlap(f, item) * 2 < area)
				continue;
			int itemArea = (item.x1 - item.x0 + 1) * (item.y1 - item.y0 + 1);
			if (itemArea < bestArea) {
				bestArea = itemArea;
				focused = (int)i;
			}
		}
	}
	if (focused < 0) {
		// a dotted rectangle around a plain text: the caption of an icon
		// (desktop, folder window)
		for (unsigned k = 0; k < c->focusCount && focused < 0; k++) {
			const RectI& f = c->focus[k];
			for (unsigned i = 0; i < c->itemCount; i++) {
				const Item& item = c->items[i];
				if (item.role != kRoleText || (item.flags & kFlagUnread))
					continue;
				if (item.x0 >= f.x0 - 3 && item.x1 <= f.x1 + 3 && item.y0 >= f.y0 - 3
					&& item.y1 <= f.y1 + 3 && f.x1 - f.x0 <= item.x1 - item.x0 + 24) {
					focused = (int)i;
					byIcon = true;
					break;
				}
			}
		}
		if (byIcon) {
			// its neighbours: the other texts that stand under a picture
			for (unsigned i = 0; i < c->itemCount; i++) {
				Item& item = c->items[i];
				if (item.role == kRoleText && !(item.flags & kFlagUnread)
					&& ((int)i == focused || IconAbove(c, item)))
					item.role = kRoleIcon;
			}
		}
	}
	if (focused < 0) {
		for (unsigned i = 0; i < c->itemCount; i++) {
			if (c->items[i].flags & kFlagCaret) {
				focused = (int)i;
				byCaret = true;
				break;
			}
		}
	}
	if (focused < 0) {
		// an edit field shows its selection only while it has the focus
		for (unsigned i = 0; i < c->itemCount; i++) {
			const Item& item = c->items[i];
			if ((item.role == kRoleEdit || item.role == kRoleCombo)
				&& (item.flags & kFlagSelected) && !(item.flags & kFlagMultiline)) {
				focused = (int)i;
				byCaret = true;
				break;
			}
		}
	}
	if (focused < 0 && c->sticky.valid) {
		// the caret is in the dark half of its blink
		for (unsigned i = 0; i < c->itemCount; i++) {
			const Item& item = c->items[i];
			if ((item.role == kRoleEdit || item.role == kRoleCombo)
				&& item.x0 == c->sticky.x && item.y0 == c->sticky.y
				&& item.x1 - item.x0 == c->sticky.w && item.y1 - item.y0 == c->sticky.h) {
				focused = (int)i;
				byCaret = true;
				break;
			}
		}
	}
	if (focused < 0) {
		// No caret, no dotted rectangle: in Windows 3.x and 9x the default
		// button is the focused one whenever the focus is on a button.
		int found = -1, count = 0;
		for (unsigned i = 0; i < c->itemCount; i++) {
			if (c->items[i].role == kRoleButton && (c->items[i].flags & kFlagDefault)) {
				found = (int)i;
				count++;
			}
		}
		if (count == 1 && (c->items[found].flags & kFlagDisabled)) {
			// A button that cannot be pressed does not have the focus (the
			// "Run" dialog before anything is typed): the first field has it.
			found = -1;
			for (unsigned i = 0; i < c->itemCount; i++) {
				const Item& item = c->items[i];
				if ((item.role != kRoleEdit && item.role != kRoleCombo)
					|| (item.flags & (kFlagDisabled | kFlagMultiline)))
					continue;
				if (found < 0 || item.y0 < c->items[found].y0
					|| (item.y0 == c->items[found].y0 && item.x0 < c->items[found].x0))
					found = (int)i;
			}
		}
		if (count == 1)
			focused = found;
	}
	c->sticky.valid = false;
	if (focused >= 0) {
		Item& item = c->items[focused];
		item.flags |= kFlagFocused;
		if (byCaret) {
			c->sticky.valid = true;
			c->sticky.x = item.x0;
			c->sticky.y = item.y0;
			c->sticky.w = (int16_t)(item.x1 - item.x0);
			c->sticky.h = (int16_t)(item.y1 - item.y0);
		}
	}
}

uint16_t CopyText(Screen* out, const char* text)
{
	size_t length = strlen(text);
	if (length == 0)
		return 0;
	if (out->poolUsed + length + 1 > kTextPool) {
		if (out->poolUsed + 8 > kTextPool)
			return 0;
		length = kTextPool - out->poolUsed - 1;
		while (length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80)
			length--;
	}
	uint16_t at = out->poolUsed;
	memcpy(out->pool + at, text, length);
	out->pool[at + length] = 0;
	out->poolUsed = (uint16_t)(out->poolUsed + length + 1);
	return at;
}

uint32_t HashText(uint32_t hash, const char* text)
{
	for (; *text; text++)
		hash = (hash ^ (uint8_t)*text) * 16777619u;
	return (hash ^ 0xFF) * 16777619u;
}

void Emit(Context* c, const Assembly& as, const char* title, Screen* out, int32_t* keys)
{
	// zone and highlight
	Colour colours[6];
	int counts[6];
	int ncolours = 0;
	for (unsigned i = 0; i < c->itemCount; i++) {
		Item& item = c->items[i];
		int cx = (item.x0 + item.x1) / 2, cy = (item.y0 + item.y1) / 2;
		bool inside = as.hasDialog && cx >= as.dialog.x0 - 4 && cx <= as.dialog.x1 + 4
			&& cy >= as.dialog.y0 - 4 && cy <= as.dialog.y1 + 4;
		if (!inside)
			item.flags |= kFlagOutside;
		if (!inside && item.role == kRoleText) {
			int k = 0;
			while (k < ncolours && colours[k] != item.colour)
				k++;
			if (k == ncolours && ncolours < 6) {
				colours[ncolours] = item.colour;
				counts[ncolours++] = 0;
			}
			if (k < ncolours)
				counts[k]++;
		}
	}
	if (ncolours >= 2) {
		int best = 0;
		for (int k = 1; k < ncolours; k++) {
			if (counts[k] > counts[best])
				best = k;
		}
		for (unsigned i = 0; i < c->itemCount; i++) {
			Item& item = c->items[i];
			if ((item.flags & kFlagOutside) && item.role == kRoleText
				&& item.colour != colours[best] && Sat(item.colour) > 60)
				item.flags |= kFlagHighlighted;
		}
	}

	// reading order: dialog first, rows top to bottom, left to right
	unsigned n = 0;
	for (unsigned i = 0; i < c->itemCount; i++) {
		const Item& item = c->items[i];
		if (item.parent >= 0)
			continue;
		keys[i] = ((item.flags & kFlagOutside) ? 1 << 24 : 0) + item.cy * 2048 + item.x0;
		c->iorder[n++] = (uint16_t)i;
	}
	SortU16(c->iorder, n, keys);
	for (unsigned start = 0; start < n;) {
		unsigned end = start + 1;
		const Item& first = c->items[c->iorder[start]];
		while (end < n) {
			const Item& next = c->items[c->iorder[end]];
			if ((next.flags & kFlagOutside) != (first.flags & kFlagOutside)
				|| next.cy - first.cy > 7)
				break;
			end++;
		}
		for (unsigned k = start; k < end; k++)
			keys[c->iorder[k]] = c->items[c->iorder[k]].x0;
		SortU16(c->iorder + start, end - start, keys);
		start = end;
	}

	// positions: radio buttons in one column, unlabelled edit fields in one row
	for (unsigned a = 0; a < n; a++) {
		Item& first = c->items[c->iorder[a]];
		if (first.pos != 0 || (first.flags & kFlagOutside))
			continue;
		if (first.role != kRoleRadio && !(first.role == kRoleEdit && first.label == 0))
			continue;
		unsigned members = 0;
		for (int pass = 0; pass < 2; pass++) {
			unsigned position = 0;
			for (unsigned b = a; b < n; b++) {
				Item& other = c->items[c->iorder[b]];
				if (other.role != first.role || (other.flags & kFlagOutside))
					continue;
				bool same = first.role == kRoleRadio
					? Abs(other.x0 - first.x0) <= 2
					: other.label == 0 && Abs(other.y0 - first.y0) <= 1
						&& other.y1 - other.y0 == first.y1 - first.y0;
				if (!same)
					continue;
				position++;
				if (pass == 1 && members > 1) {
					other.pos = (uint8_t)position;
					other.cnt = (uint8_t)members;
				}
			}
			members = position;
		}
	}

	{
		unsigned icons = 0;
		for (unsigned a = 0; a < n; a++) {
			if (c->items[c->iorder[a]].role == kRoleIcon)
				icons++;
		}
		unsigned position = 0;
		for (unsigned a = 0; a < n && icons > 0; a++) {
			Item& item = c->items[c->iorder[a]];
			if (item.role != kRoleIcon)
				continue;
			item.pos = (uint8_t)Min(255, (int)++position);
			item.cnt = (uint8_t)Min(255, (int)icons);
		}
	}

	ClearScreen(out);
	out->width = (uint16_t)c->w;
	out->height = (uint16_t)c->h;
	size_t titleLength = strlen(title);
	memcpy(out->title, title, titleLength + 1);
	if (as.hasDialog) {
		out->dialog.x = as.dialog.x0;
		out->dialog.y = as.dialog.y0;
		out->dialog.w = (int16_t)(as.dialog.x1 - as.dialog.x0 + 1);
		out->dialog.h = (int16_t)(as.dialog.y1 - as.dialog.y0 + 1);
	}
	uint32_t hash = HashText(2166136261u, title);
	for (unsigned k = 0; k < n; k++) {
		for (int child = -1; child < (int)c->itemCount; child++) {
			const Item* item;
			if (child < 0) {
				item = &c->items[c->iorder[k]];
			} else {
				item = &c->items[child];
				if (item->parent != (int)c->iorder[k])
					continue;
			}
			if (out->count >= kMaxElements)
				break;
			Element& e = out->items[out->count];
			e.role = item->role;
			e.percent = item->percent;
			e.flags = item->flags;
			e.rect.x = item->x0;
			e.rect.y = item->y0;
			e.rect.w = (int16_t)(item->x1 - item->x0 + 1);
			e.rect.h = (int16_t)(item->y1 - item->y0 + 1);
			e.text = CopyText(out, c->pool + item->text);
			e.label = CopyText(out, c->pool + item->label);
			e.accelerator = item->accel;
			e.position = item->pos;
			e.count = item->cnt;
			e.colour = item->role == kRoleText ? item->colour : 0;
			if ((e.flags & kFlagFocused) && out->focus < 0)
				out->focus = (int16_t)out->count;
			if (e.role == kRoleText && !(e.flags & kFlagOutside))
				hash = HashText(hash, c->pool + item->text);
			out->count++;
			if (child < 0 && item->role != kRoleList && item->role != kRoleMenu)
				break;
		}
	}
	out->pageHash = as.hasDialog ? hash : 0;
	if (as.hasDialog) {
		// a message box: nothing but text and a few buttons, clearly smaller
		// than the screen
		unsigned buttons = 0, controls = 0;
		for (unsigned i = 0; i < out->count; i++) {
			const Element& e = out->items[i];
			if (e.flags & kFlagOutside)
				continue;
			if (e.role == kRoleButton)
				buttons++;
			else if (e.role != kRoleText && e.role != kRoleMenu && e.role != kRoleMenuItem
				&& e.role != kRoleIcon)
				controls++;
		}
		bool small = out->dialog.w * 4 < c->w * 3 && out->dialog.h * 5 < c->h * 3;
		out->kind = controls == 0 && buttons <= 4 && small ? kWindowMessage : kWindowNormal;
	}
}

}	// namespace

// ---------------------------------------------------------------------------
// public: set-up

size_t ContextBytes()
{
	return sizeof(Context);
}

Context* Init(void* memory, size_t bytes)
{
	if (memory == nullptr || bytes < sizeof(Context))
		return nullptr;
	Context* c = (Context*)memory;
	c->fontCount = c->glyphCount = c->colCount = c->preferred = 0;
	c->w = c->h = 0;
	c->sticky.valid = false;
	c->poolUsed = 1;
	c->pool[0] = 0;
	return c;
}

bool AddFon(Context* c, const uint8_t* bytes, size_t size)
{
	if (c == nullptr || bytes == nullptr || size < 64)
		return false;
	if (bytes[0] != 'M' || bytes[1] != 'Z')
		return AddFnt(c, bytes, size);
	uint32_t ne = Rd32(bytes + 0x3C);
	if ((uint64_t)ne + 0x40 > size || bytes[ne] != 'N' || bytes[ne + 1] != 'E')
		return false;
	size_t table = ne + Rd16(bytes + ne + 0x24);
	if (table + 2 > size)
		return false;
	unsigned shift = Rd16(bytes + table);
	if (shift > 16)
		return false;
	size_t at = table + 2;
	bool any = false;
	while (at + 8 <= size) {
		unsigned type = Rd16(bytes + at), count = Rd16(bytes + at + 2);
		if (type == 0)
			break;
		at += 8;
		for (unsigned i = 0; i < count; i++, at += 12) {
			if (at + 12 > size)
				return any;
			if (type != 0x8008)
				continue;
			size_t offset = (size_t)Rd16(bytes + at) << shift;
			size_t length = (size_t)Rd16(bytes + at + 2) << shift;
			if (offset >= size)
				continue;
			if (length > size - offset)
				length = size - offset;
			if (AddFnt(c, bytes + offset, length))
				any = true;
		}
	}
	return any;
}

unsigned FontCount(const Context* c)
{
	return c ? c->fontCount : 0;
}

void ResetHistory(Context* c)
{
	if (c)
		c->sticky.valid = false;
}

void ClearScreen(Screen* screen)
{
	screen->count = 0;
	screen->focus = -1;
	screen->width = screen->height = 0;
	screen->dialog.x = screen->dialog.y = screen->dialog.w = screen->dialog.h = 0;
	screen->pageHash = 0;
	screen->poolUsed = 1;
	screen->kind = kWindowNone;
	screen->title[0] = 0;
	screen->pool[0] = 0;
}

// ---------------------------------------------------------------------------
// public: analysis

void Analyse(Context* c, const uint32_t* pixels, unsigned width, unsigned height,
	unsigned stride, Screen* out)
{
	ClearScreen(out);
	if (c == nullptr || pixels == nullptr || width < 16 || height < 16
		|| width > kMaxW || height > kMaxH || stride < width)
		return;
	c->w = (int)width;
	c->h = (int)height;
	for (unsigned y = 0; y < height; y++) {
		const uint32_t* source = pixels + (size_t)y * stride;
		Colour* target = c->work + y * width;
		for (unsigned x = 0; x < width; x++)
			target[x] = source[x] & 0x00FFFFFFu;
	}
	c->poolUsed = 1;
	c->pool[0] = 0;
	c->itemCount = c->discCount = c->exclCount = 0;

	// the comps array is idle until FindComponents: use its space for sort keys
	int32_t* keys = (int32_t*)c->cost[0];

	PaintLattices(c);
	FindSegments(c);
	FindFocusRects(c);
	FindBoxes(c);
	FindCarets(c);
	if (c->caretCount > 0)
		FindBoxes(c);       // a caret at the edge of a field cuts its box
	FindPanels(c, keys);
	for (unsigned i = 0; i < c->boxCount; i++) {
		const Box& box = c->boxes[i];
		int w = box.x1 - box.x0 + 1, h = box.y1 - box.y0 + 1;
		if (IsCheckBox(box) || (box.kind == kRaised && w <= 22 && h <= 26))
			AddExclusion(c, box.x0 - 3, box.y0 - 3, box.x1 + 3, box.y1 + 3);
	}
	FindComponents(c);
	for (unsigned i = 0; i < c->compCount; i++) {
		Disc disc;
		if (c->discCount < kMaxDiscs && IsDisc(c, c->comps[i], &disc)) {
			c->discs[c->discCount++] = disc;
			AddExclusion(c, disc.x0 - 2, disc.y0 - 2, disc.x1 + 2, disc.y1 + 2);
		}
	}
	for (unsigned i = 0; i < c->compCount; i++)
		ValidateComp(c, c->comps[i]);

	// sort keys for up to kMaxComps components do not fit in cost[]: use runs'
	// parent links, which are no longer needed
	static_assert(sizeof(Run) >= sizeof(int32_t), "key storage");
	int32_t* compKeys = (int32_t*)c->runs;
	static_assert(sizeof(c->runs) >= sizeof(int32_t) * kMaxComps, "key storage");
#ifdef PCWIN_DEBUG
	for (unsigned i = 0; i < c->segCount; i++) {
		if (!c->segs[i].dither)
			PCWIN_LOG("seg y=%d x=%d..%d\n", c->segs[i].y, c->segs[i].x0, c->segs[i].x1);
	}
	for (unsigned i = 0; i < c->focusCount; i++)
		PCWIN_LOG("focus (%d,%d)-(%d,%d)\n", c->focus[i].x0, c->focus[i].y0, c->focus[i].x1,
			c->focus[i].y1);
	for (unsigned i = 0; i < c->boxCount; i++) {
		const Box& b = c->boxes[i];
		PCWIN_LOG("box %u (%d,%d)-(%d,%d) %06X kind=%d T=%06X,%06X L=%06X B=%06X,%06X R=%06X\n",
			i, b.x0, b.y0, b.x1, b.y1, b.colour, b.kind, b.e[0][0], b.e[0][1], b.e[1][0],
			b.e[2][0], b.e[2][1], b.e[3][0]);
	}
	PCWIN_LOG("segs=%u boxes=%u runs=%u comps=%u discs=%u carets=%u\n", c->segCount,
		c->boxCount, c->runCount, c->compCount, c->discCount, c->caretCount);
#endif
	GroupLines(c, compKeys);
	ReadLines(c);
	MergeRows(c);
	SortLines(c, keys);

	Assembly as;
	char title[kTitleBytes];
	BuildMenus(c, keys);
	BuildProgress(c);
	BuildContainers(c, keys);
	BuildTabs(c);
	BuildButtons(c);
	BuildTitle(c, &as, title);
	JoinParagraphs(c);
	BuildGroups(c);
	BuildMarks(c);
	BuildLabels(c);
	BuildStatics(c);
	BuildUnread(c);
	AssignFocus(c);
	Emit(c, as, title, out, keys);
}

// ---------------------------------------------------------------------------
// public: speech

namespace {

struct Writer {
	char*  out;
	size_t capacity, length;
};

void Put(Writer& w, const char* text, size_t length)
{
	if (w.capacity == 0)
		return;
	if (w.length + length + 1 > w.capacity) {
		length = w.capacity - w.length - 1;
		while (length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80)
			length--;
	}
	memcpy(w.out + w.length, text, length);
	w.length += length;
	w.out[w.length] = 0;
}

void Put(Writer& w, const char* text)
{
	Put(w, text, strlen(text));
}

void PutNumber(Writer& w, int value)
{
	char digits[12];
	int n = 0;
	if (value < 0)
		value = 0;
	do {
		digits[n++] = (char)('0' + value % 10);
		value /= 10;
	} while (value > 0 && n < 11);
	while (n > 0)
		Put(w, &digits[--n], 1);
}

// separator between two announcements
void Separate(Writer& w)
{
	if (w.length == 0)
		return;
	char last = w.out[w.length - 1];
	if (last == '.' || last == '!' || last == '?' || last == ':' || last == ';')
		Put(w, " ");
	else
		Put(w, ". ");
}

// Text as it should be spoken: rulers made of dashes are left out.
void PutText(Writer& w, const char* text)
{
	while (*text) {
		size_t run = 1;
		while (text[run] == text[0])
			run++;
		if (run >= 4 && strchr("-_=*.~", text[0]) != nullptr) {
			text += run;
			while (*text == ' ' && (w.length == 0 || w.out[w.length - 1] == ' '))
				text++;
			continue;
		}
		Put(w, text, run);
		text += run;
	}
}

// A caption as it should be spoken: without "< " and " >" decorations and
// without the trailing colon.
void PutCaption(Writer& w, const char* text)
{
	while (*text == '<' || *text == ' ')
		text++;
	size_t length = strlen(text);
	while (length > 0 && (text[length - 1] == ' ' || text[length - 1] == '>'
			|| text[length - 1] == ':'))
		length--;
	// one full stop at the end would be read as the end of the announcement
	if (length > 1 && text[length - 1] == '.' && text[length - 2] != '.')
		length--;
	Put(w, text, length);
}

struct Words {
	const char* role[14];
	const char* submenu;
	const char* checked;
	const char* unchecked;
	const char* disabled;
	const char* blank;
	const char* of;
	const char* selected;
	const char* window;
	const char* dialog;
};

const Words kEnglish = {
	{ "", "button", "edit", "check box", "radio button", "list", "", "combo box",
		"progress bar", "grouping", "menu", "menu item", "", "tab" },
	"submenu",
	"checked", "not checked", "unavailable", "blank", " of ", "selected",
	"window", "dialog"
};

const Words kPolish = {
	{ "", "przycisk", "pole edycji", "pole wyboru", "przycisk opcji", "lista", "",
		"lista rozwijana", "pasek post\xC4\x99pu", "grupa", "menu", "pozycja menu", "", "karta" },
	"podmenu",
	"zaznaczony", "niezaznaczony", "niedost\xC4\x99pny", "puste", " z ", "wybrany",
	"okno", "okno dialogowe"
};

// r410 (user point 6h, "a blind person has the right to know when to type the key"): Setup's product-key
// page - its text names the key and it has a row of three or more edit fields. The key itself is never
// logged or kept; the fields are read as any edit field is, with the field and the count of typed characters.
bool HasNoCase(const char* text, const char* word)
{
	for (; *text; text++) {
		size_t i = 0;
		while (word[i] && text[i] && ((text[i] | 0x20) == (word[i] | 0x20) || (text[i] == word[i])))
			i++;
		if (!word[i])
			return true;
	}
	return false;
}

int KeyFieldCount(const Screen& s)
{
	bool named = false;
	int fields = 0;
	for (unsigned i = 0; i < s.count; i++) {
		const Element& e = s.items[i];
		const char* text = s.pool + e.text;
		if (e.role == kRoleText && (HasNoCase(text, "product key") || HasNoCase(text, "product id")
			|| HasNoCase(text, "cd key") || HasNoCase(text, "klucz produktu")))
			named = true;
		if (e.role == kRoleEdit && !(e.flags & kFlagMultiline) && e.count >= 3 && e.count > fields)
			fields = e.count;
	}
	if (!named && s.title[0] && HasNoCase(s.title, "product key"))
		named = true;
	return named ? fields : 0;
}

void PutElement(Writer& w, const Screen& s, const Element& e, unsigned flags)
{
	const Words& words = (flags & kSpeakPolish) ? kPolish : kEnglish;
	const char* text = s.pool + e.text;
	const char* label = s.pool + e.label;
	size_t start = w.length;
	switch (e.role) {
		case kRoleText:
			PutText(w, text);
			return;
		case kRoleListItem:
			PutText(w, text);
			if (e.flags & kFlagSelected) {
				Put(w, ", ");
				Put(w, words.selected);
			}
			return;
		case kRoleMenu:
			Put(w, words.role[kRoleMenu]);
			return;
		case kRoleIcon:
			PutText(w, text);
			break;
		case kRoleMenuItem:
			PutCaption(w, text);
			if (w.length > start)
				Put(w, ", ");
			Put(w, (e.flags & kFlagSubmenu) ? words.submenu : words.role[kRoleMenuItem]);
			if (e.flags & kFlagChecked) {
				Put(w, ", ");
				Put(w, words.checked);
			}
			break;
		case kRoleButton:
		case kRoleCheckBox:
		case kRoleRadio:
		case kRoleGroup:
		case kRoleTab:
			PutCaption(w, text);
			break;
		default:
			PutCaption(w, label);
			break;
	}
	if (e.role != kRoleMenuItem && e.role != kRoleIcon) {
		if (w.length > start)
			Put(w, ", ");
		Put(w, words.role[e.role < 14 ? e.role : 0]);
	}
	switch (e.role) {
		case kRoleCheckBox:
		case kRoleRadio:
			Put(w, ", ");
			Put(w, (e.flags & kFlagChecked) ? words.checked : words.unchecked);
			break;
		case kRoleEdit:
		case kRoleCombo:
		case kRoleList:
			Put(w, ", ");
			if (*text)
				PutText(w, text);
			else
				Put(w, words.blank);
			if (e.role == kRoleEdit && e.count >= 3 && e.position > 0 && KeyFieldCount(s) > 0) {   // r410 (6h)
				Put(w, (flags & kSpeakPolish) ? ", pole klucza " : ", key field ");
				PutNumber(w, e.position);
				Put(w, words.of);
				PutNumber(w, e.count);
				Put(w, (flags & kSpeakPolish) ? ", wpisane znaki: " : ", characters typed: ");
				PutNumber(w, (int)strlen(text));
				if (e.count == 5) {
					Put(w, words.of);
					PutNumber(w, 5);
				}
			}
			break;
		case kRoleProgress:
			if (e.percent >= 0) {
				Put(w, ", ");
				PutNumber(w, e.percent);
				Put(w, "%");
			}
			break;
		default:
			break;
	}
	if (e.flags & kFlagDisabled) {
		Put(w, ", ");
		Put(w, words.disabled);
	}
	if (e.position > 0 && e.count > 1 && !(e.role == kRoleEdit && e.count >= 3 && KeyFieldCount(s) > 0)) {
		Put(w, ", ");
		PutNumber(w, e.position);
		Put(w, words.of);
		PutNumber(w, e.count);
	}
}

void PutTitle(Writer& w, const Screen& s, unsigned flags)
{
	const Words& words = (flags & kSpeakPolish) ? kPolish : kEnglish;
	if (s.dialog.w == 0)
		return;
	Put(w, s.title);
	if (s.title[0])
		Put(w, ", ");
	Put(w, s.kind == kWindowMessage ? words.dialog : words.window);
}

bool HasMenu(const Screen& s)
{
	for (unsigned i = 0; i < s.count; i++) {
		if (s.items[i].role == kRoleMenu)
			return true;
	}
	return false;
}

bool SameRect(const Element& a, const Element& b)
{
	return Abs(a.rect.x - b.rect.x) <= 2 && Abs(a.rect.y - b.rect.y) <= 2
		&& Abs(a.rect.w - b.rect.w) <= 4 && Abs(a.rect.h - b.rect.h) <= 4;
}

// r410 (6h): a page with a product-key row says so before the page's own text
void PutKeyIntro(Writer& w, const Screen& now, unsigned flags)
{
	int fields = KeyFieldCount(now);
	if (fields == 0)
		return;
	Separate(w);
	Put(w, (flags & kSpeakPolish) ? "Instalator prosi teraz o klucz produktu: p\xC3\xB3l " : "Setup now asks for the product key: fields ");
	PutNumber(w, fields);
	if (fields == 5)
		Put(w, (flags & kSpeakPolish) ? ", w ka\xC5\xBC" "dym 5 znak\xC3\xB3w" : ", 5 characters in each");
	Put(w, ".");
}

}	// namespace

size_t SpeakElement(const Screen& now, unsigned index, unsigned flags, char* out,
	size_t capacity)
{
	if (out == nullptr || capacity == 0)
		return 0;
	out[0] = 0;
	if (index >= now.count)
		return 0;
	Writer w = { out, capacity, 0 };
	PutElement(w, now, now.items[index], flags);
	return w.length;
}

size_t SpeakAll(const Screen& now, unsigned flags, char* out, size_t capacity)
{
	if (out == nullptr || capacity == 0)
		return 0;
	out[0] = 0;
	Writer w = { out, capacity, 0 };
	PutTitle(w, now, flags);
	PutKeyIntro(w, now, flags);
	bool outside = (flags & kSpeakOutside) || now.dialog.w == 0;
	for (unsigned i = 0; i < now.count; i++) {
		const Element& e = now.items[i];
		bool menu = e.role == kRoleMenu || e.role == kRoleMenuItem;
		if ((e.flags & (kFlagChild | kFlagUnread))
			|| (!outside && !menu && (e.flags & kFlagOutside)))
			continue;
		Separate(w);
		if (e.role == kRoleIcon)
			PutText(w, now.pool + e.text);      // without "N of M" on every one
		else
			PutElement(w, now, e, flags);
	}
	if (w.length > 0 && w.out[w.length - 1] != '.' && w.out[w.length - 1] != ':')
		Put(w, ".");
	return w.length;
}

size_t SpeakChange(const Screen& before, const Screen& now, unsigned flags, char* out,
	size_t capacity)
{
	if (out == nullptr || capacity == 0)
		return 0;
	out[0] = 0;
	Writer w = { out, capacity, 0 };
	const Words& words = (flags & kSpeakPolish) ? kPolish : kEnglish;
	bool titleChanged = strcmp(before.title, now.title) != 0;
	const Element* nf = now.focus >= 0 ? &now.items[now.focus] : nullptr;
	const Element* bf = before.focus >= 0 ? &before.items[before.focus] : nullptr;
	bool menuNow = HasMenu(now), menuBefore = HasMenu(before);
	if (menuNow) {
		// a menu covers the window: only the menu speaks
		if (!menuBefore)
			Put(w, words.role[kRoleMenu]);
		if (nf != nullptr && nf->role == kRoleMenuItem
			&& (!menuBefore || bf == nullptr || bf->role != kRoleMenuItem
				|| !SameRect(*bf, *nf)
				|| strcmp(before.pool + bf->text, now.pool + nf->text) != 0)) {
			Separate(w);
			PutElement(w, now, *nf, flags);
		}
		return w.length;
	}
	// The texts a menu covered are back when it closes: that is not a new page.
	bool newPage = now.dialog.w != 0
		&& (titleChanged || before.dialog.w == 0
			|| (before.pageHash != now.pageHash && !menuBefore));

	if (newPage) {
		if (titleChanged || !(flags & kSpeakTerse))
			PutTitle(w, now, flags);
		PutKeyIntro(w, now, flags);   // r410 (6h, r410a-me3): a wizard page change speaks it too, not only SpeakAll
		for (unsigned i = 0; i < now.count; i++) {
			const Element& e = now.items[i];
			if (e.flags & (kFlagOutside | kFlagUnread))
				continue;
			if (e.role != kRoleText && e.role != kRoleProgress)
				continue;
			Separate(w);
			PutElement(w, now, e, flags);
		}
		if (nf) {
			Separate(w);
			PutElement(w, now, *nf, flags);
		}
	} else if (nf) {
		if (bf == nullptr || bf->role != nf->role || !SameRect(*bf, *nf)) {
			PutElement(w, now, *nf, flags);
		} else if (nf->role == kRoleList || nf->role == kRoleCombo) {
			if (strcmp(before.pool + bf->text, now.pool + nf->text) != 0) {
				// another row: the row and its place among the visible ones
				PutText(w, now.pool + nf->text);
				if (nf->position > 0 && nf->count > 1) {
					Put(w, ", ");
					PutNumber(w, nf->position);
					Put(w, words.of);
					PutNumber(w, nf->count);
				}
			}
		} else if (nf->role == kRoleCheckBox || nf->role == kRoleRadio) {
			if ((bf->flags ^ nf->flags) & kFlagChecked)
				Put(w, (nf->flags & kFlagChecked) ? words.checked : words.unchecked);
		}
	}

	// progress
	if (!newPage) {
		for (unsigned i = 0; i < now.count; i++) {
			const Element& e = now.items[i];
			if (e.role != kRoleProgress)
				continue;
			const Element* old = nullptr;
			for (unsigned k = 0; k < before.count && old == nullptr; k++) {
				const Element& b = before.items[k];
				if (b.role == kRoleProgress && Abs(b.rect.y - e.rect.y) <= 2
					&& Abs(b.rect.x - e.rect.x) <= 2)
					old = &b;
			}
			if (old != nullptr && old->percent == e.percent)
				continue;
			Separate(w);
			if (old != nullptr) {
				PutNumber(w, e.percent < 0 ? 0 : e.percent);
				Put(w, "%");
			} else {
				PutElement(w, now, e, flags);
			}
		}
	} else {
		for (unsigned i = 0; i < now.count; i++) {
			const Element& e = now.items[i];
			if (e.role != kRoleProgress || !(e.flags & kFlagOutside))
				continue;
			bool known = false;
			for (unsigned k = 0; k < before.count; k++) {
				const Element& b = before.items[k];
				if (b.role == kRoleProgress && b.percent == e.percent
					&& Abs(b.rect.y - e.rect.y) <= 2)
					known = true;
			}
			if (!known) {
				Separate(w);
				PutElement(w, now, e, flags);
			}
		}
	}

	// the highlighted step and time outside the dialog
	for (unsigned i = 0; i < now.count; i++) {
		const Element& e = now.items[i];
		if (e.role != kRoleText || (e.flags & (kFlagOutside | kFlagHighlighted | kFlagUnread))
				!= (kFlagOutside | kFlagHighlighted))
			continue;
		const char* text = now.pool + e.text;
		bool known = false;
		for (unsigned k = 0; k < before.count && !known; k++) {
			const Element& b = before.items[k];
			if (b.role == kRoleText && (b.flags & kFlagHighlighted)
				&& strcmp(before.pool + b.text, text) == 0)
				known = true;
		}
		if (known)
			continue;
		Separate(w);
		if (i > 0) {
			const Element& previous = now.items[i - 1];
			const char* lead = now.pool + previous.text;
			size_t length = strlen(lead);
			if (previous.role == kRoleText && (previous.flags & kFlagOutside)
				&& length > 0 && lead[length - 1] == ':') {
				Put(w, lead);
				Put(w, " ");
			}
		}
		Put(w, text);
	}
	return w.length;
}

bool SetElementText(Screen* screen, unsigned index, const char* utf8)
{
	if (screen == nullptr || utf8 == nullptr || index >= screen->count)
		return false;
	Element& e = screen->items[index];
	size_t length = strlen(utf8);
	if (screen->poolUsed + length + 1 > kTextPool)
		return false;
	e.text = length ? CopyText(screen, utf8) : 0;
	e.flags &= (uint16_t)~kFlagUnread;
	if (e.role == kRoleText && !(e.flags & kFlagOutside) && screen->dialog.w != 0)
		screen->pageHash = HashText(screen->pageHash, utf8);
	return true;
}

int ReviewStep(const Screen& screen, int index, int direction)
{
	if (screen.count == 0)
		return -1;
	if (index < 0)
		return direction > 0 ? 0 : (int)screen.count - 1;
	int next = index + (direction > 0 ? 1 : -1);
	return next >= 0 && next < (int)screen.count ? next : -1;
}

int ReviewStepControl(const Screen& screen, int index, int direction)
{
	for (;;) {
		index = ReviewStep(screen, index, direction);
		if (index < 0)
			return -1;
		const Element& e = screen.items[index];
		if (e.role != kRoleText && e.role != kRoleListItem)
			return index;
	}
}

}	// namespace pcwin
