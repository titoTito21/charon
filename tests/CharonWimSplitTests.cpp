// SPDX-License-Identifier: GPL-2.0-or-later
// Host test for src/efi/CharonWimSplit.h.
// Usage: CharonWimSplitTests <source.wim> <output dir> [part limit] [scratch bytes]
// Writes install.swm, install2.swm, ... into the (existing) output directory;
// the set is then checked with wimlib-imagex / DISM by the caller.
#define _CRT_SECURE_NO_WARNINGS
#include "../src/efi/CharonWimSplit.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace hakefi::wimsplit;

static int gFailures = 0;

static void Check(bool ok, const char* what)
{
	printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		gFailures++;
}

struct Output {
	std::string directory;
	std::vector<FILE*> files;
	std::vector<uint64_t> written;
	uint64_t calls = 0;
};

static std::string PartName(const std::string& directory, uint32_t part)
{
	std::string name = directory + "/install";
	if (part != 1)
		name += std::to_string(part);
	return name + ".swm";
}

static bool FileSink(void* context, uint32_t part, uint64_t offset,
	const uint8_t* data, size_t size)
{
	Output& out = *static_cast<Output*>(context);
	if (part == 0 || part > out.files.size())
		return false;
	FILE* f = out.files[part - 1];
	if (_fseeki64(f, int64_t(offset), SEEK_SET) != 0)
		return false;
	out.written[part - 1] += size;
	out.calls++;
	return fwrite(data, 1, size, f) == size;
}

static bool ReadAt(FILE* f, uint64_t offset, uint8_t* out, size_t size)
{
	return _fseeki64(f, int64_t(offset), SEEK_SET) == 0
		&& fread(out, 1, size, f) == size;
}

static bool NullSink(void*, uint32_t, uint64_t, const uint8_t*, size_t)
{
	return true;
}

// Synthetic checks that need no real WIM.
static void SyntheticTests(const uint8_t* realHeader)
{
	uint8_t h[kHeaderBytes];
	Header header;
	memcpy(h, realHeader, sizeof(h));
	h[12] = 0x00; h[13] = 0x0e; h[14] = 0;
	Check(!ParseHeader(h, header) && header.error == Error::Solid,
		"ParseHeader refuses version 0xe00 (solid/ESD)");
	memcpy(h, realHeader, sizeof(h));
	h[16] |= 0x08;
	Check(!ParseHeader(h, header) && header.error == Error::AlreadySplit,
		"ParseHeader refuses the spanned flag");
	memcpy(h, realHeader, sizeof(h));
	h[42] = 3;
	Check(!ParseHeader(h, header) && header.error == Error::AlreadySplit,
		"ParseHeader refuses total_parts 3");
	memcpy(h, realHeader, sizeof(h));
	memcpy(h, "WLPWM\0\0\0", 8);
	Check(!ParseHeader(h, header) && header.error == Error::Pipable,
		"ParseHeader refuses a pipable WIM");
	memcpy(h, realHeader, sizeof(h));
	h[0] = 'X';
	Check(!ParseHeader(h, header) && header.error == Error::BadMagic,
		"ParseHeader refuses a wrong magic");
	memcpy(h, realHeader, sizeof(h));
	h[16] |= 0x40;
	Check(!ParseHeader(h, header) && header.error == Error::WriteInProgress,
		"ParseHeader refuses write-in-progress");

	// Tiny synthetic WIM: data A(1000)@208, B(3000)@1208, hole, metadata(500)@5000,
	// C(2000)@6000; blob table order: C, metadata, A, B.
	uint8_t table[4 * kEntryBytes];
	memset(table, 0, sizeof(table));
	struct { uint64_t size, offset; uint8_t flags; } rows[4] = {
		{2000, 6000, 0x04}, {500, 5000, 0x06}, {1000, 208, 0x04}, {3000, 1208, 0}};
	for (int i = 0; i < 4; i++) {
		uint8_t* e = table + i * kEntryBytes;
		detail::P64(e, rows[i].size);
		e[7] = rows[i].flags;
		detail::P64(e + 8, rows[i].offset);
		detail::P64(e + 16, rows[i].size * 2);
		detail::P16(e + 24, 1);
		detail::P32(e + 26, 1);
		memset(e + 30, 0x10 + i, 20);
	}
	memset(h, 0, sizeof(h));
	memcpy(h, "MSWIM\0\0\0", 8);
	detail::P32(h + 8, 208);
	detail::P32(h + 12, kVersion);
	detail::P32(h + 16, 0x00040082);
	detail::P32(h + 20, 32768);
	detail::P16(h + 40, 1);
	detail::P16(h + 42, 1);
	detail::P32(h + 44, 1);
	detail::PutReshdr(h + 48, sizeof(table), 0x02, 8000);
	detail::PutReshdr(h + 72, 100, 0x02, 8200);
	Check(ParseHeader(h, header), "synthetic header parses");
	Resource resources[4];
	Part parts[8];
	Plan plan;
	// Part 1 = 208 + 500 metadata + A 1000 + 2 entries + XML 100 = 1908 <= 2000.
	bool ok = MakePlan(header, table, sizeof(table), 3500, plan, resources, 4,
		parts, 8);
	Check(ok && plan.partCount == 3, "synthetic plan: 3 parts at limit 3500");
	if (ok) {
		bool layout = true;
		for (size_t i = 0; i < 4; i++) {
			const Resource& r = resources[i];
			if (r.entry == 1)
				layout = layout && r.part == 1 && r.partOffset == 208;
			if (r.entry == 2)
				layout = layout && r.part == 1 && r.partOffset == 708;
			if (r.entry == 3)
				layout = layout && r.part == 2 && r.partOffset == 208;
			if (r.entry == 0)
				layout = layout && r.part == 3 && r.partOffset == 208;
		}
		Check(layout, "synthetic plan: metadata first in part 1, data in offset order");
		Check(parts[0].size == 208 + 500 + 1000 + 100 + 100
			&& parts[1].size == 208 + 3000 + 50 + 100
			&& parts[2].size == 208 + 2000 + 50 + 100, "synthetic plan: part sizes");
		Writer w;
		Begin(w, plan);
		uint8_t zeros[4096] = {};
		Check(NextNeeded(w) == 208, "NextNeeded starts at the first resource");
		ok = Feed(w, 208, zeros, 4000, NullSink, nullptr);
		Check(ok && NextNeeded(w) == 5000, "feeding may start at the first resource");
		ok = Feed(w, 5000, zeros, 3000, NullSink, nullptr);
		Check(ok && NextNeeded(w) == ~uint64_t(0), "feeding may skip a hole");
		Check(!Feed(w, 100, zeros, 10, NullSink, nullptr) && w.error == Error::Order,
			"Feed refuses going backwards");
		Begin(w, plan);
		Check(!Feed(w, 300, zeros, 10, NullSink, nullptr) && w.error == Error::Order,
			"Feed refuses skipping resource bytes");
		Begin(w, plan);
		uint8_t scratch[kMinScratch];
		uint8_t guid[16] = {};
		Check(Feed(w, 0, zeros, 1000, NullSink, nullptr)
			&& !Finish(w, zeros, 100, guid, scratch, sizeof(scratch), NullSink, nullptr)
			&& w.error == Error::Incomplete, "Finish refuses an incomplete stream");
	}
	Check(!MakePlan(header, table, sizeof(table), 3300, plan, resources, 4, parts, 8)
		&& plan.error == Error::TooLarge,
		"MakePlan: a resource that cannot fit in any part is an error");
	Check(!MakePlan(header, table, sizeof(table), 3500, plan, resources, 4, parts, 2)
		&& plan.error == Error::Capacity, "MakePlan: too few Part slots is an error");
	Check(!MakePlan(header, table, sizeof(table), 3500, plan, resources, 3, parts, 8)
		&& plan.error == Error::Capacity, "MakePlan: too few Resource slots is an error");
	detail::P64(table + 2 * kEntryBytes + 8, 5400); // A overlaps the metadata
	Check(!MakePlan(header, table, sizeof(table), 100000, plan, resources, 4, parts, 8)
		&& plan.error == Error::Overlap, "MakePlan: overlapping resources are an error");
}

int main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s source.wim output-dir [limit] [scratch]\n", argv[0]);
		return 2;
	}
	const uint64_t limit = argc > 3 ? strtoull(argv[3], nullptr, 0) : 300ull << 20;
	const size_t scratchBytes = argc > 4 ? strtoull(argv[4], nullptr, 0) : 65536;
	FILE* wim = fopen(argv[1], "rb");
	if (wim == nullptr) {
		printf("FAIL cannot open %s\n", argv[1]);
		return 1;
	}
	uint8_t h[kHeaderBytes];
	Header header;
	Check(ReadAt(wim, 0, h, sizeof(h)) && ParseHeader(h, header), "ParseHeader(real WIM)");
	if (gFailures != 0)
		return 1;
	SyntheticTests(h);
	printf("INFO version 0x%x flags 0x%x chunk %u images %u boot %u blob table %llu bytes @%llu"
		" xml %llu bytes @%llu integrity %llu bytes\n", header.version, header.flags,
		header.chunkSize, header.imageCount, header.bootIndex,
		(unsigned long long)header.blobTable.size,
		(unsigned long long)header.blobTable.offset,
		(unsigned long long)header.xml.size, (unsigned long long)header.xml.offset,
		(unsigned long long)header.integrity.size);

	std::vector<uint8_t> table(header.blobTable.size), xml(header.xml.size);
	Check(ReadAt(wim, header.blobTable.offset, table.data(), table.size())
		&& ReadAt(wim, header.xml.offset, xml.data(), xml.size()),
		"read blob table and XML by range");
	std::vector<Resource> resources(table.size() / kEntryBytes);
	std::vector<Part> parts(64);
	Plan plan;
	bool ok = MakePlan(header, table.data(), table.size(), limit, plan,
		resources.data(), resources.size(), parts.data(), parts.size());
	Check(ok, "MakePlan(real WIM)");
	if (!ok) {
		printf("INFO plan error %u\n", unsigned(plan.error));
		return 1;
	}
	uint64_t metadataOffset = 0, lastData = 0, payload = 0;
	for (size_t i = 0; i < plan.resourceCount; i++) {
		const Resource& r = resources[i];
		payload += r.size;
		if ((table[size_t(r.entry) * kEntryBytes + 7] & kResMetadata) != 0)
			metadataOffset = r.offset;
		else if (r.offset > lastData)
			lastData = r.offset;
	}
	printf("INFO %zu resources, %llu payload bytes, %u parts, max table %llu bytes,"
		" metadata @%llu, last data resource @%llu\n", plan.resourceCount,
		(unsigned long long)payload, plan.partCount,
		(unsigned long long)plan.maxTableBytes, (unsigned long long)metadataOffset,
		(unsigned long long)lastData);

	Output out;
	out.directory = argv[2];
	bool sizesOk = true;
	for (uint32_t part = 1; part <= plan.partCount; part++) {
		const Part& p = parts[part - 1];
		printf("INFO part %u: %llu bytes, %u resources, table @%llu (%llu bytes)\n", part,
			(unsigned long long)p.size, p.resources, (unsigned long long)p.tableOffset,
			(unsigned long long)p.tableBytes);
		sizesOk = sizesOk && p.size <= limit;
		FILE* f = fopen(PartName(out.directory, part).c_str(), "wb+");
		if (f == nullptr) {
			printf("FAIL cannot create part %u\n", part);
			return 1;
		}
		out.files.push_back(f);
		out.written.push_back(0);
	}
	Check(sizesOk, "every planned part is within the limit");

	// Stream the whole file through Feed in odd chunks (1 MiB + 7 bytes).
	Writer writer;
	Begin(writer, plan);
	std::vector<uint8_t> chunk((1u << 20) + 7);
	_fseeki64(wim, 0, SEEK_SET);
	uint64_t offset = 0;
	ok = true;
	for (;;) {
		size_t got = fread(chunk.data(), 1, chunk.size(), wim);
		if (got == 0)
			break;
		if (!Feed(writer, offset, chunk.data(), got, FileSink, &out)) {
			ok = false;
			break;
		}
		offset += got;
	}
	Check(ok, "Feed(whole WIM, 1 MiB + 7 byte chunks)");
	Check(NextNeeded(writer) == ~uint64_t(0), "no resource byte is missing after the pass");
	const uint64_t dataCalls = out.calls;
	uint8_t guid[16];
	for (int i = 0; i < 16; i++)
		guid[i] = uint8_t(0xC0 + i * 3);
	std::vector<uint8_t> scratch(scratchBytes);
	ok = Finish(writer, xml.data(), xml.size(), guid, scratch.data(), scratch.size(),
		FileSink, &out);
	Check(ok, "Finish(blob tables, XML, headers)");
	printf("INFO scratch %zu bytes, %llu data writes, %llu finish writes\n", scratchBytes,
		(unsigned long long)dataCalls, (unsigned long long)(out.calls - dataCalls));
	Check(!Feed(writer, offset, chunk.data(), 1, FileSink, &out),
		"Feed after Finish is refused");

	bool fileSizes = true, writtenOnce = true;
	for (uint32_t part = 1; part <= plan.partCount; part++) {
		FILE* f = out.files[part - 1];
		_fseeki64(f, 0, SEEK_END);
		fileSizes = fileSizes && uint64_t(_ftelli64(f)) == parts[part - 1].size;
		// Every byte of a part is written exactly once, so the totals must match.
		writtenOnce = writtenOnce && out.written[part - 1] == parts[part - 1].size;
		fclose(f);
	}
	Check(fileSizes, "part files have exactly the planned sizes");
	Check(writtenOnce, "bytes written per part equal the part size (no gaps, no rewrites)");
	fclose(wim);
	printf("%s CharonWimSplitTests (%d failures)\n", gFailures == 0 ? "PASS" : "FAIL",
		gFailures);
	return gFailures == 0 ? 0 : 1;
}
