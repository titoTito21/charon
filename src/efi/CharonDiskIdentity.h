// SPDX-License-Identifier: GPL-2.0-or-later
// Charon r409: serial number and model of a disk from its identify data.
//
// An unattended plan may only run on the disk whose serial number its
// [disk.N] section names (CharonPlanModel.h MatchDisk). The identify block
// comes from the firmware's EFI_DISK_INFO_PROTOCOL (CharonWsDisks.h): the ATA
// IDENTIFY DEVICE block of an IDE or AHCI disk, or the NVMe Identify
// Controller block. A disk that offers neither (USB, SCSI, virtio) has no
// serial here, and a plan that needs one is refused - never matched by
// anything weaker.
//
// Pure logic, host tested.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace hakefi {
namespace diskidentity {

// Copies printable ASCII, drops leading and trailing blanks, joins inner runs
// of blanks. False (and an empty text) when a byte is not printable ASCII.
inline bool Clean(const uint8_t* from, size_t count, bool swapPairs, char* out, size_t capacity)
{
	size_t n = 0;
	bool blank = true;
	if (capacity == 0)
		return false;
	for (size_t i = 0; i < count; i++) {
		const uint8_t c = swapPairs ? from[i ^ 1] : from[i];
		if (c == 0)
			break;
		if (c < 0x20 || c > 0x7E) {
			out[0] = 0;
			return false;
		}
		if (c == ' ') {
			blank = true;
			continue;
		}
		if (blank && n != 0 && n + 1 < capacity)
			out[n++] = ' ';
		blank = false;
		if (n + 1 < capacity)
			out[n++] = char(c);
	}
	out[n] = 0;
	return true;
}
// ATA IDENTIFY DEVICE (512 bytes): serial in words 10-19, model in words
// 27-46, each word with its two characters swapped.
inline bool FromAta(const uint8_t* identify, size_t size, char* serial, size_t serialCapacity, char* model, size_t modelCapacity)
{
	if (serialCapacity != 0)
		serial[0] = 0;
	if (modelCapacity != 0)
		model[0] = 0;
	if (identify == nullptr || size < 512)
		return false;
	const bool ok = Clean(identify + 20, 20, true, serial, serialCapacity);
	Clean(identify + 54, 40, true, model, modelCapacity);
	return ok && serial[0] != 0;
}
// NVMe Identify Controller: serial number bytes 4-23, model number 24-63.
inline bool FromNvme(const uint8_t* identify, size_t size, char* serial, size_t serialCapacity, char* model, size_t modelCapacity)
{
	if (serialCapacity != 0)
		serial[0] = 0;
	if (modelCapacity != 0)
		model[0] = 0;
	if (identify == nullptr || size < 64)
		return false;
	const bool ok = Clean(identify + 4, 20, false, serial, serialCapacity);
	Clean(identify + 24, 40, false, model, modelCapacity);
	return ok && serial[0] != 0;
}

} // namespace diskidentity
} // namespace hakefi
