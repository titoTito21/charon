// Minimal UEFI graphics/HII declarations for Charon's resident reader (r357).
// Layouts follow UEFI 2.10: 12.9 Graphics Output Protocol, 34.1 HII Font
// Protocol, 34.2 HII String Protocol. GPL-2.0-or-later.
#pragma once
#include <efi/system-table.h>
namespace hakefi {
static efi_guid gopGuid={0x9042a9de,0x23dc,0x4a38,{0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
static efi_guid hiiFontGuid={0xe9ca4775,0x8657,0x47fc,{0x97,0xe7,0x7e,0xd6,0x5a,0x08,0x43,0x24}};
static efi_guid hiiStringGuid={0x0fd96974,0x23aa,0x4cdc,{0xb9,0xcb,0x98,0xd1,0x77,0x50,0x32,0x2a}};
struct GopModeInfo {uint32_t version,horizontal,vertical,pixelFormat,masks[4],pixelsPerScanLine;};
struct GopMode {uint32_t maxMode,mode;GopModeInfo* info;size_t sizeOfInfo;uint64_t frameBufferBase;size_t frameBufferSize;};
struct Gop;
using GopBltFn=efi_status (EFIAPI *)(Gop*,uint32_t* buffer,uint32_t operation,size_t sourceX,size_t sourceY,
    size_t destinationX,size_t destinationY,size_t width,size_t height,size_t delta);
struct Gop {
    efi_status (EFIAPI *QueryMode)(Gop*,uint32_t,size_t*,GopModeInfo**);
    efi_status (EFIAPI *SetMode)(Gop*,uint32_t);
    GopBltFn Blt;
    GopMode* Mode;
};
enum : uint32_t {bltVideoFill=0,bltVideoToBuffer=1,bltBufferToVideo=2,bltVideoToVideo=3};
// UEFI 2.10 14.4 EFI_PCI_IO_PROTOCOL: only the configuration and memory
// read entries are used (r358, bochs DISPI registers).
static efi_guid pciIoGuid={0x4cf5b200,0x68b8,0x4ca5,{0x9e,0xec,0xb2,0x3e,0x3f,0x50,0x02,0x9a}};
struct PciIo;
using PciIoMemFn=efi_status (EFIAPI *)(PciIo*,uint32_t width,uint8_t bar,uint64_t offset,size_t count,void* buffer);
using PciIoConfigFn=efi_status (EFIAPI *)(PciIo*,uint32_t width,uint32_t offset,size_t count,void* buffer);
struct PciIo {
    void* PollMem;void* PollIo;
    struct {PciIoMemFn Read,Write;} Mem,Io;
    struct {PciIoConfigFn Read,Write;} Pci;
};
enum : uint32_t {pciIoWidth8=0,pciIoWidth16=1,pciIoWidth32=2};
// EFI_GRAPHICS_OUTPUT_BLT_PIXEL as one 32-bit value: blue, green, red, reserved.
struct FontDisplayInfo {uint32_t foreground,background,fontInfoMask;};
struct HiiFont;
using StringToImageFn=efi_status (EFIAPI *)(HiiFont*,uint32_t flags,const char16_t* string,const FontDisplayInfo* info,
    void** blt,size_t x,size_t y,void** rowInfo,size_t* rowCount,size_t* columnInfo);
using StringIdToImageFn=efi_status (EFIAPI *)(HiiFont*,uint32_t flags,void* packageList,uint16_t stringId,const char* language,
    const FontDisplayInfo* info,void** blt,size_t x,size_t y,void** rowInfo,size_t* rowCount,size_t* columnInfo);
struct HiiFont {
    StringToImageFn StringToImage;
    StringIdToImageFn StringIdToImage;
    void* GetGlyph;
    void* GetFontInfo;
};
struct HiiString {
    void* NewString;
    efi_status (EFIAPI *GetString)(HiiString*,const char* language,void* packageList,uint16_t stringId,
        char16_t* string,size_t* stringSize,void** fontInfo);
};
constexpr uint32_t hiiDirectToScreen=0x80; // EFI_HII_DIRECT_TO_SCREEN
}
