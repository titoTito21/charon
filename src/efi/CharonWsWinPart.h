#pragma once
// r409: Windows without a USB stick - the installer's files on a temporary partition of the target disk.
//
// Why not a RAM disk (measured in VMware Workstation, Windows 11 Setup build 26100.9457, see
// tests/charon-r409-windows-ram-results.txt): the firmware has EFI_RAM_DISK_PROTOCOL, but Windows Setup's
// WinPE mounts no disk for a memory range described in the ACPI NFIT - neither as a volatile or persistent
// virtual disk, nor as a virtual CD, nor as a persistent memory region - although the NVDIMM root device
// (ACPI0012) is found and its bus driver loads. After WinPE has left the boot services a RAM disk does not
// exist for it, so Setup has no sources. The way that works on every firmware is a real partition:
//
//   1. after all questions and the typed confirmation, Charon makes a FAT32 partition at the end of the free
//      space of the target disk (type: basic data, name "Charon Windows media"), sized for the image;
//      "whole disk": Charon writes the new partition table itself - EFI system partition, Microsoft reserved,
//      Windows, media - puts itself back on the new EFI partition at once (with its firmware entries, and with
//      its answer sets and plan state when the disk was its own) and the answer file installs into the
//      Windows partition without wiping the disk;
//   2. the image is downloaded once and written onto that partition exactly as onto a USB stick
//      (winusb::Make: split WIM), then the same answer file, NVDA for Setup and Charon's files as on a stick;
//   3. at the first sign-in, after Charon is installed from it, Charon-MediaCleanup.ps1 removes the partition
//      and gives the space to Windows (moving the recovery partition behind it when Setup made one).
// When a step fails after the partition was added, the partition entry is removed again (Undo).
// GPL-2.0-or-later.
#ifdef HAK_CHARON_HTTPS
namespace hakefi {
namespace ws {
namespace winpart {
constexpr uint64_t mega=uint64_t(1)<<20;
struct State {
    bool made,changed,whole;
    media::BlockIo* io;efi_handle disk;uint64_t blocks;
    seeddisk::Guid unique;uint64_t firstLba;
    efi_handle volume;
};
inline State& W() {static State state;return state;}
inline bool Changed() {return W().changed;}
// The partition's unique GUID as Windows prints it (lower case, no braces).
inline void GuidText(const uint8_t* g,char* out) {
    static const char hex[]="0123456789abcdef";static const int order[16]={3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15};size_t n=0;
    for(int i=0;i<16;++i) {if(i==4 || i==6 || i==8 || i==10)out[n++]='-';out[n++]=hex[g[order[i]]>>4];out[n++]=hex[g[order[i]]&15];}
    out[n]=0;
}
// Removes the media partition's entry again (a failed attempt leaves no empty partition behind).
inline void Undo() {
    auto& w=W();
    if(!w.made || !w.io)return;
    // The disk's block service is asked for again by its handle: the pointer kept from before the partitions were
    // read anew is used only when the handle no longer answers.
    {media::BlockIo* now=nullptr;
     if(w.disk && U().host->boot->HandleProtocol(w.disk,&media::blockIoGuid,(void**)&now)==EFI_SUCCESS && now)w.io=now;}
    seeddisk::Disk sd{w.io,512,w.blocks,images::SeedRead,images::SeedWrite};
    const auto error=seeddisk::RemovePartition(sd,images::SeedWork(),seeddisk::kWorkBytes,w.unique,seeddisk::kTypeBasicData,w.firstLba);
    charontrace::Value("CHARON winpart undo",uint64_t(error));
    if(w.io->FlushBlocks)reinterpret_cast<images::BlockFlush>(w.io->FlushBlocks)(w.io);
    w.made=false;w.volume=nullptr;
    Say(error==seeddisk::Error::None?T(u"Usunąłem tymczasową partycję instalatora.",u"I removed the installer's temporary partition.")
        :T(u"Uwaga: nie udało się usunąć tymczasowej partycji instalatora o nazwie Charon Windows media; można ją usunąć w zarządzaniu dyskami.",
           u"Note: the installer's temporary partition, named Charon Windows media, could not be removed; it can be deleted in a disk management tool."));
}
inline bool Add(const seeddisk::Disk& sd,const seeddisk::Guid& type,const seeddisk::Guid& unique,const char16_t* name,uint64_t firstLba,uint64_t bytes,seeddisk::AddResult& added) {
    seeddisk::AddRequest request{};request.type=type;request.unique=unique;request.name=name;request.sizeBytes=bytes;request.alignmentBytes=0;
    request.placement=seeddisk::Placement::At;request.firstLba=firstLba;request.attributes=0;
    return seeddisk::AddPartition(sd,images::SeedWork(),seeddisk::kWorkBytes,request,added)==seeddisk::Error::None;
}
inline size_t DiskByGuid(const uint8_t* guid) {
    auto& s=disks::D();
    for(size_t i=0;i<s.count;++i) {bool same=true;for(int b=0;b<16 && same;++b)same=s.list[i].guid[b]==guid[b];if(same)return i;}
    return SIZE_MAX;
}
inline size_t PartByFirst(const disks::Disk& d,uint64_t first) {
    for(size_t i=0;i<d.partCount;++i)if(d.parts[i].first==first)return i;
    return SIZE_MAX;
}
// r409: the media partition of an earlier attempt (the installation was stopped after the download, the computer was
// switched off, Setup refused the computer). It is found by its GPT name on a fixed disk. It is used again as it is -
// nothing is downloaded twice - and it is NEVER treated as an ordinary stick: an answer file that wipes the disk would
// make Windows Setup erase the very partition it is running from.
struct Kept {uint64_t first,blocks;};
inline Kept& K() {static Kept kept;return kept;}
inline bool Locate(efi_handle volume,size_t& disk,size_t& part) {
    size_t bytes=0;const uint8_t* p=DevicePathOf(volume,bytes);if(!p)return false;
    const uint8_t* unique=nullptr;
    for(size_t at=0;at+4<=bytes;) {
        const uint8_t type=p[at],sub=p[at+1];const size_t length=size_t(p[at+2])|(size_t(p[at+3])<<8);
        if(length<4 || type==0x7f)break;
        if(type==4 && sub==1 && length>=42 && p[at+40]==2 && p[at+41]==2)unique=p+at+24;
        at+=length;
    }
    if(!unique)return false;
    auto& s=disks::D();disks::Load();
    for(size_t i=0;i<s.count;++i)for(size_t k=0;k<s.list[i].partCount;++k) {
        bool same=true;for(int b=0;b<16 && same;++b)same=s.list[i].parts[k].unique[b]==unique[b];
        if(same) {disk=i;part=k;return true;}
    }
    return false;
}
inline bool IsLeftover(efi_handle volume) {
    size_t disk=0,part=0;if(!volume || !Locate(volume,disk,part))return false;
    const auto& d=disks::D().list[disk];
    return !d.usb && !d.removable && SameFold(d.parts[part].name,u"Charon Windows media");
}
// winusb::Make calls this once the image's size is known. Null: said; nothing usable was made.
inline efi_handle Provide(uint64_t total) {
    auto& w=W();auto& s=disks::D();auto& t=install::Chosen();
    if(t.disk>=s.count)return nullptr;
    const disks::Disk d=s.list[t.disk];   // a copy: the list is read again below
    if(d.blockSize!=512) {Fail(T(u"Ten dysk ma sektory większe niż 512 bajtów; tymczasową partycję instalatora Charon potrafi na razie utworzyć tylko na dysku z sektorami po 512 bajtów. Użyj pendrive'a.",
        u"This disk has sectors larger than 512 bytes; for now Charon can make the installer's temporary partition only on a disk with 512-byte sectors. Use a USB stick."));return nullptr;}
    const bool whole=ask::Is("diskmode","whole");
    const uint64_t charon=hakefi::selfcopy::Ready()?hakefi::selfcopy::Total():uint64_t(256)<<20;
    uint64_t mediaBytes=(total+charon+(uint64_t(700)<<20)+mega-1)/mega*mega;
    const bool kept=whole && K().first!=0;   // the media partition of an earlier attempt stays where it is, with its files
    if(kept)mediaBytes=K().blocks*512;
    const uint64_t mediaBlocks=mediaBytes/512;
    const uint64_t windowsLeast=uint64_t(12)<<30;   // the floor the dialogue uses for any system
    static char16_t text[900];
    seeddisk::Disk sd{d.io,d.blockSize,d.blocks,images::SeedRead,images::SeedWrite};
    seeddisk::Guid unique;images::RandomGuid(unique,131);
    uint64_t tempFirst=0,oldTargetFirst=0;
    uint8_t diskGuid[16];for(int i=0;i<16;++i)diskGuid[i]=d.guid[i];
    if(ask::Is("diskmode","partition") && t.part<d.partCount)oldTargetFirst=d.parts[t.part].first;
    auto sizes=[&](const char16_t* pl,const char16_t* en,uint64_t have) {
        Set(text,900,T(pl,en));systools::PutSize(text,900,total,Pl());
        Cat(text,900,T(u", a tymczasowa partycja na pliki instalatora musi mieć ",u", and the temporary partition for the installer's files must have "));systools::PutSize(text,900,mediaBytes,Pl());
        Cat(text,900,T(u"; dostępne jest ",u"; available: "));systools::PutSize(text,900,have,Pl());
        Cat(text,900,T(u". Zwolnij miejsce na dysku, wybierz wymazanie całego dysku albo użyj pendrive'a.",u". Free some disk space, choose to erase the whole disk, or use a USB stick."));Fail(text);};
    if(whole) {
        const uint64_t espBytes=((charon*5/4+(uint64_t(100)<<20)>(uint64_t(300)<<20)?charon*5/4+(uint64_t(100)<<20):uint64_t(300)<<20)+mega-1)/mega*mega;
        const uint64_t msrBytes=uint64_t(16)<<20;
        if(disks::Bytes(d)<espBytes+msrBytes+mediaBytes+windowsLeast+4*mega) {
            sizes(u"Dysk jest za mały na Windows razem z plikami instalatora. Obraz Windows ma ",u"The disk is too small for Windows together with the installer's files. The Windows image has ",disks::Bytes(d));return nullptr;}
        if(d.hasCharon && !hakefi::selfram::Active() && !images::EnsureInMemory())return nullptr;
        if(kept)Set(text,900,T(u"Pliki instalatora z poprzedniej próby zostają na swojej tymczasowej partycji. Tworzę nową tablicę partycji: partycja systemowa EFI z Charonem, partycja Windows i ta sama tymczasowa partycja na pliki instalatora, ",
            u"The installer's files of the earlier attempt stay on their temporary partition. I am writing a new partition table: an EFI system partition with Charon, the Windows partition and the same temporary partition for the installer's files, "));
        else {Set(text,900,T(u"Obraz Windows ma ",u"The Windows image has "));systools::PutSize(text,900,total,Pl());
        Cat(text,900,T(u". Tworzę nową tablicę partycji: partycja systemowa EFI z Charonem, partycja Windows i na końcu dysku tymczasowa partycja na pliki instalatora, ",
            u". I am writing a new partition table: an EFI system partition with Charon, the Windows partition and, at the end of the disk, a temporary partition for the installer's files, "));}
        systools::PutSize(text,900,mediaBytes,Pl());Cat(text,900,T(u". Windows usunie ją przy pierwszym logowaniu i odzyska to miejsce.",u". Windows removes it at the first sign-in and takes the space back."));Say(text);
        seeddisk::Guid newDisk;images::RandomGuid(newDisk,132);
        if(!dossys::EmptyGpt(d.io,d.blocks,newDisk)) {Fail(T(u"Nie udało się zapisać tablicy partycji.",u"The partition table could not be written."));return nullptr;}
        w.changed=true;w.whole=true;
        for(int i=0;i<16;++i)diskGuid[i]=newDisk.bytes[i];
        seeddisk::Guid espUnique,msrUnique,winUnique;images::RandomGuid(espUnique,133);images::RandomGuid(msrUnique,134);images::RandomGuid(winUnique,135);
        const uint64_t espFirst=2048,msrFirst=espFirst+espBytes/512,winFirst=msrFirst+msrBytes/512;
        tempFirst=kept?K().first:(d.blocks-33-mediaBlocks)/2048*2048;
        if(tempFirst<winFirst+windowsLeast/512) {Fail(T(u"Na dysku nie ma miejsca na Windows przed tymczasową partycją instalatora.",u"The disk has no room for Windows before the installer's temporary partition."));return nullptr;}
        seeddisk::AddResult esp{},msr{},win{},temp{};
        if(!Add(sd,seeddisk::kTypeEfiSystem,espUnique,u"EFI system partition",espFirst,espBytes,esp)
            || !Add(sd,seeddisk::kTypeMicrosoftReserved,msrUnique,u"Microsoft reserved partition",msrFirst,msrBytes,msr)
            || !Add(sd,seeddisk::kTypeBasicData,winUnique,u"Basic data partition",winFirst,(tempFirst-winFirst)*512,win)
            || !Add(sd,seeddisk::kTypeBasicData,unique,u"Charon Windows media",tempFirst,mediaBytes,temp)) {
            Fail(T(u"Nie udało się utworzyć partycji. Dysk ma już nową, pustą tablicę partycji.",u"The partitions could not be created. The disk already has a new, empty partition table."));return nullptr;}
        w.made=true;w.io=d.io;w.disk=d.handle;w.blocks=d.blocks;w.unique=unique;w.firstLba=temp.firstLba;
        uint32_t lo,hi;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));
        dossys::Zero(d.io,win.firstLba,2048);   // nothing of an older file system may show through where Windows will be
        if(!dossys::FormatFat32(d.io,esp.firstLba,esp.lbaCount,"SYSTEM",lo^hi) || (!kept && !dossys::FormatFat32(d.io,temp.firstLba,temp.lbaCount,"CHARONWIN",hi^(lo*2654435761u)))) {
            Fail(T(u"Formatowanie nowych partycji nie powiodło się.",u"Formatting the new partitions failed."));return nullptr;}
        if(d.io->FlushBlocks)reinterpret_cast<images::BlockFlush>(d.io->FlushBlocks)(d.io);
        images::Reconnect(d.handle);
        // Charon back on the computer before Windows Setup starts (its own disk: with answer sets and plan state).
        if(const efi_handle espVolume=images::VolumeOfPartition(espUnique.bytes)) {
            if(images::CharonOnto(espVolume,d.hasCharon)) {
                efi_status status=dossys::PutOption(true,u"Charon voice",espVolume,u"\\EFI\\HAK\\CHARONVOICE.EFI",true,nullptr,nullptr,true);
                if(status==EFI_UNSUPPORTED)status=dossys::PutOption(true,u"Charon voice",espVolume,u"\\EFI\\HAK\\CHARONVOICE.EFI",true,nullptr);
                efi_status manager=dossys::PutOption(false,u"Charon",espVolume,u"\\EFI\\HAK\\HAKBOOT.EFI",true,nullptr,nullptr,true);
                if(manager==EFI_UNSUPPORTED)manager=dossys::PutOption(false,u"Charon",espVolume,u"\\EFI\\HAK\\HAKBOOT.EFI",true,nullptr);
                charontrace::Value("CHARON winpart entries",uint64_t(status)|(uint64_t(manager)<<32));
                Say(status==EFI_SUCCESS && manager==EFI_SUCCESS?T(u"Charon jest z powrotem na partycji systemowej EFI tego dysku.",u"Charon is back on this disk's EFI system partition.")
                    :T(u"Pliki Charona są na nowej partycji systemowej EFI, ale firmware nie przyjęło jego wpisów; Windows doda je przy pierwszym logowaniu.",
                       u"Charon's files are on the new EFI system partition, but the firmware did not accept its entries; Windows adds them at the first sign-in."));
            } else Say(T(u"Uwaga: nie udało się skopiować Charona na nową partycję systemową EFI; Windows zainstaluje go przy pierwszym logowaniu.",
                         u"Note: Charon could not be copied to the new EFI system partition; Windows installs it at the first sign-in."));
        }
        disks::Load();
        t.disk=DiskByGuid(diskGuid);
        if(t.disk==SIZE_MAX || (t.part=PartByFirst(s.list[t.disk],win.firstLba))==SIZE_MAX) {Fail(T(u"Firmware nie widzi nowych partycji.",u"The firmware does not see the new partitions."));return nullptr;}
        ask::Put("diskmode","partition");   // the answer file installs into the partition Charon made; it does not wipe the disk
    } else {
        if(!d.gpt) {Fail(T(u"Ten dysk nie ma tablicy partycji GPT, więc nie da się na nim dodać tymczasowej partycji instalatora. Wybierz wymazanie całego dysku albo użyj pendrive'a.",
            u"This disk has no GPT partition table, so the installer's temporary partition cannot be added to it. Choose to erase the whole disk, or use a USB stick."));return nullptr;}
        const uint64_t gapBytes=d.freeBlocks*512,need=mediaBytes+2*mega+(ask::Is("diskmode","free")?windowsLeast:0);
        if(gapBytes<need) {
            sizes(u"Na dysku jest za mało wolnego miejsca poza partycjami. Obraz Windows ma ",u"The disk has too little free space outside its partitions. The Windows image has ",gapBytes);return nullptr;}
        if(d.hasCharon && !hakefi::selfram::Active() && !images::EnsureInMemory())return nullptr;   // the table of Charon's own disk is rewritten
        tempFirst=(d.freeFirst+d.freeBlocks-mediaBlocks)/2048*2048;
        if(tempFirst<d.freeFirst) {sizes(u"Na dysku jest za mało wolnego miejsca poza partycjami. Obraz Windows ma ",u"The disk has too little free space outside its partitions. The Windows image has ",gapBytes);return nullptr;}
        Set(text,900,T(u"Obraz Windows ma ",u"The Windows image has "));systools::PutSize(text,900,total,Pl());
        Cat(text,900,T(u". Na końcu wolnego miejsca tworzę tymczasową partycję na pliki instalatora, ",u". At the end of the free space I am making a temporary partition for the installer's files, "));
        systools::PutSize(text,900,mediaBytes,Pl());Cat(text,900,T(u". Windows usunie ją przy pierwszym logowaniu.",u". Windows removes it at the first sign-in."));Say(text);
        seeddisk::AddResult temp{};
        if(!Add(sd,seeddisk::kTypeBasicData,unique,u"Charon Windows media",tempFirst,mediaBytes,temp)) {
            Fail(T(u"Nie udało się dodać tymczasowej partycji instalatora. Na dysku nic się nie zmieniło.",u"The installer's temporary partition could not be added. Nothing changed on the disk."));return nullptr;}
        w.made=true;w.changed=true;w.whole=false;w.io=d.io;w.disk=d.handle;w.blocks=d.blocks;w.unique=unique;w.firstLba=temp.firstLba;
        uint32_t lo,hi;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));
        if(!dossys::FormatFat32(d.io,temp.firstLba,temp.lbaCount,"CHARONWIN",lo^hi)) {Fail(T(u"Formatowanie tymczasowej partycji nie powiodło się.",u"Formatting the temporary partition failed."));Undo();return nullptr;}
        if(d.io->FlushBlocks)reinterpret_cast<images::BlockFlush>(d.io->FlushBlocks)(d.io);
        images::Reconnect(d.handle);
        disks::Load();
        t.disk=DiskByGuid(diskGuid);
        if(t.disk!=SIZE_MAX && oldTargetFirst)t.part=PartByFirst(s.list[t.disk],oldTargetFirst);
        if(t.disk==SIZE_MAX || (oldTargetFirst && t.part==SIZE_MAX)) {Fail(T(u"Firmware nie widzi dysku po zmianie tablicy partycji.",u"The firmware does not see the disk after its partition table changed."));Undo();return nullptr;}
    }
    w.volume=images::VolumeOfPartition(unique.bytes);
    if(!w.volume) {Fail(T(u"Firmware nie widzi tymczasowej partycji instalatora.",u"The firmware does not see the installer's temporary partition."));Undo();return nullptr;}
    char guid[40];GuidText(unique.bytes,guid);ask::Put("wmediaguid",guid);
    return w.volume;
}
// r409, a multi-system plan (CharonWsPlanDialogue.h InstallWindows): Windows has a range of its own in the plan's
// layout, and everything of its installation stays inside that range. The plan's runner has made the range free
// space; once the image's size is known, the Windows partition is made at the start of the range and the media
// partition at its end. At the first sign-in the media partition goes and Windows takes the space back (the
// clean-up script looks only at what lies before the end of the removed partition), so the other systems' ranges
// are never touched and never counted.
struct PlanRange {bool on;uint64_t first,count;};
inline PlanRange& InPlan() {static PlanRange range;return range;}
inline efi_handle ProvidePlan(uint64_t total) {
    auto& w=W();auto& s=disks::D();auto& t=install::Chosen();const PlanRange range=InPlan();
    if(t.disk>=s.count || !range.on)return nullptr;
    const disks::Disk d=s.list[t.disk];
    if(d.blockSize!=512) {Fail(T(u"Ten dysk ma sektory większe niż 512 bajtów; tymczasową partycję instalatora Charon potrafi na razie utworzyć tylko na dysku z sektorami po 512 bajtów.",
        u"This disk has sectors larger than 512 bytes; for now Charon can make the installer's temporary partition only on a disk with 512-byte sectors."));return nullptr;}
    const uint64_t charon=hakefi::selfcopy::Ready()?hakefi::selfcopy::Total():uint64_t(256)<<20;
    const uint64_t mediaBytes=(total+charon+(uint64_t(700)<<20)+mega-1)/mega*mega,mediaBlocks=mediaBytes/512;
    const uint64_t windowsLeast=uint64_t(20)<<30;
    static char16_t text[700];
    if(range.count*512<mediaBytes+windowsLeast) {
        Set(text,700,T(u"Zakres, który plan przeznaczył na Windows, jest za mały na Windows razem z plikami instalatora. Obraz Windows ma ",u"The range the plan gave to Windows is too small for Windows together with the installer's files. The Windows image has "));
        systools::PutSize(text,700,total,Pl());Cat(text,700,T(u", a zakres ",u", and the range "));systools::PutSize(text,700,range.count*512,Pl());Cat(text,700,u".");Fail(text);return nullptr;}
    const uint64_t tempFirst=(range.first+range.count-mediaBlocks)/2048*2048;
    Set(text,700,T(u"Obraz Windows ma ",u"The Windows image has "));systools::PutSize(text,700,total,Pl());
    Cat(text,700,T(u". W zakresie, który plan przeznaczył na Windows, tworzę partycję Windows, a na jego końcu tymczasową partycję na pliki instalatora, ",
        u". Inside the range the plan gave to Windows I am making the Windows partition and, at the end of the range, a temporary partition for the installer's files, "));
    systools::PutSize(text,700,mediaBytes,Pl());Cat(text,700,T(u". Windows usunie ją przy pierwszym logowaniu i odzyska to miejsce.",u". Windows removes it at the first sign-in and takes the space back."));Say(text);
    seeddisk::Disk sd{d.io,d.blockSize,d.blocks,images::SeedRead,images::SeedWrite};
    seeddisk::Guid unique,winUnique;images::RandomGuid(unique,136);images::RandomGuid(winUnique,137);
    uint8_t diskGuid[16];for(int i=0;i<16;++i)diskGuid[i]=d.guid[i];
    seeddisk::AddResult win{},temp{};
    if(!Add(sd,seeddisk::kTypeBasicData,winUnique,u"Basic data partition",range.first,(tempFirst-range.first)*512,win)) {
        Fail(T(u"Nie udało się utworzyć partycji Windows w jej zakresie. Poza tym zakresem na dysku nic się nie zmieniło.",u"The Windows partition could not be made in its range. Nothing outside that range changed on the disk."));return nullptr;}
    w.changed=true;w.whole=false;
    if(!Add(sd,seeddisk::kTypeBasicData,unique,u"Charon Windows media",tempFirst,mediaBytes,temp)) {
        Fail(T(u"Nie udało się dodać tymczasowej partycji instalatora. Poza zakresem Windows na dysku nic się nie zmieniło.",u"The installer's temporary partition could not be added. Nothing outside the range of Windows changed on the disk."));return nullptr;}
    w.made=true;w.io=d.io;w.disk=d.handle;w.blocks=d.blocks;w.unique=unique;w.firstLba=temp.firstLba;
    uint32_t lo,hi;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));
    dossys::Zero(d.io,win.firstLba,2048);   // nothing of an older file system may show through where Windows will be
    if(!dossys::FormatFat32(d.io,temp.firstLba,temp.lbaCount,"CHARONWIN",lo^hi)) {Fail(T(u"Formatowanie tymczasowej partycji nie powiodło się.",u"Formatting the temporary partition failed."));Undo();return nullptr;}
    if(d.io->FlushBlocks)reinterpret_cast<images::BlockFlush>(d.io->FlushBlocks)(d.io);
    images::Reconnect(d.handle);
    disks::Load();
    t.disk=DiskByGuid(diskGuid);
    if(t.disk==SIZE_MAX || (t.part=PartByFirst(s.list[t.disk],win.firstLba))==SIZE_MAX) {Fail(T(u"Firmware nie widzi nowych partycji.",u"The firmware does not see the new partitions."));Undo();return nullptr;}
    w.volume=images::VolumeOfPartition(unique.bytes);
    if(!w.volume) {Fail(T(u"Firmware nie widzi tymczasowej partycji instalatora.",u"The firmware does not see the installer's temporary partition."));Undo();return nullptr;}
    char guid[40];GuidText(unique.bytes,guid);ask::Put("wmediaguid",guid);
    return w.volume;
}
// What Windows runs at the first sign-in, from %ProgramData%\Charon: the media partition goes, Windows gets the space.
inline constexpr char cleanupScript[]=R"PS(# Charon-MediaCleanup.ps1 - written by Charon (CharonWsWinPart.h). Removes the temporary partition that held the
# Windows installer's files and gives its space to Windows. -Guid: the partition's unique GUID.
param([string]$Guid)
$ErrorActionPreference='Continue'
$log=Join-Path $env:ProgramData 'Charon\media-cleanup.log'
function Note($t){Add-Content -Path $log -Value ((Get-Date -Format s)+' '+$t)}
try{
 $p=Get-Partition | Where-Object {$_.Guid -eq ('{'+$Guid+'}')}
 if(-not $p){Note 'media partition not found';exit 0}
 $disk=$p.DiskNumber
 $limit=$p.Offset+$p.Size
 $sys=Get-Partition -DriveLetter $env:SystemDrive.Substring(0,1)
 # The answer file holds the password in clear text: overwritten before the partition's entry goes (removing a
 # partition leaves its blocks as they are).
 if($p.DriveLetter -and [char]::IsLetter($p.DriveLetter)){
  foreach($name in 'Autounattend.xml','sources\Autounattend.xml'){
   $file=Join-Path ([string]$p.DriveLetter+':\') $name
   if(Test-Path -LiteralPath $file){try{$n=(Get-Item -LiteralPath $file -Force).Length;[IO.File]::WriteAllBytes($file,(New-Object byte[] $n));Remove-Item -LiteralPath $file -Force;Note ($name+' overwritten and deleted')}catch{Note ($name+' could not be overwritten: '+$_)}}
  }
 } else {Note 'the media partition has no drive letter; the answer file was not overwritten'}
 Remove-Partition -DiskNumber $disk -PartitionNumber $p.PartitionNumber -Confirm:$false -ErrorAction Stop
 Note 'media partition removed'
 if($sys.DiskNumber -ne $disk){Note 'Windows is on another disk; space left free';exit 0}
 # Only what lies between Windows and the end of the removed partition is Windows' own (a multi-system plan has
 # other systems' partitions behind it; they are not looked at and not touched).
 $after=@(Get-Partition -DiskNumber $disk | Where-Object {$_.Offset -gt $sys.Offset -and $_.Offset -lt $limit} | Sort-Object Offset)
 $recType='{de94bba4-06d1-4d40-a16a-bfd50179d6ac}'
 if($after.Count -eq 1 -and $after[0].GptType -eq $recType){
  $rec=$after[0];$size=$rec.Size
  & reagentc.exe /disable | Out-Null
  if(Test-Path (Join-Path $env:SystemRoot 'System32\Recovery\Winre.wim')){
   Remove-Partition -DiskNumber $disk -PartitionNumber $rec.PartitionNumber -Confirm:$false -ErrorAction Stop
   $max=(Get-PartitionSupportedSize -DiskNumber $disk -PartitionNumber $sys.PartitionNumber).SizeMax
   Resize-Partition -DiskNumber $disk -PartitionNumber $sys.PartitionNumber -Size ($max-$size-1MB) -ErrorAction Stop
   $new=New-Partition -DiskNumber $disk -UseMaximumSize -GptType $recType -ErrorAction Stop
   $new | Format-Volume -FileSystem NTFS -NewFileSystemLabel 'Recovery' -Confirm:$false | Out-Null
   $script=Join-Path $env:TEMP 'charon-recovery.txt'
   Set-Content -Path $script -Encoding ASCII -Value @("select disk $disk","select partition $($new.PartitionNumber)",'gpt attributes=0x8000000000000001')
   & diskpart.exe /s $script | Out-Null
   Remove-Item $script -ErrorAction SilentlyContinue
   Note 'Windows partition extended; recovery partition moved to the end of the disk'
  } else {Note 'the recovery image did not return to Windows; recovery partition left in place, space left free'}
  & reagentc.exe /enable | Out-Null
 } elseif($after.Count -eq 0){
  $max=(Get-PartitionSupportedSize -DiskNumber $disk -PartitionNumber $sys.PartitionNumber).SizeMax
  if($max -gt $sys.Size){Resize-Partition -DiskNumber $disk -PartitionNumber $sys.PartitionNumber -Size $max -ErrorAction Stop;Note 'Windows partition extended'}
 } else {Note 'other partitions follow Windows; space left free'}
}catch{Note ('error: '+$_)}
)PS";
// After the questions and the confirmation: makes the media. False: said; the caller starts nothing.
inline bool Prepare(efi_handle& media,const char16_t*& loader) {
    auto& w=W();w.made=false;w.changed=false;w.whole=false;w.io=nullptr;w.disk=nullptr;w.volume=nullptr;ask::Put("wmediaguid","");
    K().first=0;
    {efi_handle old=nullptr;bool oldWritable=false;const char16_t* oldLoader=nullptr;
     if(install::FindWindowsMedia(old,oldWritable,oldLoader) && IsLeftover(old)) {
        // The partition of an earlier attempt: no download. The target is the disk that holds it.
        auto& s=disks::D();auto& t=install::Chosen();
        uint64_t targetFirst=0;
        if(ask::Is("diskmode","partition") && t.disk<s.count && t.part<s.list[t.disk].partCount)targetFirst=s.list[t.disk].parts[t.part].first;
        size_t disk=0,part=0;Locate(old,disk,part);   // reads the disk list again
        const disks::Part p=s.list[disk].parts[part];
        if(targetFirst==p.first) {
            Fail(T(u"Wybrana partycja to tymczasowa partycja instalatora; Windows nie może zostać zainstalowany na niej samej. Wybierz inną partycję, wolne miejsce albo cały dysk.",
                   u"The chosen partition is the installer's temporary partition; Windows cannot be installed onto it. Choose another partition, free space or the whole disk."));return false;}
        t.disk=disk;if(targetFirst)t.part=PartByFirst(s.list[disk],targetFirst);
        if(ask::Is("diskmode","whole")) {
            K().first=p.first;K().blocks=p.last-p.first+1;
            const bool made=Provide(0)!=nullptr;
            K().first=0;
            if(!made)return false;
        } else {
            Say(InPlan().on?T(u"Pliki instalatora są już na tymczasowej partycji tego dysku; używam ich, nic nie pobieram.",u"The installer's files are already on this disk's temporary partition; I am using them, nothing is downloaded.")   // r410: a plan put them there at its start
                :T(u"Pliki instalatora z poprzedniej próby są na tymczasowej partycji tego dysku; używam ich, nic nie pobieram.",u"The installer's files of an earlier attempt are on this disk's temporary partition; I am using them, nothing is downloaded."));
            w.volume=old;for(int i=0;i<16;++i)w.unique.bytes[i]=p.unique[i];
            char guid[40];GuidText(p.unique,guid);ask::Put("wmediaguid",guid);
        }
     } else {
        if(!install::EnsureNetwork())return false;
        const char* mine=Pl()?"pl-PL":"en-US";
        if(!winusb::Make(nullptr,mine,InPlan().on?ProvidePlan:Provide)) {Undo();return false;}
     }}
    bool writable=false;
    if(!install::FindWindowsMedia(media,writable,loader) || media!=w.volume || !writable) {
        Fail(T(u"Pliki instalatora są na tymczasowej partycji, ale firmware nie widzi jej jako nośnika instalacyjnego Windows.",u"The installer's files are on the temporary partition, but the firmware does not see it as Windows installation media."));
        Undo();return false;}
    if(!install::WriteText(media,u"\\charon\\Charon-MediaCleanup.ps1",cleanupScript,sizeof(cleanupScript)-1)) {
        MakeFolders(media,u"\\charon");
        if(!install::WriteText(media,u"\\charon\\Charon-MediaCleanup.ps1",cleanupScript,sizeof(cleanupScript)-1)) {
            Fail(T(u"Nie udało się zapisać skryptu, który usuwa tymczasową partycję.",u"The script that removes the temporary partition could not be written."));Undo();return false;}
    }
    return true;
}
}  // namespace winpart
}  // namespace ws
}  // namespace hakefi
#endif
// r409: the Ubuntu installer's answers when the computer has no FAT volume at all (RunUbuntu). Only when the
// whole disk is to be erased anyway: a new partition table with one small FAT partition; the installer reads
// its answers and Charon from it before it erases the disk. Null: not possible here (nothing was changed).
namespace hakefi {
namespace ws {
namespace install {
#ifndef HAK_CHARON_HTTPS
inline efi_handle SeedPartition() {return nullptr;}   // a build without the installers' disk helpers (the resident driver)
#else
inline efi_handle SeedPartition() {
    auto& s=disks::D();const auto& t=Chosen();
    if(t.disk>=s.count || !ask::Is("diskmode","whole"))return nullptr;
    const disks::Disk d=s.list[t.disk];
    if(d.blockSize!=512 || d.readOnly)return nullptr;
    if(d.hasCharon && !hakefi::selfram::Active() && !images::EnsureInMemory())return nullptr;
    constexpr uint64_t mega=uint64_t(1)<<20;
    const uint64_t charon=hakefi::selfcopy::Ready()?hakefi::selfcopy::Total():uint64_t(128)<<20;
    const uint64_t bytes=(charon*5/4+(uint64_t(96)<<20)+mega-1)/mega*mega;
    if(disks::Bytes(d)<bytes+(uint64_t(12)<<30))return nullptr;
    Say(T(u"W komputerze nie ma partycji FAT na odpowiedzi dla instalatora, więc na dysku, który i tak zostanie wymazany, zapisuję nową tablicę partycji z małą partycją na te pliki.",
          u"The computer has no FAT partition for the installer's answers, so on the disk that is erased anyway I am writing a new partition table with a small partition for those files."));
    seeddisk::Disk sd{d.io,d.blockSize,d.blocks,images::SeedRead,images::SeedWrite};
    seeddisk::Guid diskGuid,unique;images::RandomGuid(diskGuid,141);images::RandomGuid(unique,142);
    if(!dossys::EmptyGpt(d.io,d.blocks,diskGuid)) {Fail(T(u"Nie udało się zapisać tablicy partycji.",u"The partition table could not be written."));return nullptr;}
    seeddisk::AddRequest request{};request.type=seeddisk::kTypeBasicData;request.unique=unique;request.name=u"Charon install seed";request.sizeBytes=bytes;request.alignmentBytes=0;
    request.placement=seeddisk::Placement::At;request.firstLba=2048;request.attributes=0;
    seeddisk::AddResult added{};
    uint32_t lo,hi;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));
    if(seeddisk::AddPartition(sd,images::SeedWork(),seeddisk::kWorkBytes,request,added)!=seeddisk::Error::None
        || !dossys::FormatFat32(d.io,added.firstLba,added.lbaCount,"CHARONSEED",lo^hi)) {
        Fail(T(u"Nie udało się utworzyć partycji na pliki instalatora. Dysk ma już nową, pustą tablicę partycji.",u"The partition for the installer's files could not be made. The disk already has a new, empty partition table."));return nullptr;}
    if(d.io->FlushBlocks)reinterpret_cast<images::BlockFlush>(d.io->FlushBlocks)(d.io);
    images::Reconnect(d.handle);
    const efi_handle volume=images::VolumeOfPartition(unique.bytes);
    if(!volume)Fail(T(u"Firmware nie widzi nowej partycji na pliki instalatora.",u"The firmware does not see the new partition for the installer's files."));
    return volume;
}
#endif
}  // namespace install
}  // namespace ws
}  // namespace hakefi
