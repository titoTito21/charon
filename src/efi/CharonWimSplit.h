// SPDX-License-Identifier: GPL-2.0-or-later
// Charon: splits one non-solid WIM, arriving as a sequential byte stream, into
// a spanned set (install.swm, install2.swm, ...) laid out the way wimlib's
// split.c/write.c do it. Resource bytes are copied raw, never recompressed.
// Pure logic: no allocation, the caller supplies every buffer.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace hakefi::wimsplit {
// The firmware build has no memcmp: a local one.
inline int BytesDiffer(const void* a,const void* b,size_t n) {
    const unsigned char* x=static_cast<const unsigned char*>(a);const unsigned char* y=static_cast<const unsigned char*>(b);
    for(size_t i=0;i<n;++i)if(x[i]!=y[i])return x[i]<y[i]?-1:1;
    return 0;
}


constexpr size_t kHeaderBytes = 208;
constexpr size_t kEntryBytes = 50;     // blob table entry
constexpr size_t kMinScratch = 208;    // smallest scratch Finish accepts
constexpr uint32_t kVersion = 0x10d00;
constexpr uint32_t kVersionSolid = 0xe00;
constexpr uint32_t kFlagReadOnly = 0x04, kFlagSpanned = 0x08,
	kFlagWriting = 0x40, kFlagRpFix = 0x80;
constexpr uint32_t kFlagCompressionMask = 0x00000002 | 0x00020000 | 0x00040000
	| 0x00080000 | 0x00200000;
constexpr uint8_t kResMetadata = 0x02, kResCompressed = 0x04, kResSolid = 0x10;

enum class Error : uint32_t {
	None,
	BadMagic,        // not a WIM
	Pipable,         // wimlib pipable WIM (WLPWM)
	BadHeaderSize,
	Solid,           // version 0xe00 (ESD) or a solid resource
	BadVersion,
	AlreadySplit,    // spanned flag, part/total != 1/1, or entry in another part
	WriteInProgress, // header says the WIM was not finished
	BadTable,        // blob table/XML compressed, misplaced or of a wrong size
	ImageCount,      // metadata resources do not match the header image count
	Overlap,         // two resources share bytes
	TooLarge,        // a resource (or part 1's fixed content) exceeds the limit
	Capacity,        // caller arrays too small, or more than 65535 parts
	Order,           // Feed went backwards or skipped resource bytes
	Incomplete,      // Finish before every resource byte was fed
	BadArgument,
	Sink,            // the sink refused a write
};

struct Reshdr { uint64_t size, offset, originalSize; uint8_t flags; };

struct Header {
	uint32_t version, flags, chunkSize, imageCount, bootIndex;
	uint16_t part, totalParts;
	uint8_t guid[16];
	Reshdr blobTable, xml, bootMetadata, integrity;
	Error error;
};

// One blob table entry. Sorted by original offset between MakePlan and Finish.
struct Resource {
	uint64_t offset;      // in the original WIM
	uint64_t size;        // stored (possibly compressed) size
	uint64_t partOffset;  // offset inside its part
	uint32_t part;        // 1-based
	uint32_t entry;       // index in the original blob table
};

struct Part {
	uint64_t size;        // final file size
	uint64_t tableOffset; // blob table; the XML follows it
	uint64_t tableBytes;
	uint32_t resources;
};

struct Plan {
	Header header;
	const uint8_t* blobTable; // original table; must stay valid until Finish
	Resource* resources;
	size_t resourceCount;
	Part* parts;
	uint32_t partCount;       // total number of parts
	uint64_t maxTableBytes;   // largest blob table of any part
	Error error;
};

using Sink = bool (*)(void* context, uint32_t part, uint64_t partOffset,
	const uint8_t* data, size_t size);

struct Writer {
	const Plan* plan;
	size_t index;
	uint64_t position;
	bool failed, finished;
	Error error;
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
inline void P16(uint8_t* p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
inline void P32(uint8_t* p, uint32_t v) { P16(p, v); P16(p + 2, v >> 16); }
inline void P64(uint8_t* p, uint64_t v) { P32(p, uint32_t(v)); P32(p + 4, uint32_t(v >> 32)); }

inline void GetReshdr(const uint8_t* p, Reshdr& r)
{
	r.size = U64(p) & 0x00ffffffffffffffull;
	r.flags = p[7];
	r.offset = U64(p + 8);
	r.originalSize = U64(p + 16);
}

inline void PutReshdr(uint8_t* p, uint64_t size, uint8_t flags, uint64_t offset)
{
	P64(p, size);
	p[7] = flags;
	P64(p + 8, offset);
	P64(p + 16, size);
}

struct ByOffset {
	bool operator()(const Resource& a, const Resource& b) const
	{
		return a.offset != b.offset ? a.offset < b.offset : a.size < b.size;
	}
};

struct ByPart {
	bool operator()(const Resource& a, const Resource& b) const
	{
		if (a.part != b.part)
			return a.part < b.part;
		return a.partOffset != b.partOffset ? a.partOffset < b.partOffset
			: a.size < b.size;
	}
};

// Heapsort: in place, no recursion.
template<typename Less>
inline void Sort(Resource* a, size_t count, Less less)
{
	auto sift = [&](size_t root, size_t end) {
		for (;;) {
			size_t child = root * 2 + 1;
			if (child >= end)
				return;
			if (child + 1 < end && less(a[child], a[child + 1]))
				child++;
			if (!less(a[root], a[child]))
				return;
			Resource t = a[root];
			a[root] = a[child];
			a[child] = t;
			root = child;
		}
	};
	for (size_t i = count / 2; i-- > 0;)
		sift(i, count);
	for (size_t end = count; end-- > 1;) {
		Resource t = a[0];
		a[0] = a[end];
		a[end] = t;
		sift(0, end);
	}
}

inline bool Fail(Plan& plan, Error error) { plan.error = error; return false; }
inline bool Fail(Writer& w, Error error)
{
	w.failed = true;
	w.error = error;
	return false;
}

} // namespace detail

// Parses the 208-byte header; on false, header.error says why it is refused.
inline bool ParseHeader(const uint8_t* h, Header& header)
{
	using namespace detail;
	memset(&header, 0, sizeof(header));
	auto fail = [&](Error e) { header.error = e; return false; };
	if (BytesDiffer(h, "WLPWM\0\0\0", 8) == 0)
		return fail(Error::Pipable);
	if (BytesDiffer(h, "MSWIM\0\0\0", 8) != 0)
		return fail(Error::BadMagic);
	if (U32(h + 8) != kHeaderBytes)
		return fail(Error::BadHeaderSize);
	header.version = U32(h + 12);
	header.flags = U32(h + 16);
	header.chunkSize = U32(h + 20);
	memcpy(header.guid, h + 24, 16);
	header.part = U16(h + 40);
	header.totalParts = U16(h + 42);
	header.imageCount = U32(h + 44);
	GetReshdr(h + 48, header.blobTable);
	GetReshdr(h + 72, header.xml);
	GetReshdr(h + 96, header.bootMetadata);
	header.bootIndex = U32(h + 120);
	GetReshdr(h + 124, header.integrity);
	if (header.version == kVersionSolid)
		return fail(Error::Solid);
	if (header.version != kVersion)
		return fail(Error::BadVersion);
	if ((header.flags & kFlagSpanned) != 0 || header.part != 1
		|| header.totalParts != 1)
		return fail(Error::AlreadySplit);
	if ((header.flags & kFlagWriting) != 0)
		return fail(Error::WriteInProgress);
	const Reshdr* tables[2] = {&header.blobTable, &header.xml};
	for (const Reshdr* t : tables) {
		if ((t->flags & (kResCompressed | kResSolid)) != 0
			|| t->size != t->originalSize || t->offset < kHeaderBytes)
			return fail(Error::BadTable);
	}
	if (header.blobTable.size % kEntryBytes != 0)
		return fail(Error::BadTable);
	return true;
}

// Assigns every resource of the original blob table to a part so that no part
// file (header + resources + its blob table + XML) exceeds partLimit bytes.
// Part 1 holds all metadata resources, placed right after its header.
// resourceCapacity >= blobTableBytes / 50; partCapacity >= resulting parts.
inline bool MakePlan(const Header& header, const uint8_t* blobTable,
	size_t blobTableBytes, uint64_t partLimit, Plan& plan,
	Resource* resources, size_t resourceCapacity, Part* parts,
	size_t partCapacity)
{
	using namespace detail;
	memset(&plan, 0, sizeof(plan));
	plan.header = header;
	plan.blobTable = blobTable;
	plan.resources = resources;
	plan.parts = parts;
	if (header.error != Error::None || blobTable == nullptr)
		return Fail(plan, Error::BadArgument);
	if (blobTableBytes != header.blobTable.size
		|| blobTableBytes % kEntryBytes != 0)
		return Fail(plan, Error::BadTable);
	const size_t count = blobTableBytes / kEntryBytes;
	if (count > resourceCapacity || count > 0xffffffffu || partCapacity == 0)
		return Fail(plan, Error::Capacity);
	const uint64_t xml = header.xml.size;
	const uint64_t fixed = kHeaderBytes + xml;
	uint64_t metadataEnd = kHeaderBytes;
	uint32_t images = 0;
	for (size_t i = 0; i < count; i++) {
		const uint8_t* e = blobTable + i * kEntryBytes;
		Reshdr r;
		GetReshdr(e, r);
		if ((r.flags & kResSolid) != 0)
			return Fail(plan, Error::Solid);
		if (U16(e + 24) != 1)
			return Fail(plan, Error::AlreadySplit);
		if (r.size != 0 && r.offset < kHeaderBytes)
			return Fail(plan, Error::BadTable);
		if (r.size > partLimit || r.offset + r.size < r.offset)
			return Fail(plan, Error::TooLarge);
		Resource& out = resources[i];
		out = {r.offset, r.size, 0, 0, uint32_t(i)};
		if ((r.flags & kResMetadata) != 0) {
			// Image order is table order; keep it in part 1.
			out.part = 1;
			out.partOffset = metadataEnd;
			metadataEnd += r.size;
			images++;
			if (metadataEnd > partLimit)
				return Fail(plan, Error::TooLarge);
		}
	}
	if (images != header.imageCount)
		return Fail(plan, Error::ImageCount);
	if (fixed > partLimit || metadataEnd + images * kEntryBytes + xml > partLimit)
		return Fail(plan, Error::TooLarge);
	Sort(resources, count, ByOffset());
	for (size_t i = 0; i + 1 < count; i++) {
		if (resources[i].offset + resources[i].size > resources[i + 1].offset)
			return Fail(plan, Error::Overlap);
	}
	uint32_t part = 1, inPart = images;
	uint64_t data = metadataEnd;
	auto close = [&]() {
		Part& p = parts[part - 1];
		p.tableOffset = data;
		p.tableBytes = uint64_t(inPart) * kEntryBytes;
		p.size = data + p.tableBytes + xml;
		p.resources = inPart;
		if (p.tableBytes > plan.maxTableBytes)
			plan.maxTableBytes = p.tableBytes;
	};
	for (size_t i = 0; i < count; i++) {
		Resource& r = resources[i];
		if (r.part == 1)
			continue; // metadata, already placed
		if (data + r.size + (uint64_t(inPart) + 1) * kEntryBytes + xml > partLimit) {
			if (fixed + r.size + kEntryBytes > partLimit)
				return Fail(plan, Error::TooLarge);
			if (part >= partCapacity || part >= 0xffff)
				return Fail(plan, Error::Capacity);
			close();
			part++;
			inPart = 0;
			data = kHeaderBytes;
		}
		r.part = part;
		r.partOffset = data;
		data += r.size;
		inPart++;
	}
	close();
	plan.resourceCount = count;
	plan.partCount = part;
	return true;
}

inline void Begin(Writer& writer, const Plan& plan)
{
	writer = {&plan, 0, 0, plan.error != Error::None || plan.partCount == 0,
		false, plan.error};
}

// First original-WIM offset that still has to be fed, or ~0 when none is left.
inline uint64_t NextNeeded(const Writer& w)
{
	for (size_t i = w.index; i < w.plan->resourceCount; i++) {
		const Resource& r = w.plan->resources[i];
		if (r.size != 0 && r.offset + r.size > w.position)
			return r.offset > w.position ? r.offset : w.position;
	}
	return ~uint64_t(0);
}

// Feeds bytes [wimOffset, wimOffset + size) of the original WIM. Offsets must
// not go backwards; ranges without resource bytes may be left out entirely.
inline bool Feed(Writer& w, uint64_t wimOffset, const uint8_t* data, size_t size,
	Sink sink, void* context)
{
	using namespace detail;
	if (w.failed || w.finished)
		return false;
	if (wimOffset < w.position)
		return Fail(w, Error::Order);
	const uint64_t end = wimOffset + size;
	while (w.index < w.plan->resourceCount) {
		const Resource& r = w.plan->resources[w.index];
		const uint64_t want = r.offset > w.position ? r.offset : w.position;
		if (r.size == 0 || want >= r.offset + r.size) {
			w.index++;
			continue;
		}
		if (want < wimOffset)
			return Fail(w, Error::Order); // resource bytes were skipped
		if (want >= end)
			break;
		const uint64_t last = r.offset + r.size < end ? r.offset + r.size : end;
		if (!sink(context, r.part, r.partOffset + (want - r.offset),
				data + (want - wimOffset), size_t(last - want)))
			return Fail(w, Error::Sink);
		w.position = last;
	}
	w.position = end;
	return true;
}

// Writes every part's blob table, the XML and the header (at offset 0).
// scratch: any size >= kMinScratch; tables are emitted in scratch-sized pieces
// (one piece per part when scratchBytes >= plan.maxTableBytes).
// Reorders plan.resources; the writer cannot be fed afterwards.
inline bool Finish(Writer& w, const uint8_t* xml, size_t xmlBytes,
	const uint8_t guid16[16], uint8_t* scratch, size_t scratchBytes, Sink sink,
	void* context)
{
	using namespace detail;
	if (w.failed || w.finished)
		return false;
	const Plan& plan = *w.plan;
	if (scratch == nullptr || scratchBytes < kMinScratch
		|| xmlBytes != plan.header.xml.size)
		return Fail(w, Error::BadArgument);
	if (NextNeeded(w) != ~uint64_t(0))
		return Fail(w, Error::Incomplete);
	w.finished = true;
	Sort(plan.resources, plan.resourceCount, ByPart());
	size_t next = 0;
	for (uint32_t part = 1; part <= plan.partCount; part++) {
		const Part& p = plan.parts[part - 1];
		uint64_t at = p.tableOffset;
		size_t used = 0;
		for (uint32_t i = 0; i < p.resources; i++, next++) {
			const Resource& r = plan.resources[next];
			if (next >= plan.resourceCount || r.part != part)
				return Fail(w, Error::BadArgument);
			if (used + kEntryBytes > scratchBytes) {
				if (!sink(context, part, at, scratch, used))
					return Fail(w, Error::Sink);
				at += used;
				used = 0;
			}
			uint8_t* e = scratch + used;
			memcpy(e, plan.blobTable + size_t(r.entry) * kEntryBytes, kEntryBytes);
			P64(e + 8, r.partOffset);
			P16(e + 24, part);
			used += kEntryBytes;
		}
		if (used != 0 && !sink(context, part, at, scratch, used))
			return Fail(w, Error::Sink);
		const uint64_t xmlOffset = p.tableOffset + p.tableBytes;
		if (xmlBytes != 0 && !sink(context, part, xmlOffset, xml, xmlBytes))
			return Fail(w, Error::Sink);
		uint8_t* h = scratch;
		memset(h, 0, kHeaderBytes);
		memcpy(h, "MSWIM\0\0\0", 8);
		P32(h + 8, kHeaderBytes);
		P32(h + 12, plan.header.version);
		P32(h + 16, (plan.header.flags
			& (kFlagRpFix | kFlagReadOnly | kFlagCompressionMask)) | kFlagSpanned);
		P32(h + 20, plan.header.chunkSize);
		memcpy(h + 24, guid16, 16);
		P16(h + 40, part);
		P16(h + 42, plan.partCount);
		P32(h + 44, plan.header.imageCount);
		PutReshdr(h + 48, p.tableBytes, kResMetadata, p.tableOffset);
		PutReshdr(h + 72, xmlBytes, kResMetadata, xmlOffset);
		// Like wimlib: no boot index, boot metadata or integrity table in a set.
		if (!sink(context, part, 0, h, kHeaderBytes))
			return Fail(w, Error::Sink);
	}
	return true;
}

} // namespace hakefi::wimsplit
