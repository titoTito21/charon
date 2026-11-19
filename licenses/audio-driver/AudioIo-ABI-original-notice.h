// ABI adapted from OpenCorePkg 1.0.8 Include/Acidanthera/Protocol/AudioIo.h.
// Copyright (c) 2018 John Davis
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
#pragma once
namespace hakaudioefi {
struct Protocol;
struct Port {uint32_t type,bits,frequencies,device,location,surface;};
using Callback=void (EFIAPI *)(Protocol*,void*);
struct Protocol {
    size_t revision;
    efi_status (EFIAPI *outputs)(Protocol*,Port**,size_t*);
    efi_status (EFIAPI *gain)(Protocol*,uint64_t,uint8_t,int8_t*);
    efi_status (EFIAPI *setup)(Protocol*,uint64_t,int8_t,uint32_t,uint32_t,uint8_t,size_t);
    efi_status (EFIAPI *play)(Protocol*,void*,size_t,size_t);
    efi_status (EFIAPI *playAsync)(Protocol*,void*,size_t,size_t,Callback,void*);
    efi_status (EFIAPI *stop)(Protocol*);
};
static efi_guid guid={0x22266891,0x2032,0x4bae,{0xb7,0xb5,0x43,0x74,0xe7,0x32,0x09,0x49}};
static_assert(sizeof(Port)==24 && sizeof(Protocol)==7*sizeof(size_t),"AudioIo ABI");
}
