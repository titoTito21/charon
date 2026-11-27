// SPDX-License-Identifier: GPL-2.0-or-later
// Charon r409: multi-system plan - small text tools shared by the plan core
// (CharonPlanModel.h, CharonPlanLayout.h, CharonPlanState.h).
//
// Pure logic: no allocation, no floating point, no library call; the caller
// supplies every buffer. Text is UTF-8 (the voice interface takes UTF-8;
// Utf16() converts for the console). Compiles in the EFI build and on a host.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace hakefi::plan {

inline size_t Len(const char* s)
{
	size_t n = 0;
	if (s != nullptr)
		while (s[n] != 0)
			n++;
	return n;
}
inline char Lower(char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; }
inline bool Eq(const char* a, const char* b)
{
	if (a == nullptr || b == nullptr)
		return a == b;
	size_t i = 0;
	for (; a[i] != 0 && a[i] == b[i]; i++) {
	}
	return a[i] == b[i];
}
inline bool EqNoCase(const char* a, const char* b)
{
	if (a == nullptr || b == nullptr)
		return a == b;
	size_t i = 0;
	for (; a[i] != 0 && Lower(a[i]) == Lower(b[i]); i++) {
	}
	return Lower(a[i]) == Lower(b[i]);
}
inline bool StartsNoCase(const char* text, const char* prefix)
{
	if (text == nullptr || prefix == nullptr)
		return false;
	size_t i = 0;
	for (; prefix[i] != 0; i++)
		if (Lower(text[i]) != Lower(prefix[i]))
			return false;
	return true;
}
// Copies with truncation; always NUL ended. False when the text was cut.
inline bool Copy(char* to, size_t capacity, const char* from)
{
	if (to == nullptr || capacity == 0)
		return false;
	size_t i = 0;
	if (from != nullptr)
		for (; from[i] != 0 && i + 1 < capacity; i++)
			to[i] = from[i];
	to[i] = 0;
	return from == nullptr || from[i] == 0;
}
// Decimal number that fills the whole text; nothing else is accepted.
inline bool ParseNumber(const char* text, uint64_t& value)
{
	if (text == nullptr || text[0] == 0)
		return false;
	uint64_t v = 0;
	for (size_t i = 0; text[i] != 0; i++) {
		if (text[i] < '0' || text[i] > '9' || i >= 18)
			return false;
		v = v * 10 + uint64_t(text[i] - '0');
	}
	value = v;
	return true;
}
inline bool IsYes(const char* text) { return EqNoCase(text, "yes") || EqNoCase(text, "true") || Eq(text, "1"); }
inline bool IsNo(const char* text) { return EqNoCase(text, "no") || EqNoCase(text, "false") || Eq(text, "0"); }

// CRC-32 (IEEE), bit by bit: the plan file is small.
inline uint32_t Crc32(const char* data, size_t size)
{
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < size; i++) {
		crc ^= uint8_t(data[i]);
		for (int k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
	}
	return ~crc;
}

// Text builder over a caller's buffer. Never writes past it; `overflow` says
// that something did not fit (the text is then cut, still NUL ended).
struct Out {
	char* text;
	size_t capacity, length;
	bool overflow;
	Out(char* buffer, size_t size) : text(buffer), capacity(size), length(0), overflow(false)
	{
		if (text != nullptr && capacity != 0)
			text[0] = 0;
		else
			overflow = true;
	}
	Out& Ch(char c)
	{
		if (text == nullptr || length + 1 >= capacity) {
			overflow = true;
			return *this;
		}
		text[length++] = c;
		text[length] = 0;
		return *this;
	}
	Out& S(const char* s)
	{
		if (s != nullptr)
			for (size_t i = 0; s[i] != 0; i++)
				Ch(s[i]);
		return *this;
	}
	Out& SN(const char* s, size_t n)
	{
		for (size_t i = 0; i < n; i++)
			Ch(s[i]);
		return *this;
	}
	Out& N(uint64_t v)
	{
		char digits[24];
		int n = 0;
		do {
			digits[n++] = char('0' + v % 10);
			v /= 10;
		} while (v != 0);
		while (n > 0)
			Ch(digits[--n]);
		return *this;
	}
	Out& Hex(uint64_t v, int digits)
	{
		for (int i = digits - 1; i >= 0; i--)
			Ch("0123456789abcdef"[(v >> (4 * i)) & 15]);
		return *this;
	}
};

inline bool ParseHex(const char* text, size_t digits, uint64_t& value)
{
	uint64_t v = 0;
	for (size_t i = 0; i < digits; i++) {
		const char c = Lower(text[i]);
		if (c >= '0' && c <= '9')
			v = v * 16 + uint64_t(c - '0');
		else if (c >= 'a' && c <= 'f')
			v = v * 16 + uint64_t(c - 'a' + 10);
		else
			return false;
	}
	value = v;
	return true;
}

// A partition GUID as text, the usual form (first three fields little endian
// on disk): "c12a7328-f81f-11d2-ba4b-00a0c93ec93b".
inline void GuidText(const uint8_t bytes[16], Out& out)
{
	static const int order[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
	for (int i = 0; i < 16; i++) {
		if (i == 4 || i == 6 || i == 8 || i == 10)
			out.Ch('-');
		out.Hex(bytes[order[i]], 2);
	}
}
inline bool ParseGuid(const char* text, uint8_t bytes[16])
{
	static const int order[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
	if (Len(text) != 36)
		return false;
	uint8_t parsed[16];
	size_t at = 0;
	for (int i = 0; i < 16; i++) {
		if (i == 4 || i == 6 || i == 8 || i == 10) {
			if (text[at] != '-')
				return false;
			at++;
		}
		uint64_t v = 0;
		if (!ParseHex(text + at, 2, v))
			return false;
		parsed[order[i]] = uint8_t(v);
		at += 2;
	}
	for (int i = 0; i < 16; i++)
		bytes[i] = parsed[i];
	return true;
}

// UTF-8 to UTF-16 for the console; bad bytes become '?'. Returns the units
// written without the final NUL.
inline size_t Utf16(const char* utf8, char16_t* to, size_t capacity)
{
	size_t n = 0;
	if (to == nullptr || capacity == 0)
		return 0;
	const uint8_t* s = reinterpret_cast<const uint8_t*>(utf8);
	while (s != nullptr && *s != 0) {
		uint32_t c = *s++;
		int more = 0;
		if (c >= 0xF0) {
			c &= 7;
			more = 3;
		} else if (c >= 0xE0) {
			c &= 15;
			more = 2;
		} else if (c >= 0xC0) {
			c &= 31;
			more = 1;
		} else if (c >= 0x80) {
			c = '?';
		}
		for (; more > 0; more--) {
			if ((*s & 0xC0) != 0x80) {
				c = '?';
				break;
			}
			c = (c << 6) | (*s++ & 0x3F);
		}
		if (c >= 0x10000) {
			if (n + 2 >= capacity)
				break;
			c -= 0x10000;
			to[n++] = char16_t(0xD800 + (c >> 10));
			to[n++] = char16_t(0xDC00 + (c & 0x3FF));
		} else {
			if (n + 1 >= capacity)
				break;
			to[n++] = char16_t(c);
		}
	}
	to[n] = 0;
	return n;
}

// Polish noun after a number: 1 system, 2 systemy, 5 systemow.
inline const char* PolishForm(uint64_t n, const char* one, const char* few, const char* many)
{
	if (n == 1)
		return one;
	const uint64_t d = n % 10, h = n % 100;
	return d >= 2 && d <= 4 && !(h >= 12 && h <= 14) ? few : many;
}
// "40 gigabytes" / "40 gigabajtów".
inline void Gigabytes(Out& out, uint64_t n, bool polish)
{
	out.N(n).Ch(' ');
	if (polish)
		out.S(PolishForm(n, "gigabajt", "gigabajty", "gigabajtów"));
	else
		out.S(n == 1 ? "gigabyte" : "gigabytes");
}
inline void Megabytes(Out& out, uint64_t n, bool polish)
{
	out.N(n).Ch(' ');
	if (polish)
		out.S(PolishForm(n, "megabajt", "megabajty", "megabajtów"));
	else
		out.S(n == 1 ? "megabyte" : "megabytes");
}
// A size given in MiB, spoken in the unit that reads best.
inline void Size(Out& out, uint64_t mib, bool polish)
{
	if (mib >= 1024 && mib % 1024 == 0)
		Gigabytes(out, mib / 1024, polish);
	else if (mib >= 10 * 1024)
		Gigabytes(out, (mib + 512) / 1024, polish);
	else
		Megabytes(out, mib, polish);
}

} // namespace hakefi::plan
