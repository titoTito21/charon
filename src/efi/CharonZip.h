// SPDX-License-Identifier: GPL-2.0-or-later
// Charon: read-only ZIP reader (PKWARE APPNOTE 6.3, including ZIP64) over a
// random-access source of known size. Lists the central directory and
// extracts stored (0) and deflated (8) entries, verifying size and CRC-32.
// Pure logic: no allocation, the caller supplies every buffer.
//
// The source is expected to be slow (HTTP range requests): the end of the
// archive is normally found with one read, the central directory is read in
// buffer-sized pieces, and an entry's local header arrives together with the
// first piece of its data.
//
// Refused: multi-disk archives, encrypted entries (Error::Encrypted) and
// every method but 0 and 8 (Error::Method). Listing still works for those.
//
// Buffers:
//   Open     scratch   >= kMinScratch; 64 KiB + 22 finds the end record in
//                      one read whatever the archive comment length
//   Begin    buffer    >= kMinDirectoryBuffer and at least 46 + name length +
//                      extra length of the longest entry (worst case 131116;
//                      4 KiB is plenty for ordinary archives). Larger buffers
//                      mean fewer reads. Entry::name points into it.
//   Extract  input     >= kMinInput; compressed data is read in pieces of
//                      this size (64 KiB or more recommended)
//            window    >= inflate::kWindowBytes, and one inflate::Decoder
//                      (about 4 KiB); both only for deflated entries
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "CharonInflate.h"

namespace hakefi::zip {

constexpr size_t kMinScratch = 1024;
constexpr size_t kMinDirectoryBuffer = 512;
constexpr size_t kMinInput = 512;
constexpr uint16_t kMethodStored = 0, kMethodDeflate = 8;
constexpr uint16_t kFlagEncrypted = 0x0001, kFlagDataDescriptor = 0x0008,
	kFlagStrongEncryption = 0x0040, kFlagUtf8 = 0x0800,
	kFlagMaskedDirectory = 0x2000;

enum class Error : uint32_t {
	None,
	End,             // Next: no more entries
	BadArgument,
	Read,            // the source refused a read
	NotZip,          // no end-of-central-directory record
	MultiDisk,       // spanned archive
	BadDirectory,    // inconsistent end record or central directory
	BufferTooSmall,  // an entry's name and extra data do not fit the buffer
	Encrypted,
	Method,          // compression method other than stored and deflate
	BadLocalHeader,  // local header missing or contradicting the directory
	Corrupt,         // invalid deflate data
	Size,            // extracted size differs from the directory
	Crc,             // CRC-32 mismatch
	Sink,            // the sink refused a write
};

struct Source {
	void* context;
	bool (*read)(void* context, uint64_t offset, uint8_t* out, size_t size);
};

struct Sink {
	void* context;
	bool (*write)(void* context, const uint8_t* data, size_t size);
};

struct Archive {
	Source source;
	uint64_t size;            // of the whole file
	uint64_t entryCount;
	uint64_t directoryOffset; // absolute offset in the file
	uint64_t directorySize;
	uint64_t base;            // bytes in front of the archive (SFX stub); 0 normally
	bool zip64;
	Error error;
};

struct Entry {
	const uint8_t* name;      // not terminated; valid until the next Next/Begin
	uint16_t nameLength;
	// True when the bytes are UTF-8 for certain: the UTF-8 flag is set, the
	// name is plain ASCII, or it came from an Info-ZIP Unicode Path field.
	// False means a legacy code page (normally CP437), passed through raw.
	bool nameIsUtf8;
	bool isDirectory;
	bool encrypted;
	uint16_t flags;
	uint16_t method;
	uint16_t dosTime, dosDate;
	uint32_t crc32;
	uint64_t compressedSize;
	uint64_t uncompressedSize;
	uint64_t localHeaderOffset; // as stored; add Archive::base for the file offset
};

struct Cursor {
	const Archive* archive;
	uint8_t* buffer;
	size_t bufferBytes;
	uint64_t position;  // absolute offset of the next record
	uint64_t index;     // entries returned so far
	uint64_t loadedAt;
	size_t loaded;
	Error error;
};

struct ExtractWork {
	uint8_t* input;
	size_t inputBytes;
	inflate::Decoder* decoder; // deflate only
	uint8_t* window;           // deflate only
	size_t windowBytes;
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

struct CrcTable {
	uint32_t value[256];
	constexpr CrcTable() : value{}
	{
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
			value[i] = c;
		}
	}
};
inline constexpr CrcTable kCrcTable{};

constexpr uint32_t kEndSignature = 0x06054b50, kLocatorSignature = 0x07064b50,
	kEnd64Signature = 0x06064b50, kCentralSignature = 0x02014b50,
	kLocalSignature = 0x04034b50;

// Hands the compressed bytes of one entry to the consumer piece by piece.
struct Feeder {
	Source source;
	uint8_t* buffer;
	size_t bufferBytes;
	uint64_t position;
	uint64_t remaining;
	const uint8_t* preloaded;
	size_t preloadedBytes;
	bool failed;

	static size_t Next(void* context, const uint8_t** data)
	{
		Feeder& self = *static_cast<Feeder*>(context);
		if (self.preloadedBytes != 0) {
			const size_t size = self.preloadedBytes;
			*data = self.preloaded;
			self.preloadedBytes = 0;
			return size;
		}
		if (self.remaining == 0 || self.failed)
			return 0;
		const size_t size = self.remaining < self.bufferBytes
			? size_t(self.remaining) : self.bufferBytes;
		if (!self.source.read(self.source.context, self.position, self.buffer,
				size)) {
			self.failed = true;
			return 0;
		}
		self.position += size;
		self.remaining -= size;
		*data = self.buffer;
		return size;
	}
};

} // namespace detail

// CRC-32 (IEEE 802.3) as used by ZIP; start with crc = 0 and chain calls.
inline uint32_t Crc32(uint32_t crc, const uint8_t* data, size_t size)
{
	crc = ~crc;
	for (size_t i = 0; i < size; i++)
		crc = detail::kCrcTable.value[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
	return ~crc;
}

// Finds the end-of-central-directory record (and the ZIP64 one) and fills
// archive. The scratch buffer is free again when Open returns.
inline Error Open(const Source& source, uint64_t size, uint8_t* scratch,
	size_t scratchBytes, Archive& archive)
{
	using namespace detail;
	memset(&archive, 0, sizeof(archive));
	archive.source = source;
	archive.size = size;
	auto fail = [&archive](Error error) {
		archive.error = error;
		return error;
	};
	if (source.read == nullptr || scratch == nullptr
		|| scratchBytes < kMinScratch)
		return fail(Error::BadArgument);
	if (size < 22)
		return fail(Error::NotZip);

	// The record sits at the end, followed only by a comment of up to 65535
	// bytes. Search backwards in scratch-sized pieces overlapping by 21 bytes.
	const uint64_t lowest = size > 22 + 65535 ? size - (22 + 65535) : 0;
	// A record whose comment ends exactly at the end of the file wins; failing
	// that, the last record whose comment fits (trailing junk after an archive).
	uint64_t end = size, endRecord = 0;
	uint8_t record[22];
	uint8_t before[20];
	bool found = false, exact = false, haveBefore = false;
	while (!exact) {
		const size_t length = end - lowest < scratchBytes
			? size_t(end - lowest) : scratchBytes;
		const uint64_t start = end - length;
		if (length < 22)
			break;
		if (!source.read(source.context, start, scratch, length))
			return fail(Error::Read);
		for (size_t i = length - 22 + 1; i-- != 0;) {
			if (U32(scratch + i) != kEndSignature)
				continue;
			const uint64_t tail = start + i + 22 + U16(scratch + i + 20);
			if (tail > size || (found && tail != size))
				continue;
			memcpy(record, scratch + i, 22);
			endRecord = start + i;
			haveBefore = i >= 20;
			if (haveBefore)
				memcpy(before, scratch + i - 20, 20);
			found = true;
			if (tail == size) {
				exact = true;
				break;
			}
		}
		if (start == lowest)
			break;
		end = start + 21;
	}
	if (!found)
		return fail(Error::NotZip);

	uint64_t entries = U16(record + 10);
	uint64_t directorySize = U32(record + 12);
	uint64_t directoryOffset = U32(record + 16);
	uint32_t disk = U16(record + 4), directoryDisk = U16(record + 6);
	uint64_t entriesHere = U16(record + 8);
	uint64_t anchor = endRecord; // the directory ends where this record starts

	if (!haveBefore && endRecord >= 20) {
		if (!source.read(source.context, endRecord - 20, before, 20))
			return fail(Error::Read);
		haveBefore = true;
	}
	if (haveBefore && U32(before) == kLocatorSignature) {
		if (U32(before + 4) != 0 || U32(before + 16) > 1)
			return fail(Error::MultiDisk);
		uint8_t end64[56];
		uint64_t at = U64(before + 8);
		bool ok = at <= size && size - at >= 56
			&& source.read(source.context, at, end64, 56)
			&& U32(end64) == kEnd64Signature;
		if (!ok && endRecord >= 20 + 56) {
			// Data in front of the archive shifts every stored offset; the
			// record normally sits right in front of its locator.
			at = endRecord - 20 - 56;
			if (!source.read(source.context, at, end64, 56))
				return fail(Error::Read);
			ok = U32(end64) == kEnd64Signature;
		}
		if (!ok)
			return fail(Error::BadDirectory);
		disk = U32(end64 + 16);
		directoryDisk = U32(end64 + 20);
		entriesHere = U64(end64 + 24);
		entries = U64(end64 + 32);
		directorySize = U64(end64 + 40);
		directoryOffset = U64(end64 + 48);
		anchor = at;
		archive.zip64 = true;
	} else if (entries == 0xffff || directorySize == 0xffffffffu
		|| directoryOffset == 0xffffffffu) {
		return fail(Error::BadDirectory); // ZIP64 markers without the records
	}
	if (disk != 0 || directoryDisk != 0 || entriesHere != entries)
		return fail(Error::MultiDisk);
	if (directorySize > anchor || directoryOffset > anchor - directorySize
		|| entries > directorySize / 46)
		return fail(Error::BadDirectory);
	archive.base = anchor - directorySize - directoryOffset;
	archive.entryCount = entries;
	archive.directoryOffset = archive.base + directoryOffset;
	archive.directorySize = directorySize;
	return Error::None;
}

// Starts listing. The buffer belongs to the cursor until listing is over.
inline void Begin(Cursor& cursor, const Archive& archive, uint8_t* buffer,
	size_t bufferBytes)
{
	cursor.archive = &archive;
	cursor.buffer = buffer;
	cursor.bufferBytes = bufferBytes;
	cursor.position = archive.directoryOffset;
	cursor.index = 0;
	cursor.loadedAt = 0;
	cursor.loaded = 0;
	cursor.error = archive.error;
	if (cursor.error == Error::None && (buffer == nullptr
			|| bufferBytes < kMinDirectoryBuffer))
		cursor.error = Error::BadArgument;
}

// Returns Error::None with the next entry, Error::End after the last one, or
// the error that stops the listing (it then repeats on every call).
inline Error Next(Cursor& cursor, Entry& entry)
{
	using namespace detail;
	if (cursor.error != Error::None)
		return cursor.error;
	const Archive& archive = *cursor.archive;
	if (cursor.index >= archive.entryCount)
		return Error::End;
	const uint64_t directoryEnd = archive.directoryOffset + archive.directorySize;
	// Makes [position, position + bytes) available and returns its address.
	auto need = [&cursor, &archive, directoryEnd](size_t bytes) -> const uint8_t* {
		if (bytes > directoryEnd - cursor.position) {
			cursor.error = Error::BadDirectory;
			return nullptr;
		}
		if (cursor.position < cursor.loadedAt
			|| cursor.position + bytes > cursor.loadedAt + cursor.loaded) {
			if (bytes > cursor.bufferBytes) {
				cursor.error = Error::BufferTooSmall;
				return nullptr;
			}
			const uint64_t left = directoryEnd - cursor.position;
			const size_t length = left < cursor.bufferBytes ? size_t(left)
				: cursor.bufferBytes;
			cursor.loaded = 0;
			if (!archive.source.read(archive.source.context, cursor.position,
					cursor.buffer, length)) {
				cursor.error = Error::Read;
				return nullptr;
			}
			cursor.loadedAt = cursor.position;
			cursor.loaded = length;
		}
		return cursor.buffer + size_t(cursor.position - cursor.loadedAt);
	};
	const uint8_t* p = need(46);
	if (p == nullptr)
		return cursor.error;
	if (U32(p) != kCentralSignature)
		return cursor.error = Error::BadDirectory;
	const size_t nameLength = U16(p + 28), extraLength = U16(p + 30),
		commentLength = U16(p + 32);
	p = need(46 + nameLength + extraLength);
	if (p == nullptr)
		return cursor.error;
	if (commentLength > directoryEnd - cursor.position - 46 - nameLength
			- extraLength)
		return cursor.error = Error::BadDirectory;

	entry.flags = U16(p + 8);
	entry.method = U16(p + 10);
	entry.dosTime = U16(p + 12);
	entry.dosDate = U16(p + 14);
	entry.crc32 = U32(p + 16);
	entry.compressedSize = U32(p + 20);
	entry.uncompressedSize = U32(p + 24);
	entry.localHeaderOffset = U32(p + 42);
	entry.name = p + 46;
	entry.nameLength = uint16_t(nameLength);
	entry.encrypted = (entry.flags & (kFlagEncrypted | kFlagStrongEncryption
		| kFlagMaskedDirectory)) != 0;
	const uint32_t diskStart = U16(p + 34);
	const uint32_t externalAttributes = U32(p + 38);

	bool ascii = true;
	for (size_t i = 0; i < nameLength; i++)
		ascii = ascii && entry.name[i] < 0x80;
	entry.nameIsUtf8 = ascii || (entry.flags & kFlagUtf8) != 0;

	// Extra fields: ZIP64 sizes and the Info-ZIP Unicode Path.
	const uint8_t* extra = p + 46 + nameLength;
	const uint8_t* unicodeName = nullptr;
	size_t unicodeLength = 0;
	bool disk64 = false;
	for (size_t at = 0; at + 4 <= extraLength;) {
		const uint16_t id = U16(extra + at);
		const size_t length = U16(extra + at + 2);
		const uint8_t* field = extra + at + 4;
		if (length > extraLength - at - 4)
			break; // malformed tail: ignore, like other readers do
		if (id == 0x0001) {
			size_t used = 0;
			auto take = [&](uint64_t& value) {
				if (used + 8 > length)
					return false;
				value = U64(field + used);
				used += 8;
				return true;
			};
			if ((entry.uncompressedSize == 0xffffffffu
					&& !take(entry.uncompressedSize))
				|| (entry.compressedSize == 0xffffffffu
					&& !take(entry.compressedSize))
				|| (entry.localHeaderOffset == 0xffffffffu
					&& !take(entry.localHeaderOffset)))
				return cursor.error = Error::BadDirectory;
			if (diskStart == 0xffff) {
				if (used + 4 > length || U32(field + used) != 0)
					return cursor.error = Error::MultiDisk;
				disk64 = true;
			}
		} else if (id == 0x7075 && length > 5 && field[0] == 1
			&& !entry.nameIsUtf8
			&& U32(field + 1) == Crc32(0, entry.name, nameLength)) {
			unicodeName = field + 5;
			unicodeLength = length - 5;
		}
		at += 4 + length;
	}
	if (diskStart != 0 && !disk64)
		return cursor.error = Error::MultiDisk;
	if (unicodeName != nullptr) {
		entry.name = unicodeName;
		entry.nameLength = uint16_t(unicodeLength);
		entry.nameIsUtf8 = true;
	}
	entry.isDirectory = (entry.nameLength != 0
			&& entry.name[entry.nameLength - 1] == '/')
		|| (externalAttributes & 0x10) != 0;

	cursor.position += 46 + nameLength + extraLength + commentLength;
	cursor.index++;
	return Error::None;
}

// True when the entry's name is exactly the given zero-terminated string.
inline bool NameEquals(const Entry& entry, const char* name)
{
	size_t i = 0;
	for (; i < entry.nameLength; i++) {
		if (name[i] == 0 || uint8_t(name[i]) != entry.name[i])
			return false;
	}
	return name[i] == 0;
}

// Streams one entry's content to the sink in order and checks its size and
// CRC-32 at the end (so the sink has seen the data before a Size/Crc error is
// known). Only the Entry's numbers are used: its name may be gone by now.
inline Error Extract(const Archive& archive, const Entry& entry,
	const ExtractWork& work, const Sink& sink)
{
	using namespace detail;
	if (archive.error != Error::None || sink.write == nullptr
		|| work.input == nullptr || work.inputBytes < kMinInput)
		return Error::BadArgument;
	if (entry.encrypted)
		return Error::Encrypted;
	if (entry.method != kMethodStored && entry.method != kMethodDeflate)
		return Error::Method;
	if (entry.method == kMethodDeflate && (work.decoder == nullptr
			|| work.window == nullptr
			|| work.windowBytes < inflate::kWindowBytes))
		return Error::BadArgument;
	if (entry.method == kMethodStored
		&& entry.compressedSize != entry.uncompressedSize)
		return Error::BadDirectory;
	// Directories and empty stored files: nothing to fetch.
	if (entry.compressedSize == 0 && entry.uncompressedSize == 0)
		return entry.crc32 == 0 ? Error::None : Error::Crc;
	if (entry.localHeaderOffset > archive.size
		|| archive.base > archive.size - entry.localHeaderOffset)
		return Error::BadLocalHeader;
	const uint64_t header = archive.base + entry.localHeaderOffset;
	if (archive.size - header < 30)
		return Error::BadLocalHeader;

	// One read for the local header and, as far as it reaches, the data.
	uint64_t first = archive.size - header;
	if (first > work.inputBytes)
		first = work.inputBytes;
	if (entry.compressedSize < first && first - entry.compressedSize > 30 + 2 * 65535)
		first = entry.compressedSize + 30 + 2 * 65535;
	if (!archive.source.read(archive.source.context, header, work.input,
			size_t(first)))
		return Error::Read;
	const uint8_t* p = work.input;
	if (U32(p) != kLocalSignature || U16(p + 8) != entry.method
		|| (U16(p + 6) & kFlagEncrypted) != 0)
		return Error::BadLocalHeader;
	const uint64_t skip = 30 + uint64_t(U16(p + 26)) + U16(p + 28);
	if (skip > archive.size - header
		|| entry.compressedSize > archive.size - header - skip)
		return Error::BadLocalHeader;

	Feeder feeder{archive.source, work.input, work.inputBytes, header + skip,
		entry.compressedSize, nullptr, 0, false};
	if (skip < first) {
		uint64_t have = first - skip;
		if (have > entry.compressedSize)
			have = entry.compressedSize;
		feeder.preloaded = work.input + size_t(skip);
		feeder.preloadedBytes = size_t(have);
		feeder.position += have;
		feeder.remaining -= have;
	}

	struct Checked {
		Sink sink;
		uint64_t limit, total;
		uint32_t crc;
		bool tooLong, refused;
		static bool Write(void* context, const uint8_t* data, size_t size)
		{
			Checked& self = *static_cast<Checked*>(context);
			if (size > self.limit - self.total) {
				self.tooLong = true;
				return false;
			}
			self.crc = Crc32(self.crc, data, size);
			self.total += size;
			if (!self.sink.write(self.sink.context, data, size)) {
				self.refused = true;
				return false;
			}
			return true;
		}
	} checked{sink, entry.uncompressedSize, 0, 0, false, false};

	if (entry.method == kMethodStored) {
		for (;;) {
			const uint8_t* data = nullptr;
			const size_t size = Feeder::Next(&feeder, &data);
			if (size == 0)
				break;
			if (!Checked::Write(&checked, data, size))
				break;
		}
		if (feeder.failed)
			return Error::Read;
	} else {
		const inflate::Input input{&feeder, Feeder::Next};
		const inflate::Output output{&checked, Checked::Write};
		const inflate::Error result = work.decoder->Run(input, work.window,
			work.windowBytes, &output);
		if (feeder.failed)
			return Error::Read;
		if (result != inflate::Error::None && result != inflate::Error::Sink)
			return Error::Corrupt;
	}
	if (checked.refused)
		return Error::Sink;
	if (checked.tooLong || checked.total != entry.uncompressedSize)
		return Error::Size;
	if (checked.crc != entry.crc32)
		return Error::Crc;
	return Error::None;
}

} // namespace hakefi::zip
