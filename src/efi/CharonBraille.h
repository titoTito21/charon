// Charon braille (r390). GPL-2.0-or-later.
//
// A braille display in firmware: Baum protocol displays (Baum Vario,
// SuperVario, PocketVario, VarioConnect and the many displays that speak
// it) on USB through their FTDI serial chip. Firmware has no FTDI driver,
// so Charon drives the USB interface itself with EFI_USB_IO_PROTOCOL: FTDI
// vendor requests set 19200 8N1, bulk OUT carries Baum packets, bulk IN is
// polled (each 64-byte packet starts with two FTDI status bytes).
//
// The display shows the line of the review cursor (it follows the
// highlighted entry, the review keys, the paused review) in 8-dot computer
// braille (North American Braille Computer Code; Polish letters as in Polish
// braille plus dot 8, capitals with dot 7), the review cursor as dots 7 and 8. Display
// keys: d1 pan left, d2 previous line, d3 next line, d4 highlighted entry,
// d5 say the line, d6 pan right; a routing key moves the review cursor to its
// cell and says the character.
// Not covered: HID braille displays (USB HID usage page 0x41), Bluetooth,
// other protocols (Freedom Scientific, HandyTech, Papenmeier, Eurobraille).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <efi/system-table.h>
namespace hakefi::braille {
// ---- 8-dot computer braille ---------------------------------------------
constexpr uint8_t d1=0x01,d2=0x02,d3=0x04,d4=0x08,d5=0x10,d6=0x20,d7=0x40,d8=0x80;
inline uint8_t Cell(char16_t c) {
    static const uint8_t ascii[96]={
        0,d2|d3|d4|d6,d5,d3|d4|d5|d6,d1|d2|d4|d6,d1|d4|d6,d1|d2|d3|d4|d6,d3,                       //  !"#$%&'
        d1|d2|d3|d5|d6,d2|d3|d4|d5|d6,d1|d6,d3|d4|d6,d6,d3|d6,d4|d6,d3|d4,                           // ()*+,-./
        d3|d5|d6,d2,d2|d3,d2|d5,d2|d5|d6,d2|d6,d2|d3|d5,d2|d3|d5|d6,                                 // 01234567
        d2|d3|d6,d3|d5,d1|d5|d6,d5|d6,d1|d2|d6,d1|d2|d3|d4|d5|d6,d3|d4|d5,d1|d4|d5|d6,              // 89:;<=>?
        d4|d7,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                                                          // @A-O (letters below)
        0,0,0,0,0,0,0,0,0,0,0,d2|d4|d6|d7,d1|d2|d5|d6|d7,d1|d2|d4|d5|d6|d7,d4|d5|d7,d4|d5|d6|d7,     // P-Z[\]^_
        d4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                                                             // `a-o
        0,0,0,0,0,0,0,0,0,0,0,d2|d4|d6,d1|d2|d5|d6,d1|d2|d4|d5|d6,d4|d5,0};                           // p-z{|}~
    static const uint8_t letters[26]={d1,d1|d2,d1|d4,d1|d4|d5,d1|d5,d1|d2|d4,d1|d2|d4|d5,d1|d2|d5,d2|d4,d2|d4|d5,
        d1|d3,d1|d2|d3,d1|d3|d4,d1|d3|d4|d5,d1|d3|d5,d1|d2|d3|d4,d1|d2|d3|d4|d5,d1|d2|d3|d5,d2|d3|d4,d2|d3|d4|d5,
        d1|d3|d6,d1|d2|d3|d6,d2|d4|d5|d6,d1|d3|d4|d6,d1|d3|d4|d5|d6,d1|d3|d5|d6};
    if(c>=u'a' && c<=u'z')return letters[c-u'a'];
    if(c>=u'A' && c<=u'Z')return uint8_t(letters[c-u'A']|d7);
    if(c>=0x20 && c<0x80)return ascii[c-0x20];
    // Polish letters (Polish braille), capitals with dot 7.
    struct Pl {char16_t lower,upper;uint8_t dots;};
    static const Pl pl[]={{0x105,0x104,d1|d6},{0x107,0x106,d1|d4|d6},{0x119,0x118,d1|d5|d6},{0x142,0x141,d1|d2|d6},{0x144,0x143,d1|d4|d5|d6},
        {0xf3,0xd3,d3|d4|d6},{0x15b,0x15a,d2|d4|d6},{0x17a,0x179,d2|d3|d4|d6},{0x17c,0x17b,d1|d2|d3|d4|d6}};
    // r390: with dot 8 (the 6-dot Polish letters are ASCII signs in 8-dot NABCC: ł = '<', ś = '{').
    for(const auto& p:pl) {if(c==p.lower)return uint8_t(p.dots|d8);if(c==p.upper)return uint8_t(p.dots|d7|d8);}
    if(c>=0x2500 && c<=0x257f)return d2|d5;   // frame lines: a light mark
    if(c==0xa0)return 0;
    return d1|d4|d5|d6|d7|d8;                 // unknown: "?" with dots 7 and 8
}
// ---- USB (EFI_USB_IO_PROTOCOL, only the calls used here) ----------------
struct UsbRequest {uint8_t requestType,request;uint16_t value,index,length;};
struct UsbDeviceDescriptor {uint8_t length,type;uint16_t usb;uint8_t cls,subClass,protocol,maxPacket;uint16_t vendor,product,device;uint8_t manufacturer,productName,serial,configurations;};
struct UsbEndpointDescriptor {uint8_t length,type,address,attributes;uint16_t maxPacket;uint8_t interval;};
struct UsbIfDescriptor {uint8_t length,type,number,alternate,endpoints,interfaceClass,subClass,protocol,name;};
struct UsbIoApi {
    efi_status (EFIAPI *ControlTransfer)(UsbIoApi*,UsbRequest*,uint32_t direction,uint32_t timeout,void* data,size_t length,uint32_t* status);
    efi_status (EFIAPI *BulkTransfer)(UsbIoApi*,uint8_t endpoint,void* data,size_t* length,size_t timeout,uint32_t* status);
    void* AsyncInterruptTransfer;void* SyncInterruptTransfer;void* IsochronousTransfer;void* AsyncIsochronousTransfer;
    efi_status (EFIAPI *GetDeviceDescriptor)(UsbIoApi*,UsbDeviceDescriptor*);
    void* GetConfigDescriptor;
    efi_status (EFIAPI *GetInterfaceDescriptor)(UsbIoApi*,UsbIfDescriptor*);
    efi_status (EFIAPI *GetEndpointDescriptor)(UsbIoApi*,uint8_t index,UsbEndpointDescriptor*);
    void* GetStringDescriptor;void* GetSupportedLanguages;void* PortReset;
};
enum class Key : uint8_t {none,panLeft,panRight,lineUp,lineDown,highlight,sayLine,route};
struct Event {Key key=Key::none;uint8_t cell=0;};
class Display {
public:
    static constexpr size_t maxCells=84;
    bool Present() const {return io_!=nullptr;}
    size_t Cells() const {return cells_;}
    const char* Name() const {return name_;}
    // Finds a Baum display on USB (FTDI 0403 with Baum product ids, or
    // Baum's own vendor id 0904) and asks it for its identity and size.
    bool Find(efi_boot_services* boot) {
        if(io_ || !boot || !boot->LocateHandleBuffer)return io_!=nullptr;
        efi_guid guid={0x2b2f68d6,0x0cd2,0x44cf,{0x8e,0x8b,0xbb,0xa2,0x0b,0x1b,0x5b,0x75}};
        size_t count=0;efi_handle* handles=nullptr;
        if(boot->LocateHandleBuffer(ByProtocol,&guid,nullptr,&count,&handles)!=EFI_SUCCESS || !handles)return false;
        for(size_t i=0;i<count && !io_;++i) {
            UsbIoApi* io=nullptr;
            if(boot->HandleProtocol(handles[i],&guid,(void**)&io)!=EFI_SUCCESS || !io || !io->GetDeviceDescriptor || !io->BulkTransfer)continue;
            UsbDeviceDescriptor dev{};if(io->GetDeviceDescriptor(io,&dev)!=EFI_SUCCESS)continue;
            const bool baum=(dev.vendor==0x0403 && (dev.product==0xfe70 || dev.product==0xfe71 || dev.product==0xfe72 || dev.product==0xfe73 || dev.product==0xfe74
                || dev.product==0xfe75 || dev.product==0xfe76 || dev.product==0xfe77)) || dev.vendor==0x0904;
            if(!baum)continue;
            UsbIfDescriptor itf{};if(!io->GetInterfaceDescriptor || io->GetInterfaceDescriptor(io,&itf)!=EFI_SUCCESS)continue;
            uint8_t in=0,out=0;
            for(uint8_t e=0;e<itf.endpoints && io->GetEndpointDescriptor;++e) {
                UsbEndpointDescriptor ep{};if(io->GetEndpointDescriptor(io,e,&ep)!=EFI_SUCCESS || (ep.attributes&3)!=2)continue;
                if(ep.address&0x80)in=ep.address;else out=ep.address;
            }
            if(!in || !out)continue;
            io_=io;in_=in;out_=out;ftdi_=dev.vendor==0x0403;handle_=handles[i];boot_=boot;
        }
        boot->FreePool(handles);
        if(!io_)return false;
        if(ftdi_)SetupFtdi();
        cells_=40;name_[0]=0;
        // Identity and cell count; answers arrive with the next polls.
        const uint8_t identity[]={0x1b,0x84};Write(identity,2);
        return true;
    }
    // Shows text (one line) with the cursor at `cursor` (SIZE_MAX: none);
    // `offset` is the first character shown (panning).
    // r393: `marks` (may be null) adds dots 7 and 8 to highlighted cells.
    void Show(const char16_t* text,size_t length,size_t offset,size_t cursor,const bool* marks=nullptr) {
        if(!Alive())return;
        uint8_t cells[maxCells]={};
        for(size_t k=0;k<cells_;++k) {
            const size_t i=offset+k;
            uint8_t dots=i<length?Cell(text[i]):0;
            if(i==cursor)dots|=d7|d8;
            if(marks && i<length && marks[i] && text[i]!=u' ')dots|=d7|d8;
            cells[k]=dots;
        }
        bool same=true;for(size_t k=0;k<cells_;++k)if(cells[k]!=shown_[k])same=false;
        if(same && shownOnce_)return;
        for(size_t k=0;k<cells_;++k)shown_[k]=cells[k];shownOnce_=true;
        uint8_t packet[2+maxCells*2];size_t n=0;packet[n++]=0x1b;packet[n++]=0x01;
        for(size_t k=0;k<cells_;++k) {packet[n++]=cells[k];if(cells[k]==0x1b)packet[n++]=0x1b;}   // ESC is doubled
        Write(packet,n);
    }
    // Reads what the display sent; returns one key event (or none).
    Event Poll() {
        Event event{};
        if(!Alive())return event;
        uint8_t buffer[64];size_t length=sizeof(buffer);uint32_t status=0;
        if(io_->BulkTransfer(io_,in_,buffer,&length,1,&status)!=EFI_SUCCESS || !length)return event;
        size_t from=ftdi_?2:0;   // FTDI status bytes
        for(size_t i=from;i<length;++i)Feed(buffer[i],event);
        return event;
    }
private:
    // The display may be unplugged: its UsbIo instance is then freed by the bus
    // driver. True only while the handle still carries this very instance;
    // otherwise the display is forgotten (Present() false, Find looks again).
    bool Alive() {
        if(!io_)return false;
        efi_guid guid={0x2b2f68d6,0x0cd2,0x44cf,{0x8e,0x8b,0xbb,0xa2,0x0b,0x1b,0x5b,0x75}};void* now=nullptr;
        if(boot_ && handle_ && boot_->HandleProtocol(handle_,&guid,&now)==EFI_SUCCESS && now==static_cast<void*>(io_))return true;
        io_=nullptr;handle_=nullptr;shownOnce_=false;state_=0;escaped_=false;topKeys_=0;return false;
    }
    void SetupFtdi() {
        // Reset, 19200 baud (Baum's rate: divisor 0x809c of 3 MHz/16), 8N1, no flow control.
        auto vendor=[&](uint8_t request,uint16_t value) {UsbRequest r{0x40,request,value,0,0};uint32_t s=0;io_->ControlTransfer(io_,&r,2/*EfiUsbNoData*/,100,nullptr,0,&s);};
        vendor(0,0);vendor(3,0x809c);vendor(4,8);vendor(2,0);
    }
    void Write(const uint8_t* data,size_t length) {
        uint8_t copy[2+maxCells*2];for(size_t i=0;i<length && i<sizeof(copy);++i)copy[i]=data[i];
        size_t n=length;uint32_t status=0;io_->BulkTransfer(io_,out_,copy,&n,100,&status);
    }
    // Baum packets: ESC code payload; ESC inside the payload is doubled.
    void Feed(uint8_t byte,Event& event) {
        if(state_==0) {if(byte==0x1b)state_=1;return;}
        if(state_==1) {code_=byte;got_=0;state_=2;need_=Need(code_);if(!need_)state_=0;return;}
        if(byte==0x1b && !escaped_) {escaped_=true;return;}
        if(escaped_ && byte!=0x1b) {escaped_=false;code_=byte;got_=0;need_=Need(code_);state_=need_?2:0;return;}   // a new packet
        escaped_=false;
        if(got_<sizeof(payload_))payload_[got_]=byte;
        if(++got_<need_)return;
        state_=0;Packet(event);
    }
    size_t Need(uint8_t code) const {
        switch(code) {
            case 0x01:return 1;                     // cell count
            case 0x84:return 16;                    // device identity
            case 0x24:return 1;                     // top (display) keys
            case 0x22:return (cells_+7)/8;          // routing keys
            case 0x8a:return 8;                     // serial number
            default:return 1;
        }
    }
    void Packet(Event& event) {
        switch(code_) {
            case 0x01: {const size_t n=payload_[0];if(n>=8 && n<=maxCells) {cells_=n;shownOnce_=false;}break;}
            case 0x84: {size_t k=0;for(;k<16;++k)name_[k]=char(payload_[k]>=0x20 && payload_[k]<0x7f?payload_[k]:' ');name_[k]=0;
                while(k && name_[k-1]==' ')name_[--k]=0;
                // "Baum Vario 40" style names carry the size.
                size_t n=0;for(size_t i=0;name_[i];++i)if(name_[i]>='0' && name_[i]<='9')n=n*10+size_t(name_[i]-'0');else if(n && name_[i]==' ')break;
                if(n>=8 && n<=maxCells) {cells_=n;shownOnce_=false;}
                break;}
            case 0x24: {
                const uint8_t keys=payload_[0],pressed=uint8_t(keys&~topKeys_);topKeys_=keys;
                if(pressed&1)event.key=Key::panLeft;else if(pressed&2)event.key=Key::lineUp;else if(pressed&4)event.key=Key::lineDown;
                else if(pressed&8)event.key=Key::highlight;else if(pressed&16)event.key=Key::sayLine;else if(pressed&32)event.key=Key::panRight;
                break;}
            case 0x22: {
                for(size_t i=0;i<(cells_+7)/8 && i<sizeof(payload_);++i)if(payload_[i])
                    for(uint8_t b=0;b<8;++b)if(payload_[i]&(1u<<b)) {event.key=Key::route;event.cell=uint8_t(i*8+b);return;}
                break;}
            default:break;
        }
    }
    UsbIoApi* io_=nullptr;uint8_t in_=0,out_=0;bool ftdi_=false;
    efi_handle handle_=nullptr;efi_boot_services* boot_=nullptr;   // to see an unplugged display (Alive)
    size_t cells_=40;char name_[17]={};
    uint8_t shown_[maxCells]={};bool shownOnce_=false;
    uint8_t state_=0,code_=0,topKeys_=0;size_t got_=0,need_=0;bool escaped_=false;uint8_t payload_[16]={};
};
}  // namespace hakefi::braille
