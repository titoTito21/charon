// SPDX-License-Identifier: GPL-2.0-or-later
// Charon: read-only qcow2 (version 2 and 3) reader that converts an image to a
// raw disk. Written from the format description in QEMU's
// docs/interop/qcow2.txt. Pure logic: no allocation, the caller supplies every
// buffer; all file access goes through a random-access Source callback.
//
// The source is expected to be slow (HTTP range requests), so reads are few:
// the header once, the active L1 table once, every L2 table once, and data in
// ascending guest order with host-contiguous clusters and neighbouring
// compressed clusters fetched in one read as far as the buffers allow.
//
// Not supported, and refused by Open: encryption, backing files, external
// data files, extended L2 entries (subclusters), images marked corrupt,
// unknown incompatible features and any compression type but zlib.
// Snapshots are ignored: only the active L1 table is read.
//
// Work memory (see Work):
//   l1          L1Bytes(image)       = image.l1Entries * 8
//   l2          image.clusterSize
//   cluster     >= image.clusterSize; a multiple (e.g. 1 MiB) lets runs of
//               host-contiguous clusters arrive in one read and one write
//   compressed  >= the stored size of the largest compressed cluster.
//               2 * image.clusterSize is always enough (the format's limit);
//               more (e.g. 1 MiB) batches neighbouring compressed clusters.
//               Unused when the image has no compressed clusters.
//   decoder     one inflate::Decoder (about 4 KiB), only for compressed
//               clusters
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "CharonInflate.h"

namespace hakefi::qcow2 {

constexpr uint32_t kMagic = 0x514649fb; // "QFI\xfb"
constexpr uint32_t kMinClusterBits = 9, kMaxClusterBits = 21;
constexpr uint64_t kIncompatDirty = 1, kIncompatCorrupt = 2,
	kIncompatExternalData = 4, kIncompatCompression = 8,
	kIncompatExtendedL2 = 16;
constexpr uint64_t kOffsetMask = 0x00fffffffffffe00ull; // bits 9..55
constexpr uint64_t kFlagCompressed = 1ull << 62;
constexpr uint64_t kFlagZero = 1;

enum class Error : uint32_t {
	None,
	BadArgument,
	Read,            // the source refused a read
	BadMagic,        // not a qcow2 file
	BadVersion,      // neither 2 nor 3
	BadClusterBits,  // outside 9..21
	Encrypted,
	BackingFile,
	ExternalData,
	ExtendedL2,
	MarkedCorrupt,   // the image's own "corrupt" bit is set
	UnknownFeature,  // an incompatible feature bit this reader does not know
	Compression,     // compression type other than zlib (e.g. zstd)
	BadHeader,       // inconsistent header fields
	BufferTooSmall,  // a Work buffer is too small (or missing) for this image
	BadTable,        // invalid L1 or L2 entry
	BadCompressed,   // a compressed cluster does not inflate to one cluster
	Sink,            // the sink or the zero hook refused
	Cancelled,       // the progress hook returned false
};

struct Source {
	void* context;
	bool (*read)(void* context, uint64_t offset, uint8_t* out, size_t size);
};

struct Image {
	uint64_t virtualSize;   // size of the raw disk in bytes
	uint32_t version;       // 2 or 3
	uint32_t clusterBits;
	uint64_t clusterSize;
	uint32_t l2Entries;     // entries per L2 table
	uint32_t l1Entries;     // L1 entries needed for virtualSize
	uint64_t l1Offset;
	uint64_t incompatible;  // raw feature bits
	bool dirty;             // not closed cleanly; still readable
	Error error;
};

// Called for allocated clusters only, in ascending guest order, never twice
// for the same byte and never past virtualSize.
struct Sink {
	void* context;
	bool (*write)(void* context, uint64_t guestOffset, const uint8_t* data,
		size_t size);
};

// Both optional. zero() reports every range the sink is not called for:
// allocated == false for unallocated clusters, true for clusters with the
// zero flag. Either way the guest reads zeros there. progress() gets the
// guest bytes finished so far; returning false cancels the conversion.
struct Hooks {
	void* context;
	bool (*zero)(void* context, uint64_t guestOffset, uint64_t size,
		bool allocated);
	bool (*progress)(void* context, uint64_t done, uint64_t total);
};

struct Work {
	uint8_t* l1;
	size_t l1Bytes;
	uint8_t* l2;
	size_t l2Bytes;
	uint8_t* cluster;
	size_t clusterBytes;
	uint8_t* compressed;
	size_t compressedBytes;
	inflate::Decoder* decoder;
	// Size of the qcow2 file if known, else 0. Give it whenever possible: a
	// compressed cluster's stored size is rounded up to 512-byte sectors and
	// may therefore reach past the end of the file; with fileSize the read is
	// clamped, without it such a read is passed to the source as it is.
	uint64_t fileSize;
};

namespace detail {

inline uint32_t Be32(const uint8_t* p)
{
	return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16)
		| (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
inline uint64_t Be64(const uint8_t* p)
{
	return (uint64_t(Be32(p)) << 32) | Be32(p + 4);
}

} // namespace detail

inline uint64_t L1Bytes(const Image& image)
{
	return uint64_t(image.l1Entries) * 8;
}

// Reads and validates the header (one read of 112 bytes at offset 0).
inline Error Open(const Source& source, Image& image)
{
	using namespace detail;
	memset(&image, 0, sizeof(image));
	auto fail = [&image](Error error) {
		image.error = error;
		return error;
	};
	if (source.read == nullptr)
		return fail(Error::BadArgument);
	uint8_t h[112];
	if (!source.read(source.context, 0, h, sizeof(h)))
		return fail(Error::Read);
	if (Be32(h) != kMagic)
		return fail(Error::BadMagic);
	image.version = Be32(h + 4);
	if (image.version != 2 && image.version != 3)
		return fail(Error::BadVersion);
	image.clusterBits = Be32(h + 20);
	if (image.clusterBits < kMinClusterBits
		|| image.clusterBits > kMaxClusterBits)
		return fail(Error::BadClusterBits);
	image.clusterSize = 1ull << image.clusterBits;
	image.l2Entries = uint32_t(image.clusterSize / 8);
	if (Be32(h + 32) != 0)
		return fail(Error::Encrypted);
	if (Be64(h + 8) != 0 || Be32(h + 16) != 0)
		return fail(Error::BackingFile);
	uint8_t compression = 0;
	if (image.version >= 3) {
		const uint32_t headerLength = Be32(h + 100);
		if (headerLength < 104 || headerLength > image.clusterSize)
			return fail(Error::BadHeader);
		image.incompatible = Be64(h + 72);
		if (image.incompatible & kIncompatCorrupt)
			return fail(Error::MarkedCorrupt);
		if (image.incompatible & kIncompatExternalData)
			return fail(Error::ExternalData);
		if (image.incompatible & kIncompatExtendedL2)
			return fail(Error::ExtendedL2);
		if (image.incompatible & ~(kIncompatDirty | kIncompatCorrupt
				| kIncompatExternalData | kIncompatCompression
				| kIncompatExtendedL2))
			return fail(Error::UnknownFeature);
		if (headerLength > 104)
			compression = h[104];
		if (compression != 0)
			return fail(Error::Compression);
		// The bit without a type field would mean a non-zlib type we cannot see.
		if ((image.incompatible & kIncompatCompression) && headerLength <= 104)
			return fail(Error::Compression);
		image.dirty = (image.incompatible & kIncompatDirty) != 0;
	}
	image.virtualSize = Be64(h + 24);
	const uint64_t perL1 = image.clusterSize * image.l2Entries;
	const uint64_t needed = image.virtualSize / perL1
		+ (image.virtualSize % perL1 != 0 ? 1 : 0);
	// QEMU's own limit on the L1 table is 32 MiB.
	if (needed > (32u << 20) / 8 || Be32(h + 36) < needed)
		return fail(Error::BadHeader);
	image.l1Entries = uint32_t(needed);
	image.l1Offset = Be64(h + 40);
	if (needed != 0 && (image.l1Offset == 0
			|| (image.l1Offset & (image.clusterSize - 1)) != 0))
		return fail(Error::BadHeader);
	return Error::None;
}

// Converts step by step: Begin once, then Step until it stops returning
// Status::More. Every step handles one run (a batch of data clusters, one
// compressed cluster, or a range of zero/unallocated clusters) and costs at
// most two source reads.
class Converter {
public:
	enum class Status : uint32_t { More, Done, Failed };

	// Reads the L1 table. Everything passed in must outlive the conversion.
	Error Begin(const Image& image, const Source& source, const Work& work,
		const Sink& sink, const Hooks* hooks = nullptr)
	{
		image_ = &image;
		source_ = source;
		work_ = work;
		sink_ = sink;
		hooks_ = Hooks{nullptr, nullptr, nullptr};
		if (hooks != nullptr)
			hooks_ = *hooks;
		l1Index_ = 0;
		l2Index_ = 0;
		l2Loaded_ = false;
		done_ = 0;
		cacheStart_ = cacheBytes_ = 0;
		error_ = Error::None;
		if (image.error != Error::None || image.clusterSize == 0
			|| source.read == nullptr || sink.write == nullptr)
			return error_ = Error::BadArgument;
		if (work.l1 == nullptr || work.l1Bytes < L1Bytes(image)
			|| work.l2 == nullptr || work.l2Bytes < image.clusterSize
			|| work.cluster == nullptr || work.clusterBytes < image.clusterSize)
			return error_ = Error::BufferTooSmall;
		if (image.l1Entries != 0 && !source.read(source.context,
				image.l1Offset, work.l1, size_t(L1Bytes(image))))
			return error_ = Error::Read;
		return Error::None;
	}

	Status Step()
	{
		if (error_ != Error::None)
			return Status::Failed;
		const Image& image = *image_;
		if (done_ >= image.virtualSize)
			return Status::Done;
		const uint64_t clusterSize = image.clusterSize;
		if (!l2Loaded_) {
			const uint64_t offset = detail::Be64(work_.l1
				+ size_t(l1Index_) * 8) & kOffsetMask;
			if (offset == 0) {
				// No L2 table: the whole range is unallocated.
				const uint64_t span = Clip(clusterSize * image.l2Entries);
				if (!Zero(span, false))
					return Status::Failed;
				l1Index_++;
				return Advance(span);
			}
			if ((offset & (clusterSize - 1)) != 0
				|| (work_.fileSize != 0 && offset >= work_.fileSize))
				return Fail(Error::BadTable);
			if (!source_.read(source_.context, offset, work_.l2,
					size_t(clusterSize)))
				return Fail(Error::Read);
			l2Loaded_ = true;
			l2Index_ = 0;
		}
		// Clusters left in this L2 table that lie inside the virtual disk.
		const uint64_t left = image.virtualSize - done_;
		uint64_t clusters = left / clusterSize + (left % clusterSize ? 1 : 0);
		if (clusters > image.l2Entries - l2Index_)
			clusters = image.l2Entries - l2Index_;
		const uint64_t entry = Entry(l2Index_);
		const Kind kind = Classify(entry);
		uint32_t run = 1;
		uint64_t span = 0;
		switch (kind) {
		case Kind::Bad:
			return Fail(Error::BadTable);
		case Kind::Unallocated:
		case Kind::Zero:
			while (run < clusters && Classify(Entry(l2Index_ + run)) == kind)
				run++;
			span = Clip(run * clusterSize);
			if (!Zero(span, kind == Kind::Zero))
				return Status::Failed;
			break;
		case Kind::Data: {
			const uint64_t host = entry & kOffsetMask;
			const uint64_t most = work_.clusterBytes / clusterSize;
			while (run < clusters && run < most) {
				const uint64_t next = Entry(l2Index_ + run);
				if (Classify(next) != Kind::Data
					|| (next & kOffsetMask) != host + run * clusterSize)
					break;
				run++;
			}
			span = Clip(run * clusterSize);
			// Whole clusters are read; a file known to end inside the last
			// one reads as zeros from there on.
			uint64_t bytes = run * clusterSize;
			if (work_.fileSize != 0 && host + bytes > work_.fileSize) {
				const uint64_t have = work_.fileSize - host;
				memset(work_.cluster + size_t(have), 0, size_t(bytes - have));
				bytes = have;
			}
			if (!source_.read(source_.context, host, work_.cluster,
					size_t(bytes)))
				return Fail(Error::Read);
			if (!sink_.write(sink_.context, done_, work_.cluster, size_t(span)))
				return Fail(Error::Sink);
			break;
		}
		case Kind::Compressed: {
			uint64_t host, stored;
			if (!CompressedExtent(entry, host, stored))
				return Fail(Error::BadTable);
			if (work_.compressed == nullptr || work_.decoder == nullptr
				|| stored > work_.compressedBytes)
				return Fail(Error::BufferTooSmall);
			if (host < cacheStart_ || host + stored > cacheStart_ + cacheBytes_) {
				// Fetch this cluster together with the compressed clusters
				// that follow it in the file, as far as the buffer reaches.
				uint64_t end = host + stored;
				for (uint64_t i = 1; i < clusters; i++) {
					const uint64_t next = Entry(l2Index_ + uint32_t(i));
					if (!(next & kFlagCompressed))
						continue;
					uint64_t nextHost, nextStored;
					if (!CompressedExtent(next, nextHost, nextStored)
						|| nextHost < host
						|| nextHost + nextStored - host > work_.compressedBytes)
						break;
					if (nextHost + nextStored > end)
						end = nextHost + nextStored;
				}
				cacheBytes_ = 0;
				if (!source_.read(source_.context, host, work_.compressed,
						size_t(end - host)))
					return Fail(Error::Read);
				cacheStart_ = host;
				cacheBytes_ = end - host;
			}
			inflate::MemoryInput memory{work_.compressed
				+ size_t(host - cacheStart_), size_t(stored)};
			const inflate::Input input{&memory, inflate::MemoryInput::Next};
			uint64_t produced = 0;
			const inflate::Error result = work_.decoder->Run(input,
				work_.cluster, size_t(clusterSize), nullptr, &produced);
			// Like QEMU: the stream must fill exactly one cluster; whether
			// its end marker fits the stored size is not checked.
			if (produced != clusterSize || result == inflate::Error::Corrupt
				|| result == inflate::Error::BadArgument)
				return Fail(Error::BadCompressed);
			span = Clip(clusterSize);
			if (!sink_.write(sink_.context, done_, work_.cluster, size_t(span)))
				return Fail(Error::Sink);
			break;
		}
		}
		l2Index_ += run;
		if (l2Index_ >= image.l2Entries) {
			l2Loaded_ = false;
			l1Index_++;
		}
		return Advance(span);
	}

	Error error() const { return error_; }
	uint64_t done() const { return done_; }

private:
	enum class Kind { Bad, Unallocated, Zero, Data, Compressed };

	uint64_t Entry(uint32_t index) const
	{
		return detail::Be64(work_.l2 + size_t(index) * 8);
	}

	Kind Classify(uint64_t entry) const
	{
		if (entry & kFlagCompressed)
			return Kind::Compressed;
		if (entry & kFlagZero)
			return image_->version >= 3 ? Kind::Zero : Kind::Bad;
		const uint64_t offset = entry & kOffsetMask;
		if (offset == 0)
			return Kind::Unallocated;
		if ((offset & (image_->clusterSize - 1)) != 0
			|| (work_.fileSize != 0 && offset >= work_.fileSize))
			return Kind::Bad;
		return Kind::Data;
	}

	// Host offset and stored size of a compressed cluster. The size is given
	// in 512-byte sectors, the first of which is the one holding the offset.
	bool CompressedExtent(uint64_t entry, uint64_t& host, uint64_t& stored) const
	{
		const uint32_t sizeBits = image_->clusterBits - 8;
		const uint32_t shift = 62 - sizeBits;
		host = entry & ((1ull << shift) - 1);
		const uint64_t sectors = ((entry >> shift) & ((1ull << sizeBits) - 1)) + 1;
		stored = sectors * 512 - (host & 511);
		if (host == 0)
			return false;
		if (work_.fileSize != 0) {
			if (host >= work_.fileSize)
				return false;
			if (stored > work_.fileSize - host)
				stored = work_.fileSize - host;
		}
		return true;
	}

	uint64_t Clip(uint64_t span) const
	{
		const uint64_t left = image_->virtualSize - done_;
		return span < left ? span : left;
	}

	bool Zero(uint64_t span, bool allocated)
	{
		if (hooks_.zero != nullptr
			&& !hooks_.zero(hooks_.context, done_, span, allocated)) {
			error_ = Error::Sink;
			return false;
		}
		return true;
	}

	Status Advance(uint64_t span)
	{
		done_ += span;
		if (hooks_.progress != nullptr && !hooks_.progress(hooks_.context,
				done_, image_->virtualSize))
			return Fail(Error::Cancelled);
		return done_ >= image_->virtualSize ? Status::Done : Status::More;
	}

	Status Fail(Error error)
	{
		error_ = error;
		return Status::Failed;
	}

	const Image* image_;
	Source source_;
	Work work_;
	Sink sink_;
	Hooks hooks_;
	uint32_t l1Index_, l2Index_;
	bool l2Loaded_;
	uint64_t done_;
	uint64_t cacheStart_, cacheBytes_;
	Error error_;
};

// The whole conversion in one call.
inline Error Convert(const Image& image, const Source& source,
	const Work& work, const Sink& sink, const Hooks* hooks = nullptr)
{
	Converter converter;
	if (converter.Begin(image, source, work, sink, hooks) != Error::None)
		return converter.error();
	while (converter.Step() == Converter::Status::More) {
	}
	return converter.error();
}

} // namespace hakefi::qcow2
