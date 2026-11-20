// SPDX-License-Identifier: GPL-2.0-or-later
// Host test for src/efi/CharonQcow2.h and src/efi/CharonInflate.h.
// Usage: CharonQcow2Tests <data directory>
// The directory comes from tests/make-qcow2-testdata.py: manifest.txt lists
// qcow2 images, the raw disk each must convert to, and the expected result.
#define _CRT_SECURE_NO_WARNINGS
#include "../src/efi/CharonQcow2.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace hakefi::qcow2;
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
	uint64_t reads = 0, readBytes = 0;
	bool strict = true; // refuse reads past the end, like an HTTP range would
};

static bool FileRead(void* context, uint64_t offset, uint8_t* out, size_t size)
{
	File& file = *static_cast<File*>(context);
	if (offset + size > file.bytes.size())
		return false;
	memcpy(out, file.bytes.data() + offset, size);
	file.reads++;
	file.readBytes += size;
	return true;
}

struct Target {
	std::vector<uint8_t> disk;
	uint64_t next = 0;       // every callback must start here or later
	uint64_t covered = 0;
	uint64_t written = 0, zeroed = 0, zeroAllocated = 0;
	uint64_t writes = 0, progressCalls = 0, lastProgress = 0;
	uint64_t cancelAt = UINT64_MAX;
	bool order = true, progressOrder = true, applyZero = true;
};

static bool TargetWrite(void* context, uint64_t offset, const uint8_t* data,
	size_t size)
{
	Target& t = *static_cast<Target*>(context);
	if (offset < t.next || offset + size > t.disk.size() || size == 0)
		t.order = false;
	else
		memcpy(t.disk.data() + offset, data, size);
	t.next = offset + size;
	t.covered += size;
	t.written += size;
	t.writes++;
	return true;
}

static bool TargetZero(void* context, uint64_t offset, uint64_t size,
	bool allocated)
{
	Target& t = *static_cast<Target*>(context);
	if (offset < t.next || offset + size > t.disk.size() || size == 0)
		t.order = false;
	else if (t.applyZero)
		memset(t.disk.data() + offset, 0, size_t(size));
	t.next = offset + size;
	t.covered += size;
	t.zeroed += size;
	if (allocated)
		t.zeroAllocated += size;
	return true;
}

static bool TargetProgress(void* context, uint64_t done, uint64_t total)
{
	Target& t = *static_cast<Target*>(context);
	if (done < t.lastProgress || done > total || total != t.disk.size())
		t.progressOrder = false;
	t.lastProgress = done;
	t.progressCalls++;
	return done < t.cancelAt;
}

static const char* Name(Error e)
{
	switch (e) {
	case Error::None: return "ok";
	case Error::BadArgument: return "BadArgument";
	case Error::Read: return "Read";
	case Error::BadMagic: return "BadMagic";
	case Error::BadVersion: return "BadVersion";
	case Error::BadClusterBits: return "BadClusterBits";
	case Error::Encrypted: return "Encrypted";
	case Error::BackingFile: return "BackingFile";
	case Error::ExternalData: return "ExternalData";
	case Error::ExtendedL2: return "ExtendedL2";
	case Error::MarkedCorrupt: return "MarkedCorrupt";
	case Error::UnknownFeature: return "UnknownFeature";
	case Error::Compression: return "Compression";
	case Error::BadHeader: return "BadHeader";
	case Error::BufferTooSmall: return "BufferTooSmall";
	case Error::BadTable: return "BadTable";
	case Error::BadCompressed: return "BadCompressed";
	case Error::Sink: return "Sink";
	case Error::Cancelled: return "Cancelled";
	}
	return "?";
}

struct Buffers {
	std::vector<uint8_t> l1, l2, cluster, compressed;
	inflate::Decoder decoder;
	Work Make(uint64_t fileSize)
	{
		return Work{l1.data(), l1.size(), l2.data(), l2.size(), cluster.data(),
			cluster.size(), compressed.data(), compressed.size(), &decoder,
			fileSize};
	}
};

// One conversion; returns the error and leaves the result in target.
static Error Run(File& file, const Image& image, size_t clusterBytes,
	size_t compressedBytes, bool knowFileSize, bool hooksOn, Target& target)
{
	static Buffers buffers; // the decoder is too big for the stack by rule
	buffers.l1.assign(size_t(L1Bytes(image)) + 1, 0);
	buffers.l2.assign(size_t(image.clusterSize), 0);
	buffers.cluster.assign(clusterBytes, 0);
	buffers.compressed.assign(compressedBytes, 0);
	const Work work = buffers.Make(knowFileSize ? file.bytes.size() : 0);
	const Source source{&file, FileRead};
	const Sink sink{&target, TargetWrite};
	const Hooks hooks{&target, TargetZero, TargetProgress};
	return Convert(image, source, work, sink, hooksOn ? &hooks : nullptr);
}

static void TestImage(const std::string& directory, const std::string& name,
	const std::string& rawName, const std::string& expect)
{
	File file;
	if (!ReadFile(directory + "/" + name, file.bytes)) {
		Check(false, name + ": cannot read the image");
		return;
	}
	const Source source{&file, FileRead};
	Image image;
	const Error opened = Open(source, image);
	if (expect != "ok") {
		Check(expect == Name(opened), name + ": Open reports " + Name(opened)
			+ ", expected " + expect);
		return;
	}
	Check(opened == Error::None && file.reads == 1, name + ": Open ("
		+ Name(opened) + "), one read");
	if (opened != Error::None)
		return;
	std::vector<uint8_t> raw;
	if (!ReadFile(directory + "/" + rawName, raw)) {
		Check(false, name + ": cannot read " + rawName);
		return;
	}
	Check(image.virtualSize == raw.size(), name + ": virtual size "
		+ std::to_string(image.virtualSize));
	if (image.virtualSize != raw.size())
		return;
	const size_t cs = size_t(image.clusterSize);

	// A: smallest buffers, file size known, zero hook fills a dirty target.
	{
		Target t;
		t.disk.assign(raw.size(), 0xa5);
		file.reads = 0;
		const Error e = Run(file, image, cs, 2 * cs, true, true, t);
		Check(e == Error::None, name + ": minimal buffers convert ("
			+ Name(e) + ")");
		Check(t.disk == raw, name + ": minimal buffers, bytes equal the raw disk");
		Check(t.order && t.covered == raw.size(), name
			+ ": callbacks ascending, every byte reported exactly once");
		Check(t.progressOrder && (raw.empty() || t.lastProgress == raw.size()),
			name + ": progress monotonic up to the virtual size");
		printf("     %s: %llu reads, %llu writes, %llu B data, %llu B zero "
			"(%llu B flagged)\n", name.c_str(), (unsigned long long)file.reads,
			(unsigned long long)t.writes, (unsigned long long)t.written,
			(unsigned long long)t.zeroed, (unsigned long long)t.zeroAllocated);
	}
	// B: big buffers, no hooks, target already zero. Fewer reads expected.
	uint64_t minimalReads = file.reads;
	{
		Target t;
		t.disk.assign(raw.size(), 0);
		file.reads = 0;
		const size_t big = cs > (1u << 20) ? 2 * cs : (1u << 20);
		const Error e = Run(file, image, big, 2 * big, true, false, t);
		Check(e == Error::None && t.disk == raw, name
			+ ": batched buffers without hooks, bytes equal (" + Name(e) + ")");
		Check(file.reads <= minimalReads, name + ": batching reads "
			+ std::to_string(file.reads) + " <= " + std::to_string(minimalReads));
		Check(t.order, name + ": batched writes ascending");
	}
	// C: file size unknown. Works unless the last compressed cluster's sector
	// rounding passes the end of the file; then it must be a clean Read error.
	{
		Target t;
		t.disk.assign(raw.size(), 0);
		const Error e = Run(file, image, cs, 2 * cs, false, false, t);
		Check((e == Error::None && t.disk == raw) || e == Error::Read, name
			+ std::string(": unknown file size -> ") + Name(e));
	}
	// D: step interface, cancelled half way.
	if (raw.size() > 4 * cs) {
		Target t;
		t.disk.assign(raw.size(), 0);
		t.cancelAt = raw.size() / 2;
		const Error e = Run(file, image, cs, 2 * cs, true, true, t);
		Check(e == Error::Cancelled && t.lastProgress >= t.cancelAt
			&& t.lastProgress < raw.size(), name + ": progress hook cancels");
	}
}

// ---- header and table errors on a mutated copy ----------------------------

static void P32(uint8_t* p, uint32_t v)
{
	p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8);
	p[3] = uint8_t(v);
}
static void P64(uint8_t* p, uint64_t v) { P32(p, uint32_t(v >> 32)); P32(p + 4, uint32_t(v)); }
static uint64_t G64(const uint8_t* p)
{
	uint64_t v = 0;
	for (int i = 0; i < 8; i++)
		v = (v << 8) | p[i];
	return v;
}

static void TestHeaderErrors(const std::string& directory)
{
	File original;
	if (!ReadFile(directory + "/hand-64k.qcow2", original.bytes)) {
		Check(false, "hand-64k.qcow2 missing");
		return;
	}
	auto open = [&](void (*mutate)(uint8_t*), Error expect, const char* what) {
		File file = original;
		mutate(file.bytes.data());
		Image image;
		const Error e = Open(Source{&file, FileRead}, image);
		Check(e == expect && image.error == expect, std::string("header: ")
			+ what + " -> " + Name(e));
	};
	open([](uint8_t* h) { h[3] = 0; }, Error::BadMagic, "bad magic");
	open([](uint8_t* h) { P32(h + 4, 4); }, Error::BadVersion, "version 4");
	open([](uint8_t* h) { P32(h + 4, 1); }, Error::BadVersion, "version 1");
	open([](uint8_t* h) { P32(h + 20, 22); }, Error::BadClusterBits, "cluster bits 22");
	open([](uint8_t* h) { P32(h + 20, 8); }, Error::BadClusterBits, "cluster bits 8");
	open([](uint8_t* h) { P32(h + 32, 1); }, Error::Encrypted, "AES encryption");
	open([](uint8_t* h) { P32(h + 32, 2); }, Error::Encrypted, "LUKS encryption");
	open([](uint8_t* h) { P64(h + 8, 4096); P32(h + 16, 8); }, Error::BackingFile, "backing file");
	open([](uint8_t* h) { P64(h + 72, 2); }, Error::MarkedCorrupt, "corrupt bit");
	open([](uint8_t* h) { P64(h + 72, 4); }, Error::ExternalData, "external data file");
	open([](uint8_t* h) { P64(h + 72, 16); }, Error::ExtendedL2, "extended L2");
	open([](uint8_t* h) { P64(h + 72, 32); }, Error::UnknownFeature, "unknown incompatible bit 5");
	open([](uint8_t* h) { P64(h + 72, 1ull << 63); }, Error::UnknownFeature, "unknown incompatible bit 63");
	open([](uint8_t* h) { P64(h + 72, 8); h[104] = 1; }, Error::Compression, "zstd compression type");
	open([](uint8_t* h) { P64(h + 72, 8); P32(h + 100, 104); }, Error::Compression, "compression bit without type field");
	open([](uint8_t* h) { P32(h + 36, 0); }, Error::BadHeader, "L1 table too short");
	open([](uint8_t* h) { P64(h + 40, 0); }, Error::BadHeader, "no L1 offset");
	open([](uint8_t* h) { P64(h + 40, 65536 + 512); }, Error::BadHeader, "unaligned L1 offset");
	open([](uint8_t* h) { P32(h + 100, 72); }, Error::BadHeader, "v3 header length 72");
	open([](uint8_t* h) { P64(h + 72, 1); P64(h + 80, ~0ull); P64(h + 88, ~0ull); }, Error::None, "dirty bit and unknown compatible bits accepted");
	{
		File empty;
		empty.bytes.assign(64, 0);
		Image image;
		Check(Open(Source{&empty, FileRead}, image) == Error::Read,
			"header: short file -> Read");
		Check(Open(Source{nullptr, nullptr}, image) == Error::BadArgument,
			"header: null source -> BadArgument");
	}

	// Table and data damage. hand-64k: L1 at cluster 1, first L2 at cluster 4,
	// guest cluster 0 is compressed.
	Image image;
	Open(Source{&original, FileRead}, image);
	const size_t cs = size_t(image.clusterSize);
	const size_t l1 = size_t(image.l1Offset);
	const size_t l2 = size_t(G64(original.bytes.data() + l1) & kOffsetMask);
	auto convert = [&](File& file, size_t compressedBytes, bool decoder,
		const char* what, Error expect) {
		Target t;
		t.disk.assign(size_t(image.virtualSize), 0);
		static Buffers b;
		b.l1.assign(size_t(L1Bytes(image)), 0);
		b.l2.assign(cs, 0);
		b.cluster.assign(cs, 0);
		b.compressed.assign(compressedBytes, 0);
		Work work = b.Make(file.bytes.size());
		if (!decoder)
			work.decoder = nullptr;
		const Error e = Convert(image, Source{&file, FileRead}, work,
			Sink{&t, TargetWrite});
		Check(e == expect, std::string("convert: ") + what + " -> " + Name(e));
	};
	{
		File file = original;
		P64(file.bytes.data() + l1, l2 + 512);
		convert(file, 2 * cs, true, "unaligned L2 offset", Error::BadTable);
	}
	{
		File file = original;
		P64(file.bytes.data() + l1, uint64_t(file.bytes.size()) + 16 * cs);
		convert(file, 2 * cs, true, "L2 table past the end", Error::BadTable);
	}
	{
		// Find a standard cluster and misalign it.
		File file = original;
		for (size_t i = 0; i < cs / 8; i++) {
			const uint64_t e = G64(file.bytes.data() + l2 + i * 8);
			if (!(e & kFlagCompressed) && !(e & 1) && (e & kOffsetMask)) {
				P64(file.bytes.data() + l2 + i * 8, e + 512);
				break;
			}
		}
		convert(file, 2 * cs, true, "unaligned data cluster", Error::BadTable);
	}
	const uint64_t first = G64(original.bytes.data() + l2);
	const uint64_t host = first & ((1ull << (62 - (image.clusterBits - 8))) - 1);
	Check((first & kFlagCompressed) != 0, "fixture: guest cluster 0 is compressed");
	{
		// An invalid block type (3) at the start of the stream.
		File file = original;
		file.bytes[size_t(host)] |= 0x06;
		convert(file, 2 * cs, true, "invalid deflate block", Error::BadCompressed);
	}
	{
		// A stream that ends early: one empty final stored block.
		File file = original;
		const uint8_t stored[5] = {1, 0, 0, 0xff, 0xff};
		memcpy(file.bytes.data() + host, stored, 5);
		convert(file, 2 * cs, true, "compressed cluster too short", Error::BadCompressed);
	}
	{
		File file = original;
		convert(file, 64, true, "compressed buffer too small", Error::BufferTooSmall);
		convert(file, 2 * cs, false, "no decoder", Error::BufferTooSmall);
	}
	{
		// Version 2 has no zero flag.
		File file;
		if (ReadFile(directory + "/hand-v2.qcow2", file.bytes)) {
			Image v2;
			Open(Source{&file, FileRead}, v2);
			const size_t t = size_t(G64(file.bytes.data() + size_t(v2.l1Offset))
				& kOffsetMask);
			P64(file.bytes.data() + t + 8, 1);
			Target target;
			target.disk.assign(size_t(v2.virtualSize), 0);
			const Error e = Run(file, v2, cs, 2 * cs, true, false, target);
			Check(e == Error::BadTable, std::string("convert: zero flag in "
				"version 2 -> ") + Name(e));
		}
	}
	{
		// Buffers below the documented minimum.
		File file = original;
		Target t;
		t.disk.assign(size_t(image.virtualSize), 0);
		static Buffers b;
		b.l1.assign(size_t(L1Bytes(image)), 0);
		b.l2.assign(cs, 0);
		b.cluster.assign(cs - 1, 0);
		b.compressed.assign(2 * cs, 0);
		Check(Convert(image, Source{&file, FileRead}, b.Make(0),
				Sink{&t, TargetWrite}) == Error::BufferTooSmall,
			"convert: cluster buffer one byte short -> BufferTooSmall");
		b.cluster.assign(cs, 0);
		Check(Convert(image, Source{&file, FileRead}, b.Make(0),
				Sink{&t, nullptr}) == Error::BadArgument,
			"convert: null sink -> BadArgument");
	}
}

// ---- the inflate decoder on its own ---------------------------------------

struct Pieces {
	const uint8_t* data;
	size_t size, step, at;
	static size_t Next(void* context, const uint8_t** out)
	{
		Pieces& p = *static_cast<Pieces*>(context);
		const size_t n = p.size - p.at < p.step ? p.size - p.at : p.step;
		*out = p.data + p.at;
		p.at += n;
		return n;
	}
};

struct Collect {
	std::vector<uint8_t> bytes;
	size_t calls = 0;
	static bool Write(void* context, const uint8_t* data, size_t size)
	{
		Collect& c = *static_cast<Collect*>(context);
		c.bytes.insert(c.bytes.end(), data, data + size);
		c.calls++;
		return true;
	}
};

static void TestInflate(const std::string& directory)
{
	// inflate-*.bin: raw deflate streams, inflate-*.out: their content.
	static inflate::Decoder decoder;
	for (const char* name : {"inflate-fixed", "inflate-dynamic",
			"inflate-stored", "inflate-mixed", "inflate-far"}) {
		std::vector<uint8_t> in, expect;
		if (!ReadFile(directory + "/" + name + ".bin", in)
			|| !ReadFile(directory + "/" + name + ".out", expect)) {
			Check(false, std::string(name) + ": test data missing");
			continue;
		}
		bool all = true;
		for (size_t step : {size_t(1), size_t(7), size_t(4096), in.size()}) {
			for (size_t window : {size_t(32768), size_t(40000), size_t(1) << 20}) {
				Pieces pieces{in.data(), in.size(), step, 0};
				Collect out;
				std::vector<uint8_t> buffer(window);
				uint64_t produced = 0;
				const inflate::Output output{&out, Collect::Write};
				const inflate::Error e = decoder.Run(
					inflate::Input{&pieces, Pieces::Next}, buffer.data(),
					window, &output, &produced);
				all = all && e == inflate::Error::None && out.bytes == expect
					&& produced == expect.size();
			}
		}
		Check(all, std::string(name) + ": streaming, pieces of 1/7/4096/all, "
			"three window sizes (" + std::to_string(expect.size()) + " bytes)");
		// One shot into an exact buffer, and one byte short.
		std::vector<uint8_t> flat(expect.size() + 1);
		inflate::MemoryInput memory{in.data(), in.size()};
		uint64_t produced = 0;
		inflate::Error e = decoder.Run(inflate::Input{&memory,
			inflate::MemoryInput::Next}, flat.data(), expect.size(), nullptr,
			&produced);
		flat.resize(expect.size());
		Check(e == inflate::Error::None && produced == expect.size()
			&& flat == expect, std::string(name) + ": one shot, exact buffer");
		memory = inflate::MemoryInput{in.data(), in.size()};
		e = decoder.Run(inflate::Input{&memory, inflate::MemoryInput::Next},
			flat.data(), expect.size() - 1, nullptr, &produced);
		Check(e == inflate::Error::Overflow && produced == expect.size() - 1,
			std::string(name) + ": one shot, buffer one byte short -> Overflow");
		// Truncated input.
		memory = inflate::MemoryInput{in.data(), in.size() - 1};
		Collect out;
		std::vector<uint8_t> buffer(32768);
		const inflate::Output output{&out, Collect::Write};
		e = decoder.Run(inflate::Input{&memory, inflate::MemoryInput::Next},
			buffer.data(), buffer.size(), &output, &produced);
		Check(e == inflate::Error::Truncated || e == inflate::Error::Corrupt,
			std::string(name) + ": last byte missing -> error");
	}
	uint8_t tiny[16];
	inflate::MemoryInput memory{tiny, 0};
	Check(decoder.Run(inflate::Input{&memory, inflate::MemoryInput::Next},
			tiny, 16, nullptr) == inflate::Error::Truncated,
		"inflate: empty input -> Truncated");
	Collect out;
	const inflate::Output output{&out, Collect::Write};
	Check(decoder.Run(inflate::Input{&memory, inflate::MemoryInput::Next},
			tiny, 16, &output) == inflate::Error::BadArgument,
		"inflate: streaming window below 32 KiB -> BadArgument");
	// Fixed block: literal 'a', then a match at distance 2 with one byte out.
	const uint8_t early[] = {0x4b, 0x04, 0x42};
	memory = inflate::MemoryInput{early, sizeof(early)};
	Check(decoder.Run(inflate::Input{&memory, inflate::MemoryInput::Next},
			tiny, 16, nullptr) == inflate::Error::Corrupt,
		"inflate: distance before the start of the output -> Corrupt");
	printf("     sizeof(inflate::Decoder) = %zu, sizeof(Converter) = %zu\n",
		sizeof(inflate::Decoder), sizeof(Converter));
	Check(sizeof(inflate::Decoder) < 6000, "inflate: decoder stays below 6 KiB");
}

int main(int argc, char** argv)
{
	if (argc < 2) {
		printf("usage: CharonQcow2Tests <data directory>\n");
		return 2;
	}
	const std::string directory = argv[1];
	TestInflate(directory);
	TestHeaderErrors(directory);
	FILE* manifest = fopen((directory + "/manifest.txt").c_str(), "r");
	if (manifest == nullptr) {
		printf("FAIL no manifest.txt in %s\n", directory.c_str());
		return 1;
	}
	char image[256], raw[256], expect[64];
	int images = 0;
	while (fscanf(manifest, "%255s %255s %63s", image, raw, expect) == 3) {
		TestImage(directory, image, raw, expect);
		images++;
	}
	fclose(manifest);
	Check(images >= 7, "manifest lists " + std::to_string(images) + " images");
	if (gFailures != 0) {
		printf("FAILED: %d\n", gFailures);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
