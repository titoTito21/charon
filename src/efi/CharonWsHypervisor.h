// SPDX-License-Identifier: GPL-2.0-or-later
// r410 (agent 2): Charon Hypervisor - the install step of a plan (design: work/charon-hypervisor-design.md, parts 2, 3, 9).
//
// The plan (agent 1, CharonWsPlanDialogue.h WriteHypervisor) makes TWO partitions in the hypervisor's range:
//   P1 "Hypervisor"       kBootType (Microsoft basic data), FAT32, label kBootLabel, kBootMiB
//   P2 "Hypervisor data"  kDataType (Linux filesystem), the rest, NOT formatted, first MiB zeroed
// and then calls RunPlanStep with P1's volume. The step writes FILES on P1 only, never a partition table.
//
// Charon ships neither the mini Linux nor QEMU (user's point 2b). They are DOWNLOADED here from Alpine Linux's official
// server and checked before anything is written:
//   1. releases/x86_64/latest-releases.yaml of the branch -> the current alpine-standard ISO and its sha256;
//      the ISO (about 370 MB) must have exactly that sha256; its boot/vmlinuz-lts, boot/initramfs-lts and
//      boot/modloop-lts are kept (Alpine's own kernel, initramfs and module image).
//   2. main and community APKINDEX.tar.gz: RSA PKCS#1 v1.5 SHA-1 signature (.SIGN.RSA.<key>) over the second gzip
//      stream, checked with Alpine's public keys (alpine-keys, MIT) from Charon's medium (\EFI\HAK\hypervisor\keys).
//   3. the dependency closure of \EFI\HAK\hypervisor\world.txt is resolved from the two indexes; every .apk must have
//      the control checksum the signed index gives (C: "Q1" + base64 SHA-1 of its control gzip stream, which carries
//      the hash of the data).
// P1 then holds Alpine's diskless layout (boot/, apks/<repo>/x86_64/ with the official signed index, .boot_repository)
// plus Charon's own overlay charonhv.apkovl.tar.gz (manager, wizard, sound set-up; from \EFI\HAK\hypervisor) and
// \charonhv\charonhv.cfg (P2's unique GUID, language). Alpine's initramfs installs the packages from P1 into RAM at
// every start; the hypervisor formats P2 (ext4) at its first start. Host reference: tools/charon-hv-fetch.py.
#pragma once

#include "CharonSeedDisk.h"
#include "CharonInflate.h"
#include "CharonSha256.h"
#include "CharonWsAsk.h"

namespace hakefi {
namespace ws {
namespace hv {

constexpr seeddisk::Guid kBootType = seeddisk::kTypeBasicData;   // P1
constexpr seeddisk::Guid kDataType = seeddisk::kTypeLinuxData;   // P2
constexpr seeddisk::Guid kPartitionType = kBootType;             // the first signature's name (kept for the hook)
constexpr const char kBootLabel[] = "CHARONHV";                  // FAT volume label of P1 (11 characters at most)
constexpr const char16_t kBootName[] = u"Hypervisor";            // GPT names
constexpr const char16_t kDataName[] = u"Hypervisor data";
constexpr uint64_t kBootMiB = 2048;
constexpr uint64_t kDataMinimumMiB = 8 * 1024;
constexpr uint64_t kDataDefaultMiB = 64 * 1024;
constexpr const char16_t kMirror[] = u"https://dl-cdn.alpinelinux.org/alpine/v3.24/";
// The packages and the indexes: HTTPS too. Plain HTTP was tried (r410 run hvxp1, second run): Charon's client fetched the
// indexes but stalled on the third package, so it is not used. Integrity comes from the signed index either way.
constexpr const char16_t kPackages[] = u"https://dl-cdn.alpinelinux.org/alpine/v3.24/";
constexpr const char16_t kMedium[] = u"\\EFI\\HAK\\hypervisor";
constexpr size_t kMaxPackages = 400;
constexpr size_t kMaxEntries = 60000;

// ---- small helpers --------------------------------------------------------------------------------------------------
inline bool Same(const char* a, size_t an, const char* b, size_t bn)
{
	if (an != bn) return false;
	for (size_t i = 0; i < an; ++i) if (a[i] != b[i]) return false;
	return true;
}
inline size_t CLen(const char* s) { size_t n = 0; while (s[n]) ++n; return n; }
inline void Wide(char16_t* out, size_t cap, const char* s, size_t n)
{
	size_t i = 0;
	for (; i < n && i + 1 < cap; ++i) out[i] = char16_t(uint8_t(s[i]));
	out[i] = 0;
}
inline void CatAscii(char16_t* out, size_t cap, const char* s, size_t n)
{
	size_t at = Len(out);
	for (size_t i = 0; i < n && at + 1 < cap; ++i) out[at++] = char16_t(uint8_t(s[i]));
	out[at] = 0;
}
inline uint32_t Le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

struct Sha1 {
#ifdef HAK_CHARON_HTTPS
	br_sha1_context c;
	void Start() { br_sha1_init(&c); }
	void Add(const uint8_t* p, size_t n) { br_sha1_update(&c, p, n); }
	void End(uint8_t out[20]) { br_sha1_out(&c, out); }
#else
	void Start() {}
	void Add(const uint8_t*, size_t) {}
	void End(uint8_t out[20]) { for (int i = 0; i < 20; ++i) out[i] = 0; }
#endif
};

inline size_t Base64(const uint8_t* in, size_t n, char* out)
{
	static const char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t o = 0;
	for (size_t i = 0; i < n; i += 3) {
		const uint32_t v = uint32_t(in[i]) << 16 | (i + 1 < n ? uint32_t(in[i + 1]) << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
		out[o++] = abc[(v >> 18) & 63];
		out[o++] = abc[(v >> 12) & 63];
		out[o++] = i + 1 < n ? abc[(v >> 6) & 63] : '=';
		out[o++] = i + 2 < n ? abc[v & 63] : '=';
	}
	out[o] = 0;
	return o;
}
inline int B64Value(char c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

// ---- gzip streams (apk v2: concatenated gzip members) ---------------------------------------------------------------
struct ByteInput {
	const uint8_t* p;
	size_t left, taken;
	static size_t Next(void* ctx, const uint8_t** data)
	{
		ByteInput& s = *static_cast<ByteInput*>(ctx);
		if (!s.left) return 0;
		*data = s.p;
		++s.p; --s.left; ++s.taken;
		return 1;
	}
};
inline uint8_t* Work() { alignas(16) static uint8_t buffer[2u << 20]; return buffer; }   // zero-initialised, no run-time init
inline inflate::Decoder& Dec() { static inflate::Decoder d; return d; }
// Start of the deflate data of the gzip member at `p` (RFC 1952), or 0.
inline size_t GzipHeader(const uint8_t* p, size_t n)
{
	if (n < 18 || p[0] != 0x1f || p[1] != 0x8b || p[2] != 8) return 0;
	const uint8_t flags = p[3];
	size_t at = 10;
	if (flags & 4) { if (at + 2 > n) return 0; at += 2 + (size_t(p[at]) | size_t(p[at + 1]) << 8); }
	if (flags & 8) { while (at < n && p[at]) ++at; ++at; }
	if (flags & 16) { while (at < n && p[at]) ++at; ++at; }
	if (flags & 2) at += 2;
	return at < n ? at : 0;
}
// Decompresses ONE small gzip member at p into Work(); `memberBytes` its exact length. False: not a member / too big.
inline bool Member(const uint8_t* p, size_t n, size_t& memberBytes, size_t& plainBytes)
{
	const size_t start = GzipHeader(p, n);
	if (!start) return false;
	ByteInput in{p + start, n - start, 0};
	uint64_t produced = 0;
	const inflate::Input input{&in, ByteInput::Next};
	if (Dec().Run(input, Work(), 2u << 20, nullptr, &produced) != inflate::Error::None) return false;
	// the trailer (CRC32, ISIZE) follows the deflate data; allow for a decoder that read a few bytes ahead
	for (size_t back = 0; back <= 4 && back <= in.taken; ++back) {
		const size_t end = start + in.taken - back;
		if (end + 8 <= n && Le32(p + end + 4) == uint32_t(produced) && (end + 8 == n || (p[end + 8] == 0x1f && p[end + 9] == 0x8b))) {
			memberBytes = end + 8;
			plainBytes = size_t(produced);
			return true;
		}
	}
	return false;
}
// The first member `name` of a tar segment (no end blocks needed). Returns its data or null.
inline const uint8_t* TarFind(const uint8_t* t, size_t n, const char* name, size_t& size, bool prefix = false, const char** found = nullptr)
{
	for (size_t at = 0; at + 512 <= n;) {
		const uint8_t* h = t + at;
		if (!h[0]) return nullptr;
		size_t len = 0;
		while (len < 100 && h[len]) ++len;
		uint64_t s = 0;
		for (int i = 124; i < 136 && h[i]; ++i) if (h[i] >= '0' && h[i] <= '7') s = s * 8 + uint64_t(h[i] - '0');
		const size_t want = CLen(name);
		if (prefix ? (len > want && Same(reinterpret_cast<const char*>(h), want, name, want)) : Same(reinterpret_cast<const char*>(h), len, name, want)) {
			if (at + 512 + s > n) return nullptr;
			size = size_t(s);
			if (found) *found = reinterpret_cast<const char*>(h);
			return h + 512;
		}
		at += 512 + size_t((s + 511) / 512 * 512);
	}
	return nullptr;
}

// ---- RSA keys from Charon's medium ------------------------------------------------------------------------------------
struct Der {
	const uint8_t* p;
	size_t n;
	bool Read(uint8_t& tag, Der& value)
	{
		if (n < 2) return false;
		tag = p[0];
		size_t len = p[1], at = 2;
		if (len & 0x80) {
			const size_t k = len & 0x7f;
			if (k > 4 || n < 2 + k) return false;
			len = 0;
			for (size_t i = 0; i < k; ++i) len = len << 8 | p[2 + i];
			at = 2 + k;
		}
		if (at + len > n) return false;
		value = {p + at, len};
		p += at + len;
		n -= at + len;
		return true;
	}
};
struct Key {
	uint8_t n[1024], e[8];
	size_t nlen, elen;
};
// \EFI\HAK\hypervisor\keys\<name> (PEM SubjectPublicKeyInfo) -> modulus and exponent.
inline bool LoadKey(const char* name, size_t nameLen, Key& key)
{
	static FileRef ref;
	ref.volume = U().host->selfDevice;
	Set(ref.path, pathUnits, kMedium);
	Cat(ref.path, pathUnits, u"\\keys\\");
	CatAscii(ref.path, pathUnits, name, nameLen);
	uint8_t* text = nullptr;
	size_t size = 0;
	if (ReadWhole(ref, 16384, &text, &size) != EFI_SUCCESS) return false;
	static uint8_t der[4096];
	size_t dn = 0;
	uint32_t acc = 0;
	int bits = 0;
	bool body = false;
	for (size_t i = 0; i < size; ++i) {
		const char c = char(text[i]);
		if (c == '-') { body = !body && i > 0; while (i < size && text[i] != '\n') ++i; continue; }
		const int v = B64Value(c);
		if (v < 0) continue;
		acc = acc << 6 | uint32_t(v);
		bits += 6;
		if (bits >= 8) { bits -= 8; if (dn < sizeof(der)) der[dn++] = uint8_t(acc >> bits); }
	}
	Release(text);
	Der all{der, dn}, spki, alg, bitstr, seq, mod, exp;
	uint8_t tag;
	if (!all.Read(tag, spki) || !spki.Read(tag, alg) || !spki.Read(tag, bitstr) || bitstr.n < 2) return false;
	Der inner{bitstr.p + 1, bitstr.n - 1};
	if (!inner.Read(tag, seq) || !seq.Read(tag, mod) || !seq.Read(tag, exp)) return false;
	while (mod.n && !mod.p[0]) { ++mod.p; --mod.n; }
	if (mod.n > sizeof(key.n) || exp.n > sizeof(key.e)) return false;
	memcpy(key.n, mod.p, mod.n); key.nlen = mod.n;
	memcpy(key.e, exp.p, exp.n); key.elen = exp.n;
	return true;
}

// ---- what is fetched and held -------------------------------------------------------------------------------------
struct Field { const char* p; uint32_t n; };
enum F { fP, fV, fC, fD, fp, fi, fk, fS, fCount };
struct Entry { Field f[fCount]; uint8_t repo; };
struct Chosen { uint32_t entry; Memory apk; };
// r410 (agent 1's preset "Debian image + Charon Hypervisor" in 8 GB): the ISO is never held whole (370 MB in one block
// failed beside the 3 GiB image). It is streamed once: hashed as it arrives, its directory read from the first MB, and
// only /boot's three files are kept, each in pieces of at most 64 MB.
constexpr size_t kPiece = size_t(64) << 20;
constexpr size_t kMaxPieces = 8;   // 512 MB per file; modloop-lts of 3.24.4 is 294 MB
constexpr size_t kHead = size_t(1) << 20;
constexpr int kBootFiles = 3;
inline const char* BootName(int i) { static const char* const n[kBootFiles] = {"vmlinuz-lts", "initramfs-lts", "modloop-lts"}; return n[i]; }
struct Pieces { Memory part[kMaxPieces]; size_t count; uint64_t start, size, have; };
struct State {
	bool fetched;
	Pieces boot[kBootFiles];
	Memory index[2], plain[2], entries;
	size_t entryCount;
	Chosen chosen[kMaxPackages];
	size_t chosenCount;
	char version[24];
	uint64_t bytes;
	char report[16384];
	size_t reportSize;
	size_t unsaid;
};
inline State& S() { static State s; return s; }
inline const char* RepoName(int r) { return r ? "community" : "main"; }
inline Entry* Entries() { return reinterpret_cast<Entry*>(S().entries.data); }

// r410 (agent 1, h8 run 3, 8 GB VMware): the firmware gives AllocateAnyPages only below 4 GiB, which the 3 GB image
// had filled while ~5 GB above 4 GiB were free. Same order as held::Make (CharonWsWinUsb.h): any pages, then by the
// highest address, then the start of a free range of the memory map itself, highest ranges first.
inline bool ReserveHigh(Memory& m, size_t bytes)
{
	if (Reserve(m, bytes)) return true;
	auto* boot = U().host->boot;
	const size_t pages = (bytes + 4095) / 4096;
	efi_physical_addr address = ~efi_physical_addr(0);
	bool ok = pages && boot->AllocatePages(AllocateMaxAddress, m.reserved ? EfiReservedMemoryType : EfiLoaderData, pages, &address) == EFI_SUCCESS && address;
	if (!ok && pages) {
		size_t size = 0, key = 0, descriptor = 0;
		uint32_t version = 0;
		if (boot->GetMemoryMap(&size, nullptr, &key, &descriptor, &version) == EFI_BUFFER_TOO_SMALL && descriptor >= sizeof(efi_memory_descriptor)) {
			size += descriptor * 16;
			void* buffer = Allocate(size);
			size_t used = size;
			if (buffer && boot->GetMemoryMap(&used, static_cast<efi_memory_descriptor*>(buffer), &key, &descriptor, &version) == EFI_SUCCESS) {
				efi_physical_addr tried = ~efi_physical_addr(0);
				while (!ok) {
					const efi_memory_descriptor* best = nullptr;
					for (size_t at = 0; at + descriptor <= used; at += descriptor) {
						const auto* d = reinterpret_cast<const efi_memory_descriptor*>(static_cast<const uint8_t*>(buffer) + at);
						if (d->Type == EfiConventionalMemory && d->NumberOfPages >= pages && d->PhysicalStart < tried && (!best || d->PhysicalStart > best->PhysicalStart)) best = d;
					}
					if (!best) break;
					tried = best->PhysicalStart;
					address = best->PhysicalStart + (best->NumberOfPages - pages) * 4096;
					ok = boot->AllocatePages(AllocateAddress, EfiLoaderData, pages, &address) == EFI_SUCCESS && address;
				}
			}
			Release(buffer);
		}
	}
	if (!ok) return false;
	m.data = reinterpret_cast<uint8_t*>(address);
	m.capacity = pages * 4096;
	m.size = 0;
	return true;
}

inline void Note(const char* a, const char* b = nullptr, size_t bn = 0)
{
	auto& s = S();
	for (size_t i = 0; a[i] && s.reportSize + 2 < sizeof(s.report); ++i) s.report[s.reportSize++] = a[i];
	for (size_t i = 0; b && i < bn && s.reportSize + 2 < sizeof(s.report); ++i) s.report[s.reportSize++] = b[i];
	s.report[s.reportSize] = 0;
}
inline void NoteLine(const char* a, const char* b = nullptr, size_t bn = 0) { Note(a, b, bn); Note("\n"); }

inline void FreeAll()
{
	auto& s = S();
	for (auto& b : s.boot) { for (size_t i = 0; i < b.count; ++i) Free(b.part[i]); b = Pieces{}; }
	for (auto& m : s.index) Free(m);
	for (auto& m : s.plain) Free(m);
	Free(s.entries);
	for (size_t i = 0; i < s.chosenCount; ++i) Free(s.chosen[i].apk);
	s.chosenCount = 0;
	s.entryCount = 0;
	s.fetched = false;
	s.bytes = 0;
}
inline bool Download(const char16_t* url, Memory& m, const char16_t* what)
{
	static FileRef none;
	none.volume = nullptr;
	none.path[0] = 0;
	m.data = nullptr; m.capacity = 0; m.size = 0; m.extra = 0; m.reserved = false;
	uint64_t received = 0;
	for (int attempt = 0; attempt < 3; ++attempt) {
		if (net::DownloadAny(url, none, &received, &m, what)) {
			S().bytes += m.size;
			return true;
		}
		Free(m);
	}
	return false;
}
inline void FailWith(const char16_t* pl, const char16_t* en, const char* detail = nullptr, size_t n = 0)
{
	static char16_t text[600];
	Set(text, 600, T(pl, en));
	if (detail) { Cat(text, 600, u" "); CatAscii(text, 600, detail, n); }
	Fail(text);
	NoteLine("FAILED: ", detail ? detail : "", detail ? n : 0);
}

// ---- step 1: the ISO -----------------------------------------------------------------------------------------------
inline bool HexEq(const uint8_t* digest, const char* hex)
{
	static const char h[] = "0123456789abcdef";
	for (int i = 0; i < 32; ++i)
		if (h[digest[i] >> 4] != (hex[2 * i] | 0x20) || h[digest[i] & 15] != (hex[2 * i + 1] | 0x20)) return false;
	return true;
}
// A directory entry of the ISO by its Rock Ridge name (or its ISO 9660 name without ";1"), looked up in the first
// `have` bytes of the image. 1 found, 0 not there, -1 the directory is not all in `have` yet.
inline int IsoFind(const uint8_t* iso, size_t have, uint32_t lba, uint32_t len, const char* name, uint32_t& outLba, uint32_t& outLen)
{
	if (uint64_t(lba) * 2048 + len > have) return -1;
	const uint8_t* d = iso + size_t(lba) * 2048;
	const size_t wl = CLen(name);
	for (size_t i = 0; i < len;) {
		const uint8_t rl = d[i];
		if (!rl) { i = (i / 2048 + 1) * 2048; continue; }
		if (i + rl > len || rl < 34) break;
		const uint8_t* r = d + i;
		const uint8_t nameLen = r[32];
		const char* nm = reinterpret_cast<const char*>(r + 33);
		size_t nmLen = nameLen;
		for (size_t j = 33 + nameLen + ((nameLen + 1) % 2); j + 4 <= rl && r[j + 2];) {
			if (r[j] == 'N' && r[j + 1] == 'M' && r[j + 2] > 5) { nm = reinterpret_cast<const char*>(r + j + 5); nmLen = size_t(r[j + 2]) - 5; }
			j += r[j + 2];
		}
		if (Same(nm, nmLen, name, wl)) { outLba = Le32(r + 2); outLen = Le32(r + 10); return 1; }
		i += rl;
	}
	return 0;
}
struct IsoJob {
	Sha256 h;
	Memory head;
	uint64_t pos, total;
	int parsed;          // 0 not yet, 1 the three files located
	const char* why;     // a failure of the stream itself: English detail ...
	const char16_t* whyPl;   // ... and the same in Polish (whyName: the English detail is a file name, added to both)
	bool whyName;
	unsigned rounds;
};
inline bool Why(IsoJob& j, const char* en, const char16_t* pl, bool name = false)
{
	j.why = en; j.whyPl = pl; j.whyName = name;
	return false;
}
// The bytes [at, at+size) of the image into the pieces of the boot files they belong to.
inline bool IsoKeep(const uint8_t* data, size_t size, uint64_t at)
{
	for (auto& b : S().boot) {
		uint64_t from = at > b.start ? at : b.start;
		const uint64_t end = at + size < b.start + b.size ? at + size : b.start + b.size;
		while (from < end) {
			const uint64_t o = from - b.start;
			const size_t k = size_t(o / kPiece);
			if (k >= kMaxPieces) return false;
			while (b.count <= k) {
				Memory& m = b.part[b.count];
				m.extra = 0; m.reserved = false;
				const uint64_t left = b.size - uint64_t(b.count) * kPiece;
				if (!ReserveHigh(m, size_t(left < kPiece ? left : kPiece))) return false;
				++b.count;
			}
			const uint64_t pieceEnd = (uint64_t(k) + 1) * kPiece;
			const uint64_t take = (end - from < pieceEnd - o) ? end - from : pieceEnd - o;
			memcpy(b.part[k].data + (o - uint64_t(k) * kPiece), data + (from - at), size_t(take));
			if (o + take > b.have) b.have = o + take;
			from += take;
		}
	}
	return true;
}
inline bool IsoLocate(IsoJob& j, size_t have)
{
	const uint8_t* iso = j.head.data;
	if (have < 17 * 2048) return true;
	if (iso[16 * 2048] != 1 || !Same(reinterpret_cast<const char*>(iso + 16 * 2048 + 1), 5, "CD001", 5)) { return Why(j, "not an ISO 9660 image", u"to nie jest obraz ISO 9660"); }
	const uint8_t* pvd = iso + 16 * 2048;
	uint32_t bootLba = 0, bootLen = 0;
	int r = IsoFind(iso, have, Le32(pvd + 158), Le32(pvd + 166), "boot", bootLba, bootLen);
	if (r < 0) return true;
	if (r == 0) { return Why(j, "no /boot", u"brak katalogu /boot"); }
	uint32_t lba[kBootFiles], len[kBootFiles];
	for (int i = 0; i < kBootFiles; ++i) {
		r = IsoFind(iso, have, bootLba, bootLen, BootName(i), lba[i], len[i]);
		if (r < 0) return true;
		if (r == 0) { return Why(j, BootName(i), u"brak pliku startowego", true); }
	}
	for (int i = 0; i < kBootFiles; ++i) {
		Pieces& b = S().boot[i];
		b.start = uint64_t(lba[i]) * 2048; b.size = len[i];
		if (b.size > uint64_t(kPiece) * kMaxPieces) { return Why(j, "a boot file above 512 MB", u"plik startowy większy niż 512 MB"); }
	}
	j.parsed = 1;
	return IsoKeep(iso, have, 0) || Why(j, "not enough RAM for a 64 MB piece", u"za mało pamięci RAM na część 64 MB");
}
inline IsoJob& Job() { static IsoJob j{}; return j; }
inline bool IsoSink(void* context, const uint8_t* data, size_t size)
{
	auto& j = *static_cast<IsoJob*>(context);
	const uint64_t at = j.pos;
	j.h.Update(data, size);
	if (at < kHead) memcpy(j.head.data + at, data, size_t(at + size <= kHead ? size : kHead - at));
	j.pos += size;
	if (!j.parsed) {
		const size_t have = size_t(j.pos < kHead ? j.pos : kHead);
		if (!IsoLocate(j, have)) return false;
		if (!j.parsed) {
			if (j.pos >= kHead) { return Why(j, "the directory is not in the image's first MB", u"katalogu nie ma w pierwszym MB obrazu"); }
		} else {
			// a file whose bytes between the first MB and this piece went by before the directory was read
			for (auto& b : S().boot)
				if (at > kHead && b.start < at && b.start + b.size > kHead) { return Why(j, "a boot file before its directory", u"plik startowy przed swoim katalogiem"); }
		}
	}
	if (j.parsed && !IsoKeep(data, size, at)) { return Why(j, "not enough RAM for a 64 MB piece", u"za mało pamięci RAM na część 64 MB"); }
	if (++j.rounds % 16 == 0 && j.total) Progress(T(u"Alpine Linux (jądro i moduły)", u"Alpine Linux (kernel and modules)"), j.pos, j.total);
	Pump();
	return true;
}
inline bool StreamIso(const char16_t* url, const char* expected)
{
#ifdef HAK_CHARON_HTTPS
	static https::Client client;
	static https::Response probe, answer;
	static char ascii[600];
	size_t n = 0;
	for (; url[n] && n + 1 < sizeof(ascii); ++n) ascii[n] = char(url[n]);
	ascii[n] = 0;
	IsoJob& j = Job();
	j.head.extra = 0; j.head.reserved = false;
	if (!j.head.data && !ReserveHigh(j.head, kHead)) { FailWith(u"Za mało pamięci RAM na początek obrazu Alpine.", u"Not enough RAM for the start of the Alpine image."); return false; }
	https::Result r = https::Result::ok;
	for (int attempt = 0; attempt < 3; ++attempt) {
		for (auto& b : S().boot) { for (size_t i = 0; i < b.count; ++i) Free(b.part[i]); b = Pieces{}; }
		j.h = Sha256{}; j.pos = 0; j.parsed = 0; j.why = nullptr; j.rounds = 0;
		client.transport = net::tcp::Transport();
		r = https::Probe(client, ascii, probe);
		if (r != https::Result::ok) continue;
		j.total = probe.totalLength != https::unknownLength ? probe.totalLength : probe.contentLength != https::unknownLength ? probe.contentLength : 0;
		uint64_t offset = 0;
		r = https::Download(client, ascii, offset, probe.validator[0] ? probe.validator : nullptr, IsoSink, &j, answer);
		if (r == https::Result::ok && !j.why && j.parsed) break;
		if (j.why) break;   // the image itself or the memory: another attempt cannot help
	}
	Free(j.head);
	if (j.why) {
		static char16_t text[600];
		const char16_t* pl = u"Obrazu Alpine nie da się odebrać w częściach po 64 MB:";
		const char16_t* head = T(pl, u"The Alpine image cannot be received in pieces of 64 MB:");
		Set(text, 600, head);
		Cat(text, 600, u" ");
		if (head == pl && j.whyPl) {
			Cat(text, 600, j.whyPl);
			if (j.whyName) { Cat(text, 600, u" "); CatAscii(text, 600, j.why, CLen(j.why)); }
		} else {
			if (j.whyName) Cat(text, 600, u"missing boot file ");
			CatAscii(text, 600, j.why, CLen(j.why));
		}
		Fail(text);
		NoteLine("FAILED: ", j.why, CLen(j.why));
		return false;
	}
	if (r != https::Result::ok || !j.parsed) { net::HttpsFail(client, r); NoteLine("FAILED: the Alpine image"); return false; }
	for (auto& b : S().boot)
		if (b.have != b.size) { FailWith(u"Obraz Alpine urwał się przed końcem plików startowych.", u"The Alpine image ended before the end of its boot files."); return false; }
	Progress(T(u"Alpine Linux (jądro i moduły)", u"Alpine Linux (kernel and modules)"), j.pos, j.total ? j.total : j.pos);
	uint8_t digest[32];
	j.h.Final(digest);
	if (!HexEq(digest, expected)) { FailWith(u"Obraz Alpine ma inną sumę SHA-256 niż podaje serwer; nic nie zapisano.", u"The Alpine image has another SHA-256 than the server gives; nothing was written."); return false; }
	S().bytes += j.pos;
	return true;
#else
	(void)url; (void)expected;
	FailWith(u"Ta kompilacja Charona nie ma klienta HTTPS.", u"This build of Charon has no HTTPS client.");
	return false;
#endif
}
inline bool FetchIso()
{
	auto& s = S();
	static char16_t url[300];
	Memory yaml{};
	Set(url, 300, kMirror);
	Cat(url, 300, u"releases/x86_64/latest-releases.yaml");
	if (!Download(url, yaml, T(u"lista wydań Alpine Linux", u"the list of Alpine Linux releases"))) return false;
	const char* y = reinterpret_cast<const char*>(yaml.data);
	const size_t yn = yaml.size;
	static const char want[] = "file: alpine-standard-";
	const size_t wl = sizeof(want) - 1;
	size_t at = 0;
	for (; at + wl < yn; ++at) if (Same(y + at, wl, want, wl)) break;
	if (at + wl >= yn) { Free(yaml); FailWith(u"Na serwerze Alpine nie ma obrazu alpine-standard.", u"Alpine's server lists no alpine-standard image."); return false; }
	const char* name = y + at + 6;
	size_t nl = 0;
	while (name + nl < y + yn && name[nl] != '\n' && name[nl] != '\r') ++nl;
	size_t vn = 0;
	while (vn + 1 < sizeof(s.version) && name[16 + vn] != '-' && 16 + vn < nl) { s.version[vn] = name[16 + vn]; ++vn; }
	s.version[vn] = 0;
	const char* sha = nullptr;
	for (size_t k = at; k + 8 < yn && k < at + 2000; ++k)
		if (Same(y + k, 8, "sha256: ", 8)) { sha = y + k + 8; break; }
	static char expected[65];
	if (!sha || sha + 64 > y + yn) { Free(yaml); FailWith(u"Lista wydań Alpine nie podaje sumy SHA-256 obrazu.", u"Alpine's release list gives no SHA-256 of the image."); return false; }
	memcpy(expected, sha, 64); expected[64] = 0;
	Set(url, 300, kMirror);
	Cat(url, 300, u"releases/x86_64/");
	CatAscii(url, 300, name, nl);
	Free(yaml);
	NoteLine("release list: https://dl-cdn.alpinelinux.org/alpine/v3.24/releases/x86_64/latest-releases.yaml");
	if (!StreamIso(url, expected)) return false;
	Note("standard ISO "); NoteLine(name, nullptr, 0);
	Note("  sha256 "); NoteLine(expected, nullptr, 0);
	return true;
}

// ---- step 2: the signed indexes ----------------------------------------------------------------------------------------
inline bool VerifyIndex(int r)
{
	auto& s = S();
	Memory& m = s.index[r];
	size_t sigBytes = 0, sigPlain = 0;
	if (!Member(m.data, m.size, sigBytes, sigPlain)) { FailWith(u"Indeks pakietów Alpine nie ma podpisu.", u"Alpine's package index carries no signature.", RepoName(r), CLen(RepoName(r))); return false; }
	const char* signName = nullptr;
	size_t sigSize = 0;
	const uint8_t* sig = TarFind(Work(), sigPlain, ".SIGN.RSA.", sigSize, true, &signName);
	if (!sig || !signName || sigSize > 1024) { FailWith(u"Indeks pakietów Alpine nie ma podpisu RSA.", u"Alpine's package index has no RSA signature."); return false; }
	static uint8_t signature[1024];
	memcpy(signature, sig, sigSize);
	const char* keyName = signName + 10;
	size_t keyLen = 0;
	while (keyLen < 90 && keyName[keyLen]) ++keyLen;
	static Key key;
	if (!LoadKey(keyName, keyLen, key)) { FailWith(u"Indeks jest podpisany kluczem, którego Charon nie zna:", u"The index is signed with a key Charon does not know:", keyName, keyLen); return false; }
	uint8_t digest[20], got[20];
	Sha1 h; h.Start(); h.Add(m.data + sigBytes, m.size - sigBytes); h.End(digest);
#ifdef HAK_CHARON_HTTPS
	br_rsa_public_key pk{key.n, key.nlen, key.e, key.elen};
	const bool ok = br_rsa_pkcs1_vrfy_get_default()(signature, sigSize, BR_HASH_OID_SHA1, 20, &pk, got) == 1 && Same(reinterpret_cast<const char*>(got), 20, reinterpret_cast<const char*>(digest), 20);
#else
	const bool ok = false;
	(void)got;
#endif
	if (!ok) { FailWith(u"Podpis indeksu pakietów Alpine się nie zgadza; nic nie zapisano.", u"The signature of Alpine's package index does not match; nothing was written.", RepoName(r), CLen(RepoName(r))); return false; }
	Note("index "); Note(RepoName(r)); Note(" signed by "); NoteLine(keyName, nullptr, 0);
	// the index itself: second member, one shot into its own memory
	const size_t start = GzipHeader(m.data + sigBytes, m.size - sigBytes);
	Memory& plain = s.plain[r];
	plain.extra = 0; plain.reserved = false;
	if (!start || !ReserveHigh(plain, 64u << 20)) { FailWith(u"Za mało pamięci na indeks pakietów.", u"Not enough memory for the package index."); return false; }
	inflate::MemoryInput mi{m.data + sigBytes + start, m.size - sigBytes - start};
	uint64_t produced = 0;
	if (Dec().Run(inflate::Input{&mi, inflate::MemoryInput::Next}, plain.data, plain.capacity, nullptr, &produced) != inflate::Error::None) {
		FailWith(u"Indeksu pakietów nie da się rozpakować.", u"The package index cannot be unpacked."); return false;
	}
	plain.size = size_t(produced);
	size_t textSize = 0;
	const uint8_t* text = TarFind(plain.data, plain.size, "APKINDEX", textSize);
	if (!text) { FailWith(u"W indeksie brak pliku APKINDEX.", u"The index holds no APKINDEX file."); return false; }
	// parse: blocks separated by an empty line, lines "X:value"
	Entry* E = Entries();
	const char* t = reinterpret_cast<const char*>(text);
	Entry cur{};
	bool any = false;
	for (size_t i = 0; i < textSize;) {
		size_t e = i;
		while (e < textSize && t[e] != '\n') ++e;
		if (e == i) {
			if (any && s.entryCount < kMaxEntries) { cur.repo = uint8_t(r); E[s.entryCount++] = cur; }
			cur = Entry{}; any = false;
		} else if (e - i >= 2 && t[i + 1] == ':') {
			int f = -1;
			switch (t[i]) { case 'P': f = fP; break; case 'V': f = fV; break; case 'C': f = fC; break; case 'D': f = fD; break;
				case 'p': f = fp; break; case 'i': f = fi; break; case 'k': f = fk; break; case 'S': f = fS; break; default: break; }
			if (f >= 0) { cur.f[f] = {t + i + 2, uint32_t(e - i - 2)}; any = true; }
		}
		i = e + 1;
	}
	if (any && s.entryCount < kMaxEntries) { cur.repo = uint8_t(r); E[s.entryCount++] = cur; }
	return true;
}

// ---- step 3: the dependency closure --------------------------------------------------------------------------------
inline size_t NameLen(const char* p, size_t n)
{
	size_t k = 0;
	while (k < n && p[k] != '<' && p[k] != '>' && p[k] != '=' && p[k] != '~') ++k;
	return k;
}
inline bool Provides(const Entry& e, const char* name, size_t n)
{
	if (Same(e.f[fP].p, e.f[fP].n, name, n)) return true;
	const char* p = e.f[fp].p;
	const size_t pn = e.f[fp].n;
	for (size_t i = 0; i < pn;) {
		while (i < pn && p[i] == ' ') ++i;
		size_t j = i;
		while (j < pn && p[j] != ' ') ++j;
		if (j > i && Same(p + i, NameLen(p + i, j - i), name, n)) return true;
		i = j;
	}
	return false;
}
inline bool Satisfied(const char* name, size_t n)
{
	auto& s = S();
	for (size_t i = 0; i < s.chosenCount; ++i) if (Provides(Entries()[s.chosen[i].entry], name, n)) return true;
	return false;
}
inline long Find(const char* name, size_t n)
{
	auto& s = S();
	Entry* E = Entries();
	for (size_t i = 0; i < s.entryCount; ++i) if (Same(E[i].f[fP].p, E[i].f[fP].n, name, n)) return long(i);
	long best = -1, bestPriority = -1;
	for (size_t i = 0; i < s.entryCount; ++i) {
		if (!Provides(E[i], name, n)) continue;
		long pr = 0;
		for (uint32_t k = 0; k < E[i].f[fk].n; ++k) pr = pr * 10 + (E[i].f[fk].p[k] - '0');
		if (pr > bestPriority) { best = long(i); bestPriority = pr; }
	}
	return best;
}
struct Todo { const char* p; uint32_t n; };
inline Todo* Queue() { static Todo q[8192]; return q; }
inline bool Resolve(const char* world, size_t worldSize)
{
	auto& s = S();
	Todo* q = Queue();
	size_t qn = 0;
	auto push = [&](const char* p, size_t n) {
		for (size_t i = 0; i < n;) {
			while (i < n && (p[i] == ' ' || p[i] == '\n' || p[i] == '\r')) ++i;
			size_t j = i;
			while (j < n && p[j] != ' ' && p[j] != '\n' && p[j] != '\r') ++j;
			if (j > i && p[i] != '!' && qn < 8192) q[qn++] = {p + i, uint32_t(j - i)};
			i = j;
		}
	};
	for (size_t i = 0; i < worldSize;) {     // world.txt: one name a line, '#' comments
		size_t e = i;
		while (e < worldSize && world[e] != '\n') ++e;
		if (e > i && world[i] != '#') push(world + i, e - i);
		i = e + 1;
	}
	for (int round = 0; round < 50; ++round) {
		while (qn) {
			const Todo t = q[--qn];
			const size_t n = NameLen(t.p, t.n);
			if (Satisfied(t.p, n)) continue;
			const long found = Find(t.p, n);
			if (found < 0) { FailWith(u"Pakietu nie ma w repozytorium Alpine:", u"A package is missing from Alpine's repository:", t.p, n); return false; }
			if (s.chosenCount >= kMaxPackages) { FailWith(u"Za dużo pakietów.", u"Too many packages."); return false; }
			s.chosen[s.chosenCount].entry = uint32_t(found);
			s.chosen[s.chosenCount].apk = Memory{};
			++s.chosenCount;
			const Entry& e = Entries()[found];
			push(e.f[fD].p, e.f[fD].n);
		}
		bool added = false;      // install_if: apk adds these by itself, so they must be in the local repository
		for (size_t i = 0; i < s.entryCount; ++i) {
			const Entry& e = Entries()[i];
			if (!e.f[fi].n) continue;
			bool chosen = false;
			for (size_t c = 0; c < s.chosenCount && !chosen; ++c) chosen = s.chosen[c].entry == i;
			if (chosen || Find(e.f[fP].p, e.f[fP].n) != long(i)) continue;
			bool all = true;
			const char* p = e.f[fi].p;
			for (size_t a = 0; a < e.f[fi].n && all;) {
				while (a < e.f[fi].n && p[a] == ' ') ++a;
				size_t b = a;
				while (b < e.f[fi].n && p[b] != ' ') ++b;
				if (b > a && p[a] != '!') all = Satisfied(p + a, NameLen(p + a, b - a));
				a = b;
			}
			if (all && s.chosenCount < kMaxPackages) {
				s.chosen[s.chosenCount].entry = uint32_t(i);
				s.chosen[s.chosenCount].apk = Memory{};
				++s.chosenCount;
				push(e.f[fD].p, e.f[fD].n);
				added = true;
			}
		}
		if (!added && !qn) return true;
	}
	return true;
}
inline void ApkName(const Entry& e, char16_t* out, size_t cap)
{
	Wide(out, cap, e.f[fP].p, e.f[fP].n);
	Cat(out, cap, u"-");
	CatAscii(out, cap, e.f[fV].p, e.f[fV].n);
	Cat(out, cap, u".apk");
}
// r410 (agent 3, user 2026-11-26: "packages must download several at once"): ALL packages in one batch of
// net::many::Download (CharonWsDownloads.h) - 6 kept-alive HTTPS connections to the mirror at once instead of one
// connection and one TLS handshake per package (151 packages took 25 minutes). Each package's memory is reserved first
// (ReserveHigh, as DownloadKnown did); each package is checked against the signed index as soon as it is complete,
// while the others still download; its name is said as it arrives (the newest, "and 12 more" when faster than speech).
struct PackageJob { char url[256]; char16_t name[96]; };
inline bool CheckPackage(net::many::Item& item)
{
	const Chosen& c = *static_cast<const Chosen*>(item.user);
	const Entry& e = Entries()[c.entry];
	size_t sigBytes = 0, sigPlain = 0, ctlBytes = 0, ctlPlain = 0;
	bool ok = c.apk.size == item.size && Member(c.apk.data, c.apk.size, sigBytes, sigPlain)
		&& Member(c.apk.data + sigBytes, c.apk.size - sigBytes, ctlBytes, ctlPlain);
	if (ok) {
		uint8_t digest[20];
		Sha1 h; h.Start(); h.Add(c.apk.data + sigBytes, ctlBytes); h.End(digest);
		char q1[40] = "Q1";
		Base64(digest, 20, q1 + 2);
		ok = Same(q1, CLen(q1), e.f[fC].p, e.f[fC].n);
	}
	return ok;
}
inline bool FetchPackages()
{
	auto& s = S();
	if (!s.chosenCount) return true;
	auto* items = static_cast<net::many::Item*>(Allocate(sizeof(net::many::Item) * s.chosenCount));
	auto* jobs = static_cast<PackageJob*>(Allocate(sizeof(PackageJob) * s.chosenCount));
	if (!items || !jobs) { Release(items); Release(jobs); FailWith(u"Za mało pamięci.", u"Not enough memory."); return false; }
	bool ok = true;
	for (size_t i = 0; i < s.chosenCount && ok; ++i) {
		Chosen& c = s.chosen[i];
		const Entry& e = Entries()[c.entry];
		net::many::Item& it = items[i];
		memset(&it, 0, sizeof(it));
		static char16_t file[200];
		ApkName(e, file, 200);
		PackageJob& j = jobs[i];
		size_t n = 0;
		for (const char16_t* k = kPackages; *k && n + 1 < sizeof(j.url); ++k) j.url[n++] = char(*k);
		for (const char* k = RepoName(e.repo); *k && n + 1 < sizeof(j.url); ++k) j.url[n++] = *k;
		for (const char* k = "/x86_64/"; *k && n + 1 < sizeof(j.url); ++k) j.url[n++] = *k;
		for (size_t k = 0; file[k] && n + 1 < sizeof(j.url); ++k) j.url[n++] = char(file[k]);
		j.url[n] = 0;
		Wide(j.name, 96, e.f[fP].p, e.f[fP].n);
		uint64_t size = 0;
		for (uint32_t k = 0; k < e.f[fS].n; ++k) size = size * 10 + uint64_t(e.f[fS].p[k] - '0');
		c.apk.data = nullptr; c.apk.capacity = 0; c.apk.size = 0; c.apk.extra = 0; c.apk.reserved = false;
		if (!size || !ReserveHigh(c.apk, size_t(size))) { FailWith(u"Za mało pamięci.", u"Not enough memory.", j.url, n); ok = false; break; }
		it.url = j.url; it.size = size; it.memory = &c.apk; it.check = CheckPackage; it.name = j.name; it.user = &c;
	}
	if (ok) {
		net::many::Options o{};
		o.connections = 6; o.perHost = 6;
		o.label = T(u"Charon Hypervisor: pakiety", u"Charon Hypervisor: packages");
		const net::many::Outcome r = net::many::Download(items, s.chosenCount, o);
		NoteLine(r.failed ? "packages: some failed" : "packages: all downloaded and checked against the signed index");
		if (r.failed) {
			ok = false;
			net::many::Item& f = *r.firstFailure;
			const size_t an = CLen(f.url);
			if (f.checkFailed)
				FailWith(u"Pakiet nie zgadza się z podpisanym indeksem; nic nie zapisano:", u"A package does not match the signed index; nothing was written:", f.url, an);
			else { net::many::Explain(f); NoteLine("FAILED: ", f.url, an); }
		} else
			for (size_t i = 0; i < s.chosenCount; ++i) s.bytes += s.chosen[i].apk.size;
	}
	Release(items);
	Release(jobs);
	return ok;
}

// ---- the medium's own files ----------------------------------------------------------------------------------------------
inline bool ReadMedium(const char16_t* name, uint8_t** data, size_t* size, size_t limit)
{
	static FileRef ref;
	ref.volume = U().host->selfDevice;
	Set(ref.path, pathUnits, kMedium);
	Cat(ref.path, pathUnits, name);
	return ReadWhole(ref, limit, data, size) == EFI_SUCCESS;
}

// Everything, in memory, checked. False: said why (nothing was written anywhere).
inline bool FetchAll()
{
	auto& s = S();
	if (s.fetched) return true;
	FreeAll();
	s.reportSize = 0;
	s.report[0] = 0;
	NoteLine("Charon Hypervisor install report (Charon r410)");
	uint8_t* world = nullptr;
	size_t worldSize = 0;
	if (!ReadMedium(u"\\world.txt", &world, &worldSize, 65536)) {
		FailWith(u"Na nośniku Charona brak plików Charon Hypervisor (EFI\\HAK\\hypervisor).", u"Charon's medium lacks the Charon Hypervisor files (EFI\\HAK\\hypervisor).");
		return false;
	}
	Say(T(u"Pobieram Charon Hypervisor z oficjalnych serwerów Alpine Linux i sprawdzam podpisy.",
	      u"Downloading Charon Hypervisor from Alpine Linux's official servers and checking the signatures."));
	s.entries.extra = 0; s.entries.reserved = false;
	bool ok = ReserveHigh(s.entries, sizeof(Entry) * kMaxEntries);
	if (!ok) FailWith(u"Za mało pamięci.", u"Not enough memory.");
	ok = ok && FetchIso();
	// r410 (agent 3): both signed indexes at once (net::many::Download), then their signatures are checked.
	if (ok) {
		static char urls[2][200];
		static net::many::Item items[2];
		for (int r = 0; r < 2; ++r) {
			size_t n = 0;
			for (const char16_t* k = kPackages; *k && n + 1 < 200; ++k) urls[r][n++] = char(*k);
			for (const char* k = RepoName(r); *k && n + 1 < 200; ++k) urls[r][n++] = *k;
			for (const char* k = "/x86_64/APKINDEX.tar.gz"; *k && n + 1 < 200; ++k) urls[r][n++] = *k;
			urls[r][n] = 0;
			memset(&items[r], 0, sizeof(items[r]));
			s.index[r] = Memory{};
			items[r].url = urls[r]; items[r].size = https::unknownLength; items[r].memory = &s.index[r];
		}
		net::many::Options o{};
		o.connections = 2;
		o.label = T(u"indeks pakietów Alpine", u"Alpine's package index");
		const net::many::Outcome r = net::many::Download(items, 2, o);
		if (r.failed) { net::many::Explain(*r.firstFailure); NoteLine("FAILED: ", r.firstFailure->url, CLen(r.firstFailure->url)); ok = false; }
		for (int k = 0; ok && k < 2; ++k) { S().bytes += s.index[k].size; ok = VerifyIndex(k); }
	}
	ok = ok && Resolve(reinterpret_cast<const char*>(world), worldSize);
	Release(world);
	if (ok) {
		static char16_t text[200];
		Set(text, 200, T(u"Pakietów do pobrania: ", u"Packages to download: "));
		CatN(text, 200, s.chosenCount);
		Cat(text, 200, u".");
		Say(text);
	}
	ok = ok && FetchPackages();
	if (!ok) { FreeAll(); return false; }
	s.fetched = true;
	Say(T(u"Charon Hypervisor pobrany i sprawdzony.", u"Charon Hypervisor downloaded and checked."));
	return true;
}

// Bytes the step downloads (for the plan's everything-first list): the ISO, two indexes and the packages, about 560 MB.
inline uint64_t PlanBytes() { return uint64_t(560) << 20; }

inline void ProgressSink(void (*progress)(void*, uint64_t), void* context, uint64_t done) { if (progress) progress(context, done); }
// Up-front download into Charon's own memory pages. False: said why.
inline bool FetchUpfront(void (*progress)(void* context, uint64_t done), void* context)
{
	const bool ok = FetchAll();
	ProgressSink(progress, context, ok ? S().bytes : 0);
	return ok;
}

// ---- writing P1 --------------------------------------------------------------------------------------------------------
inline bool Write(efi_handle volume, const char16_t* path, const uint8_t* data, size_t size)
{
	static FileRef ref;
	ref.volume = volume;
	Set(ref.path, pathUnits, path);
	static char16_t folder[pathUnits];
	ParentOf(path, folder, pathUnits);
	if (folder[1]) MakeFolders(volume, folder);
	const efi_status st = WriteWhole(ref, data, size);
	if (st != EFI_SUCCESS) {
		static char16_t text[700];
		Set(text, 700, T(u"Nie udało się zapisać na partycji Hypervisor: ", u"Could not write on the Hypervisor partition: "));
		Cat(text, 700, path);
		Fail(text, st);
		return false;
	}
	return true;
}
// A file kept in 64 MB pieces: written piece after piece into one file (deleted first, as WriteWhole does).
inline bool WritePieces(efi_handle volume, const char16_t* path, const Pieces& b)
{
	static FileRef ref;
	ref.volume = volume;
	Set(ref.path, pathUnits, path);
	static char16_t folder[pathUnits];
	ParentOf(path, folder, pathUnits);
	if (folder[1]) MakeFolders(volume, folder);
	efi_status st = EFI_SUCCESS;
	efi_file_protocol* file = nullptr;
	if (OpenFile(ref, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0, &file) == EFI_SUCCESS && file->Delete(file) != EFI_SUCCESS) st = EFI_WRITE_PROTECTED;
	if (st == EFI_SUCCESS) st = OpenFile(ref, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0, &file);
	if (st == EFI_SUCCESS) {
		uint64_t left = b.size;
		for (size_t k = 0; k < b.count && st == EFI_SUCCESS && left; ++k) {
			const size_t inPiece = size_t(left < kPiece ? left : kPiece);
			for (size_t done = 0; done < inPiece && st == EFI_SUCCESS;) {
				size_t part = inPiece - done < 65536 ? inPiece - done : 65536;
				const size_t asked = part;
				st = file->Write(file, &part, b.part[k].data + done);
				if (st == EFI_SUCCESS && part != asked) st = EFI_VOLUME_FULL;
				done += part;
				Pump();
			}
			left -= inPiece;
		}
		if (st == EFI_SUCCESS && file->Flush) st = file->Flush(file);
		file->Close(file);
	}
	if (st != EFI_SUCCESS) {
		static char16_t text[700];
		Set(text, 700, T(u"Nie udało się zapisać na partycji Hypervisor: ", u"Could not write on the Hypervisor partition: "));
		Cat(text, 700, path);
		Fail(text, st);
		return false;
	}
	return true;
}
inline void GuidText(const uint8_t* g, char* out)
{
	static const char h[] = "0123456789abcdef";
	const int order[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
	size_t o = 0;
	for (int i = 0; i < 16; ++i) {
		if (i == 4 || i == 6 || i == 8 || i == 10) out[o++] = '-';
		out[o++] = h[g[order[i]] >> 4];
		out[o++] = h[g[order[i]] & 15];
	}
	out[o] = 0;
}

// The step. False: said why; the plan's failure question applies.
inline bool RunPlanStep(const disks::Disk& disk, const uint8_t bootUnique[16], const uint8_t dataUnique[16], efi_handle bootVolume)
{
	(void)disk;
	(void)bootUnique;
	if (!bootVolume || VolumeReadOnly(bootVolume)) {
		Fail(T(u"Partycja Hypervisor nie jest dostępna do zapisu.", u"The Hypervisor partition cannot be written."));
		return false;
	}
	if (!FetchAll()) return false;
	auto& s = S();
	Say(T(u"Zapisuję Charon Hypervisor na partycji Hypervisor.", u"Writing Charon Hypervisor on the Hypervisor partition."));
	static char16_t path[pathUnits];
	uint64_t total = 0, done = 0;
	for (size_t i = 0; i < s.chosenCount; ++i) total += s.chosen[i].apk.size;
	for (int i = 0; i < kBootFiles; ++i) {
		if (!s.boot[i].size || s.boot[i].have != s.boot[i].size) { FailWith(u"W obrazie Alpine brak pliku:", u"The Alpine image lacks the file:", BootName(i), CLen(BootName(i))); FreeAll(); return false; }
		total += s.boot[i].size;
	}
	for (int i = 0; i < kBootFiles; ++i) {
		Set(path, pathUnits, u"\\boot\\");
		CatAscii(path, pathUnits, BootName(i), CLen(BootName(i)));
		if (!WritePieces(bootVolume, path, s.boot[i])) { FreeAll(); return false; }
		done += s.boot[i].size;
		Progress(T(u"Zapisywanie Charon Hypervisor", u"Writing Charon Hypervisor"), done, total);
	}
	for (int r = 0; r < 2; ++r) {
		Set(path, pathUnits, u"\\apks\\");
		CatAscii(path, pathUnits, RepoName(r), CLen(RepoName(r)));
		Cat(path, pathUnits, u"\\.boot_repository");
		if (!Write(bootVolume, path, reinterpret_cast<const uint8_t*>(""), 0)) { FreeAll(); return false; }
		Set(path, pathUnits, u"\\apks\\");
		CatAscii(path, pathUnits, RepoName(r), CLen(RepoName(r)));
		Cat(path, pathUnits, u"\\x86_64\\APKINDEX.tar.gz");
		if (!Write(bootVolume, path, s.index[r].data, s.index[r].size)) { FreeAll(); return false; }
	}
	static char16_t name[200];
	for (size_t i = 0; i < s.chosenCount; ++i) {
		const Entry& e = Entries()[s.chosen[i].entry];
		ApkName(e, name, 200);
		Set(path, pathUnits, u"\\apks\\");
		CatAscii(path, pathUnits, RepoName(e.repo), CLen(RepoName(e.repo)));
		Cat(path, pathUnits, u"\\x86_64\\");
		Cat(path, pathUnits, name);
		if (!Write(bootVolume, path, s.chosen[i].apk.data, s.chosen[i].apk.size)) { FreeAll(); return false; }
		done += s.chosen[i].apk.size;
		Progress(T(u"Zapisywanie Charon Hypervisor", u"Writing Charon Hypervisor"), done, total);
		char line[160];
		size_t ln = 0;
		for (; name[ln] && ln + 1 < sizeof(line); ++ln) line[ln] = char(name[ln]);
		Note("apk "); Note(RepoName(e.repo)); Note("/"); NoteLine(line, nullptr, 0);
		(void)ln;
	}
	// Charon's own overlay and the test marker (test discs only)
	uint8_t* ovl = nullptr;
	size_t ovlSize = 0;
	if (!ReadMedium(u"\\charonhv.apkovl.tar.gz", &ovl, &ovlSize, 16u << 20)) {
		FailWith(u"Na nośniku Charona brak nakładki Charon Hypervisor.", u"Charon's medium lacks the Charon Hypervisor overlay."); FreeAll(); return false;
	}
	const bool ovlOk = Write(bootVolume, u"\\charonhv.apkovl.tar.gz", ovl, ovlSize);
	Release(ovl);
	if (!ovlOk) { FreeAll(); return false; }
	uint8_t* marker = nullptr;
	size_t markerSize = 0;
	if (ReadMedium(u"\\test-serial", &marker, &markerSize, 4096)) {
		Write(bootVolume, u"\\charonhv\\test-serial", marker, markerSize);
		Release(marker);
	}
	static char cfg[256];
	char guid[40];
	GuidText(dataUnique, guid);
	size_t c = 0;
	auto put = [&](const char* t) { while (*t && c + 1 < sizeof(cfg)) cfg[c++] = *t++; cfg[c] = 0; };
	put("# Charon Hypervisor 1\nversion=r410\nalpine=");
	put(s.version);
	put("\ndata=");
	put(guid);
	put(U().host->pl ? "\nlanguage=pl\n" : "\nlanguage=en\n");
	// 6m: in a plan the hypervisor's computer name is the plan's shared name (AskSystem stores it as "hostname");
	// alone it stays charon-hypervisor. Only a-z 0-9 and '-' are written (an Alpine hostname), at most 63.
	{
		static char host[64];
		const char* h = ask::Get("hostname");
		size_t hn = 0;
		for (size_t i = 0; h && h[i] && hn < 63; ++i) {
			char ch = h[i];
			if (ch >= 'A' && ch <= 'Z') ch = char(ch + 32);
			if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || (ch == '-' && hn)) host[hn++] = ch;
		}
		while (hn && host[hn - 1] == '-') --hn;
		host[hn] = 0;
		put("hostname=");
		put(hn ? host : "charon-hypervisor");
		put("\n");
	}
	if (!Write(bootVolume, u"\\charonhv\\charonhv.cfg", reinterpret_cast<const uint8_t*>(cfg), c)) { FreeAll(); return false; }
	static const char machines[] = "# Charon Hypervisor machines 1\n";
	if (!Write(bootVolume, u"\\charonhv\\machines.txt", reinterpret_cast<const uint8_t*>(machines), sizeof(machines) - 1)) { FreeAll(); return false; }
	Note("data partition "); NoteLine(guid, nullptr, 0);
	Write(bootVolume, u"\\charonhv\\install-report.txt", reinterpret_cast<const uint8_t*>(s.report), s.reportSize);
	FreeAll();
	Say(T(u"Charon Hypervisor zapisany. Przy pierwszym starcie przygotuje partycję danych.",
	      u"Charon Hypervisor written. At its first start it prepares the data partition."));
	return true;
}

} // namespace hv
} // namespace ws
} // namespace hakefi
