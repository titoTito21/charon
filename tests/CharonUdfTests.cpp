// SPDX-License-Identifier: GPL-2.0-or-later
// Host test for src/efi/CharonUdf.h.
// Usage: CharonUdfTests [<image.iso> <tree directory> [chunk bytes]]
// Without arguments only the synthetic image tests run. With an image (made by
// IMAPI2 from the tree) every path, size and byte is compared with the tree.
#define _CRT_SECURE_NO_WARNINGS
#include "../src/efi/CharonUdf.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace hakefi::udf;

static int gFailures = 0;

static void Check(bool ok, const char* what)
{
	printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		gFailures++;
}

// ---- synthetic image -----------------------------------------------------

struct Image {
	std::vector<uint8_t> bytes;
	uint64_t reads = 0;
	uint8_t* Block(uint32_t sector) { return bytes.data() + size_t(sector) * kBlock; }
};

static bool MemoryRead(void* context, uint64_t offset, uint8_t* out, size_t size)
{
	Image& image = *static_cast<Image*>(context);
	if (offset + size > image.bytes.size())
		return false;
	memcpy(out, image.bytes.data() + offset, size);
	image.reads++;
	return true;
}

static void P16(uint8_t* p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
static void P32(uint8_t* p, uint32_t v) { P16(p, v); P16(p + 2, v >> 16); }
static void P64(uint8_t* p, uint64_t v) { P32(p, uint32_t(v)); P32(p + 4, uint32_t(v >> 32)); }

static void PutTag(uint8_t* p, uint16_t id)
{
	P16(p, id);
	P16(p + 2, 2);
	uint8_t sum = 0;
	for (int i = 0; i < 16; i++)
		sum = uint8_t(sum + (i == 4 ? 0 : p[i]));
	p[4] = sum;
}

static void LongAd(uint8_t* p, uint32_t length, uint32_t block)
{
	P32(p, length);
	P32(p + 4, block);
	P16(p + 8, 0);
}

// File identifier descriptor appended to directory data.
static void AddFid(std::vector<uint8_t>& dir, const std::u16string& name, uint32_t icb,
	uint8_t characteristics, bool wide)
{
	std::vector<uint8_t> id;
	if (!name.empty()) {
		id.push_back(wide ? 16 : 8);
		for (char16_t c : name) {
			if (wide)
				id.push_back(uint8_t(c >> 8));
			id.push_back(uint8_t(c));
		}
	}
	std::vector<uint8_t> fid((38 + id.size() + 3) & ~size_t(3));
	P16(fid.data() + 16, 1);
	fid[18] = characteristics;
	fid[19] = uint8_t(id.size());
	LongAd(fid.data() + 20, kBlock, icb);
	std::copy(id.begin(), id.end(), fid.begin() + 38);
	PutTag(fid.data(), 257);
	dir.insert(dir.end(), fid.begin(), fid.end());
}

// (Extended) file entry header; returns the offset of the allocation area.
static size_t FileEntry(uint8_t* b, bool extended, uint8_t type, uint16_t adKind,
	uint64_t size, uint32_t adBytes)
{
	const size_t base = extended ? 216 : 176;
	P16(b + 20, 4);
	b[27] = type;
	P16(b + 34, adKind);
	P64(b + 56, size);
	P32(b + base - 4, adBytes);
	PutTag(b, extended ? 266 : 261);
	return base;
}

struct Collected {
	std::map<uint32_t, std::vector<uint8_t>> data;
	std::map<uint32_t, uint64_t> bytes;
	bool inOrderOnly = true;
};

static bool CollectSink(void* context, uint32_t file, uint64_t fileOffset,
	const uint8_t* data, size_t size)
{
	Collected& c = *static_cast<Collected*>(context);
	std::vector<uint8_t>& out = c.data[file];
	if (out.size() < fileOffset + size)
		out.resize(fileOffset + size);
	memcpy(out.data() + fileOffset, data, size);
	c.bytes[file] += size;
	return true;
}

static std::string Narrow(const char16_t* path)
{
	std::string s;
	for (; *path != 0; path++)
		s += *path < 128 ? char(*path) : '?';
	return s;
}

static void SyntheticTests()
{
	const uint32_t kPart = 300; // partition start sector
	Image image;
	image.bytes.resize(size_t(340) * kBlock);
	for (size_t i = 0; i < image.bytes.size(); i++)
		image.bytes[i] = uint8_t(i * 7 + (i >> 11));
	auto clear = [&](uint32_t sector) { memset(image.Block(sector), 0, kBlock); return image.Block(sector); };

	uint8_t* b = clear(256); // anchor: main sequence at 257, reserve at 270
	P32(b + 16, 3 * kBlock); P32(b + 20, 257);
	P32(b + 24, 3 * kBlock); P32(b + 28, 270);
	PutTag(b, 2);
	for (uint32_t s : {257u, 270u}) {
		b = clear(s); // partition descriptor
		P16(b + 22, 7); P32(b + 188, kPart); P32(b + 192, 40);
		PutTag(b, 5);
		b = clear(s + 1); // logical volume descriptor, one type 1 map
		P32(b + 212, kBlock);
		LongAd(b + 248, kBlock, 0);
		P32(b + 264, 6); P32(b + 268, 1);
		b[440] = 1; b[441] = 6; P16(b + 442, 1); P16(b + 444, 7);
		PutTag(b, 6);
		b = clear(s + 2);
		PutTag(b, 8);
	}
	memset(image.Block(257), 0xee, kBlock); // main sequence damaged: reserve is used

	b = clear(kPart + 0); // file set descriptor, root ICB at block 1
	LongAd(b + 400, kBlock, 1);
	PutTag(b, 256);

	// Root directory, embedded in its ICB.
	std::vector<uint8_t> root;
	AddFid(root, u"", 1, 0x0a, false);                 // parent
	AddFid(root, u"emb.txt", 2, 0, false);             // embedded data
	AddFid(root, u"gone.txt", 2, 0x04, false);         // deleted: ignored
	AddFid(root, u"zażółć.bin", 3, 0, true); // 16-bit name
	AddFid(root, u"sub", 5, 0x02, false);
	b = clear(kPart + 1);
	size_t base = FileEntry(b, false, 4, 3, root.size(), uint32_t(root.size()));
	memcpy(b + base, root.data(), root.size());

	b = clear(kPart + 2); // emb.txt: 11 bytes inside the ICB, after 8 bytes of EAs
	P32(b + 168, 8);
	base = FileEntry(b, false, 5, 3, 11, 11);
	memcpy(b + base + 8, "hello world", 11);

	// Extended file entry with short_ad: 2 blocks @20, then an allocation extent
	// (block 4) with a sparse block, 2148 bytes @10 and an unrecorded tail.
	const uint64_t bigSize = 2 * kBlock + kBlock + 2148 + 500;
	b = clear(kPart + 3);
	base = FileEntry(b, true, 5, 0, bigSize, 16);
	P32(b + base, 2 * kBlock); P32(b + base + 4, 20);
	P32(b + base + 8, (3u << 30) | kBlock); P32(b + base + 12, 4);
	b = clear(kPart + 4);
	P32(b + 20, 24);
	P32(b + 24, (2u << 30) | kBlock); P32(b + 28, 0);
	P32(b + 32, 2148); P32(b + 36, 10);
	P32(b + 40, (1u << 30) | 500); P32(b + 44, 12);
	PutTag(b, 258);

	// sub: directory data in two separate blocks (6 and 15), long_ad.
	std::vector<uint8_t> sub;
	AddFid(sub, u"", 1, 0x0a, false);
	for (int i = 0; i < 60; i++) {
		char16_t name[4] = {u'n', char16_t(u'0' + i / 10), char16_t(u'0' + i % 10), 0};
		AddFid(sub, name, 7, 0, false); // hard links to one empty file
	}
	AddFid(sub, u"deep.dat", 8, 0, false);
	AddFid(sub, u"inner", 9, 0x02, false);
	b = clear(kPart + 5);
	base = FileEntry(b, false, 4, 1, sub.size(), 32);
	LongAd(b + base, kBlock, 6);
	LongAd(b + base + 16, uint32_t(sub.size() - kBlock), 15);
	clear(kPart + 6); clear(kPart + 15);
	memcpy(image.Block(kPart + 6), sub.data(), kBlock);
	memcpy(image.Block(kPart + 15), sub.data() + kBlock, sub.size() - kBlock);

	b = clear(kPart + 7); // empty file
	FileEntry(b, false, 5, 0, 0, 0);
	b = clear(kPart + 8); // deep.dat: long_ad, 3 blocks @30 + 5 bytes @25 (earlier in the image)
	base = FileEntry(b, false, 5, 1, 3 * kBlock + 5, 32);
	LongAd(b + base, 3 * kBlock, 30);
	LongAd(b + base + 16, 5, 25);
	std::vector<uint8_t> inner;
	AddFid(inner, u"", 5, 0x0a, false);
	b = clear(kPart + 9); // empty directory
	base = FileEntry(b, false, 4, 3, inner.size(), uint32_t(inner.size()));
	memcpy(b + base, inner.data(), inner.size());

	std::vector<File> files(80);
	std::vector<Extent> extents(16);
	Map map = {files.data(), 0, files.size(), extents.data(), 0, extents.size(), false,
		Error::None};
	Source source = {&image, MemoryRead};
	bool ok = Scan(source, map);
	Check(ok, "synthetic: Scan (reserve sequence, embedded root, EFE, allocation extent)");
	if (!ok) {
		printf("INFO error %u\n", unsigned(map.error));
		return;
	}
	std::map<std::string, const File*> byPath;
	for (size_t i = 0; i < map.fileCount; i++)
		byPath[Narrow(files[i].path)] = &files[i];
	Check(map.fileCount == 65, "synthetic: 65 entries (deleted entry and parents ignored)");
	Check(byPath.count("emb.txt") && byPath["emb.txt"]->size == 11
		&& byPath.count("sub") && byPath["sub"]->directory
		&& byPath.count("sub\\inner") && byPath["sub\\inner"]->directory
		&& byPath.count("sub\\n59") && byPath["sub\\n59"]->size == 0
		&& byPath.count("sub\\deep.dat") && byPath["sub\\deep.dat"]->size == 3 * kBlock + 5,
		"synthetic: paths, sizes, directory split over two extents");
	const File* big = nullptr;
	for (size_t i = 0; i < map.fileCount; i++) {
		if (std::u16string(files[i].path) == u"zażółć.bin")
			big = &files[i];
	}
	Check(big != nullptr && big->size == bigSize, "synthetic: 16-bit OSTA name decoded");
	// Extents: emb(1) + big(recorded, zero, recorded, zero = 4) + deep(2).
	bool zeros = false, order = true;
	for (size_t i = 0; i < map.extentCount; i++) {
		zeros = zeros || extents[i].zero;
		if (i != 0 && extents[i].file == extents[i - 1].file)
			order = order && extents[i].fileOffset == extents[i - 1].fileOffset + extents[i - 1].length;
	}
	Check(map.extentCount == 7 && zeros && order,
		"synthetic: 7 extents in file order, sparse/unrecorded ones flagged zero");
	const uint64_t embOffset = uint64_t(kPart + 2) * kBlock + 176 + 8;
	bool embedded = false;
	for (size_t i = 0; i < map.extentCount; i++)
		embedded = embedded || (extents[i].offset == embOffset && extents[i].length == 11);
	Check(embedded, "synthetic: embedded file reported at its image offset inside the ICB");

	SortByOffset(map);
	for (size_t chunk : {size_t(1), size_t(7), size_t(1000), size_t(4099), image.bytes.size()}) {
		Router router;
		Begin(router, map);
		Collected got;
		ok = true;
		for (size_t at = 0; at < image.bytes.size() && ok; at += chunk)
			ok = Feed(router, at, image.bytes.data() + at,
				std::min(chunk, image.bytes.size() - at), CollectSink, &got);
		ok = ok && NextNeeded(router) == ~uint64_t(0);
		// expected contents
		for (size_t i = 0; i < map.fileCount && ok; i++) {
			const File& f = files[i];
			std::string name = Narrow(f.path);
			std::vector<uint8_t> want;
			auto blocks = [&](uint32_t block, size_t n) {
				want.insert(want.end(), image.Block(kPart + block), image.Block(kPart + block) + n);
			};
			if (name == "emb.txt")
				want.assign({'h','e','l','l','o',' ','w','o','r','l','d'});
			else if (&f == big) {
				blocks(20, 2 * kBlock);
				want.resize(want.size() + kBlock); // sparse: never delivered
				blocks(10, 2148);
			} else if (name == "sub\\deep.dat") {
				blocks(30, 3 * kBlock);
				blocks(25, 5);
			}
			std::vector<uint8_t>& have = got.data[uint32_t(i)];
			uint64_t expectedBytes = &f == big ? 2 * kBlock + 2148 : want.size();
			ok = have == want && got.bytes[uint32_t(i)] == expectedBytes;
		}
		char text[96];
		snprintf(text, sizeof(text), "synthetic: router delivers every file byte once (chunk %zu)", chunk);
		Check(ok, text);
	}
	{ // skipping the gaps with NextNeeded, one extent piece at a time
		Router router;
		Begin(router, map);
		Collected got;
		ok = true;
		uint64_t fed = 0;
		for (uint64_t at; ok && (at = NextNeeded(router)) != ~uint64_t(0);) {
			size_t n = std::min<size_t>(1500, image.bytes.size() - at);
			ok = Feed(router, at, image.bytes.data() + at, n, CollectSink, &got);
			fed += n;
		}
		Check(ok && fed < 12 * kBlock, "synthetic: NextNeeded lets the caller skip non-file ranges");
		Begin(router, map);
		Check(Feed(router, 0, image.bytes.data(), 100, CollectSink, &got)
			&& !Feed(router, 50, image.bytes.data(), 100, CollectSink, &got),
			"synthetic: Feed refuses going backwards");
		Begin(router, map);
		Check(!Feed(router, embOffset + 4, image.bytes.data(), 100, CollectSink, &got),
			"synthetic: Feed refuses skipping file bytes");
	}
	// Capacity and damage handling.
	Map small = {files.data(), 0, 10, extents.data(), 0, extents.size(), false, Error::None};
	Check(!Scan(source, small) && small.overflow && small.error == Error::Overflow,
		"synthetic: too few File slots -> overflow");
	small = {files.data(), 0, files.size(), extents.data(), 0, 3, false, Error::None};
	Check(!Scan(source, small) && small.overflow, "synthetic: too few Extent slots -> overflow");
	Image broken = image;
	P64(broken.Block(kPart + 8) + 56, 9 * kBlock); // size beyond the descriptors
	PutTag(broken.Block(kPart + 8), 261);
	Source brokenSource = {&broken, MemoryRead};
	Check(!Scan(brokenSource, map) && map.error == Error::Extents,
		"synthetic: file size beyond its allocation descriptors is an error");
	broken = image;
	P32(broken.Block(270 + 1) + 212, 512);
	PutTag(broken.Block(270 + 1), 6);
	Check(!Scan(brokenSource, map) && map.error == Error::BlockSize,
		"synthetic: logical block size other than 2048 is refused");
	broken = image;
	broken.Block(256)[0] = 9;
	Check(!Scan(brokenSource, map) && map.error == Error::NoAnchor,
		"synthetic: missing anchor is reported");
	broken = image; // directory loop: sub\inner points back at sub
	AddFid(inner, u"loop", 5, 0x02, false);
	base = FileEntry(broken.Block(kPart + 9), false, 4, 3, inner.size(), uint32_t(inner.size()));
	memcpy(broken.Block(kPart + 9) + base, inner.data(), inner.size());
	std::vector<File> many(4096);
	Map loop = {many.data(), 0, many.size(), extents.data(), 0, extents.size(), false, Error::None};
	Check(!Scan(brokenSource, loop) && (loop.error == Error::PathTooLong || loop.overflow),
		"synthetic: a directory loop terminates with an error");
}

// ---- real image against the source tree ------------------------------------

static bool FileRead(void* context, uint64_t offset, uint8_t* out, size_t size)
{
	FILE* f = static_cast<FILE*>(context);
	return _fseeki64(f, int64_t(offset), SEEK_SET) == 0 && fread(out, 1, size, f) == size;
}

struct Compare {
	std::filesystem::path tree;
	const Map* map = nullptr;
	FILE* open = nullptr;
	uint32_t openFile = ~0u;
	std::vector<std::vector<std::pair<uint64_t, uint64_t>>> ranges;
	std::vector<uint8_t> buffer;
	uint64_t mismatches = 0, bytes = 0, calls = 0;
};

static bool CompareSink(void* context, uint32_t file, uint64_t fileOffset,
	const uint8_t* data, size_t size)
{
	Compare& c = *static_cast<Compare*>(context);
	if (file >= c.map->fileCount)
		return false;
	if (c.openFile != file) {
		if (c.open != nullptr)
			fclose(c.open);
		std::filesystem::path p = c.tree / std::u16string(c.map->files[file].path);
		c.open = _wfopen(p.c_str(), L"rb");
		c.openFile = file;
		if (c.open == nullptr)
			return false;
	}
	c.buffer.resize(size);
	if (_fseeki64(c.open, int64_t(fileOffset), SEEK_SET) != 0
		|| fread(c.buffer.data(), 1, size, c.open) != size
		|| memcmp(c.buffer.data(), data, size) != 0)
		c.mismatches++;
	auto& r = c.ranges[file];
	if (!r.empty() && r.back().second == fileOffset)
		r.back().second += size;
	else
		r.push_back({fileOffset, fileOffset + size});
	c.bytes += size;
	c.calls++;
	return true;
}

static void RealImage(const char* imagePath, const char* treePath, size_t chunkBytes)
{
	namespace fs = std::filesystem;
	FILE* image = fopen(imagePath, "rb");
	if (image == nullptr) {
		Check(false, "open image");
		return;
	}
	_fseeki64(image, 0, SEEK_END);
	const uint64_t imageBytes = uint64_t(_ftelli64(image));
	std::vector<File> files(4096);
	std::vector<Extent> extents(4096);
	Map map = {files.data(), 0, files.size(), extents.data(), 0, extents.size(), false,
		Error::None};
	Source source = {image, FileRead};
	bool ok = Scan(source, map);
	Check(ok, "image: Scan");
	if (!ok) {
		printf("INFO error %u overflow %d\n", unsigned(map.error), int(map.overflow));
		return;
	}
	// Expected tree.
	std::map<std::u16string, std::pair<uint64_t, bool>> expected, found;
	const fs::path tree(treePath);
	for (const auto& entry : fs::recursive_directory_iterator(tree)) {
		std::u16string relative = fs::relative(entry.path(), tree).u16string();
		std::replace(relative.begin(), relative.end(), u'/', u'\\');
		expected[relative] = {entry.is_directory() ? 0 : uint64_t(entry.file_size()),
			entry.is_directory()};
	}
	uint64_t directories = 0, embedded = 0, multi = 0, zero = 0, largest = 0;
	for (size_t i = 0; i < map.fileCount; i++) {
		found[files[i].path] = {files[i].size, files[i].directory};
		directories += files[i].directory;
	}
	std::vector<uint32_t> perFile(map.fileCount);
	bool fileOrder = true;
	for (size_t i = 0; i < map.extentCount; i++) {
		const Extent& e = extents[i];
		perFile[e.file]++;
		zero += e.zero;
		embedded += !e.zero && e.offset % kBlock != 0;
		largest = std::max(largest, e.length);
		if (i != 0 && extents[i - 1].file == e.file)
			fileOrder = fileOrder && extents[i - 1].fileOffset + extents[i - 1].length == e.fileOffset;
		else
			fileOrder = fileOrder && e.fileOffset == 0;
	}
	for (uint32_t n : perFile)
		multi += n > 1;
	printf("INFO image %llu bytes: %zu entries (%llu directories), %zu extents, %llu embedded,"
		" %llu zero, %llu files with several extents, largest extent %llu bytes\n",
		(unsigned long long)imageBytes, map.fileCount, (unsigned long long)directories,
		map.extentCount, (unsigned long long)embedded, (unsigned long long)zero,
		(unsigned long long)multi, (unsigned long long)largest);
	Check(found.size() == map.fileCount, "image: no duplicate paths");
	Check(found == expected, "image: every path, size and directory flag equals the tree");
	if (found != expected) {
		int shown = 0;
		for (const auto& e : expected) {
			if (found.count(e.first) == 0 && shown++ < 5)
				printf("INFO missing %s\n", Narrow(e.first.c_str()).c_str());
		}
		for (const auto& e : found) {
			if (expected.count(e.first) == 0 && shown++ < 10)
				printf("INFO unexpected %s\n", Narrow(e.first.c_str()).c_str());
		}
	}
	Check(fileOrder, "image: extents of each file are reported in file order");

	SortByOffset(map);
	for (int pass = 0; pass < 2; pass++) {
		// pass 0: the whole image in odd chunks; pass 1: only what NextNeeded asks for
		Router router;
		Begin(router, map);
		Compare compare;
		compare.tree = tree;
		compare.map = &map;
		compare.ranges.resize(map.fileCount);
		std::vector<uint8_t> chunk(chunkBytes);
		uint64_t offset = 0, fed = 0;
		ok = true;
		while (ok) {
			if (pass == 1 && (offset = NextNeeded(router)) == ~uint64_t(0))
				break;
			if (offset >= imageBytes)
				break;
			const size_t n = size_t(std::min<uint64_t>(chunk.size(), imageBytes - offset));
			ok = FileRead(image, offset, chunk.data(), n)
				&& Feed(router, offset, chunk.data(), n, CompareSink, &compare);
			offset += n;
			fed += n;
		}
		if (compare.open != nullptr)
			fclose(compare.open);
		bool complete = NextNeeded(router) == ~uint64_t(0);
		for (size_t i = 0; i < map.fileCount; i++) {
			auto& r = compare.ranges[i];
			std::sort(r.begin(), r.end());
			uint64_t at = 0;
			for (const auto& piece : r) {
				complete = complete && piece.first == at; // no gap, no byte twice
				at = piece.second;
			}
			complete = complete && at == files[i].size;
		}
		printf("INFO pass %d: fed %llu bytes in %zu-byte chunks, %llu file bytes in %llu sink calls\n",
			pass, (unsigned long long)fed, chunkBytes, (unsigned long long)compare.bytes,
			(unsigned long long)compare.calls);
		Check(ok, pass == 0 ? "image: Feed(whole image, odd chunks)" : "image: Feed(only NextNeeded ranges)");
		Check(compare.mismatches == 0, "image: every delivered byte equals the original file");
		Check(complete, "image: every file byte delivered exactly once");
	}
	fclose(image);
}

int main(int argc, char** argv)
{
	SyntheticTests();
	if (argc >= 3)
		RealImage(argv[1], argv[2], argc > 3 ? strtoull(argv[3], nullptr, 0) : (1u << 20) + 7);
	printf("%s CharonUdfTests (%d failures)\n", gFailures == 0 ? "PASS" : "FAIL", gFailures);
	return gFailures == 0 ? 0 : 1;
}
