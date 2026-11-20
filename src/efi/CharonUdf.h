// SPDX-License-Identifier: GPL-2.0-or-later
// Charon: read-only UDF reader (ECMA-167 / OSTA UDF 1.02-2.60 as written by
// oscdimg and IMAPI2) that maps every file to byte ranges of the image, and a
// router that hands a sequentially streamed image to the files it contains.
// Pure logic: no allocation, the caller supplies every buffer.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace hakefi::udf {
// The firmware build has no memcmp: a local one.
inline int BytesDiffer(const void* a,const void* b,size_t n) {
    const unsigned char* x=static_cast<const unsigned char*>(a);const unsigned char* y=static_cast<const unsigned char*>(b);
    for(size_t i=0;i<n;++i)if(x[i]!=y[i])return x[i]<y[i]?-1:1;
    return 0;
}


constexpr uint32_t kBlock = 2048;     // the only logical block size accepted
constexpr size_t kPathChars = 260;

enum class Error : uint32_t {
	None,
	Read,         // the source failed
	NoAnchor,     // no anchor volume descriptor pointer at sector 256
	NoVolume,     // no usable partition / logical volume descriptor
	BlockSize,    // logical block size is not 2048
	Partition,    // unsupported partition map or reference outside the maps
	FileSet,      // file set descriptor missing
	Icb,          // bad or unsupported (extended) file entry
	Extents,      // allocation descriptors inconsistent with the file size
	Directory,    // malformed file identifier descriptor
	PathTooLong,  // path does not fit File::path (also stops directory loops)
	Overflow,     // files or extents array too small
};

struct Source {
	void* context;
	bool (*read)(void* context, uint64_t offset, uint8_t* out, size_t size);
};

// Byte range of the image that belongs to file index `file`. Scan() appends a
// file's extents in file order; a zero extent has no image bytes (sparse).
struct Extent {
	uint64_t offset, length;
	uint64_t fileOffset;
	uint32_t file;
	bool zero;
};

struct File {
	char16_t path[kPathChars]; // relative to the root, '\\' separated, NUL ended
	uint64_t size;             // 0 for directories
	uint64_t icb;              // internal: partition reference << 32 | block
	bool directory;
};

struct Map {
	File* files;
	size_t fileCount, fileCapacity;
	Extent* extents;
	size_t extentCount, extentCapacity;
	bool overflow;
	Error error;
};

using Sink = bool (*)(void* context, uint32_t file, uint64_t fileOffset,
	const uint8_t* data, size_t size);

struct Router {
	const Map* map;
	size_t index;
	uint64_t position;
	bool failed;
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

// Descriptor tag: identifier and header checksum.
inline bool Tag(const uint8_t* p, uint16_t id)
{
	uint8_t sum = 0;
	for (int i = 0; i < 16; i++)
		sum = uint8_t(sum + (i == 4 ? 0 : p[i]));
	return U16(p) == id && sum == p[4];
}

struct Volume {
	const Source* source;
	Map* map;
	struct { uint16_t number; uint32_t start; } partitions[4];
	uint32_t partitionCount;
	// kind 1: physical (type 1 or sparable), 2: UDF 2.50 metadata partition
	struct { uint8_t kind; uint16_t number; uint32_t metadataFile; } maps[4];
	uint32_t mapCount;
	struct { uint64_t fileOffset, length, offset; } metadata[16];
	uint32_t metadataCount;
	uint8_t icb[kBlock];   // file entry / allocation extent being walked
	uint8_t data[kBlock];  // descriptor or directory block
};

inline bool Fail(Volume& v, Error error)
{
	if (v.map->error == Error::None)
		v.map->error = error;
	if (error == Error::Overflow)
		v.map->overflow = true;
	return false;
}

inline bool Read(Volume& v, uint64_t offset, uint8_t* out, size_t size)
{
	return v.source->read(v.source->context, offset, out, size)
		|| Fail(v, Error::Read);
}

// Image offset of a block of a partition; `run` = contiguous bytes from there.
inline bool Resolve(Volume& v, uint32_t part, uint32_t block, uint64_t& offset,
	uint64_t& run)
{
	if (part >= v.mapCount)
		return Fail(v, Error::Partition);
	if (v.maps[part].kind == 2) {
		const uint64_t at = uint64_t(block) * kBlock;
		for (uint32_t i = 0; i < v.metadataCount; i++) {
			const auto& m = v.metadata[i];
			if (at >= m.fileOffset && at - m.fileOffset < m.length) {
				offset = m.offset + (at - m.fileOffset);
				run = m.length - (at - m.fileOffset);
				return true;
			}
		}
		return Fail(v, Error::Partition);
	}
	for (uint32_t i = 0; i < v.partitionCount; i++) {
		if (v.partitions[i].number == v.maps[part].number) {
			offset = (uint64_t(v.partitions[i].start) + block) * kBlock;
			run = ~uint64_t(0) - offset;
			return true;
		}
	}
	return Fail(v, Error::Partition);
}

// Walks the data of the (extended) file entry at `ref` in file order, calling
// visit(fileOffset, imageOffset, length, zero) for each piece.
template<typename Visitor>
inline bool Walk(Volume& v, uint64_t ref, Visitor& visit)
{
	uint32_t part = uint32_t(ref >> 32);
	uint64_t at, run;
	if (!Resolve(v, part, uint32_t(ref), at, run) || !Read(v, at, v.icb, kBlock))
		return false;
	const uint8_t* b = v.icb;
	const bool extended = Tag(b, 266);
	if ((!extended && !Tag(b, 261)) || U16(b + 20) != 4)
		return Fail(v, Error::Icb);
	const uint32_t kind = U16(b + 34) & 7;
	const uint64_t size = U64(b + 56);
	const uint32_t base = extended ? 216 : 176;
	uint64_t p = uint64_t(base) + U32(b + base - 8);
	uint64_t end = p + U32(b + base - 4);
	if (end > kBlock || kind == 2)
		return Fail(v, Error::Icb);
	if (kind == 3) { // data embedded in the ICB
		if (size > end - p)
			return Fail(v, Error::Extents);
		return size == 0 || visit(0, at + p, size, false);
	}
	const uint32_t step = kind == 0 ? 8 : 16;
	uint64_t done = 0;
	for (uint32_t guard = 0; p + step <= end && done < size;) {
		const uint32_t raw = U32(b + p);
		uint32_t block = U32(b + p + 4);
		if (kind == 1)
			part = U16(b + p + 8);
		p += step;
		uint64_t length = raw & 0x3fffffffu;
		if (length == 0)
			break;
		if ((raw >> 30) == 3) { // the list continues in an allocation extent
			if (++guard > 65536 || !Resolve(v, part, block, at, run)
				|| !Read(v, at, v.icb, kBlock))
				return Fail(v, Error::Extents);
			p = 24;
			end = p + U32(b + 20);
			if (!Tag(b, 258) || end > kBlock)
				return Fail(v, Error::Extents);
			continue;
		}
		if (length > size - done)
			length = size - done;
		if ((raw >> 30) != 0) { // allocated-unrecorded or sparse: reads as zeros
			if (!visit(done, uint64_t(0), length, true))
				return false;
			done += length;
			continue;
		}
		while (length != 0) {
			if (!Resolve(v, part, block, at, run))
				return false;
			const uint64_t n = length < run ? length : run;
			if (!visit(done, at, n, false))
				return false;
			done += n;
			length -= n;
			block += uint32_t(n / kBlock);
		}
	}
	return done == size || Fail(v, Error::Extents);
}

struct AddExtents {
	Volume& v;
	uint32_t file;
	bool operator()(uint64_t fileOffset, uint64_t offset, uint64_t length, bool zero)
	{
		Map& m = *v.map;
		if (m.extentCount != 0) { // merge with a directly preceding piece
			Extent& last = m.extents[m.extentCount - 1];
			if (last.file == file && last.zero == zero
				&& last.fileOffset + last.length == fileOffset
				&& (zero || last.offset + last.length == offset)) {
				last.length += length;
				return true;
			}
		}
		if (m.extentCount >= m.extentCapacity)
			return Fail(v, Error::Overflow);
		m.extents[m.extentCount++] = {offset, length, fileOffset, file, zero};
		return true;
	}
};

struct AddMetadata {
	Volume& v;
	bool operator()(uint64_t fileOffset, uint64_t offset, uint64_t length, bool zero)
	{
		if (zero || v.metadataCount >= 16)
			return Fail(v, Error::Partition);
		v.metadata[v.metadataCount].fileOffset = fileOffset;
		v.metadata[v.metadataCount].length = length;
		v.metadata[v.metadataCount++].offset = offset;
		return true;
	}
};

// Reassembles file identifier descriptors from directory data and appends the
// children to the map.
struct ReadDirectory {
	Volume& v;
	const char16_t* parent; // nullptr for the root
	size_t have = 0, total = 0;
	uint8_t fid[kBlock];

	ReadDirectory(Volume& volume, const char16_t* path) : v(volume), parent(path) {}

	bool Entry()
	{
		const uint32_t nameBytes = fid[19];
		if ((fid[18] & 0x0c) != 0 || nameBytes < 2)
			return true; // deleted, or the parent entry
		Map& m = *v.map;
		if (m.fileCount >= m.fileCapacity)
			return Fail(v, Error::Overflow);
		File& f = m.files[m.fileCount];
		size_t n = 0;
		for (; parent != nullptr && parent[n] != 0; n++)
			f.path[n] = parent[n];
		if (n != 0)
			f.path[n++] = u'\\';
		// OSTA compressed unicode: 8 or 16 (big endian) bits per character.
		const uint8_t* name = fid + 38 + U16(fid + 36);
		const uint32_t width = name[0] == 8 ? 1 : name[0] == 16 ? 2 : 0;
		if (width == 0)
			return Fail(v, Error::Directory);
		for (uint32_t i = 1; i + width <= nameBytes; i += width) {
			if (n + 1 >= kPathChars)
				return Fail(v, Error::PathTooLong);
			f.path[n++] = width == 1 ? char16_t(name[i])
				: char16_t((name[i] << 8) | name[i + 1]);
		}
		f.path[n] = 0;
		f.icb = (uint64_t(U16(fid + 28)) << 32) | U32(fid + 24);
		uint8_t head[64];
		uint64_t at, run;
		if (!Resolve(v, U16(fid + 28), U32(fid + 24), at, run)
			|| !Read(v, at, head, sizeof(head)))
			return false;
		if (!Tag(head, 261) && !Tag(head, 266))
			return Fail(v, Error::Icb);
		if (head[27] != 4 && head[27] != 5)
			return true; // neither directory nor regular file: not reported
		f.directory = head[27] == 4;
		f.size = f.directory ? 0 : U64(head + 56);
		m.fileCount++;
		return true;
	}

	bool operator()(uint64_t, uint64_t offset, uint64_t length, bool zero)
	{
		if (zero)
			return Fail(v, Error::Directory);
		while (length != 0) {
			const size_t n = size_t(length < kBlock ? length : kBlock);
			if (!Read(v, offset, v.data, n))
				return false;
			for (size_t i = 0; i < n;) {
				const size_t need = (total != 0 ? total : 38) - have;
				const size_t take = need < n - i ? need : n - i;
				memcpy(fid + have, v.data + i, take);
				have += take;
				i += take;
				if (total == 0 && have == 38) {
					total = (38 + size_t(U16(fid + 36)) + fid[19] + 3) & ~size_t(3);
					if (!Tag(fid, 257) || total > sizeof(fid))
						return Fail(v, Error::Directory);
				}
				if (have == total) {
					if (!Entry())
						return false;
					have = total = 0;
				}
			}
			offset += n;
			length -= n;
		}
		return true;
	}
};

// Volume descriptor sequence: partition descriptors and the logical volume.
inline bool ReadSequence(Volume& v, uint32_t first, uint32_t bytes, uint64_t& fileSet)
{
	bool found = false;
	v.partitionCount = v.mapCount = 0;
	for (uint32_t i = 0; i < bytes / kBlock && i < 256; i++) {
		uint8_t* d = v.data;
		if (!Read(v, (uint64_t(first) + i) * kBlock, d, kBlock))
			return false;
		if (Tag(d, 8))
			break;
		if (Tag(d, 5) && v.partitionCount < 4) {
			v.partitions[v.partitionCount].number = U16(d + 22);
			v.partitions[v.partitionCount++].start = U32(d + 188);
		}
		if (!Tag(d, 6))
			continue;
		if (U32(d + 212) != kBlock)
			return Fail(v, Error::BlockSize);
		fileSet = (uint64_t(U16(d + 256)) << 32) | U32(d + 252);
		const uint32_t count = U32(d + 268);
		uint32_t at = 440;
		for (uint32_t m = 0; m < count; m++) {
			if (m >= 4 || at + 2 > kBlock || d[at + 1] < 6 || at + d[at + 1] > kBlock)
				return Fail(v, Error::Partition);
			const uint8_t* map = d + at;
			if (map[0] == 1) {
				v.maps[m] = {1, U16(map + 4), 0};
			} else if (map[0] == 2 && map[1] == 64
				&& BytesDiffer(map + 5, "*UDF Metadata Partition", 23) == 0) {
				v.maps[m] = {2, U16(map + 38), U32(map + 40)};
			} else if (map[0] == 2 && map[1] == 64
				&& BytesDiffer(map + 5, "*UDF Sparable Partition", 23) == 0) {
				v.maps[m] = {1, U16(map + 38), 0}; // sparing table ignored
			} else {
				return Fail(v, Error::Partition); // e.g. virtual (VAT) partition
			}
			at += map[1];
		}
		v.mapCount = count;
		found = true;
	}
	return found && v.partitionCount != 0;
}

template<typename Less>
inline void Sort(Extent* a, size_t count, Less less) // heapsort, no recursion
{
	auto sift = [&](size_t root, size_t end) {
		for (size_t child; (child = root * 2 + 1) < end; root = child) {
			if (child + 1 < end && less(a[child], a[child + 1]))
				child++;
			if (!less(a[root], a[child]))
				return;
			Extent t = a[root];
			a[root] = a[child];
			a[child] = t;
		}
	};
	for (size_t i = count / 2; i-- > 0;)
		sift(i, count);
	for (size_t end = count; end-- > 1;) {
		Extent t = a[0];
		a[0] = a[end];
		a[end] = t;
		sift(0, end);
	}
}

} // namespace detail

// Fills `map` with every directory and regular file of the UDF volume. On
// false, map.error tells why (map.overflow: the caller's arrays are too small).
inline bool Scan(const Source& source, Map& map)
{
	using namespace detail;
	map.fileCount = map.extentCount = 0;
	map.overflow = false;
	map.error = Error::None;
	Volume v;
	v.source = &source;
	v.map = &map;
	v.metadataCount = 0;
	if (!Read(v, 256ull * kBlock, v.icb, kBlock))
		return false;
	if (!Tag(v.icb, 2))
		return Fail(v, Error::NoAnchor);
	// Main volume descriptor sequence, then the reserve one.
	const uint32_t sequence[4] = {U32(v.icb + 20), U32(v.icb + 16), U32(v.icb + 28),
		U32(v.icb + 24)};
	uint64_t fileSet = 0;
	if (!ReadSequence(v, sequence[0], sequence[1], fileSet)) {
		if (map.error != Error::None && map.error != Error::Read)
			return false;
		map.error = Error::None;
		if (!ReadSequence(v, sequence[2], sequence[3], fileSet))
			return Fail(v, Error::NoVolume);
	}
	for (uint32_t m = 0; m < v.mapCount; m++) {
		if (v.maps[m].kind != 2)
			continue;
		uint32_t physical = 0;
		while (physical < v.mapCount && (v.maps[physical].kind != 1
			|| v.maps[physical].number != v.maps[m].number))
			physical++;
		AddMetadata add{v};
		if (v.metadataCount != 0 || !Walk(v, (uint64_t(physical) << 32)
				| v.maps[m].metadataFile, add))
			return Fail(v, Error::Partition);
	}
	uint64_t at, run;
	if (!Resolve(v, uint32_t(fileSet >> 32), uint32_t(fileSet), at, run)
		|| !Read(v, at, v.data, kBlock))
		return false;
	if (!Tag(v.data, 256))
		return Fail(v, Error::FileSet);
	const uint64_t root = (uint64_t(U16(v.data + 408)) << 32) | U32(v.data + 404);
	{
		ReadDirectory reader(v, nullptr);
		if (!Walk(v, root, reader))
			return false;
	}
	// Breadth first: the files array itself is the queue of directories.
	for (size_t i = 0; i < map.fileCount; i++) {
		const File& f = map.files[i];
		if (f.directory) {
			ReadDirectory reader(v, f.path);
			if (!Walk(v, f.icb, reader))
				return false;
		} else {
			AddExtents add{v, uint32_t(i)};
			if (!Walk(v, f.icb, add))
				return false;
		}
	}
	return true;
}

// Orders the extents by image offset, as Router needs them.
inline void SortByOffset(Map& map)
{
	detail::Sort(map.extents, map.extentCount, [](const Extent& a, const Extent& b) {
		return a.zero != b.zero ? a.zero : a.offset < b.offset;
	});
}

// Starts routing; `map` must be sorted with SortByOffset and stay unchanged.
inline void Begin(Router& router, const Map& map)
{
	router = {&map, 0, 0, false};
	for (size_t i = 0; i + 1 < map.extentCount; i++) {
		const Extent& a = map.extents[i];
		const Extent& b = map.extents[i + 1];
		if (!a.zero && (b.zero || a.offset + a.length > b.offset))
			router.failed = true; // unsorted or overlapping
	}
}

// First image offset that still has to be fed, or ~0 when nothing is left.
inline uint64_t NextNeeded(const Router& r)
{
	for (size_t i = r.index; i < r.map->extentCount; i++) {
		const Extent& e = r.map->extents[i];
		if (!e.zero && e.offset + e.length > r.position)
			return e.offset > r.position ? e.offset : r.position;
	}
	return ~uint64_t(0);
}

// Feeds image bytes [offset, offset + size). Offsets must not go backwards;
// ranges holding no file data may be left out. Each file byte reaches the sink
// exactly once. Zero extents are never delivered: the caller zero-fills them.
inline bool Feed(Router& r, uint64_t offset, const uint8_t* data, size_t size,
	Sink sink, void* context)
{
	if (r.failed || offset < r.position)
		return !(r.failed = true);
	const uint64_t end = offset + size;
	while (r.index < r.map->extentCount) {
		const Extent& e = r.map->extents[r.index];
		const uint64_t want = e.offset > r.position ? e.offset : r.position;
		if (e.zero || want >= e.offset + e.length) {
			r.index++;
			continue;
		}
		if (want < offset)
			return !(r.failed = true); // file bytes were skipped
		if (want >= end)
			break;
		const uint64_t last = e.offset + e.length < end ? e.offset + e.length : end;
		if (!sink(context, e.file, e.fileOffset + (want - e.offset),
				data + (want - offset), size_t(last - want)))
			return !(r.failed = true);
		r.position = last;
	}
	r.position = end;
	return true;
}

} // namespace hakefi::udf
