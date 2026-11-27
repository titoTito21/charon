// SPDX-License-Identifier: GPL-2.0-or-later
// r410 (agent 2): Charon Hypervisor in Charon's device list (design part 3 and 8, work/charon-hypervisor-design.md).
// Every FAT volume that holds \charonhv\charonhv.cfg (the "Hypervisor" partition on an internal disk or a stick) adds
// in-memory entries (source 4, like Memtest86+: startable, never a Continue target):
//   "Charon Hypervisor"              - the menu; with no machine yet the virtual machine wizard opens at once
//   "Charon Hypervisor: <machine>"   - one per line "id|name" of \charonhv\machines.txt (written by the hypervisor)
// Each starts Alpine Linux's kernel \boot\vmlinuz-lts from that volume (EFI stub) with LoadOptions naming the initrd and
// the volume (alpine_dev=UUID=<FAT serial>), Charon's language and the machine. Nothing is written anywhere.
#pragma once

#include "CharonMedia.h"

namespace hakefi {
namespace hvlist {

constexpr size_t kMaxMachines = 24;

inline void Put(char16_t* out, size_t cap, size_t& at, const char16_t* s)
{
	while (s && *s && at + 1 < cap)
		out[at++] = *s++;
	out[at] = 0;
}
inline void PutAscii(char16_t* out, size_t cap, size_t& at, const char* s, size_t n)
{
	for (size_t i = 0; i < n && s[i] && at + 1 < cap; ++i)
		out[at++] = char16_t(uint8_t(s[i]));
	out[at] = 0;
}
// UTF-8 to UTF-16 (BMP only; anything else becomes '?').
inline void PutUtf8(char16_t* out, size_t cap, size_t& at, const char* s, size_t n)
{
	for (size_t i = 0; i < n && at + 1 < cap;) {
		const uint8_t c = uint8_t(s[i]);
		uint32_t u = '?';
		size_t len = 1;
		if (c < 0x80) u = c;
		else if ((c & 0xe0) == 0xc0 && i + 1 < n) { u = ((c & 0x1f) << 6) | (uint8_t(s[i + 1]) & 0x3f); len = 2; }
		else if ((c & 0xf0) == 0xe0 && i + 2 < n) { u = ((c & 0x0f) << 12) | ((uint8_t(s[i + 1]) & 0x3f) << 6) | (uint8_t(s[i + 2]) & 0x3f); len = 3; }
		else if ((c & 0xf8) == 0xf0) len = 4;
		out[at++] = char16_t(u);
		i += len;
	}
	out[at] = 0;
}
inline bool ReadSmall(efi_file_protocol* vol, const char16_t* name, char* buf, size_t cap, size_t& size)
{
	efi_file_protocol* f = nullptr;
	size = 0;
	if (vol->Open(vol, &f, (char16_t*)name, EFI_FILE_MODE_READ, 0) != EFI_SUCCESS || !f)
		return false;
	size = cap - 1;
	const bool ok = f->Read(f, &size, buf) == EFI_SUCCESS;
	f->Close(f);
	if (!ok)
		size = 0;
	buf[size] = 0;
	return ok;
}
// A boot option record with optional data (UEFI 2.11 3.1.3: attributes, FilePathListLength, description, path, data).
inline uint8_t* BuildOption(efi_boot_services* boot, const char16_t* description, const uint8_t* path, size_t pathBytes,
	const char16_t* options, size_t& size)
{
	size_t units = 0, optUnits = 0;
	while (description[units]) ++units;
	while (options[optUnits]) ++optUnits;
	const size_t optBytes = (optUnits + 1) * 2;
	size = 6 + (units + 1) * 2 + pathBytes + 4 + optBytes;
	uint8_t* data = nullptr;
	if (boot->AllocatePool(EfiLoaderData, size, (void**)&data) != EFI_SUCCESS || !data)
		return nullptr;
	data[0] = 1; data[1] = data[2] = data[3] = 0;
	data[4] = uint8_t(pathBytes + 4); data[5] = uint8_t((pathBytes + 4) >> 8);
	for (size_t i = 0; i <= units; ++i) { data[6 + i * 2] = uint8_t(description[i]); data[7 + i * 2] = uint8_t(description[i] >> 8); }
	uint8_t* p = data + 6 + (units + 1) * 2;
	for (size_t i = 0; i < pathBytes; ++i) p[i] = path[i];
	p[pathBytes] = 0x7f; p[pathBytes + 1] = 0xff; p[pathBytes + 2] = 4; p[pathBytes + 3] = 0;
	uint8_t* o = p + pathBytes + 4;
	for (size_t i = 0; i <= optUnits; ++i) { o[i * 2] = uint8_t(options[i]); o[i * 2 + 1] = uint8_t(options[i] >> 8); }
	return data;
}
// "XXXX-XXXX" from the FAT32 boot sector (volume serial at byte 67), as Linux's blkid names it.
inline bool FatSerial(efi_boot_services* boot, efi_handle handle, char16_t* out)
{
	media::BlockIo* io = nullptr;
	if (boot->HandleProtocol(handle, &media::blockIoGuid, (void**)&io) != EFI_SUCCESS || !io || !io->Media || io->Media->BlockSize > 4096)
		return false;
	uint8_t* sector = nullptr;
	if (boot->AllocatePool(EfiLoaderData, 4096, (void**)&sector) != EFI_SUCCESS || !sector)
		return false;
	const bool ok = io->ReadBlocks(io, io->Media->MediaId, 0, io->Media->BlockSize, sector) == EFI_SUCCESS
		&& sector[510] == 0x55 && sector[511] == 0xaa && sector[82] == 'F' && sector[83] == 'A' && sector[84] == 'T';
	if (ok) {
		const uint32_t serial = uint32_t(sector[67]) | uint32_t(sector[68]) << 8 | uint32_t(sector[69]) << 16 | uint32_t(sector[70]) << 24;
		static const char16_t hex[] = u"0123456789ABCDEF";
		size_t at = 0;
		for (int i = 7; i >= 0; --i) {
			out[at++] = hex[(serial >> (i * 4)) & 15];
			if (i == 4) out[at++] = u'-';
		}
		out[at] = 0;
	}
	boot->FreePool(sector);
	return ok;
}

inline bool HasMachine(const char* list, size_t size)
{
	for (size_t i = 0; i < size; ++i)
		if (list[i] == '|') return true;
	return false;
}
// Calls add(data, size) for each entry (the caller owns `data`, allocated from pool).
template <class Add>
inline void AddEntries(efi_boot_services* boot, bool pl, Add add)
{
	efi_handle* handles = nullptr;
	size_t count = 0;
	if (boot->LocateHandleBuffer(ByProtocol, &media::fsGuid, nullptr, &count, &handles) != EFI_SUCCESS || !handles)
		return;
	static char cfg[2048], list[8192];
	static uint8_t path[2048 + 64];
	static char16_t options[600], label[160];
	for (size_t h = 0; h < count; ++h) {
		efi_simple_file_system_protocol* fs = nullptr;
		efi_file_protocol* vol = nullptr;
		if (boot->HandleProtocol(handles[h], &media::fsGuid, (void**)&fs) != EFI_SUCCESS || !fs || fs->OpenVolume(fs, &vol) != EFI_SUCCESS || !vol)
			continue;
		size_t cfgSize = 0, listSize = 0;
		const bool isHv = ReadSmall(vol, u"\\charonhv\\charonhv.cfg", cfg, sizeof(cfg), cfgSize)
			&& cfgSize >= 20 && cfg[0] == '#';
		efi_file_protocol* kernel = nullptr;
		const bool hasKernel = isHv && vol->Open(vol, &kernel, (char16_t*)u"\\boot\\vmlinuz-lts", EFI_FILE_MODE_READ, 0) == EFI_SUCCESS && kernel;
		if (kernel) kernel->Close(kernel);
		if (hasKernel && !ReadSmall(vol, u"\\charonhv\\machines.txt", list, sizeof(list), listSize))
			listSize = 0;
		vol->Close(vol);
		if (!hasKernel)
			continue;
		uint8_t* own = nullptr;
		if (boot->HandleProtocol(handles[h], &media::pathGuid, (void**)&own) != EFI_SUCCESS || !own)
			continue;
		const size_t device = media::PathLength(own);
		if (device == SIZE_MAX || !device || device > 2048)
			continue;
		const char16_t file[] = u"\\boot\\vmlinuz-lts";
		const size_t fileUnits = sizeof(file) / 2;   // with the terminating zero
		const size_t node = 4 + fileUnits * 2;
		for (size_t i = 0; i < device; ++i) path[i] = own[i];
		uint8_t* f = path + device;
		f[0] = 4; f[1] = 4; f[2] = uint8_t(node); f[3] = uint8_t(node >> 8);
		for (size_t i = 0; i < fileUnits; ++i) { f[4 + i * 2] = uint8_t(file[i]); f[5 + i * 2] = uint8_t(file[i] >> 8); }
		char16_t serial[12] = {};
		const bool bySerial = FatSerial(boot, handles[h], serial);
		// one entry for the hypervisor, then one per machine
		const char* line = list;
		const char* end = list + listSize;
		for (size_t n = 0; n <= kMaxMachines; ++n) {
			const char* id = nullptr; size_t idLen = 0;
			const char* name = nullptr; size_t nameLen = 0;
			if (n > 0) {
				while (line < end) {
					const char* e = line;
					while (e < end && *e != '\n') ++e;
					const char* bar = line;
					while (bar < e && *bar != '|') ++bar;
					const char* l = line;
					line = e < end ? e + 1 : end;
					if (*l == '#' || bar >= e) continue;
					id = l; idLen = size_t(bar - l);
					name = bar + 1; nameLen = size_t(e - name);
					if (nameLen && name[nameLen - 1] == '\r') --nameLen;
					bool clean = idLen > 0 && idLen < 40;
					for (size_t k = 0; k < idLen && clean; ++k)
						clean = (id[k] >= 'a' && id[k] <= 'z') || (id[k] >= '0' && id[k] <= '9') || id[k] == '-';
					if (clean && nameLen) break;
					id = nullptr;
				}
				if (!id) break;
			}
			size_t at = 0;
			Put(options, 600, at, u"initrd=\\boot\\initramfs-lts modules=loop,squashfs,sd-mod,usb-storage,vfat,nvme,ahci modloop=/boot/modloop-lts alpine_dev=");
			if (bySerial) { Put(options, 600, at, u"UUID="); Put(options, 600, at, serial); }
			else Put(options, 600, at, u"LABEL=CHARONHV");
			Put(options, 600, at, pl ? u" quiet charonhv.lang=pl" : u" quiet charonhv.lang=en");
			size_t lat = 0;
			Put(label, 160, lat, u"Charon Hypervisor");
			if (id) {
				Put(options, 600, at, u" charonhv.machine=");
				PutAscii(options, 600, at, id, idLen);
				Put(label, 160, lat, u": ");
				PutUtf8(label, 160, lat, name, nameLen);
			} else if (!HasMachine(list, listSize)) {
				Put(label, 160, lat, pl ? u" (nowa maszyna wirtualna)" : u" (new virtual machine)");
			}
			size_t size = 0;
			uint8_t* data = BuildOption(boot, label, path, device + node, options, size);
			if (data) add(data, size);
		}
	}
	boot->FreePool(handles);
}

} // namespace hvlist
} // namespace hakefi
