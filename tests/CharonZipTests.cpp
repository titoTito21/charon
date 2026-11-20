// SPDX-License-Identifier: GPL-2.0-or-later
// Host test for src/efi/CharonZip.h (and src/efi/CharonInflate.h through it).
// Usage: CharonZipTests <data directory>
// The directory comes from tests/make-zip-testdata.py: manifest.txt describes
// every archive and entry as Python's zipfile sees them, <archive>.expect
// holds the contents of the entries that must extract.
#define _CRT_SECURE_NO_WARNINGS
#include "../src/efi/CharonZip.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace hakefi::zip;
namespace inflate = hakefi::inflate;

static int gFailures = 0;

static void Check(bool ok, const std::string& what)
{
	printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	if (!ok)
		gFailures++;
}

static bool ReadFile(const std::string& path, std::vector<uint8_t>& out)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (f == nullptr)
		return false;
	_fseeki64(f, 0, SEEK_END);
	const long long size = _ftelli64(f);
	_fseeki64(f, 0, SEEK_SET);
	out.resize(size_t(size));
	const bool ok = size == 0 || fread(out.data(), 1, out.size(), f) == out.size();
	fclose(f);
	return ok;
}

struct File {
	std::vector<uint8_t> bytes;
	uint64_t reads = 0;
	uint64_t failAfter = UINT64_MAX; // reads allowed before failing
};

static bool FileRead(void* context, uint64_t offset, uint8_t* out, size_t size)
{
	File& file = *static_cast<File*>(context);
	if (offset + size > file.bytes.size() || file.reads >= file.failAfter)
		return false;
	memcpy(out, file.bytes.data() + offset, size);
	file.reads++;
	return true;
}

struct Collect {
	std::vector<uint8_t> bytes;
	uint64_t refuseAfter = UINT64_MAX;
	static bool Write(void* context, const uint8_t* data, size_t size)
	{
		Collect& c = *static_cast<Collect*>(context);
		if (c.bytes.size() + size > c.refuseAfter)
			return false;
		c.bytes.insert(c.bytes.end(), data, data + size);
		return true;
	}
};

static const char* Name(Error e)
{
	switch (e) {
	case Error::None: return "ok";
	case Error::End: return "End";
	case Error::BadArgument: return "BadArgument";
	case Error::Read: return "Read";
	case Error::NotZip: return "NotZip";
	case Error::MultiDisk: return "MultiDisk";
	case Error::BadDirectory: return "BadDirectory";
	case Error::BufferTooSmall: return "BufferTooSmall";
	case Error::Encrypted: return "Encrypted";
	case Error::Method: return "Method";
	case Error::BadLocalHeader: return "BadLocalHeader";
	case Error::Corrupt: return "Corrupt";
	case Error::Size: return "Size";
	case Error::Crc: return "Crc";
	case Error::Sink: return "Sink";
	}
	return "?";
}

struct Expected {
	std::vector<uint8_t> name;
	int directory, method, utf8;
	unsigned long long compressed, size;
	unsigned crc;
	std::string result;
};

struct ExpectedArchive {
	std::string file, result;
	unsigned long long entries, base;
	int zip64;
	std::vector<Expected> list;
};

struct Buffers {
	std::vector<uint8_t> scratch, directory, input, window;
	inflate::Decoder decoder;
};

static Buffers gBuffers; // the decoder does not belong on the stack

static void SetBuffers(size_t scratch, size_t directory, size_t input,
	size_t window)
{
	gBuffers.scratch.assign(scratch, 0);
	gBuffers.directory.assign(directory, 0);
	gBuffers.input.assign(input, 0);
	gBuffers.window.assign(window, 0);
}

static ExtractWork Work()
{
	return ExtractWork{gBuffers.input.data(), gBuffers.input.size(),
		&gBuffers.decoder, gBuffers.window.data(), gBuffers.window.size()};
}

static void TestArchive(const std::string& directory,
	const ExpectedArchive& expected, const char* label)
{
	const std::string id = expected.file + " [" + label + "]";
	File file;
	if (!ReadFile(directory + "/" + expected.file, file.bytes)) {
		Check(false, id + ": cannot read");
		return;
	}
	const Source source{&file, FileRead};
	Archive archive;
	const Error opened = Open(source, file.bytes.size(),
		gBuffers.scratch.data(), gBuffers.scratch.size(), archive);
	const uint64_t openReads = file.reads;
	if (expected.result != Name(opened) || opened != Error::None) {
		Check(expected.result == Name(opened), id + ": Open -> " + Name(opened)
			+ ", expected " + expected.result);
		return;
	}
	Check(archive.entryCount == expected.entries
		&& archive.zip64 == (expected.zip64 != 0)
		&& archive.base == expected.base, id + ": Open, "
		+ std::to_string(archive.entryCount) + " entries, zip64 "
		+ std::to_string(archive.zip64) + ", base "
		+ std::to_string(archive.base) + ", " + std::to_string(openReads)
		+ " reads");
	std::vector<uint8_t> content;
	if (!expected.list.empty()
		&& !ReadFile(directory + "/" + expected.file + ".expect", content)) {
		Check(false, id + ": no .expect file");
		return;
	}

	// Pass 1: list everything, keeping the entries (names copied out).
	Cursor cursor;
	Begin(cursor, archive, gBuffers.directory.data(), gBuffers.directory.size());
	std::vector<Entry> entries;
	size_t listingMismatches = 0;
	file.reads = 0;
	Error e;
	Entry entry;
	while ((e = Next(cursor, entry)) == Error::None) {
		const size_t i = entries.size();
		if (i < expected.list.size()) {
			const Expected& x = expected.list[i];
			const bool same = entry.nameLength == x.name.size()
				&& std::vector<uint8_t>(entry.name, entry.name
					+ entry.nameLength) == x.name
				&& entry.isDirectory == (x.directory != 0)
				&& entry.method == x.method
				&& entry.compressedSize == x.compressed
				&& entry.uncompressedSize == x.size && entry.crc32 == x.crc
				&& entry.nameIsUtf8 == (x.utf8 != 0);
			if (!same) {
				if (listingMismatches++ < 5)
					printf("     mismatch at entry %zu (%.*s)\n", i,
						int(entry.nameLength), (const char*)entry.name);
			}
		}
		entries.push_back(entry);
	}
	Check(e == Error::End && entries.size() == expected.list.size()
		&& listingMismatches == 0, id + ": listing matches zipfile ("
		+ std::to_string(entries.size()) + " entries, "
		+ std::to_string(file.reads) + " reads, ends with " + Name(e) + ")");
	Check(Next(cursor, entry) == Error::End, id + ": Next after the end -> End");

	// Pass 2: extract in directory order and compare with the expected bytes.
	size_t at = 0, bad = 0, extracted = 0, refused = 0;
	file.reads = 0;
	for (size_t i = 0; i < entries.size() && i < expected.list.size(); i++) {
		const Expected& x = expected.list[i];
		Collect out;
		const Error result = Extract(archive, entries[i], Work(),
			Sink{&out, Collect::Write});
		if (x.result != Name(result)) {
			if (bad++ < 5)
				printf("     entry %zu: Extract -> %s, expected %s\n", i,
					Name(result), x.result.c_str());
			continue;
		}
		if (result != Error::None) {
			refused++;
			continue;
		}
		if (x.directory)
			continue;
		if (at + out.bytes.size() > content.size() || out.bytes.size() != x.size
			|| memcmp(out.bytes.data(), content.data() + at, out.bytes.size()) != 0) {
			if (bad++ < 5)
				printf("     entry %zu: content differs\n", i);
		}
		at += size_t(x.size);
		extracted++;
	}
	Check(bad == 0 && at == content.size(), id + ": " + std::to_string(extracted)
		+ " entries extracted byte for byte, " + std::to_string(refused)
		+ " refused as expected, " + std::to_string(file.reads) + " reads");
}

// ---- damage on basic.zip --------------------------------------------------

static bool Find(const Archive& archive, const char* name, Entry& entry)
{
	Cursor cursor;
	Begin(cursor, archive, gBuffers.directory.data(), gBuffers.directory.size());
	while (Next(cursor, entry) == Error::None) {
		if (NameEquals(entry, name))
			return true;
	}
	return false;
}

static void TestDamage(const std::string& directory)
{
	SetBuffers(70000, 4096, 4096, 32768);
	File original;
	if (!ReadFile(directory + "/basic.zip", original.bytes)) {
		Check(false, "basic.zip missing");
		return;
	}
	auto extract = [&](const char* name, void (*damage)(File&, const Entry&),
		Error expect, const char* what, uint64_t refuseAfter = UINT64_MAX,
		uint64_t failAfter = UINT64_MAX, bool anyDataError = false) {
		File file = original;
		Archive archive;
		Entry entry;
		const Source source{&file, FileRead};
		if (Open(source, file.bytes.size(), gBuffers.scratch.data(),
				gBuffers.scratch.size(), archive) != Error::None
			|| !Find(archive, name, entry)) {
			Check(false, std::string("damage: cannot find ") + name);
			return;
		}
		damage(file, entry);
		if (failAfter != UINT64_MAX)
			file.failAfter = file.reads + failAfter;
		Collect out;
		out.refuseAfter = refuseAfter;
		const Error e = Extract(archive, entry, Work(), Sink{&out,
			Collect::Write});
		Check(e == expect || (anyDataError && (e == Error::Corrupt
			|| e == Error::Size)), std::string("damage: ") + what + " -> " + Name(e));
	};
	auto dataOffset = [](const File& file, const Entry& entry) {
		const uint8_t* p = file.bytes.data() + entry.localHeaderOffset;
		return size_t(entry.localHeaderOffset) + 30 + (p[26] | (p[27] << 8))
			+ (p[28] | (p[29] << 8));
	};
	static size_t (*offsetOf)(const File&, const Entry&) = dataOffset;
	extract("big/text.txt", [](File&, const Entry&) {}, Error::None,
		"untouched entry");
	extract("big/noise-stored.bin", [](File& f, const Entry& e) {
		f.bytes[offsetOf(f, e) + 150000] ^= 1; }, Error::Crc,
		"bit flip in stored data");
	extract("big/text.txt", [](File& f, const Entry& e) {
		f.bytes[offsetOf(f, e) + size_t(e.compressedSize) / 2] ^= 0x10; },
		Error::Crc, "bit flip in deflated data", UINT64_MAX, UINT64_MAX, true);
	extract("big/text.txt", [](File& f, const Entry& e) {
		memset(f.bytes.data() + offsetOf(f, e), 0xff, 64); }, Error::Corrupt,
		"deflate stream overwritten");
	extract("big/text.txt", [](File& f, const Entry& e) {
		f.bytes[size_t(e.localHeaderOffset)] = 'Q'; }, Error::BadLocalHeader,
		"local header signature");
	extract("big/text.txt", [](File& f, const Entry& e) {
		f.bytes[size_t(e.localHeaderOffset) + 8] = 0; }, Error::BadLocalHeader,
		"local header names another method");
	extract("big/text.txt", [](File&, const Entry& e) {
		const_cast<Entry&>(e).uncompressedSize--; }, Error::Size,
		"directory size one byte short");
	extract("big/text.txt", [](File&, const Entry& e) {
		const_cast<Entry&>(e).uncompressedSize++; }, Error::Size,
		"directory size one byte long");
	extract("big/text.txt", [](File&, const Entry& e) {
		const_cast<Entry&>(e).compressedSize -= 100; }, Error::Corrupt,
		"compressed size too small");
	extract("big/text.txt", [](File&, const Entry& e) {
		const_cast<Entry&>(e).localHeaderOffset = 1ull << 40; },
		Error::BadLocalHeader, "local header offset past the end");
	extract("stored.txt", [](File&, const Entry& e) {
		const_cast<Entry&>(e).compressedSize++; }, Error::BadDirectory,
		"stored entry with two sizes");
	extract("big/text.txt", [](File&, const Entry&) {}, Error::Sink,
		"sink refuses after 100000 bytes", 100000);
	extract("big/noise-stored.bin", [](File&, const Entry&) {}, Error::Sink,
		"sink refuses a stored entry", 10);
	extract("big/noise.bin", [](File&, const Entry&) {}, Error::Read,
		"source fails on the third read", UINT64_MAX, 2);
	extract("big/noise-stored.bin", [](File&, const Entry&) {}, Error::Read,
		"source fails on the first read", UINT64_MAX, 0);

	// Arguments.
	{
		File file = original;
		const Source source{&file, FileRead};
		Archive archive;
		Check(Open(source, file.bytes.size(), gBuffers.scratch.data(), 100,
				archive) == Error::BadArgument && archive.error
			== Error::BadArgument, "arguments: scratch below kMinScratch");
		Check(Open(Source{nullptr, nullptr}, file.bytes.size(),
				gBuffers.scratch.data(), gBuffers.scratch.size(), archive)
			== Error::BadArgument, "arguments: null source");
		Cursor cursor;
		Entry entry;
		Begin(cursor, archive, gBuffers.directory.data(), 4096);
		Check(Next(cursor, entry) == Error::BadArgument,
			"arguments: listing a failed archive");
		Open(source, file.bytes.size(), gBuffers.scratch.data(),
			gBuffers.scratch.size(), archive);
		Begin(cursor, archive, gBuffers.directory.data(), 100);
		Check(Next(cursor, entry) == Error::BadArgument,
			"arguments: directory buffer below the minimum");
		Check(Find(archive, "big/text.txt", entry), "NameEquals finds big/text.txt");
		Check(!NameEquals(entry, "big/text.tx") && !NameEquals(entry,
				"big/text.txt2") && !NameEquals(entry, ""),
			"NameEquals rejects prefixes and longer names");
		ExtractWork work = Work();
		work.windowBytes = 32767;
		Collect out;
		Check(Extract(archive, entry, work, Sink{&out, Collect::Write})
			== Error::BadArgument, "arguments: window below 32 KiB");
		work = Work();
		work.decoder = nullptr;
		Check(Extract(archive, entry, work, Sink{&out, Collect::Write})
			== Error::BadArgument, "arguments: deflate without a decoder");
		Check(Find(archive, "stored.txt", entry)
			&& Extract(archive, entry, work, Sink{&out, Collect::Write})
			== Error::None, "stored entry needs neither decoder nor window");
		// Directory damage: a truncated central directory.
		File cut = original;
		const Source cutSource{&cut, FileRead};
		cut.bytes[size_t(archive.directoryOffset) + 100] ^= 0xff;
		cut.bytes[size_t(archive.directoryOffset)] = 'X';
		Archive damaged;
		Check(Open(cutSource, cut.bytes.size(), gBuffers.scratch.data(),
				gBuffers.scratch.size(), damaged) == Error::None,
			"directory damage: Open still succeeds");
		Begin(cursor, damaged, gBuffers.directory.data(), 4096);
		Check(Next(cursor, entry) == Error::BadDirectory
			&& Next(cursor, entry) == Error::BadDirectory,
			"directory damage: Next -> BadDirectory, and stays there");
		// Trailing junk after the archive: still found, offsets unchanged.
		File junk = original;
		junk.bytes.insert(junk.bytes.end(), 3000, 0x5a);
		const Source junkSource{&junk, FileRead};
		Archive withJunk;
		Check(Open(junkSource, junk.bytes.size(), gBuffers.scratch.data(),
				gBuffers.scratch.size(), withJunk) == Error::None
			&& withJunk.entryCount == archive.entryCount && withJunk.base == 0,
			"3000 bytes of junk after the archive: Open succeeds");
	}
	// Name longer than the directory buffer.
	{
		File file;
		if (ReadFile(directory + "/longname.zip", file.bytes)) {
			const Source source{&file, FileRead};
			Archive archive;
			Open(source, file.bytes.size(), gBuffers.scratch.data(),
				gBuffers.scratch.size(), archive);
			Cursor cursor;
			Entry entry;
			Begin(cursor, archive, gBuffers.directory.data(), 512);
			const Error first = Next(cursor, entry);
			const Error second = Next(cursor, entry);
			Check(first == Error::None && second == Error::BufferTooSmall,
				std::string("600-byte name in a 512-byte buffer -> ")
				+ Name(second));
		} else {
			Check(false, "longname.zip missing");
		}
	}
	Check(Crc32(0, reinterpret_cast<const uint8_t*>("123456789"), 9)
		== 0xcbf43926u, "Crc32 check value");
	Check(Crc32(Crc32(0, reinterpret_cast<const uint8_t*>("1234"), 4),
			reinterpret_cast<const uint8_t*>("56789"), 5) == 0xcbf43926u,
		"Crc32 chained");
}

static std::vector<uint8_t> Unhex(const char* text)
{
	std::vector<uint8_t> out;
	if (text[0] == '-')
		return out;
	for (size_t i = 0; text[i] != 0 && text[i + 1] != 0; i += 2) {
		const char pair[3] = {text[i], text[i + 1], 0};
		out.push_back(uint8_t(strtoul(pair, nullptr, 16)));
	}
	return out;
}

int main(int argc, char** argv)
{
	if (argc < 2) {
		printf("usage: CharonZipTests <data directory>\n");
		return 2;
	}
	const std::string directory = argv[1];
	FILE* manifest = fopen((directory + "/manifest.txt").c_str(), "r");
	if (manifest == nullptr) {
		printf("FAIL no manifest.txt in %s\n", directory.c_str());
		return 1;
	}
	std::vector<ExpectedArchive> archives;
	static char line[8192], name[4096], result[64];
	while (fgets(line, sizeof(line), manifest) != nullptr) {
		if (line[0] == 'A') {
			ExpectedArchive a;
			if (sscanf(line, "A %4095s %llu %d %llu %63s", name, &a.entries,
					&a.zip64, &a.base, result) != 5)
				continue;
			a.file = name;
			a.result = result;
			archives.push_back(a);
		} else if (line[0] == 'E' && !archives.empty()) {
			Expected x;
			if (sscanf(line, "E %4095s %d %d %llu %llu %x %d %63s", name,
					&x.directory, &x.method, &x.compressed, &x.size, &x.crc,
					&x.utf8, result) != 8)
				continue;
			x.name = Unhex(name);
			x.result = result;
			archives.back().list.push_back(x);
		}
	}
	fclose(manifest);
	Check(archives.size() >= 12, "manifest lists " + std::to_string(
		archives.size()) + " archives");

	// Smallest buffers the header allows (the directory buffer must still
	// hold the 600-byte name of longname.zip).
	SetBuffers(kMinScratch, 1024, kMinInput, inflate::kWindowBytes);
	for (const ExpectedArchive& a : archives)
		TestArchive(directory, a, "minimal buffers");
	// Comfortable buffers: fewer reads.
	SetBuffers(65536 + 22, 65536, 65536, 65536);
	for (const ExpectedArchive& a : archives)
		TestArchive(directory, a, "64 KiB buffers");
	TestDamage(directory);

	if (gFailures != 0) {
		printf("FAILED: %d\n", gFailures);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
