// SPDX-License-Identifier: GPL-2.0-or-later
// Charon: what is needed after a raw "cloud" disk image (a GPT image such as
// debian-13-generic-amd64.raw) was written to a larger disk:
//
//   1. BuildFat()      builds a small FAT12/FAT16 volume in memory, e.g. the
//                      cloud-init NoCloud seed (label CIDATA, files user-data,
//                      meta-data, network-config).
//   2. Inspect()       validates the primary GPT and lists its partitions.
//   3. Relocate()      moves the backup GPT to the real end of the disk.
//   4. AddPartition()  adds one partition (the seed) to both GPT copies.
//   5. WriteImage()    writes a memory image (the FAT volume) to a partition.
//   6. r409: ReservePlaceholder(), RetypePartition(), RemovePartition(),
//      FreeGaps() - the reserved ranges of a multi-system plan (Part 3).
//
// Pure logic: no allocation, no floating point, block I/O through callbacks,
// the caller supplies every buffer. Nothing here keeps state between calls and
// nothing is cached: every function reads the primary GPT again.
//
// WRITE ORDER (a failure in the middle keeps the primary GPT valid for as long
// as possible; every function stops at the first failed write):
//
//   Relocate():     1. backup entry array at its new place (end of the disk)
//                   2. backup header in the last LBA
//                   3. primary entry array - ONLY when partition GUIDs change
//                   4. primary header (LBA 1)
//                   5. protective MBR (LBA 0)
//     Without new partition GUIDs the primary copy is valid before step 4 (old
//     contents) and after it (new contents); it is never invalid. With new
//     partition GUIDs the primary header does not match its array between
//     steps 3 and 4; the complete new backup written in steps 1-2 sits in the
//     last LBA, where firmware and Linux look when the primary copy is bad.
//
//   AddPartition(): 1. backup entry array   2. backup header
//                   3. primary entry array  4. primary header
//     Before step 3 the primary copy is the valid old table. Between steps 3
//     and 4 the primary header does not match its array and the backup copy
//     (already complete, with the new partition) is the valid one. A failed
//     AddPartition() can simply be repeated once writes work again, as long as
//     Inspect() still accepts the primary copy.
//
//   The stale backup header that the image carried at its own last block is
//   not wiped: after Relocate() it lies in free, usable space and nothing
//   refers to it.
//
// PLACEMENT OF THE SEED PARTITION. growpart (cloud-init, cloud-initramfs)
// grows a partition only into the free space that directly follows it.
// Placement::End puts the new partition at the very end of the usable area,
// so the last system partition (the root file system of a cloud image) can
// still grow up to the seed partition; it loses only the seed's own size.
// Placement::AfterLast puts it directly after the last used partition, which
// blocks that growth completely - use it only when that is wanted.
//
// SECTOR SIZES. A GPT is tied to the block size it was written for. An image
// made for 512-byte sectors that lands on a 4096-byte-sector disk (or the
// reverse) has no header in LBA 1; Inspect() reports Error::SectorSize and
// nothing here converts it. Linux mounts a FAT volume only when its sector
// size is not smaller than the block size of the device, so for a 4096-byte
// disk build the seed with FatOptions::sectorSize = 4096.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace hakefi::seeddisk {

enum class Error : uint32_t {
	None,
	Argument,      // null pointer, zero size, unsupported block or sector size
	Name,          // file name or label not representable, or a duplicate name
	Geometry,      // image size cannot hold a FAT12/FAT16 volume
	DirectoryFull, // the root directory (kFatRootEntries entries) is too small
	NoSpace,       // the files do not fit the data area
	Read,          // the disk read callback failed
	Write,         // the disk write callback failed
	NoGpt,         // no "EFI PART" signature in LBA 1
	SectorSize,    // a GPT for the other block size (512 <-> 4096) was found
	HeaderCrc,     // primary header CRC32 mismatch
	EntriesCrc,    // primary entry array CRC32 mismatch
	Layout,        // header fields unsupported or inconsistent
	Buffer,        // work buffer too small (see WorkBytes / Info)
	TooSmall,      // the disk cannot hold the partitions plus the backup GPT
	NotRelocated,  // the backup GPT is not at the end of the disk: Relocate()
	TableFull,     // no free partition entry
	NoRoom,        // no free space of that size behind the last partition
	NotFound,      // r409: no partition with that unique GUID
	Mismatch,      // r409: the partition is not what the caller expected (type, place)
};

namespace detail {

inline uint16_t U16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t U32(const uint8_t* p)
{
	return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16)
		| (uint32_t(p[3]) << 24);
}
inline uint64_t U64(const uint8_t* p)
{
	return uint64_t(U32(p)) | (uint64_t(U32(p + 4)) << 32);
}
inline void Put16(uint8_t* p, uint32_t v)
{
	p[0] = uint8_t(v);
	p[1] = uint8_t(v >> 8);
}
inline void Put32(uint8_t* p, uint32_t v)
{
	Put16(p, v);
	Put16(p + 2, v >> 16);
}
inline void Put64(uint8_t* p, uint64_t v)
{
	Put32(p, uint32_t(v));
	Put32(p + 4, uint32_t(v >> 32));
}
// The firmware build has no memcmp.
inline bool Same(const uint8_t* a, const uint8_t* b, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (a[i] != b[i])
			return false;
	return true;
}
inline bool AllZero(const uint8_t* p, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (p[i] != 0)
			return false;
	return true;
}

// CRC-32 (IEEE 802.3, reflected, polynomial 0xEDB88320), four bits at a time.
constexpr uint32_t kCrcNibble[16] = {
	0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
	0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
	0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
};

} // namespace detail

// Running CRC-32: start with 0xFFFFFFFF, invert the final state.
inline uint32_t Crc32Update(uint32_t state, const uint8_t* data, size_t size)
{
	for (size_t i = 0; i < size; i++) {
		state ^= data[i];
		state = (state >> 4) ^ detail::kCrcNibble[state & 15];
		state = (state >> 4) ^ detail::kCrcNibble[state & 15];
	}
	return state;
}

inline uint32_t Crc32(const uint8_t* data, size_t size)
{
	return ~Crc32Update(0xFFFFFFFFu, data, size);
}

// ---------------------------------------------------------------------------
// Part 1: FAT12 / FAT16 volume builder
// ---------------------------------------------------------------------------

// A file of the root directory. `name` is the long name in printable ASCII
// (at most 255 characters, none of " * / : < > ? \ |, not ending in a space or
// a dot). A name that is not already an upper-case 8.3 name gets VFAT long
// name entries plus a generated alias (user-data -> USER-D~1), so Linux vfat
// finds it by exactly the given spelling.
struct File {
	const char* name;
	const uint8_t* data;
	size_t size;
};

// Local time for every directory entry. Anything outside 1980..2107 or not a
// calendar value is written as 1980-01-01 00:00:00.
struct FatTime {
	uint16_t year;
	uint8_t month, day, hour, minute, second;
};

struct FatOptions {
	uint32_t serial;        // volume serial number (BS_VolID)
	FatTime time;
	uint32_t sectorSize;    // 0 or 512, or 4096 for a 4096-byte-sector disk
	uint32_t hiddenSectors; // BPB_HiddSec: first LBA of the partition, or 0
};

struct FatInfo {
	uint32_t fatBits;           // 12 or 16
	uint32_t sectorSize;
	uint32_t sectorsPerCluster;
	uint32_t totalSectors;
	uint32_t fatSectors;        // per FAT
	uint32_t rootEntriesUsed;
	uint32_t clusterCount;
	uint32_t usedClusters;
};

constexpr uint32_t kFatRootEntries = 512;

namespace detail {

struct FatGeometry {
	uint32_t bits, sectorSize, sectorsPerCluster, totalSectors, fatSectors;
	uint32_t rootSectors, clusters;
};

// Microsoft FAT specification: the type follows from the cluster count alone
// (< 4085: FAT12, < 65525: FAT16). Counts within 16 of a limit are avoided, as
// the specification advises, by taking the next cluster size.
inline bool FatLayout(uint64_t totalSectors, uint32_t sectorSize, FatGeometry& g)
{
	const uint32_t rootSectors = kFatRootEntries * 32 / sectorSize;
	if (totalSectors > 0xFFFFFFFFull || totalSectors < uint64_t(rootSectors) + 4)
		return false;
	const uint32_t available = uint32_t(totalSectors) - 1 - rootSectors;
	for (uint32_t spc = 1; spc <= 128; spc <<= 1) {
		for (uint32_t bits = 12; bits <= 16; bits += 4) {
			uint32_t fat = 1, clusters = 0;
			bool found = false;
			for (; fat <= 300 && 2 * fat < available; fat++) {
				clusters = (available - 2 * fat) / spc;
				const uint64_t entries = uint64_t(clusters) + 2;
				const uint64_t bytes = bits == 12 ? (entries * 3 + 1) / 2
					: entries * 2;
				if (uint64_t(fat) * sectorSize >= bytes) {
					found = true;
					break;
				}
			}
			if (!found || clusters == 0)
				continue;
			if (bits == 12 && clusters > 4084 - 16)
				continue;
			if (bits == 16 && (clusters < 4085 + 16 || clusters > 65524 - 16))
				continue;
			g.bits = bits;
			g.sectorSize = sectorSize;
			g.sectorsPerCluster = spc;
			g.totalSectors = uint32_t(totalSectors);
			g.fatSectors = fat;
			g.rootSectors = rootSectors;
			g.clusters = clusters;
			return true;
		}
	}
	return false;
}

inline char Upper(char c) { return c >= 'a' && c <= 'z' ? char(c - 32) : c; }

inline bool In(char c, const char* set)
{
	for (; *set != 0; set++)
		if (*set == c)
			return true;
	return false;
}

// Allowed in a short (8.3) name; letters are upper case there.
inline bool ShortChar(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
		|| In(c, "$%'-_@~`!(){}^#&");
}

inline bool LongChar(char c)
{
	return c >= 0x20 && c < 0x7F && !In(c, "\"*/:<>?\\|");
}

inline size_t Length(const char* s)
{
	size_t n = 0;
	while (s[n] != 0)
		n++;
	return n;
}

inline bool ValidLongName(const char* name, size_t& length)
{
	if (name == nullptr)
		return false;
	length = 0;
	while (name[length] != 0) {
		if (!LongChar(name[length]) || length == 255)
			return false;
		length++;
	}
	return length != 0 && name[length - 1] != ' ' && name[length - 1] != '.'
		&& name[0] != ' ';
}

// True when `name` already is an upper-case 8.3 name; `out` is its padded form.
inline bool ExactShort(const char* name, size_t length, uint8_t out[11])
{
	memset(out, ' ', 11);
	size_t base = 0, extension = 0;
	bool dot = false;
	for (size_t i = 0; i < length; i++) {
		const char c = name[i];
		if (c == '.') {
			if (dot || base == 0)
				return false;
			dot = true;
		} else if (!ShortChar(c)) {
			return false;
		} else if (!dot) {
			if (base == 8)
				return false;
			out[base++] = uint8_t(c);
		} else {
			if (extension == 3)
				return false;
			out[8 + extension++] = uint8_t(c);
		}
	}
	return base != 0 && (!dot || extension != 0);
}

// Generated alias: leading dots and all spaces dropped, upper case, characters
// that a short name cannot hold become '_', base cut to make room for "~n".
inline void MakeAlias(const char* name, size_t length, uint32_t number,
	uint8_t out[11])
{
	memset(out, ' ', 11);
	size_t start = 0;
	while (start < length && name[start] == '.')
		start++;
	size_t lastDot = length;
	for (size_t i = start; i < length; i++)
		if (name[i] == '.')
			lastDot = i;
	char tail[8];
	size_t tailLength = 0;
	for (uint32_t n = number; n != 0; n /= 10)
		tail[tailLength++] = char('0' + n % 10);
	size_t base = 0;
	const size_t baseLimit = 8 - (tailLength + 1);
	for (size_t i = start; i < lastDot && base < baseLimit; i++) {
		const char c = Upper(name[i]);
		if (c == ' ' || c == '.')
			continue;
		out[base++] = uint8_t(ShortChar(c) ? c : '_');
	}
	if (base == 0)
		out[base++] = '_';
	out[base++] = '~';
	while (tailLength != 0)
		out[base++] = uint8_t(tail[--tailLength]);
	size_t extension = 0;
	for (size_t i = lastDot + 1; i < length && extension < 3; i++) {
		const char c = Upper(name[i]);
		if (c == ' ')
			continue;
		out[8 + extension++] = uint8_t(ShortChar(c) ? c : '_');
	}
}

inline uint8_t ShortChecksum(const uint8_t name[11])
{
	uint8_t sum = 0;
	for (int i = 0; i < 11; i++)
		sum = uint8_t(((sum & 1) << 7) + (sum >> 1) + name[i]);
	return sum;
}

inline bool SameNameNoCase(const char* a, const char* b)
{
	for (;; a++, b++) {
		if (Upper(*a) != Upper(*b))
			return false;
		if (*a == 0)
			return true;
	}
}

inline void SetFatEntry(uint8_t* fat, uint32_t bits, uint32_t cluster,
	uint32_t value)
{
	if (bits == 16) {
		Put16(fat + size_t(cluster) * 2, value);
		return;
	}
	uint8_t* p = fat + size_t(cluster) * 3 / 2;
	if (cluster & 1) {
		p[0] = uint8_t((p[0] & 0x0F) | ((value << 4) & 0xF0));
		p[1] = uint8_t(value >> 4);
	} else {
		p[0] = uint8_t(value);
		p[1] = uint8_t((p[1] & 0xF0) | ((value >> 8) & 0x0F));
	}
}

inline void FatStamp(const FatTime& t, uint16_t& date, uint16_t& time,
	uint8_t& tenths)
{
	static constexpr uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30,
		31, 30, 31};
	bool valid = t.year >= 1980 && t.year <= 2107 && t.month >= 1
		&& t.month <= 12 && t.day >= 1 && t.hour < 24 && t.minute < 60
		&& t.second < 60;
	if (valid) {
		const bool leap = (t.year % 4 == 0 && t.year % 100 != 0)
			|| t.year % 400 == 0;
		const uint32_t days = kDays[t.month - 1] + (t.month == 2 && leap ? 1 : 0);
		valid = t.day <= days;
	}
	if (!valid) {
		date = uint16_t((1 << 5) | 1);
		time = 0;
		tenths = 0;
		return;
	}
	date = uint16_t(((t.year - 1980) << 9) | (t.month << 5) | t.day);
	time = uint16_t((t.hour << 11) | (t.minute << 5) | (t.second / 2));
	tenths = uint8_t((t.second & 1) * 100);
}

// Number of long-name entries of a name (0 for an upper-case 8.3 name).
inline uint32_t LongEntries(const char* name, size_t length)
{
	uint8_t unused[11];
	return ExactShort(name, length, unused) ? 0 : uint32_t((length + 12) / 13);
}

} // namespace detail

// Builds a complete FAT volume in `image` (`imageBytes` bytes, a multiple of
// the sector size; 1 MiB..32 MiB is the intended range, up to 4 GiB works).
// Layout: 1 reserved sector, 2 FATs, a 512-entry root directory, cluster size
// chosen so that the cluster count decides FAT12 or FAT16 unambiguously.
// `label`: up to 11 characters, stored upper case in the BPB and as a volume
// label entry (null or "" writes "NO NAME" and no label entry).
// Everything is validated first: on any error `image` and `info` are untouched.
// No scratch buffer is needed; stack use is below 200 bytes.
inline Error BuildFat(uint8_t* image, size_t imageBytes, const char* label,
	const File* files, size_t count, const FatOptions& options,
	FatInfo* info = nullptr)
{
	using namespace detail;
	const uint32_t sectorSize = options.sectorSize == 0 ? 512 : options.sectorSize;
	if (image == nullptr || (files == nullptr && count != 0)
		|| (sectorSize != 512 && sectorSize != 4096))
		return Error::Argument;
	if (imageBytes == 0 || imageBytes % sectorSize != 0)
		return Error::Argument;
	FatGeometry g;
	if (!FatLayout(uint64_t(imageBytes) / sectorSize, sectorSize, g))
		return Error::Geometry;

	uint8_t labelBytes[11];
	memset(labelBytes, ' ', 11);
	size_t labelLength = 0;
	if (label != nullptr) {
		labelLength = Length(label);
		if (labelLength > 11)
			return Error::Name;
		for (size_t i = 0; i < labelLength; i++) {
			const char c = Upper(label[i]);
			if (!ShortChar(c) && c != ' ')
				return Error::Name;
			labelBytes[i] = uint8_t(c);
		}
		if (labelLength != 0 && labelBytes[0] == ' ')
			return Error::Name;
	}

	const uint64_t clusterBytes = uint64_t(g.sectorsPerCluster) * sectorSize;
	uint64_t entriesNeeded = labelLength != 0 ? 1 : 0, clustersNeeded = 0;
	for (size_t i = 0; i < count; i++) {
		size_t length;
		if (!ValidLongName(files[i].name, length))
			return Error::Name;
		if (files[i].size != 0 && files[i].data == nullptr)
			return Error::Argument;
		for (size_t k = 0; k < i; k++)
			if (SameNameNoCase(files[i].name, files[k].name))
				return Error::Name;
		entriesNeeded += 1 + LongEntries(files[i].name, length);
		clustersNeeded += (uint64_t(files[i].size) + clusterBytes - 1)
			/ clusterBytes;
		if (clustersNeeded > g.clusters)
			return Error::NoSpace;
	}
	if (entriesNeeded > kFatRootEntries)
		return Error::DirectoryFull;

	memset(image, 0, imageBytes);

	// Boot sector with the FAT12/16 BIOS parameter block.
	image[0] = 0xEB;
	image[1] = 0x3C;
	image[2] = 0x90;
	memcpy(image + 3, "MSWIN4.1", 8);
	Put16(image + 11, sectorSize);
	image[13] = uint8_t(g.sectorsPerCluster);
	Put16(image + 14, 1);
	image[16] = 2;
	Put16(image + 17, kFatRootEntries);
	Put16(image + 19, g.totalSectors < 0x10000 ? g.totalSectors : 0);
	image[21] = 0xF8;
	Put16(image + 22, g.fatSectors);
	Put16(image + 24, 63);
	Put16(image + 26, 255);
	Put32(image + 28, options.hiddenSectors);
	Put32(image + 32, g.totalSectors < 0x10000 ? 0 : g.totalSectors);
	image[36] = 0x80;
	image[38] = 0x29;
	Put32(image + 39, options.serial);
	if (labelLength != 0)
		memcpy(image + 43, labelBytes, 11);
	else
		memcpy(image + 43, "NO NAME    ", 11);
	memcpy(image + 54, g.bits == 12 ? "FAT12   " : "FAT16   ", 8);
	image[62] = 0xEB; // not bootable: the code is an endless loop
	image[63] = 0xFE;
	image[510] = 0x55;
	image[511] = 0xAA;

	uint8_t* fat = image + sectorSize;
	const size_t fatBytes = size_t(g.fatSectors) * sectorSize;
	uint8_t* root = fat + 2 * fatBytes;
	uint8_t* data = root + size_t(g.rootSectors) * sectorSize;
	const uint32_t endOfChain = g.bits == 12 ? 0xFFF : 0xFFFF;
	SetFatEntry(fat, g.bits, 0, g.bits == 12 ? 0xFF8 : 0xFFF8);
	SetFatEntry(fat, g.bits, 1, endOfChain);

	uint16_t date, time;
	uint8_t tenths;
	FatStamp(options.time, date, time, tenths);

	uint32_t entry = 0;
	if (labelLength != 0) {
		uint8_t* e = root;
		memcpy(e, labelBytes, 11);
		e[11] = 0x08;
		Put16(e + 22, time);
		Put16(e + 24, date);
		entry = 1;
	}

	uint32_t nextCluster = 2;
	for (size_t i = 0; i < count; i++) {
		const char* name = files[i].name;
		const size_t length = Length(name);
		uint8_t shortName[11];
		const bool exact = ExactShort(name, length, shortName);
		if (!exact) {
			// The alias must differ from every entry written so far and from
			// every 8.3 name still to come.
			for (uint32_t number = 1;; number++) {
				MakeAlias(name, length, number, shortName);
				bool taken = false;
				for (uint32_t k = 0; k < entry && !taken; k++) {
					const uint8_t* e = root + size_t(k) * 32;
					taken = e[11] != 0x0F && Same(e, shortName, 11);
				}
				for (size_t k = 0; k < count && !taken; k++) {
					uint8_t other[11];
					taken = ExactShort(files[k].name, Length(files[k].name), other)
						&& Same(other, shortName, 11);
				}
				if (!taken)
					break;
			}
			const uint32_t parts = uint32_t((length + 12) / 13);
			const uint8_t sum = ShortChecksum(shortName);
			static constexpr uint8_t kSlots[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20,
				22, 24, 28, 30};
			for (uint32_t part = parts; part >= 1; part--) {
				uint8_t* e = root + size_t(entry++) * 32;
				e[0] = uint8_t(part | (part == parts ? 0x40 : 0));
				e[11] = 0x0F;
				e[13] = sum;
				for (uint32_t slot = 0; slot < 13; slot++) {
					const size_t at = size_t(part - 1) * 13 + slot;
					const uint32_t unit = at < length ? uint8_t(name[at])
						: at == length ? 0 : 0xFFFF;
					Put16(e + kSlots[slot], unit);
				}
			}
		}
		const uint32_t clusters = uint32_t((uint64_t(files[i].size)
			+ clusterBytes - 1) / clusterBytes);
		uint8_t* e = root + size_t(entry++) * 32;
		memcpy(e, shortName, 11);
		e[11] = 0x20;
		e[13] = tenths;
		Put16(e + 14, time);
		Put16(e + 16, date);
		Put16(e + 18, date);
		Put16(e + 22, time);
		Put16(e + 24, date);
		Put16(e + 26, clusters != 0 ? nextCluster : 0);
		Put32(e + 28, uint32_t(files[i].size));
		if (clusters != 0) {
			memcpy(data + size_t(nextCluster - 2) * size_t(clusterBytes),
				files[i].data, files[i].size);
			for (uint32_t k = 0; k < clusters; k++)
				SetFatEntry(fat, g.bits, nextCluster + k,
					k + 1 == clusters ? endOfChain : nextCluster + k + 1);
			nextCluster += clusters;
		}
	}
	memcpy(fat + fatBytes, fat, fatBytes);

	if (info != nullptr) {
		info->fatBits = g.bits;
		info->sectorSize = sectorSize;
		info->sectorsPerCluster = g.sectorsPerCluster;
		info->totalSectors = g.totalSectors;
		info->fatSectors = g.fatSectors;
		info->rootEntriesUsed = entry;
		info->clusterCount = g.clusters;
		info->usedClusters = nextCluster - 2;
	}
	return Error::None;
}

// ---------------------------------------------------------------------------
// Part 2: GPT repair and extension
// ---------------------------------------------------------------------------

// A GUID in its on-disk byte order (the first three fields little endian).
struct Guid {
	uint8_t bytes[16];
};

// 0FC63DAF-8483-4772-8E79-3D69D8477DE4, Linux filesystem data.
constexpr Guid kTypeLinuxData = {{0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
	0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4}};
// EBD0A0A2-B9E5-4433-87C0-68B6B72699C7, Microsoft basic data.
constexpr Guid kTypeBasicData = {{0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
	0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7}};

struct Disk {
	void* context;
	uint32_t blockSize; // 512 or 4096
	uint64_t blocks;    // total LBAs of the target disk
	bool (*read)(void* context, uint64_t lba, uint8_t* out, size_t blockCount);
	bool (*write)(void* context, uint64_t lba, const uint8_t* data,
		size_t blockCount);
};

struct Partition {
	Guid type, unique;
	uint64_t firstLba, lastLba, attributes;
	char16_t name[37]; // NUL ended
	uint32_t index;    // position in the entry array
};

struct Info {
	uint32_t headerSize;
	uint32_t entryCount, entrySize;
	uint32_t usedCount;       // used entries in the array
	uint32_t firstFreeEntry;  // == entryCount when the array is full
	size_t partitionCount;    // partitions stored in the caller's array
	uint64_t entriesLba;
	uint64_t firstUsableLba, lastUsableLba;
	uint64_t alternateLba;    // where the header says its backup is
	uint64_t imageBlocks;     // alternateLba + 1: size of the written image
	uint64_t highestUsedLba;  // last LBA of the last partition; with no
	                          // partition, firstUsableLba - 1
	Guid diskGuid;
	bool backupAtEnd;         // alternateLba is the last LBA of this disk
	bool backupValid;         // header + array at alternateLba pass their CRCs
	bool protectiveMbr;       // LBA 0 has a 0xEE entry starting at LBA 1
	uint32_t mbrSizeField;    // its size field (0 without protectiveMbr)
	size_t workBytesNeeded;   // work buffer size for this table
};

// Work buffer for a table: one block for a header plus the whole entry array
// rounded up to blocks. 512-byte blocks and the usual 128 entries of 128
// bytes need 16896 bytes, 4096-byte blocks 20480.
inline size_t WorkBytes(uint32_t blockSize, uint32_t entryCount,
	uint32_t entrySize)
{
	const uint64_t bytes = uint64_t(entryCount) * entrySize;
	return size_t(blockSize + (bytes + blockSize - 1) / blockSize * blockSize);
}
// Enough for every table with up to 128 entries of 128 bytes.
constexpr size_t kWorkBytes = 4096 + 128 * 128;

namespace detail {

constexpr uint64_t kMaxEntryBytes = 1024 * 1024;

struct Header {
	uint32_t headerSize, entryCount, entrySize, entriesCrc;
	uint64_t myLba, alternateLba, firstUsable, lastUsable, entriesLba;
	uint64_t entryBlocks; // blocks of the entry array
};

inline bool ValidDisk(const Disk& disk)
{
	return (disk.blockSize == 512 || disk.blockSize == 4096)
		&& disk.read != nullptr && disk.write != nullptr && disk.blocks >= 8
		&& disk.blocks <= (uint64_t(1) << 60);
}

inline bool Signature(const uint8_t* p)
{
	static constexpr uint8_t kSignature[8] = {'E', 'F', 'I', ' ', 'P', 'A', 'R',
		'T'};
	return Same(p, kSignature, 8);
}

inline void SealHeader(uint8_t* block)
{
	Put32(block + 16, 0);
	Put32(block + 16, Crc32(block, U32(block + 12)));
}

inline Error ParseHeader(const uint8_t* block, uint32_t blockSize,
	uint64_t lba, Header& h)
{
	if (!Signature(block))
		return Error::NoGpt;
	h.headerSize = U32(block + 12);
	if (U32(block + 8) != 0x00010000 || h.headerSize < 92
		|| h.headerSize > blockSize)
		return Error::Layout;
	static constexpr uint8_t kZero[4] = {0, 0, 0, 0};
	uint32_t crc = Crc32Update(0xFFFFFFFFu, block, 16);
	crc = Crc32Update(crc, kZero, 4);
	crc = ~Crc32Update(crc, block + 20, h.headerSize - 20);
	if (crc != U32(block + 16))
		return Error::HeaderCrc;
	h.myLba = U64(block + 24);
	h.alternateLba = U64(block + 32);
	h.firstUsable = U64(block + 40);
	h.lastUsable = U64(block + 48);
	h.entriesLba = U64(block + 72);
	h.entryCount = U32(block + 80);
	h.entrySize = U32(block + 84);
	h.entriesCrc = U32(block + 88);
	if (h.myLba != lba || h.entrySize < 128 || h.entrySize > 4096
		|| (h.entrySize & (h.entrySize - 1)) != 0 || h.entryCount == 0
		|| uint64_t(h.entryCount) * h.entrySize > kMaxEntryBytes)
		return Error::Layout;
	h.entryBlocks = (uint64_t(h.entryCount) * h.entrySize + blockSize - 1)
		/ blockSize;
	return Error::None;
}

// Reads and validates the primary GPT: header block at work[0], entry array
// at work[blockSize]. `needed` is set as soon as the header is known.
inline Error Load(const Disk& disk, uint8_t* work, size_t workBytes, Header& h,
	size_t* needed)
{
	if (!ValidDisk(disk) || work == nullptr)
		return Error::Argument;
	const uint32_t bs = disk.blockSize;
	if (workBytes < bs)
		return Error::Buffer;
	if (!disk.read(disk.context, 1, work, 1))
		return Error::Read;
	Error error = ParseHeader(work, bs, 1, h);
	if (error == Error::NoGpt) {
		// The same table written for the other block size?
		if (bs == 4096) {
			if (!disk.read(disk.context, 0, work, 1))
				return Error::Read;
			if (Signature(work + 512))
				return Error::SectorSize;
		} else {
			if (!disk.read(disk.context, 8, work, 1))
				return Error::Read;
			if (Signature(work))
				return Error::SectorSize;
		}
		return Error::NoGpt;
	}
	if (error != Error::None)
		return error;
	const size_t need = WorkBytes(bs, h.entryCount, h.entrySize);
	if (needed != nullptr)
		*needed = need;
	if (h.entriesLba < 2 || h.entriesLba >= disk.blocks
		|| h.entryBlocks > disk.blocks - h.entriesLba
		|| h.firstUsable < 2 || h.firstUsable > h.lastUsable)
		return Error::Layout;
	if (workBytes < need)
		return Error::Buffer;
	if (!disk.read(disk.context, h.entriesLba, work + bs, size_t(h.entryBlocks)))
		return Error::Read;
	if (Crc32(work + bs, size_t(h.entryCount) * h.entrySize) != h.entriesCrc)
		return Error::EntriesCrc;
	return Error::None;
}

struct Scan {
	uint32_t used, firstFree;
	uint64_t highest;
	bool any;
};

inline Error ScanEntries(const uint8_t* entries, const Header& h, Scan& scan)
{
	scan.used = 0;
	scan.firstFree = h.entryCount;
	scan.highest = 0;
	scan.any = false;
	for (uint32_t i = 0; i < h.entryCount; i++) {
		const uint8_t* e = entries + size_t(i) * h.entrySize;
		if (AllZero(e, 16)) {
			if (scan.firstFree == h.entryCount)
				scan.firstFree = i;
			continue;
		}
		const uint64_t first = U64(e + 32), last = U64(e + 40);
		if (first > last)
			return Error::Layout;
		scan.used++;
		if (!scan.any || last > scan.highest)
			scan.highest = last;
		scan.any = true;
	}
	return Error::None;
}

inline bool BackupValid(const Disk& disk, const Header& primary, uint8_t* work)
{
	const uint32_t bs = disk.blockSize;
	if (primary.alternateLba <= 1 || primary.alternateLba >= disk.blocks)
		return false;
	if (!disk.read(disk.context, primary.alternateLba, work, 1))
		return false;
	Header backup;
	if (ParseHeader(work, bs, primary.alternateLba, backup) != Error::None)
		return false;
	if (backup.entryCount != primary.entryCount
		|| backup.entrySize != primary.entrySize || backup.entriesLba < 2
		|| backup.entriesLba >= disk.blocks
		|| backup.entryBlocks > disk.blocks - backup.entriesLba)
		return false;
	if (!disk.read(disk.context, backup.entriesLba, work + bs,
			size_t(backup.entryBlocks)))
		return false;
	return Crc32(work + bs, size_t(backup.entryCount) * backup.entrySize)
		== backup.entriesCrc;
}

// Sets the size of the protective entry; creates a protective MBR when LBA 0
// has none. Returns true when it had to be created.
inline bool PatchMbr(uint8_t* block, uint64_t blocks)
{
	const uint32_t size = blocks - 1 > 0xFFFFFFFFull ? 0xFFFFFFFFu
		: uint32_t(blocks - 1);
	if (block[510] == 0x55 && block[511] == 0xAA) {
		for (int i = 0; i < 4; i++) {
			uint8_t* e = block + 446 + 16 * i;
			if (e[4] == 0xEE && U32(e + 8) == 1) {
				Put32(e + 12, size);
				return false;
			}
		}
	}
	uint8_t* e = block + 446;
	memset(e, 0, 64);
	e[2] = 0x02;
	e[4] = 0xEE;
	e[5] = 0xFF;
	e[6] = 0xFF;
	e[7] = 0xFF;
	Put32(e + 8, 1);
	Put32(e + 12, size);
	block[510] = 0x55;
	block[511] = 0xAA;
	return true;
}

// Turns the primary header in `block` into the backup header or back.
inline void SetHeaderPlace(uint8_t* block, uint64_t myLba, uint64_t alternate,
	uint64_t entriesLba)
{
	Put64(block + 24, myLba);
	Put64(block + 32, alternate);
	Put64(block + 72, entriesLba);
	SealHeader(block);
}

} // namespace detail

// Validates the primary GPT (signature, header CRC32, entry array CRC32) and
// describes it. `work`: at least WorkBytes(blockSize, entryCount, entrySize)
// bytes (kWorkBytes is enough for the usual table); with a smaller buffer the
// result is Error::Buffer and only info.workBytesNeeded is set, provided the
// buffer held one block. `partitions` (may be null with capacity 0) receives
// the used entries in array order, at most `capacity`; info.usedCount is the
// real number. On error `info` is untouched except for workBytesNeeded.
// The disk is only read.
inline Error Inspect(const Disk& disk, uint8_t* work, size_t workBytes,
	Partition* partitions, size_t capacity, Info& info)
{
	using namespace detail;
	if (partitions == nullptr && capacity != 0)
		return Error::Argument;
	Header h;
	size_t needed = 0;
	Error error = Load(disk, work, workBytes, h, &needed);
	if (error != Error::None) {
		if (error == Error::Buffer && needed != 0)
			info.workBytesNeeded = needed;
		return error;
	}
	const uint32_t bs = disk.blockSize;
	const uint8_t* entries = work + bs;
	Scan scan;
	error = ScanEntries(entries, h, scan);
	if (error != Error::None)
		return error;

	Info result;
	memset(&result, 0, sizeof(result));
	result.headerSize = h.headerSize;
	result.entryCount = h.entryCount;
	result.entrySize = h.entrySize;
	result.usedCount = scan.used;
	result.firstFreeEntry = scan.firstFree;
	result.entriesLba = h.entriesLba;
	result.firstUsableLba = h.firstUsable;
	result.lastUsableLba = h.lastUsable;
	result.alternateLba = h.alternateLba;
	result.imageBlocks = h.alternateLba + 1;
	result.highestUsedLba = scan.any ? scan.highest : h.firstUsable - 1;
	memcpy(result.diskGuid.bytes, work + 56, 16);
	result.backupAtEnd = h.alternateLba == disk.blocks - 1;
	result.workBytesNeeded = needed;

	size_t stored = 0;
	for (uint32_t i = 0; i < h.entryCount && stored < capacity; i++) {
		const uint8_t* e = entries + size_t(i) * h.entrySize;
		if (AllZero(e, 16))
			continue;
		Partition& p = partitions[stored++];
		memcpy(p.type.bytes, e, 16);
		memcpy(p.unique.bytes, e + 16, 16);
		p.firstLba = U64(e + 32);
		p.lastLba = U64(e + 40);
		p.attributes = U64(e + 48);
		for (int k = 0; k < 36; k++)
			p.name[k] = char16_t(U16(e + 56 + 2 * k));
		p.name[36] = 0;
		p.index = i;
	}
	result.partitionCount = stored;

	// The checks below reuse the work buffer; the table is no longer needed.
	result.backupValid = BackupValid(disk, h, work);
	if (!disk.read(disk.context, 0, work, 1))
		return Error::Read;
	if (work[510] == 0x55 && work[511] == 0xAA) {
		for (int i = 0; i < 4 && !result.protectiveMbr; i++) {
			const uint8_t* e = work + 446 + 16 * i;
			if (e[4] == 0xEE && U32(e + 8) == 1) {
				result.protectiveMbr = true;
				result.mbrSizeField = U32(e + 12);
			}
		}
	}
	info = result;
	return Error::None;
}

// Optional fresh identities, so that two disks made from one image do not
// carry the same GUIDs. The caller supplies the random bytes (RFC 4122
// version 4 layout is the caller's business). `partitionGuids[i]` goes to the
// i-th USED entry in array order (Inspect() order) and there must be at least
// Info::usedCount of them. Note: a system that names its partitions by
// PARTUUID (kernel command line, fstab) stops booting when these change;
// Debian's cloud images use file system UUIDs.
struct RelocateOptions {
	const Guid* diskGuid;        // null: keep
	const Guid* partitionGuids;  // null: keep
	size_t partitionGuidCount;
};

struct RelocateResult {
	uint64_t lastUsableLba;
	uint64_t backupEntriesLba;
	uint64_t backupHeaderLba;
	uint32_t mbrSizeField;  // min(blocks - 1, 0xFFFFFFFF)
	bool mbrCreated;        // LBA 0 had no protective entry; one was written
};

// After an image smaller than (or equal to) the disk was written to its start:
// puts the backup entry array and backup header at the end of the disk, sets
// LastUsableLBA and AlternateLBA, recomputes both header CRCs (and the array
// CRC when GUIDs change) and sets the protective MBR size field (0xFFFFFFFF
// when the disk has more than 2^32 blocks, i.e. above 2 TiB at 512 bytes).
// Partitions are not moved or resized. Safe to repeat. Write order: see the
// top of this file. `work` as for Inspect(). Error::TooSmall when a partition
// would collide with the backup GPT. `result` is written only on success.
inline Error Relocate(const Disk& disk, uint8_t* work, size_t workBytes,
	const RelocateOptions* options = nullptr, RelocateResult* result = nullptr)
{
	using namespace detail;
	Header h;
	Error error = Load(disk, work, workBytes, h, nullptr);
	if (error != Error::None)
		return error;
	const uint32_t bs = disk.blockSize;
	uint8_t* entries = work + bs;
	Scan scan;
	error = ScanEntries(entries, h, scan);
	if (error != Error::None)
		return error;

	if (disk.blocks < h.entryBlocks + 4)
		return Error::TooSmall;
	const uint64_t backupHeader = disk.blocks - 1;
	const uint64_t backupEntries = backupHeader - h.entryBlocks;
	const uint64_t lastUsable = backupEntries - 1;
	if (lastUsable < h.firstUsable || (scan.any && scan.highest > lastUsable)
		|| h.entriesLba + h.entryBlocks > backupEntries)
		return Error::TooSmall;

	bool entriesChanged = false;
	if (options != nullptr) {
		if (options->diskGuid != nullptr && AllZero(options->diskGuid->bytes, 16))
			return Error::Argument;
		if (options->partitionGuids != nullptr) {
			if (options->partitionGuidCount < scan.used)
				return Error::Argument;
			for (uint32_t i = 0; i < scan.used; i++)
				if (AllZero(options->partitionGuids[i].bytes, 16))
					return Error::Argument;
			uint32_t used = 0;
			for (uint32_t i = 0; i < h.entryCount; i++) {
				uint8_t* e = entries + size_t(i) * h.entrySize;
				if (!AllZero(e, 16))
					memcpy(e + 16, options->partitionGuids[used++].bytes, 16);
			}
			entriesChanged = scan.used != 0;
		}
		if (options->diskGuid != nullptr)
			memcpy(work + 56, options->diskGuid->bytes, 16);
	}
	Put64(work + 48, lastUsable);
	if (entriesChanged)
		Put32(work + 88, Crc32(entries, size_t(h.entryCount) * h.entrySize));

	// 1-2: backup copy.
	SetHeaderPlace(work, backupHeader, 1, backupEntries);
	if (!disk.write(disk.context, backupEntries, entries, size_t(h.entryBlocks)))
		return Error::Write;
	if (!disk.write(disk.context, backupHeader, work, 1))
		return Error::Write;
	// 3-4: primary copy.
	SetHeaderPlace(work, 1, backupHeader, h.entriesLba);
	if (entriesChanged
		&& !disk.write(disk.context, h.entriesLba, entries, size_t(h.entryBlocks)))
		return Error::Write;
	if (!disk.write(disk.context, 1, work, 1))
		return Error::Write;
	// 5: protective MBR (the table is no longer needed in the buffer).
	if (!disk.read(disk.context, 0, work, 1))
		return Error::Read;
	const bool created = PatchMbr(work, disk.blocks);
	if (!disk.write(disk.context, 0, work, 1))
		return Error::Write;

	if (result != nullptr) {
		result->lastUsableLba = lastUsable;
		result->backupEntriesLba = backupEntries;
		result->backupHeaderLba = backupHeader;
		result->mbrSizeField = disk.blocks - 1 > 0xFFFFFFFFull ? 0xFFFFFFFFu
			: uint32_t(disk.blocks - 1);
		result->mbrCreated = created;
	}
	return Error::None;
}

enum class Placement : uint32_t {
	End,        // at the end of the usable area (root can still grow up to it)
	AfterLast,  // directly after the last used partition (blocks growpart)
	At, // r402: exactly at AddRequest::firstLba (a gap between partitions: dual boot)
};

struct AddRequest {
	Guid type;                // e.g. kTypeLinuxData
	Guid unique;              // random, supplied by the caller, not zero
	const char16_t* name;     // UTF-16, at most 36 units, null = no name
	uint64_t sizeBytes;       // rounded up to whole blocks
	uint64_t alignmentBytes;  // 0 = 1 MiB; a multiple of the block size
	Placement placement;
	uint64_t attributes;      // GPT attribute bits, normally 0
	uint64_t firstLba = 0; // Placement::At only
};

struct AddResult {
	uint64_t firstLba, lbaCount;
	uint32_t entryIndex;
};

// Adds one partition in the free space behind the last used partition (gaps
// between existing partitions are not used). The start is aligned; the size
// is exactly sizeBytes rounded up to blocks. Needs the backup GPT at the end
// of the disk (Error::NotRelocated otherwise: call Relocate() first). Takes
// the first free entry, rewrites both entry arrays and both headers with new
// CRCs in the order given at the top of this file. Nothing is written when an
// error other than Error::Write is returned; `result` only on success.
// `work` as for Inspect().
inline Error AddPartition(const Disk& disk, uint8_t* work, size_t workBytes,
	const AddRequest& request, AddResult& result)
{
	using namespace detail;
	Header h;
	Error error = Load(disk, work, workBytes, h, nullptr);
	if (error != Error::None)
		return error;
	const uint32_t bs = disk.blockSize;
	uint8_t* entries = work + bs;
	Scan scan;
	error = ScanEntries(entries, h, scan);
	if (error != Error::None)
		return error;

	const uint64_t align = request.alignmentBytes == 0 ? 1024 * 1024
		: request.alignmentBytes;
	size_t nameLength = 0;
	if (request.name != nullptr)
		while (request.name[nameLength] != 0 && nameLength <= 36)
			nameLength++;
	if (request.sizeBytes == 0 || align % bs != 0 || nameLength > 36
		|| request.sizeBytes > (uint64_t(1) << 62)
		|| AllZero(request.type.bytes, 16) || AllZero(request.unique.bytes, 16))
		return Error::Argument;
	for (uint32_t i = 0; i < h.entryCount; i++) {
		const uint8_t* e = entries + size_t(i) * h.entrySize;
		if (!AllZero(e, 16) && Same(e + 16, request.unique.bytes, 16))
			return Error::Argument;
	}

	const uint64_t backupHeader = disk.blocks - 1;
	if (h.alternateLba != backupHeader || h.entryBlocks + 2 > backupHeader
		|| h.lastUsable >= backupHeader - h.entryBlocks)
		return Error::NotRelocated;
	const uint64_t backupEntries = backupHeader - h.entryBlocks;
	if (scan.firstFree == h.entryCount)
		return Error::TableFull;

	const uint64_t alignLbas = align / bs;
	const uint64_t count = request.sizeBytes / bs
		+ (request.sizeBytes % bs != 0 ? 1 : 0);
	uint64_t lowest = h.firstUsable;
	if (scan.any && scan.highest + 1 > lowest)
		lowest = scan.highest + 1;
	if (request.placement != Placement::At && lowest > h.lastUsable)
		return Error::NoRoom;
	uint64_t first;
	if (request.placement == Placement::At) {
		first = request.firstLba;
		if (first < h.firstUsable || first > h.lastUsable
			|| count > h.lastUsable - first + 1)
			return Error::NoRoom;
		for (uint32_t i = 0; i < h.entryCount; i++) {
			const uint8_t* used = entries + size_t(i) * h.entrySize;
			if (AllZero(used, 16))
				continue;
			if (first <= U64(used + 40) && U64(used + 32) <= first + count - 1)
				return Error::NoRoom;
		}
	} else if (request.placement == Placement::End) {
		if (count > h.lastUsable - lowest + 1)
			return Error::NoRoom;
		first = h.lastUsable + 1 - count;
		first -= first % alignLbas;
		if (first < lowest)
			return Error::NoRoom;
	} else if (request.placement == Placement::AfterLast) {
		first = lowest + (alignLbas - 1);
		first -= first % alignLbas;
		if (first > h.lastUsable || count > h.lastUsable - first + 1)
			return Error::NoRoom;
	} else {
		return Error::Argument;
	}

	uint8_t* e = entries + size_t(scan.firstFree) * h.entrySize;
	memset(e, 0, h.entrySize);
	memcpy(e, request.type.bytes, 16);
	memcpy(e + 16, request.unique.bytes, 16);
	Put64(e + 32, first);
	Put64(e + 40, first + count - 1);
	Put64(e + 48, request.attributes);
	for (size_t i = 0; i < nameLength; i++)
		Put16(e + 56 + 2 * i, request.name[i]);
	Put32(work + 88, Crc32(entries, size_t(h.entryCount) * h.entrySize));

	SetHeaderPlace(work, backupHeader, 1, backupEntries);
	if (!disk.write(disk.context, backupEntries, entries, size_t(h.entryBlocks)))
		return Error::Write;
	if (!disk.write(disk.context, backupHeader, work, 1))
		return Error::Write;
	SetHeaderPlace(work, 1, backupHeader, h.entriesLba);
	if (!disk.write(disk.context, h.entriesLba, entries, size_t(h.entryBlocks)))
		return Error::Write;
	if (!disk.write(disk.context, 1, work, 1))
		return Error::Write;

	result.firstLba = first;
	result.lbaCount = count;
	result.entryIndex = scan.firstFree;
	return Error::None;
}

// Writes a memory image (the FAT volume) to the LBAs of a partition, at most
// 64 KiB per write call. The image may be shorter than the partition; a final
// partial block is padded with zeros through `work` (one block; `work` may be
// null when imageBytes is a multiple of the block size). The rest of the
// partition is not touched. The range must lie behind LBA 1 and inside the
// disk; nothing else is checked - pass what AddPartition() returned.
inline Error WriteImage(const Disk& disk, uint64_t firstLba, uint64_t lbaCount,
	const uint8_t* image, size_t imageBytes, uint8_t* work = nullptr,
	size_t workBytes = 0)
{
	if (!detail::ValidDisk(disk) || image == nullptr || imageBytes == 0)
		return Error::Argument;
	const uint32_t bs = disk.blockSize;
	const uint64_t whole = imageBytes / bs;
	const size_t rest = imageBytes % bs;
	if (firstLba < 2 || firstLba >= disk.blocks || lbaCount > disk.blocks - firstLba
		|| whole + (rest != 0 ? 1 : 0) > lbaCount)
		return Error::Argument;
	if (rest != 0 && (work == nullptr || workBytes < bs))
		return Error::Buffer;
	const uint64_t chunk = 65536 / bs;
	for (uint64_t done = 0; done < whole;) {
		const uint64_t now = whole - done < chunk ? whole - done : chunk;
		if (!disk.write(disk.context, firstLba + done, image + size_t(done) * bs,
				size_t(now)))
			return Error::Write;
		done += now;
	}
	if (rest != 0) {
		memset(work, 0, bs);
		memcpy(work, image + size_t(whole) * bs, rest);
		if (!disk.write(disk.context, firstLba + whole, work, 1))
			return Error::Write;
	}
	return Error::None;
}

// ---------------------------------------------------------------------------
// Part 3 (r409): placeholder partitions of a multi-system plan
// ---------------------------------------------------------------------------
// A plan reserves the range of every system before anything is installed
// (ReservePlaceholder), then, just before a system's step, either turns the
// placeholder into the partition its installer expects (RetypePartition) or
// deletes it so that the range is the one free gap on the disk
// (RemovePartition; FreeGaps counts the gaps). Every function validates
// everything first: nothing is written when an error other than Error::Write
// is returned, outputs are written only on success, and the write order is
// AddPartition()'s (backup array, backup header, primary array, primary
// header), so the primary table is valid before the third write and the
// complete backup is valid after the second.

// 8DA63339-0007-60C0-C436-083AC8230908, Linux reserved: the placeholder type.
constexpr Guid kTypeLinuxReserved = {{0x39, 0x33, 0xA6, 0x8D, 0x07, 0x00, 0xC0, 0x60,
	0xC4, 0x36, 0x08, 0x3A, 0xC8, 0x23, 0x09, 0x08}};
// C12A7328-F81F-11D2-BA4B-00A0C93EC93B, EFI system partition.
constexpr Guid kTypeEfiSystem = {{0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
	0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}};
// E3C9E316-0B5C-4DB8-817D-F92DF00215AE, Microsoft reserved.
constexpr Guid kTypeMicrosoftReserved = {{0x16, 0xE3, 0xC9, 0xE3, 0x5C, 0x0B, 0xB8, 0x4D,
	0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE}};
// DE94BBA4-06D1-4D40-A16A-BFD50179D6AC, Windows recovery environment.
constexpr Guid kTypeWindowsRecovery = {{0xA4, 0xBB, 0x94, 0xDE, 0xD1, 0x06, 0x40, 0x4D,
	0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC}};

namespace detail {

// Loads the table for a change: valid primary GPT, backup at the end.
inline Error LoadForChange(const Disk& disk, uint8_t* work, size_t workBytes,
	Header& h, Scan& scan)
{
	Error error = Load(disk, work, workBytes, h, nullptr);
	if (error != Error::None)
		return error;
	error = ScanEntries(work + disk.blockSize, h, scan);
	if (error != Error::None)
		return error;
	const uint64_t backupHeader = disk.blocks - 1;
	if (h.alternateLba != backupHeader || h.entryBlocks + 2 > backupHeader
		|| h.lastUsable >= backupHeader - h.entryBlocks)
		return Error::NotRelocated;
	return Error::None;
}

// Writes the changed entry array (work + blockSize) and header (work).
inline Error Commit(const Disk& disk, uint8_t* work, const Header& h)
{
	uint8_t* entries = work + disk.blockSize;
	const uint64_t backupHeader = disk.blocks - 1;
	const uint64_t backupEntries = backupHeader - h.entryBlocks;
	Put32(work + 88, Crc32(entries, size_t(h.entryCount) * h.entrySize));
	SetHeaderPlace(work, backupHeader, 1, backupEntries);
	if (!disk.write(disk.context, backupEntries, entries, size_t(h.entryBlocks)))
		return Error::Write;
	if (!disk.write(disk.context, backupHeader, work, 1))
		return Error::Write;
	SetHeaderPlace(work, 1, backupHeader, h.entriesLba);
	if (!disk.write(disk.context, h.entriesLba, entries, size_t(h.entryBlocks)))
		return Error::Write;
	if (!disk.write(disk.context, 1, work, 1))
		return Error::Write;
	return Error::None;
}

// The used entry with that unique GUID; null when there is none.
inline uint8_t* FindUnique(uint8_t* entries, const Header& h, const Guid& unique,
	uint32_t* index)
{
	for (uint32_t i = 0; i < h.entryCount; i++) {
		uint8_t* e = entries + size_t(i) * h.entrySize;
		if (!AllZero(e, 16) && Same(e + 16, unique.bytes, 16)) {
			if (index != nullptr)
				*index = i;
			return e;
		}
	}
	return nullptr;
}

inline size_t NameLength(const char16_t* name)
{
	size_t n = 0;
	if (name != nullptr)
		while (name[n] != 0 && n <= 36)
			n++;
	return n;
}

} // namespace detail

// Reserves exactly [firstLba, firstLba + lbaCount) as a named placeholder of
// type kTypeLinuxReserved. The name is required (it is what an abort shows the
// user: "Charon plan 2: Debian"); the range must be free and inside the usable
// area. Errors as AddPartition().
inline Error ReservePlaceholder(const Disk& disk, uint8_t* work, size_t workBytes,
	uint64_t firstLba, uint64_t lbaCount, const Guid& unique,
	const char16_t* name, AddResult& result)
{
	if (!detail::ValidDisk(disk) || name == nullptr || name[0] == 0 || lbaCount == 0
		|| lbaCount > (uint64_t(1) << 62) / disk.blockSize)
		return Error::Argument;
	AddRequest request;
	request.type = kTypeLinuxReserved;
	request.unique = unique;
	request.name = name;
	request.sizeBytes = lbaCount * disk.blockSize;
	request.alignmentBytes = disk.blockSize;
	request.placement = Placement::At;
	request.attributes = 0;
	request.firstLba = firstLba;
	return AddPartition(disk, work, workBytes, request, result);
}

// Changes the type GUID (and, with a name, the name) of the partition with
// that unique GUID. The partition must currently have `expectedType`
// (Error::Mismatch otherwise): a plan may only retype its own placeholder,
// never somebody else's partition. Place, size, unique GUID and attributes
// stay. `result` (may be null) describes the partition after the change.
inline Error RetypePartition(const Disk& disk, uint8_t* work, size_t workBytes,
	const Guid& unique, const Guid& expectedType, const Guid& newType,
	const char16_t* newName = nullptr, Partition* result = nullptr)
{
	using namespace detail;
	Header h;
	Scan scan;
	Error error = LoadForChange(disk, work, workBytes, h, scan);
	if (error != Error::None)
		return error;
	const size_t nameLength = NameLength(newName);
	if (AllZero(unique.bytes, 16) || AllZero(newType.bytes, 16)
		|| AllZero(expectedType.bytes, 16) || nameLength > 36)
		return Error::Argument;
	uint32_t index = 0;
	uint8_t* e = FindUnique(work + disk.blockSize, h, unique, &index);
	if (e == nullptr)
		return Error::NotFound;
	if (!Same(e, expectedType.bytes, 16))
		return Error::Mismatch;
	memcpy(e, newType.bytes, 16);
	if (newName != nullptr) {
		memset(e + 56, 0, 72);
		for (size_t i = 0; i < nameLength; i++)
			Put16(e + 56 + 2 * i, newName[i]);
	}
	Partition after;
	memcpy(after.type.bytes, e, 16);
	memcpy(after.unique.bytes, e + 16, 16);
	after.firstLba = U64(e + 32);
	after.lastLba = U64(e + 40);
	after.attributes = U64(e + 48);
	for (int k = 0; k < 36; k++)
		after.name[k] = char16_t(U16(e + 56 + 2 * k));
	after.name[36] = 0;
	after.index = index;
	error = Commit(disk, work, h);
	if (error != Error::None)
		return error;
	if (result != nullptr)
		*result = after;
	return Error::None;
}

// Removes the partition with that unique GUID from both tables; its blocks are
// not touched. It must have `expectedType`, and with expectedFirstLba != 0 it
// must start there (Error::Mismatch otherwise). `freed` (may be null) receives
// the range that became free.
inline Error RemovePartition(const Disk& disk, uint8_t* work, size_t workBytes,
	const Guid& unique, const Guid& expectedType, uint64_t expectedFirstLba = 0,
	AddResult* freed = nullptr)
{
	using namespace detail;
	Header h;
	Scan scan;
	Error error = LoadForChange(disk, work, workBytes, h, scan);
	if (error != Error::None)
		return error;
	if (AllZero(unique.bytes, 16) || AllZero(expectedType.bytes, 16))
		return Error::Argument;
	uint32_t index = 0;
	uint8_t* e = FindUnique(work + disk.blockSize, h, unique, &index);
	if (e == nullptr)
		return Error::NotFound;
	const uint64_t first = U64(e + 32), last = U64(e + 40);
	if (!Same(e, expectedType.bytes, 16)
		|| (expectedFirstLba != 0 && first != expectedFirstLba))
		return Error::Mismatch;
	memset(e, 0, h.entrySize);
	error = Commit(disk, work, h);
	if (error != Error::None)
		return error;
	if (freed != nullptr) {
		freed->firstLba = first;
		freed->lbaCount = last - first + 1;
		freed->entryIndex = index;
	}
	return Error::None;
}

struct Gap {
	uint64_t firstLba, lbaCount;
};

// Lists the free ranges of the usable area in disk order: each start rounded
// up to `alignmentBytes` (0 = 1 MiB), each length cut down to whole alignment
// units, ranges shorter than `minimumBytes` left out. `count` is the real
// number even when it exceeds `capacity` (then only `capacity` are stored).
// "Exactly one gap" for an installer that takes the biggest free space is
// count == 1 with minimumBytes set to what that installer would consider.
// The disk is only read. Outputs only on success.
inline Error FreeGaps(const Disk& disk, uint8_t* work, size_t workBytes,
	uint64_t alignmentBytes, uint64_t minimumBytes, Gap* gaps, size_t capacity,
	size_t& count)
{
	using namespace detail;
	if (gaps == nullptr && capacity != 0)
		return Error::Argument;
	Header h;
	Error error = Load(disk, work, workBytes, h, nullptr);
	if (error != Error::None)
		return error;
	const uint32_t bs = disk.blockSize;
	const uint8_t* entries = work + bs;
	Scan scan;
	error = ScanEntries(entries, h, scan);
	if (error != Error::None)
		return error;
	const uint64_t align = alignmentBytes == 0 ? 1024 * 1024 : alignmentBytes;
	if (align % bs != 0)
		return Error::Argument;
	const uint64_t alignLbas = align / bs;
	const uint64_t minLbas = minimumBytes / bs + (minimumBytes % bs != 0 ? 1 : 0);
	size_t found = 0;
	uint64_t position = h.firstUsable;
	while (position <= h.lastUsable) {
		// The partition that covers `position`, else the nearest one behind it.
		uint64_t next = h.lastUsable + 1;
		bool covered = false;
		for (uint32_t i = 0; i < h.entryCount; i++) {
			const uint8_t* e = entries + size_t(i) * h.entrySize;
			if (AllZero(e, 16))
				continue;
			const uint64_t first = U64(e + 32), last = U64(e + 40);
			if (first <= position && position <= last) {
				position = last + 1;
				covered = true;
				break;
			}
			if (first > position && first < next)
				next = first;
		}
		if (covered)
			continue;
		uint64_t first = position + (alignLbas - 1);
		first -= first % alignLbas;
		if (first < next) {
			uint64_t length = next - first;
			length -= length % alignLbas;
			if (length != 0 && length >= minLbas) {
				if (found < capacity) {
					gaps[found].firstLba = first;
					gaps[found].lbaCount = length;
				}
				found++;
			}
		}
		position = next;
	}
	count = found;
	return Error::None;
}

} // namespace hakefi::seeddisk
