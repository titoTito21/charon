// SPDX-License-Identifier: GPL-2.0-or-later
// Host test for src/efi/CharonSeedDisk.h.
// Usage: CharonSeedDiskTests [<scratch directory>]
// Without arguments only the in-memory tests run. With a scratch directory
// (prepared by tests/charon-seeddisk-make-image.py) FAT images, the expected
// file contents and manifest.txt are written there and the disk files
// disk-*.img are repaired and extended; tests/charon-seeddisk-verify.py then
// checks everything independently. Only ordinary files are touched.
#define _CRT_SECURE_NO_WARNINGS
#include "../src/efi/CharonSeedDisk.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace hakefi::seeddisk;

static int gFailures = 0;

static void Check(bool ok, const std::string& what)
{
	printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	if (!ok)
		gFailures++;
}

// ---- independent helpers (do not use the header's code) -------------------

static uint32_t SlowCrc(const uint8_t* p, size_t n)
{
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i++) {
		c ^= p[i];
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
	}
	return ~c;
}
static uint32_t G32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
static uint64_t G64(const uint8_t* p) { return G32(p) | uint64_t(G32(p + 4)) << 32; }
static void P32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i)); }
static void P64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = uint8_t(v >> (8 * i)); }

// ---- sparse memory disk with write log and failure injection --------------

struct MemDisk {
	uint32_t bs = 512;
	uint64_t blocks = 0;
	std::map<uint64_t, std::vector<uint8_t>> data;
	int failWriteAt = -1; // index of the write call that fails
	int writes = 0;
	std::vector<std::pair<uint64_t, size_t>> log;

	std::vector<uint8_t> Get(uint64_t lba) const
	{
		auto it = data.find(lba);
		return it == data.end() ? std::vector<uint8_t>(bs, 0) : it->second;
	}
	void Put(uint64_t lba, const uint8_t* p) { data[lba].assign(p, p + bs); }
};

static bool MemRead(void* context, uint64_t lba, uint8_t* out, size_t count)
{
	MemDisk& d = *static_cast<MemDisk*>(context);
	if (lba + count > d.blocks)
		return false;
	for (size_t i = 0; i < count; i++) {
		auto block = d.Get(lba + i);
		memcpy(out + i * d.bs, block.data(), d.bs);
	}
	return true;
}

static bool MemWrite(void* context, uint64_t lba, const uint8_t* in, size_t count)
{
	MemDisk& d = *static_cast<MemDisk*>(context);
	if (lba + count > d.blocks)
		return false;
	if (d.writes++ == d.failWriteAt)
		return false;
	d.log.push_back({lba, count});
	for (size_t i = 0; i < count; i++)
		d.Put(lba + i, in + i * d.bs);
	return true;
}

static Disk AsDisk(MemDisk& d) { return Disk{&d, d.bs, d.blocks, MemRead, MemWrite}; }

static const uint8_t kBiosBoot[16] = {0x48, 0x61, 0x68, 0x21, 0x49, 0x64, 0x6F, 0x6E,
	0x74, 0x4E, 0x65, 0x65, 0x64, 0x45, 0x46, 0x49};
static const uint8_t kEsp[16] = {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
	0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};

// A Debian-like image: entry 0 = root (last on disk), 13 = BIOS boot, 14 = ESP.
static void Synthetic(MemDisk& d, uint64_t imageBytes, uint32_t entryCount = 128)
{
	const uint32_t bs = d.bs;
	const uint64_t image = imageBytes / bs, mib = 1024 * 1024 / bs;
	const uint64_t entryBlocks = (uint64_t(entryCount) * 128 + bs - 1) / bs;
	std::vector<uint8_t> entries(entryBlocks * bs, 0);
	auto add = [&](uint32_t index, const uint8_t* type, uint64_t first, uint64_t last, const char* name) {
		uint8_t* e = entries.data() + index * 128;
		memcpy(e, type, 16);
		for (int i = 0; i < 16; i++)
			e[16 + i] = uint8_t(0x10 + index * 7 + i);
		P64(e + 32, first);
		P64(e + 40, last);
		for (int i = 0; name[i]; i++)
			e[56 + 2 * i] = uint8_t(name[i]);
	};
	add(0, kTypeLinuxData.bytes, 20 * mib, image - mib - 1, "root");
	if (entryCount > 14) {
		add(13, kBiosBoot, mib, 4 * mib - 1, "bios");
		add(14, kEsp, 4 * mib, 20 * mib - 1, "esp");
	}
	std::vector<uint8_t> header(bs, 0);
	auto makeHeader = [&](uint64_t my, uint64_t alt, uint64_t entriesLba) {
		std::fill(header.begin(), header.end(), 0);
		memcpy(header.data(), "EFI PART", 8);
		P32(header.data() + 8, 0x00010000);
		P32(header.data() + 12, 92);
		P64(header.data() + 24, my);
		P64(header.data() + 32, alt);
		P64(header.data() + 40, 2 + entryBlocks);
		P64(header.data() + 48, image - 2 - entryBlocks);
		for (int i = 0; i < 16; i++)
			header[56 + i] = uint8_t(0xD0 + i);
		P64(header.data() + 72, entriesLba);
		P32(header.data() + 80, entryCount);
		P32(header.data() + 84, 128);
		P32(header.data() + 88, SlowCrc(entries.data(), entryCount * 128));
		P32(header.data() + 16, SlowCrc(header.data(), 92));
	};
	std::vector<uint8_t> mbr(bs, 0);
	mbr[446 + 2] = 2;
	mbr[446 + 4] = 0xEE;
	mbr[446 + 5] = mbr[446 + 6] = mbr[446 + 7] = 0xFF;
	P32(mbr.data() + 446 + 8, 1);
	P32(mbr.data() + 446 + 12, uint32_t(image - 1));
	mbr[510] = 0x55;
	mbr[511] = 0xAA;
	d.Put(0, mbr.data());
	makeHeader(1, image - 1, 2);
	d.Put(1, header.data());
	for (uint64_t i = 0; i < entryBlocks; i++) {
		d.Put(2 + i, entries.data() + i * bs);
		d.Put(image - 1 - entryBlocks + i, entries.data() + i * bs);
	}
	makeHeader(image - 1, 1, image - 1 - entryBlocks);
	d.Put(image - 1, header.data());
}

struct Table {
	bool valid = false;
	uint64_t my = 0, alt = 0, first = 0, last = 0, entriesLba = 0;
	uint32_t count = 0;
	std::vector<uint8_t> entries, guid;
};

// Independent validation of the GPT copy whose header is at `lba`.
static Table ReadTable(const MemDisk& d, uint64_t lba)
{
	Table t;
	auto h = d.Get(lba);
	if (memcmp(h.data(), "EFI PART", 8) != 0)
		return t;
	const uint32_t size = G32(h.data() + 12), crc = G32(h.data() + 16);
	if (size < 92 || size > d.bs)
		return t;
	P32(h.data() + 16, 0);
	if (SlowCrc(h.data(), size) != crc)
		return t;
	t.my = G64(h.data() + 24);
	t.alt = G64(h.data() + 32);
	t.first = G64(h.data() + 40);
	t.last = G64(h.data() + 48);
	t.guid.assign(h.begin() + 56, h.begin() + 72);
	t.entriesLba = G64(h.data() + 72);
	t.count = G32(h.data() + 80);
	const uint32_t entrySize = G32(h.data() + 84);
	const uint64_t bytes = uint64_t(t.count) * entrySize;
	for (uint64_t i = 0; i * d.bs < bytes; i++) {
		auto b = d.Get(t.entriesLba + i);
		t.entries.insert(t.entries.end(), b.begin(), b.end());
	}
	t.entries.resize(bytes);
	t.valid = t.my == lba && SlowCrc(t.entries.data(), bytes) == G32(h.data() + 88);
	return t;
}

static Guid MakeGuid(uint8_t seed)
{
	Guid g;
	for (int i = 0; i < 16; i++)
		g.bytes[i] = uint8_t(seed * 17 + i * 29 + 1);
	g.bytes[7] = uint8_t((g.bytes[7] & 0x0F) | 0x40);
	g.bytes[8] = uint8_t((g.bytes[8] & 0x3F) | 0x80);
	return g;
}

static std::vector<uint8_t> Pattern(size_t size, uint32_t seed)
{
	std::vector<uint8_t> out(size);
	uint32_t x = seed * 2654435761u + 12345;
	for (size_t i = 0; i < size; i++) {
		x = x * 1664525u + 1013904223u;
		out[i] = uint8_t(x >> 24);
	}
	return out;
}

// ---- seed contents ---------------------------------------------------------

struct Blob {
	std::string name;
	std::vector<uint8_t> data;
};

static std::vector<uint8_t> Text(const char* s) { return std::vector<uint8_t>(s, s + strlen(s)); }

static std::vector<Blob> SeedFiles()
{
	return {
		{"user-data", Text("#cloud-config\nhostname: charon-debian\nusers:\n  - name: tester\n"
			"    lock_passwd: false\n    passwd: $6$rounds=4096$saltsalt$abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./abcdefghijklmnopqrstuv\n"
			"    sudo: ALL=(ALL) NOPASSWD:ALL\n    shell: /bin/bash\npackages:\n  - espeakup\n  - speech-dispatcher\n"
			"  - orca\n  - brltty\nruncmd:\n  - [systemctl, enable, --now, espeakup]\n")},
		{"meta-data", Text("instance-id: charon-0001\nlocal-hostname: charon-debian\n")},
		{"network-config", Text("version: 2\nethernets:\n  all:\n    match:\n      name: \"e*\"\n    dhcp4: true\n")},
	};
}

static std::vector<Blob> ManyFiles(size_t bigBytes)
{
	std::vector<Blob> files = SeedFiles();
	files.push_back({"README.TXT", Text("plain 8.3 name\r\n")});
	files.push_back({"empty-file", {}});
	files.push_back({"big-payload.bin", Pattern(bigBytes, 7)});
	files.push_back({"user-data-2", Text("second file with the same alias stem\n")});
	files.push_back({"My Notes+v2.conf", Text("spaces and a plus sign\n")});
	files.push_back({"abcdefghijklm", Pattern(513, 3)});
	files.push_back({"abcdefghijklmnopqrstuvwxyz", Pattern(4097, 4)});
	files.push_back({".hidden", Text("leading dot\n")});
	files.push_back({"vendor.cfg", Text("lower-case 8.3\n")});
	files.push_back({"USER-D~3", Text("an 8.3 name that looks like an alias\n")});
	files.push_back({"user-data-3", Text("third\n")});
	return files;
}

static std::vector<File> AsFiles(const std::vector<Blob>& blobs)
{
	std::vector<File> files;
	for (const Blob& b : blobs)
		files.push_back({b.name.c_str(), b.data.data(), b.data.size()});
	return files;
}

static const FatTime kTime = {2026, 11, 20, 12, 34, 57};

// ---- FAT tests ---------------------------------------------------------------

static void TestCrc()
{
	const uint8_t* s = reinterpret_cast<const uint8_t*>("123456789");
	Check(Crc32(s, 9) == 0xCBF43926u, "crc32: check value 0xCBF43926");
	auto p = Pattern(5000, 1);
	Check(Crc32(p.data(), p.size()) == SlowCrc(p.data(), p.size()), "crc32: equals the bitwise implementation");
	uint32_t state = Crc32Update(0xFFFFFFFFu, p.data(), 1234);
	state = Crc32Update(state, p.data() + 1234, p.size() - 1234);
	Check(~state == Crc32(p.data(), p.size()), "crc32: incremental update");
}

static uint8_t LfnSum(const uint8_t* n)
{
	uint8_t s = 0;
	for (int i = 0; i < 11; i++)
		s = uint8_t(((s & 1) << 7) + (s >> 1) + n[i]);
	return s;
}

static void TestFat()
{
	auto blobs = ManyFiles(300000);
	auto files = AsFiles(blobs);
	std::vector<uint8_t> image(4 * 1024 * 1024);
	FatInfo info{};
	FatOptions options{0x12345678, kTime, 0, 2048};
	Check(BuildFat(image.data(), image.size(), "cidata", files.data(), files.size(), options, &info) == Error::None,
		"fat: 4 MiB volume builds");
	Check(info.fatBits == 16 && info.clusterCount >= 4085 && info.clusterCount < 65525, "fat: 4 MiB is FAT16 by cluster count");
	Check(image[510] == 0x55 && image[511] == 0xAA && image[21] == 0xF8 && image[16] == 2 && image[11] == 0 && image[12] == 2,
		"fat: boot signature, media F8, 2 FATs, 512-byte sectors");
	Check(memcmp(image.data() + 43, "CIDATA     ", 11) == 0 && G32(image.data() + 39) == 0x12345678 && G32(image.data() + 28) == 2048,
		"fat: BPB label upper-cased, serial, hidden sectors");
	const uint8_t* root = image.data() + 512 * (1 + 2 * info.fatSectors);
	Check(memcmp(root, "CIDATA     ", 11) == 0 && root[11] == 0x08, "fat: volume label entry first in the root");
	// user-data: one long-name entry then alias USER-D~1 with a matching checksum
	Check(root[32] == 0x41 && root[32 + 11] == 0x0F && memcmp(root + 64, "USER-D~1   ", 11) == 0
		&& root[32 + 13] == LfnSum(root + 64) && root[32 + 1] == 'u' && root[32 + 2] == 0,
		"fat: user-data has a long-name entry and alias USER-D~1");
	bool alias2 = false, alias4 = false, exact3 = false, dateOk = true;
	for (uint32_t i = 0; i < info.rootEntriesUsed; i++) {
		const uint8_t* e = root + i * 32;
		if (e[11] == 0x0F)
			continue;
		alias2 |= memcmp(e, "USER-D~2   ", 11) == 0;
		alias4 |= memcmp(e, "USER-D~4   ", 11) == 0;
		exact3 |= memcmp(e, "USER-D~3   ", 11) == 0;
		dateOk &= (e[24] | e[25] << 8) == (((2026 - 1980) << 9) | (11 << 5) | 20)
			&& (e[22] | e[23] << 8) == ((12 << 11) | (34 << 5) | 28);
	}
	Check(alias2 && alias4 && exact3, "fat: aliases stay unique (USER-D~2, real USER-D~3 kept, next is USER-D~4)");
	Check(dateOk, "fat: write date and time on every entry");
	Check(memcmp(image.data() + 512, image.data() + 512 * (1 + info.fatSectors), 512 * info.fatSectors) == 0,
		"fat: both FAT copies identical");

	struct Case { size_t bytes; uint32_t sector; uint32_t bits; } cases[] = {
		{64 * 1024, 512, 12}, {1024 * 1024, 512, 12}, {2 * 1024 * 1024, 512, 12}, {4140 * 512, 512, 12},
		{16 * 1024 * 1024, 512, 16}, {32 * 1024 * 1024, 512, 16}, {64 * 1024 * 1024, 512, 16},
		{1024 * 1024, 4096, 12}, {8 * 1024 * 1024, 4096, 12}, {32 * 1024 * 1024, 4096, 16},
	};
	auto seed = SeedFiles();
	auto seedFiles = AsFiles(seed);
	for (const Case& c : cases) {
		std::vector<uint8_t> v(c.bytes);
		FatInfo i{};
		FatOptions o{1, kTime, c.sector, 0};
		Error e = BuildFat(v.data(), v.size(), "CIDATA", seedFiles.data(), seedFiles.size(), o, &i);
		bool typeOk = i.fatBits == c.bits && (c.bits == 12 ? i.clusterCount < 4085 - 16 : i.clusterCount >= 4085 + 16 && i.clusterCount < 65525 - 16);
		Check(e == Error::None && typeOk && memcmp(v.data() + 54, c.bits == 12 ? "FAT12   " : "FAT16   ", 8) == 0,
			"fat: " + std::to_string(c.bytes) + " bytes, sector " + std::to_string(c.sector) + " -> FAT" + std::to_string(c.bits)
			+ ", " + std::to_string(i.clusterCount) + " clusters of " + std::to_string(i.sectorsPerCluster) + " sectors");
	}

	// errors leave the image untouched
	std::vector<uint8_t> small(1024 * 1024, 0xAA), before = small;
	auto big = ManyFiles(2 * 1024 * 1024);
	auto bigFiles = AsFiles(big);
	FatOptions o{1, kTime, 0, 0};
	Check(BuildFat(small.data(), small.size(), "CIDATA", bigFiles.data(), bigFiles.size(), o) == Error::NoSpace && small == before,
		"fat: files that do not fit -> NoSpace, image untouched");
	File bad[2] = {{"user:data", nullptr, 0}, {"x", nullptr, 0}};
	Check(BuildFat(small.data(), small.size(), "CIDATA", bad, 1, o) == Error::Name && small == before, "fat: invalid character -> Name");
	File dup[2] = {{"User-Data", nullptr, 0}, {"user-data", nullptr, 0}};
	Check(BuildFat(small.data(), small.size(), "CIDATA", dup, 2, o) == Error::Name, "fat: duplicate name (case-insensitive) -> Name");
	Check(BuildFat(small.data(), small.size(), "TWELVE_CHARS", dup, 1, o) == Error::Name, "fat: label over 11 characters -> Name");
	Check(BuildFat(small.data(), 1000, "CIDATA", dup, 1, o) == Error::Argument, "fat: size not a multiple of the sector -> Argument");
	Check(BuildFat(small.data(), 8192, "CIDATA", dup, 1, o) == Error::Geometry, "fat: 8 KiB -> Geometry");
	std::vector<Blob> many;
	for (int i = 0; i < 300; i++)
		many.push_back({"long-file-name-number-" + std::to_string(i), {}});
	auto manyFiles = AsFiles(many);
	Check(BuildFat(small.data(), small.size(), "CIDATA", manyFiles.data(), manyFiles.size(), o) == Error::DirectoryFull && small == before,
		"fat: 300 long names (900 entries) -> DirectoryFull");
	FatOptions badTime{1, {2026, 2, 30, 0, 0, 0}, 0, 0};
	Check(BuildFat(small.data(), small.size(), nullptr, dup, 1, badTime) == Error::None && memcmp(small.data() + 43, "NO NAME    ", 11) == 0
		&& small[512 * (1 + 2 * (small[22] | small[23] << 8)) + 32 + 24] == 0x21
		&& small[512 * (1 + 2 * (small[22] | small[23] << 8)) + 32 + 25] == 0,
		"fat: no label -> NO NAME; invalid date -> 1980-01-01");
}

// ---- GPT tests ---------------------------------------------------------------

static const uint64_t kImage = 64ull << 20, kDisk = 256ull << 20;

static MemDisk Fresh(uint32_t bs, uint64_t diskBytes = kDisk, uint64_t imageBytes = kImage, uint32_t entryCount = 128)
{
	MemDisk d;
	d.bs = bs;
	d.blocks = diskBytes / bs;
	Synthetic(d, imageBytes, entryCount);
	return d;
}

static AddRequest SeedRequest(uint64_t bytes, Placement placement)
{
	AddRequest r{};
	r.type = kTypeLinuxData;
	r.unique = MakeGuid(99);
	r.name = u"CIDATA";
	r.sizeBytes = bytes;
	r.alignmentBytes = 0;
	r.placement = placement;
	return r;
}

static void TestGpt(uint32_t bs)
{
	const std::string tag = "gpt/" + std::to_string(bs) + ": ";
	const uint64_t mib = 1024 * 1024 / bs, image = kImage / bs, entryBlocks = 128 * 128 / bs;
	std::vector<uint8_t> work(kWorkBytes);
	Partition parts[8];
	Info info{};

	MemDisk d = Fresh(bs);
	Disk disk = AsDisk(d);
	Check(WorkBytes(bs, 128, 128) == bs + 16384 && WorkBytes(bs, 128, 128) <= kWorkBytes, tag + "WorkBytes");
	Check(Inspect(disk, work.data(), work.size(), parts, 8, info) == Error::None, tag + "Inspect accepts the image table");
	Check(info.entryCount == 128 && info.entrySize == 128 && info.usedCount == 3 && info.partitionCount == 3 && info.firstFreeEntry == 1
		&& info.firstUsableLba == 2 + entryBlocks && info.lastUsableLba == image - 2 - entryBlocks && info.alternateLba == image - 1
		&& info.imageBlocks == image && info.highestUsedLba == image - mib - 1 && !info.backupAtEnd && info.backupValid
		&& info.protectiveMbr && info.mbrSizeField == image - 1 && info.workBytesNeeded == bs + 16384 && info.diskGuid.bytes[0] == 0xD0,
		tag + "Inspect fields (counts, usable range, data end, backup, MBR)");
	Check(parts[0].index == 0 && parts[0].firstLba == 20 * mib && parts[0].lastLba == image - mib - 1 && parts[0].name[0] == u'r'
		&& parts[0].name[4] == 0 && parts[1].index == 13 && memcmp(parts[1].type.bytes, kBiosBoot, 16) == 0 && parts[2].index == 14
		&& parts[2].firstLba == 4 * mib && parts[2].unique.bytes[0] == 0x10 + 14 * 7, tag + "Inspect partitions");
	Info two{};
	Check(Inspect(disk, work.data(), work.size(), parts, 2, two) == Error::None && two.partitionCount == 2 && two.usedCount == 3,
		tag + "Inspect with a short partition array");
	Info none{};
	Check(Inspect(disk, work.data(), bs + 16384 - 1, parts, 8, none) == Error::Buffer && none.workBytesNeeded == bs + 16384 && none.entryCount == 0,
		tag + "Inspect with a short work buffer -> Buffer + size needed");
	Check(d.writes == 0, tag + "Inspect writes nothing");

	AddResult added{};
	Check(AddPartition(disk, work.data(), work.size(), SeedRequest(8 << 20, Placement::End), added) == Error::NotRelocated && d.writes == 0,
		tag + "AddPartition before Relocate -> NotRelocated, nothing written");

	// Relocate
	RelocateResult moved{};
	Check(Relocate(disk, work.data(), work.size(), nullptr, &moved) == Error::None, tag + "Relocate");
	const uint64_t last = d.blocks - 1;
	Check(moved.backupHeaderLba == last && moved.backupEntriesLba == last - entryBlocks && moved.lastUsableLba == last - entryBlocks - 1
		&& moved.mbrSizeField == last && !moved.mbrCreated, tag + "Relocate result");
	Check(d.log.size() == 4 && d.log[0] == std::make_pair(last - entryBlocks, size_t(entryBlocks)) && d.log[1] == std::make_pair(last, size_t(1))
		&& d.log[2] == std::make_pair(uint64_t(1), size_t(1)) && d.log[3] == std::make_pair(uint64_t(0), size_t(1)),
		tag + "Relocate write order: backup array, backup header, primary header, MBR");
	Table p = ReadTable(d, 1), b = ReadTable(d, last);
	Check(p.valid && b.valid && p.alt == last && b.alt == 1 && p.last == last - entryBlocks - 1 && b.last == p.last && p.first == b.first
		&& b.entriesLba == last - entryBlocks && p.entriesLba == 2 && p.entries == b.entries && p.guid == b.guid,
		tag + "both copies valid and consistent after Relocate (independent check)");
	auto mbr = d.Get(0);
	Check(G32(mbr.data() + 446 + 12) == last && mbr[446 + 4] == 0xEE && mbr[510] == 0x55, tag + "protective MBR size = blocks - 1");
	Check(Inspect(disk, work.data(), work.size(), parts, 8, info) == Error::None && info.backupAtEnd && info.backupValid && info.usedCount == 3
		&& info.highestUsedLba == image - mib - 1 && info.mbrSizeField == last, tag + "Inspect after Relocate");
	const int writesAfterRelocate = d.writes;
	Check(Relocate(disk, work.data(), work.size()) == Error::None && ReadTable(d, 1).entries == p.entries && ReadTable(d, last).valid,
		tag + "Relocate twice is harmless");

	// AddPartition at the end
	MemDisk e = d;
	Disk edisk = AsDisk(e);
	e.log.clear();
	Check(AddPartition(edisk, work.data(), work.size(), SeedRequest((8 << 20) + 1, Placement::End), added) == Error::None,
		tag + "AddPartition at the end");
	const uint64_t count = 8 * mib + 1;
	Check(added.entryIndex == 1 && added.lbaCount == count && added.firstLba % mib == 0 && added.firstLba + count - 1 <= info.lastUsableLba
		&& info.lastUsableLba - (added.firstLba + count - 1) < mib && added.firstLba > info.highestUsedLba,
		tag + "end placement: aligned start, inside the usable area, less than 1 MiB from its end");
	Check(e.log.size() == 4 && e.log[0].first == last - entryBlocks && e.log[1].first == last && e.log[2].first == 2 && e.log[3].first == 1,
		tag + "AddPartition write order: backup array, backup header, primary array, primary header");
	p = ReadTable(e, 1);
	b = ReadTable(e, last);
	const uint8_t* entry = p.entries.data() + 128;
	Check(p.valid && b.valid && p.entries == b.entries && memcmp(entry, kTypeLinuxData.bytes, 16) == 0
		&& memcmp(entry + 16, MakeGuid(99).bytes, 16) == 0 && G64(entry + 32) == added.firstLba && G64(entry + 40) == added.firstLba + count - 1
		&& entry[56] == 'C' && entry[57] == 0 && entry[66] == 'A' && entry[68] == 0, tag + "new entry in both valid copies");
	Check(Inspect(edisk, work.data(), work.size(), parts, 8, info) == Error::None && info.usedCount == 4 && info.firstFreeEntry == 2
		&& parts[1].index == 1 && parts[1].name[5] == u'A' && info.highestUsedLba == added.firstLba + count - 1 && info.backupValid,
		tag + "Inspect sees the new partition");
	AddResult again{};
	Check(AddPartition(edisk, work.data(), work.size(), SeedRequest(8 << 20, Placement::End), again) == Error::Argument,
		tag + "same unique GUID twice -> Argument");
	AddRequest behind = SeedRequest(1 << 20, Placement::End);
	behind.unique = MakeGuid(5);
	Check(AddPartition(edisk, work.data(), work.size(), behind, again) == Error::NoRoom, tag + "nothing free behind the last partition -> NoRoom");

	// directly after the last partition
	MemDisk a = d;
	Disk adisk = AsDisk(a);
	AddRequest after = SeedRequest(4 << 20, Placement::AfterLast);
	after.alignmentBytes = 4096;
	Check(AddPartition(adisk, work.data(), work.size(), after, added) == Error::None && added.firstLba == image - mib && added.lbaCount == 4 * mib
		&& ReadTable(a, 1).valid && ReadTable(a, last).valid, tag + "AfterLast placement starts right behind the root partition");
	AddRequest second = SeedRequest(1 << 20, Placement::End);
	second.unique = MakeGuid(6);
	Check(AddPartition(adisk, work.data(), work.size(), second, again) == Error::None && again.entryIndex == 2
		&& again.firstLba > added.firstLba + added.lbaCount, tag + "a second partition still fits at the end");

	// errors: nothing written
	MemDisk n = d;
	Disk ndisk = AsDisk(n);
	const int base = n.writes;
	Check(AddPartition(ndisk, work.data(), work.size(), SeedRequest(kDisk, Placement::End), added) == Error::NoRoom
		&& AddPartition(ndisk, work.data(), work.size(), SeedRequest(kDisk, Placement::AfterLast), added) == Error::NoRoom
		&& n.writes == base, tag + "too large -> NoRoom, nothing written");
	AddRequest odd = SeedRequest(1 << 20, Placement::End);
	odd.alignmentBytes = 1000;
	AddRequest zero = SeedRequest(1 << 20, Placement::End);
	memset(zero.unique.bytes, 0, 16);
	AddRequest longName = SeedRequest(1 << 20, Placement::End);
	longName.name = u"0123456789012345678901234567890123456";
	Check(AddPartition(ndisk, work.data(), work.size(), odd, added) == Error::Argument
		&& AddPartition(ndisk, work.data(), work.size(), zero, added) == Error::Argument
		&& AddPartition(ndisk, work.data(), work.size(), longName, added) == Error::Argument && n.writes == base,
		tag + "bad alignment, zero GUID, 37-unit name -> Argument");

	// full table
	MemDisk f = Fresh(bs, kDisk, kImage, 4);
	Disk fdisk = AsDisk(f);
	Check(Relocate(fdisk, work.data(), work.size()) == Error::None, tag + "Relocate with a 4-entry table");
	bool filled = true;
	for (int i = 0; i < 3; i++) {
		AddRequest r = SeedRequest(1 << 20, Placement::AfterLast);
		r.unique = MakeGuid(uint8_t(20 + i));
		filled &= AddPartition(fdisk, work.data(), work.size(), r, added) == Error::None && added.entryIndex == uint32_t(i + 1);
	}
	AddRequest r = SeedRequest(1 << 20, Placement::End);
	r.unique = MakeGuid(30);
	Check(filled && AddPartition(fdisk, work.data(), work.size(), r, added) == Error::TableFull && ReadTable(f, 1).valid
		&& ReadTable(f, f.blocks - 1).valid, tag + "three more fit a 4-entry table, the next -> TableFull");

	// corrupt tables
	MemDisk c = Fresh(bs);
	Disk cdisk = AsDisk(c);
	c.data[1][40] ^= 1;
	Check(Inspect(cdisk, work.data(), work.size(), parts, 8, info) == Error::HeaderCrc && Relocate(cdisk, work.data(), work.size()) == Error::HeaderCrc
		&& c.writes == 0, tag + "damaged header -> HeaderCrc, nothing written");
	c = Fresh(bs);
	c.data[2][200] ^= 1;
	Check(Inspect(cdisk, work.data(), work.size(), parts, 8, info) == Error::EntriesCrc && Relocate(cdisk, work.data(), work.size()) == Error::EntriesCrc
		&& c.writes == 0, tag + "damaged entry array -> EntriesCrc, nothing written");
	c = Fresh(bs);
	c.data[1][0] = 'X';
	Check(Inspect(cdisk, work.data(), work.size(), parts, 8, info) == Error::NoGpt, tag + "no signature -> NoGpt");
	c = Fresh(bs);
	c.data[kImage / bs - 1][60] ^= 1;
	Check(Inspect(cdisk, work.data(), work.size(), parts, 8, info) == Error::None && !info.backupValid, tag + "damaged backup is reported, primary accepted");
	c = Fresh(bs);
	memset(c.data[0].data() + 446, 0, 66);
	Check(Inspect(cdisk, work.data(), work.size(), parts, 8, info) == Error::None && !info.protectiveMbr
		&& Relocate(cdisk, work.data(), work.size(), nullptr, &moved) == Error::None && moved.mbrCreated
		&& Inspect(cdisk, work.data(), work.size(), parts, 8, info) == Error::None && info.protectiveMbr && info.mbrSizeField == last,
		tag + "missing protective MBR is created");

	// the image written for the other block size
	MemDisk other = Fresh(bs == 512 ? 4096 : 512);
	MemDisk wrong;
	wrong.bs = bs;
	wrong.blocks = kDisk / bs;
	for (auto& block : other.data) {
		const uint64_t byte = block.first * other.bs;
		for (uint32_t o = 0; o < other.bs; o += 512) {
			auto target = wrong.Get((byte + o) / bs);
			memcpy(target.data() + (byte + o) % bs, block.second.data() + o, 512);
			wrong.Put((byte + o) / bs, target.data());
		}
	}
	Disk wdisk = AsDisk(wrong);
	Check(Inspect(wdisk, work.data(), work.size(), parts, 8, info) == Error::SectorSize, tag + "table of the other block size -> SectorSize");

	// disk exactly as large as the image, and smaller
	MemDisk same = Fresh(bs, kImage);
	Disk sdisk = AsDisk(same);
	Check(Relocate(sdisk, work.data(), work.size(), nullptr, &moved) == Error::None && moved.lastUsableLba == image - 2 - entryBlocks
		&& ReadTable(same, 1).valid && ReadTable(same, image - 1).valid, tag + "disk == image: Relocate keeps the layout");
	MemDisk tiny = Fresh(bs);
	tiny.blocks = image - mib;
	Disk tdisk = AsDisk(tiny);
	Check(Relocate(tdisk, work.data(), work.size()) == Error::TooSmall && tiny.writes == 0, tag + "disk smaller than the partitions -> TooSmall, nothing written");

	// fresh GUIDs
	MemDisk g = Fresh(bs);
	Disk gdisk = AsDisk(g);
	Guid diskGuid = MakeGuid(40), partGuids[3] = {MakeGuid(41), MakeGuid(42), MakeGuid(43)};
	RelocateOptions options{&diskGuid, partGuids, 2};
	Check(Relocate(gdisk, work.data(), work.size(), &options) == Error::Argument && g.writes == 0, tag + "too few partition GUIDs -> Argument");
	options.partitionGuidCount = 3;
	Check(Relocate(gdisk, work.data(), work.size(), &options) == Error::None, tag + "Relocate with fresh GUIDs");
	Check(g.log.size() == 5 && g.log[0].first == last - entryBlocks && g.log[1].first == last && g.log[2].first == 2 && g.log[3].first == 1
		&& g.log[4].first == 0, tag + "write order with GUIDs: backup array, backup header, primary array, primary header, MBR");
	p = ReadTable(g, 1);
	b = ReadTable(g, last);
	Check(p.valid && b.valid && p.entries == b.entries && memcmp(p.guid.data(), diskGuid.bytes, 16) == 0 && b.guid == p.guid
		&& memcmp(p.entries.data() + 16, partGuids[0].bytes, 16) == 0 && memcmp(p.entries.data() + 13 * 128 + 16, partGuids[1].bytes, 16) == 0
		&& memcmp(p.entries.data() + 14 * 128 + 16, partGuids[2].bytes, 16) == 0 && G64(p.entries.data() + 32) == 20 * mib
		&& memcmp(p.entries.data() + 13 * 128, kBiosBoot, 16) == 0, tag + "GUIDs replaced in both copies, nothing else changed");

	// failure injection: Relocate without GUIDs never leaves the primary invalid
	bool primaryAlways = true, sawFailure = false;
	for (int k = 0; k < 4; k++) {
		MemDisk x = Fresh(bs);
		Disk xdisk = AsDisk(x);
		x.failWriteAt = k;
		sawFailure |= Relocate(xdisk, work.data(), work.size()) == Error::Write;
		primaryAlways &= ReadTable(x, 1).valid && Inspect(xdisk, work.data(), work.size(), parts, 8, info) == Error::None && info.usedCount == 3;
		x.failWriteAt = -1;
		primaryAlways &= Relocate(xdisk, work.data(), work.size()) == Error::None && ReadTable(x, last).valid && ReadTable(x, 1).alt == last;
	}
	Check(sawFailure && primaryAlways, tag + "Relocate: a failed write at any step leaves a valid primary; repeating repairs");
	bool oneValid = true;
	for (int k = 0; k < 5; k++) {
		MemDisk x = Fresh(bs);
		Disk xdisk = AsDisk(x);
		x.failWriteAt = k;
		oneValid &= Relocate(xdisk, work.data(), work.size(), &options) == Error::Write;
		oneValid &= ReadTable(x, 1).valid || ReadTable(x, last).valid;
		if (k != 3)
			oneValid &= ReadTable(x, 1).valid;
		else
			oneValid &= !ReadTable(x, 1).valid && ReadTable(x, last).valid;
	}
	Check(oneValid, tag + "Relocate with GUIDs: primary invalid only between its array and header, backup at the last LBA valid then");
	oneValid = true;
	for (int k = 0; k < 4; k++) {
		MemDisk x = d;
		Disk xdisk = AsDisk(x);
		x.failWriteAt = x.writes + k;
		oneValid &= AddPartition(xdisk, work.data(), work.size(), SeedRequest(8 << 20, Placement::End), added) == Error::Write;
		Table xp = ReadTable(x, 1), xb = ReadTable(x, last);
		oneValid &= xp.valid || xb.valid;
		if (k < 3)
			oneValid &= xp.valid && xp.entries == ReadTable(d, 1).entries; // still the old table
		if (k >= 2)
			oneValid &= xb.valid && xb.entries != ReadTable(d, 1).entries; // backup complete and new
		if (k < 3) {
			x.failWriteAt = -1;
			oneValid &= AddPartition(xdisk, work.data(), work.size(), SeedRequest(8 << 20, Placement::End), added) == Error::None
				&& ReadTable(x, 1).valid && ReadTable(x, last).valid;
		}
	}
	Check(oneValid, tag + "AddPartition: a failed write always leaves one valid copy; repeating completes it");
	(void)writesAfterRelocate;

	// WriteImage
	MemDisk w = e;
	Disk wd = AsDisk(w);
	p = ReadTable(w, 1);
	const uint64_t first = G64(p.entries.data() + 128 + 32);
	auto payload = Pattern(300 * 1024 + 100, 9);
	w.log.clear();
	Check(WriteImage(wd, first, count, payload.data(), payload.size(), work.data(), work.size()) == Error::None, tag + "WriteImage");
	std::vector<uint8_t> back((payload.size() / bs + 1) * bs);
	MemRead(&w, first, back.data(), back.size() / bs);
	bool padded = true;
	for (size_t i = payload.size(); i < back.size(); i++)
		padded &= back[i] == 0;
	bool chunks = true;
	for (auto& entryLog : w.log)
		chunks &= entryLog.second * bs <= 65536 && entryLog.first >= first && entryLog.first + entryLog.second <= first + count;
	Check(memcmp(back.data(), payload.data(), payload.size()) == 0 && padded && chunks && ReadTable(w, 1).valid && ReadTable(w, last).valid,
		tag + "WriteImage: bytes identical, last block zero padded, writes of at most 64 KiB inside the partition");
	Check(WriteImage(wd, first, 4, payload.data(), payload.size(), work.data(), work.size()) == Error::Argument
		&& WriteImage(wd, 1, count, payload.data(), payload.size(), work.data(), work.size()) == Error::Argument
		&& WriteImage(wd, first, count, payload.data(), payload.size(), nullptr, 0) == Error::Buffer
		&& WriteImage(wd, w.blocks - 2, 8, payload.data(), bs, nullptr, 0) == Error::Argument,
		tag + "WriteImage: image larger than the range, LBA 1, missing pad buffer, range past the disk");
}

static void TestHuge()
{
	// 3 TiB of 512-byte blocks: the protective MBR saturates.
	std::vector<uint8_t> work(kWorkBytes);
	MemDisk d = Fresh(512, 3ull << 40);
	Disk disk = AsDisk(d);
	RelocateResult moved{};
	Check(Relocate(disk, work.data(), work.size(), nullptr, &moved) == Error::None && moved.mbrSizeField == 0xFFFFFFFFu
		&& G32(d.Get(0).data() + 446 + 12) == 0xFFFFFFFFu, "huge: 3 TiB disk -> MBR size field 0xFFFFFFFF");
	Table p = ReadTable(d, 1), b = ReadTable(d, d.blocks - 1);
	Check(p.valid && b.valid && p.alt == d.blocks - 1 && p.last == d.blocks - 34, "huge: 64-bit LBAs in both headers");
	AddResult added{};
	Check(AddPartition(disk, work.data(), work.size(), SeedRequest(8 << 20, Placement::End), added) == Error::None
		&& added.firstLba > 0xFFFFFFFFull && added.firstLba % 2048 == 0 && ReadTable(d, d.blocks - 1).valid, "huge: seed partition above 2 TiB");
	// 2 TiB exactly at 512 bytes: blocks - 1 = 0xFFFFFFFF fits
	MemDisk e = Fresh(512, 1ull << 41);
	Disk edisk = AsDisk(e);
	Check(Relocate(edisk, work.data(), work.size(), nullptr, &moved) == Error::None && moved.mbrSizeField == 0xFFFFFFFFu, "huge: exactly 2 TiB");
}

// ---- file mode -----------------------------------------------------------------

struct FileDisk {
	FILE* file = nullptr;
	uint32_t bs = 512;
};

static bool FileRead(void* context, uint64_t lba, uint8_t* out, size_t count)
{
	FileDisk& d = *static_cast<FileDisk*>(context);
	return _fseeki64(d.file, int64_t(lba * d.bs), SEEK_SET) == 0 && fread(out, d.bs, count, d.file) == count;
}

static bool FileWrite(void* context, uint64_t lba, const uint8_t* in, size_t count)
{
	FileDisk& d = *static_cast<FileDisk*>(context);
	return _fseeki64(d.file, int64_t(lba * d.bs), SEEK_SET) == 0 && fwrite(in, d.bs, count, d.file) == count && fflush(d.file) == 0;
}

static bool Save(const std::filesystem::path& path, const uint8_t* data, size_t size)
{
	FILE* f = fopen(path.string().c_str(), "wb");
	if (f == nullptr)
		return false;
	const bool ok = size == 0 || fwrite(data, 1, size, f) == size;
	return fclose(f) == 0 && ok;
}

static void SaveExpected(const std::filesystem::path& dir, const std::vector<Blob>& blobs)
{
	std::filesystem::create_directories(dir);
	bool ok = true;
	for (const Blob& b : blobs)
		ok &= Save(dir / b.name, b.data.data(), b.data.size());
	Check(ok, "file: expected contents written to " + dir.filename().string());
}

static void FileMode(const std::filesystem::path& scratch)
{
	std::string manifest;
	struct FatCase { const char* name; size_t bytes; uint32_t sector; bool many; } fats[] = {
		{"fat-1m-512", 1 << 20, 512, true}, {"fat-2m-512", 2 << 20, 512, true}, {"fat-boundary-512", 4140 * 512, 512, true},
		{"fat-4m-512", 4 << 20, 512, true}, {"fat-16m-512", 16 << 20, 512, true}, {"fat-32m-512", 32 << 20, 512, true},
		{"fat-seed-1m-512", 1 << 20, 512, false}, {"fat-8m-4096", 8 << 20, 4096, true}, {"fat-32m-4096", 32 << 20, 4096, true},
	};
	for (const FatCase& c : fats) {
		auto blobs = c.many ? ManyFiles(c.bytes / 3) : SeedFiles();
		auto files = AsFiles(blobs);
		std::vector<uint8_t> image(c.bytes);
		FatInfo info{};
		FatOptions options{0xC1DA7A00u + uint32_t(c.bytes >> 16), kTime, c.sector, 0};
		Error e = BuildFat(image.data(), image.size(), "CIDATA", files.data(), files.size(), options, &info);
		Check(e == Error::None && Save(scratch / (std::string(c.name) + ".img"), image.data(), image.size()),
			std::string("file: ") + c.name + ".img (FAT" + std::to_string(info.fatBits) + ")");
		SaveExpected(scratch / "expected" / c.name, blobs);
		manifest += std::string("fat\t") + c.name + ".img\t" + std::to_string(c.sector) + "\t" + std::to_string(info.fatBits) + "\tCIDATA\texpected/"
			+ c.name + "\t" + std::to_string(options.serial) + "\n";
	}

	struct DiskCase { const char* name; uint32_t bs; size_t seedBytes; Placement placement; bool guids; } disks[] = {
		{"disk-512", 512, 8 << 20, Placement::End, false},
		{"disk-512-after", 512, 1 << 20, Placement::AfterLast, false},
		{"disk-4096", 4096, 32 << 20, Placement::End, true},
	};
	for (const DiskCase& c : disks) {
		const std::string tag = std::string("file: ") + c.name + ": ";
		const auto path = scratch / (std::string(c.name) + ".img");
		FileDisk fd;
		fd.bs = c.bs;
		fd.file = fopen(path.string().c_str(), "r+b");
		if (fd.file == nullptr) {
			Check(false, tag + "open (run tests/charon-seeddisk-make-image.py first)");
			continue;
		}
		Disk disk{&fd, c.bs, std::filesystem::file_size(path) / c.bs, FileRead, FileWrite};
		std::vector<uint8_t> work(kWorkBytes);
		Partition parts[8];
		Info info{};
		Error error = Inspect(disk, work.data(), work.size(), parts, 8, info);
		Check(error == Error::None && info.usedCount == 3 && !info.backupAtEnd && info.backupValid,
			tag + "Inspect: 3 partitions, backup not at the end, image of " + std::to_string(info.imageBlocks) + " blocks, data ends at LBA "
			+ std::to_string(info.highestUsedLba));
		Guid diskGuid = MakeGuid(70), partGuids[3] = {MakeGuid(71), MakeGuid(72), MakeGuid(73)};
		RelocateOptions options{&diskGuid, partGuids, 3};
		RelocateResult moved{};
		Check(Relocate(disk, work.data(), work.size(), c.guids ? &options : nullptr, &moved) == Error::None, tag + "Relocate");
		AddRequest request = SeedRequest(c.seedBytes, c.placement);
		AddResult added{};
		error = AddPartition(disk, work.data(), work.size(), request, added);
		Check(error == Error::None, tag + "AddPartition -> LBA "
			+ std::to_string(added.firstLba) + ", " + std::to_string(added.lbaCount) + " blocks, entry " + std::to_string(added.entryIndex));
		auto blobs = SeedFiles();
		auto files = AsFiles(blobs);
		std::vector<uint8_t> image(size_t(added.lbaCount) * c.bs);
		FatInfo fat{};
		FatOptions fo{0x5EED0000u + c.bs, kTime, c.bs, uint32_t(added.firstLba)};
		error = BuildFat(image.data(), image.size(), "CIDATA", files.data(), files.size(), fo, &fat);
		Check(error == Error::None
			&& WriteImage(disk, added.firstLba, added.lbaCount, image.data(), image.size()) == Error::None,
			tag + "seed volume FAT" + std::to_string(fat.fatBits) + " written to the partition");
		Check(Inspect(disk, work.data(), work.size(), parts, 8, info) == Error::None && info.usedCount == 4 && info.backupAtEnd && info.backupValid
			&& info.protectiveMbr && info.highestUsedLba == added.firstLba + added.lbaCount - 1, tag + "Inspect after all steps");
		fclose(fd.file);
		SaveExpected(scratch / "expected" / c.name, blobs);
		manifest += std::string("disk\t") + c.name + ".img\t" + std::to_string(c.bs) + "\tcloud-" + std::to_string(c.bs) + ".img\t"
			+ std::to_string(added.firstLba) + "\t" + std::to_string(added.lbaCount) + "\t" + std::to_string(added.entryIndex) + "\t"
			+ (c.placement == Placement::End ? "end" : "after") + "\t" + (c.guids ? "1" : "0") + "\texpected/" + c.name + "\t"
			+ std::to_string(fat.fatBits) + "\n";
	}
	Check(Save(scratch / "manifest.txt", reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size()), "file: manifest.txt");
}

int main(int argc, char** argv)
{
	TestCrc();
	TestFat();
	TestGpt(512);
	TestGpt(4096);
	TestHuge();
	if (argc > 1)
		FileMode(argv[1]);
	printf("%s CharonSeedDiskTests (%d failures)\n", gFailures == 0 ? "PASS" : "FAIL", gFailures);
	return gFailures == 0 ? 0 : 1;
}
