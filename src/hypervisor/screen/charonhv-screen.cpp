// SPDX-License-Identifier: GPL-2.0-or-later
// charonhv-screen: Charon's screen model (src/efi/CharonScreenModel.h, the same recogniser Charon's resident reader
// uses for boot managers) run on a guest's text screen for the Charon Hypervisor's guest reader (6g).
//
// stdin:  "ROWS COLS CURSOR_ROW CURSOR_COL CURSOR_SHOWN COUNTING_DOWN NAME\n"
//         ROWS lines of UTF-8 text (one screen row each)
//         ROWS lines of attributes, two hex digits per cell (VGA: bits 0-3 foreground, 4-6 background)
// stdout: key=value lines (manager, screen, title, list.*, item.N.*, countdown.*, progress.*, edit.*, message.*, help.N)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "CharonScreenModel.h"

namespace sm = hakefi::screenmodel;

static std::u16string Utf16(const std::string& s)
{
	std::u16string out;
	for (size_t i = 0; i < s.size();) {
		const unsigned char c = static_cast<unsigned char>(s[i]);
		unsigned cp = c, n = 0;
		if (c >= 0xf0) { cp = c & 7; n = 3; } else if (c >= 0xe0) { cp = c & 15; n = 2; } else if (c >= 0xc0) { cp = c & 31; n = 1; }
		++i;
		for (unsigned k = 0; k < n && i < s.size(); ++k, ++i) cp = cp << 6 | (static_cast<unsigned char>(s[i]) & 63);
		out.push_back(cp > 0xffff ? u'?' : char16_t(cp));
	}
	return out;
}

static std::string Utf8(const char16_t* p, size_t n)
{
	std::string out;
	for (size_t i = 0; i < n; ++i) {
		const unsigned c = p[i] ? p[i] : u' ';
		if (c < 0x80) out.push_back(char(c));
		else if (c < 0x800) { out.push_back(char(0xc0 | c >> 6)); out.push_back(char(0x80 | (c & 63))); }
		else { out.push_back(char(0xe0 | c >> 12)); out.push_back(char(0x80 | (c >> 6 & 63))); out.push_back(char(0x80 | (c & 63))); }
	}
	while (!out.empty() && out.back() == ' ') out.pop_back();
	size_t a = 0;
	while (a < out.size() && out[a] == ' ') ++a;
	return out.substr(a);
}

int main()
{
	char head[512];
	if (!std::fgets(head, sizeof(head), stdin)) return 2;
	unsigned rows = 0, cols = 0, cr = 0, cc = 0, shown = 0, counting = 0;
	char name[256] = "";
	if (std::sscanf(head, "%u %u %u %u %u %u %255s", &rows, &cols, &cr, &cc, &shown, &counting, name) < 6 || !rows || !cols || rows > 200 || cols > 400) return 2;
	std::vector<char16_t> text(size_t(rows) * cols, u' ');
	std::vector<uint8_t> attr(size_t(rows) * cols, 0x87);
	static char line[16384];
	for (unsigned r = 0; r < rows && std::fgets(line, sizeof(line), stdin); ++r) {
		std::string s(line);
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
		const std::u16string u = Utf16(s);
		for (size_t c = 0; c < cols && c < u.size(); ++c) text[r * cols + c] = u[c];
	}
	unsigned bgCount[8] = {};
	for (unsigned r = 0; r < rows && std::fgets(line, sizeof(line), stdin); ++r)
		for (unsigned c = 0; c < cols && line[2 * c] && line[2 * c + 1] && line[2 * c] != '\n'; ++c) {
			const char hex[3] = {line[2 * c], line[2 * c + 1], 0};
			const uint8_t a = uint8_t(std::strtoul(hex, nullptr, 16));
			attr[r * cols + c] = uint8_t(0x80 | (a & 0x7f));
			++bgCount[(a >> 4) & 7];
		}
	sm::View v;
	v.text = text.data(); v.attr = attr.data(); v.stride = cols; v.rows = rows; v.columns = cols;
	v.cursorRow = cr; v.cursorCol = cc; v.cursorShown = shown != 0; v.countingDown = counting != 0;
	for (uint8_t b = 1; b < 8; ++b) if (bgCount[b] > bgCount[v.majorityBg]) v.majorityBg = b;
	const std::u16string who = Utf16(name[0] == '-' ? "" : name);
	static sm::Model m;
	sm::Build(v, who.c_str(), m);
	auto row = [&](unsigned r, unsigned from = 0, unsigned to = 0xffff) {
		if (r >= rows) return std::string();
		if (to > cols) to = cols;
		return from < to ? Utf8(&text[r * cols + from], to - from) : std::string();
	};
	std::printf("manager=%u\nscreen=%u\n", unsigned(m.manager), unsigned(m.screen));
	if (m.titleRow != sm::none8) std::printf("title=%s\ntitle.sure=%d\n", row(m.titleRow).c_str(), m.titleSure);
	const sm::List& l = m.list;
	std::printf("list.found=%d\nlist.sure=%d\nlist.count=%u\nlist.selected=%d\nlist.default=%d\nlist.more_above=%d\nlist.more_below=%d\n",
		l.found, l.sure, unsigned(l.count), l.selected == sm::none8 ? -1 : int(l.selected), l.defaultIndex == sm::none8 ? -1 : int(l.defaultIndex),
		l.moreAbove, l.moreBelow);
	for (unsigned i = 0; i < l.count && i < sm::maxItems; ++i) {
		const sm::Item& it = l.items[i];
		if (it.row == sm::none8) continue;
		std::printf("item.%u=%s\nitem.%u.flags=%u\n", i, row(it.row, it.left, it.right).c_str(), i, unsigned(it.flags));
	}
	if (m.countdown.found) std::printf("countdown.sure=%d\ncountdown.seconds=%u\n", m.countdown.sure, unsigned(m.countdown.seconds));
	if (m.progress.found) std::printf("progress.sure=%d\nprogress.percent=%u\n", m.progress.sure, unsigned(m.progress.percent));
	if (m.edit.found) std::printf("edit.sure=%d\nedit.text=%s\n", m.edit.sure, row(m.edit.row, m.edit.left, m.edit.right).c_str());
	if (m.message.found) {
		std::string msg;
		for (unsigned r = m.message.top; r <= m.message.bottom && r < rows; ++r) { const std::string t = row(r); if (!t.empty()) msg += (msg.empty() ? "" : " ") + t; }
		std::printf("message.sure=%d\nmessage.kind=%u\nmessage.text=%s\n", m.message.sure, unsigned(m.message.kind), msg.c_str());
	}
	for (unsigned i = 0; i < m.helpCount && i < sm::maxHelp; ++i) std::printf("help.%u=%s\n", i, row(m.help[i]).c_str());
	return 0;
}
