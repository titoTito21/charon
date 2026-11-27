#pragma once
// r402: Charon workspace - disk images from the network.
//
// Two things an installer does not do for the user:
//  - a ready system image ("cloud image": an official raw disk image of an
//    installed system) is written straight onto a chosen disk, block by block
//    as it arrives; nothing is kept in memory or on another disk;
//  - a bootable ISO (a "hybrid" image) is written the same way onto a USB
//    stick, or is started from memory as a RAM disk (CharonWsRamDisk.h).
// The disk Charon itself runs from is never a target: its files are needed
// afterwards (they are copied onto the new system's EFI partition).
// Everything here erases the target disk, so the caller must have had the
// typed confirmation (install::ConfirmPlan / ConfirmErase below).
// GPL-2.0-or-later.
#ifdef HAK_CHARON_HTTPS
namespace hakefi {
namespace ws {
namespace images {
typedef efi_status (*BlockWrite)(media::BlockIo*,uint32_t,uint64_t,size_t,void*) EFIAPI;
typedef efi_status (*BlockFlush)(media::BlockIo*) EFIAPI;
constexpr size_t chunkBytes=size_t(1)<<20;
struct Job {
    media::BlockIo* io;uint32_t blockSize;uint64_t lba,blocks;   // next block, size of the disk
    uint8_t* buffer;size_t fill;
    uint64_t done,total;const char16_t* label;unsigned rounds;
    bool failed,tooBig,cancelled;efi_status status;
};
inline Job& J() {static Job job;return job;}
// r402 dual boot: instead of the whole disk, the image's partitions go into
// partitions made for them in the free space of a disk that already holds a
// system (CharonWsSeed.h plans it from the image's own partition table, which
// is the first thing of the image to arrive). Bytes of the image outside its
// partitions (its partition tables, gaps) are dropped.
struct Segment {uint64_t first,end,target;};   // image bytes [first,end) -> byte offset on the disk
struct Remap {
    bool on,ready,failed;const disks::Disk* disk;uint64_t gapFirst,gapEnd;   // the free space, bytes
    Segment segments[8];size_t count;uint8_t espUnique[16];bool hasEsp;uint64_t seedLba;uint64_t need;
};
inline Remap& R() {static Remap remap;return remap;}
inline bool PlanRemap(const uint8_t* head,size_t size);   // CharonWsSeed.h
inline bool PutMapped(media::BlockIo* io,uint64_t offset,const uint8_t* data,size_t size) {
    auto& r=R();
    if(!r.ready) {if(offset!=0 || !PlanRemap(data,size)) {r.failed=true;return false;}}
    for(size_t i=0;i<r.count;++i) {
        const Segment& g=r.segments[i];
        const uint64_t a=offset>g.first?offset:g.first,b=offset+size<g.end?offset+size:g.end;
        if(a>=b)continue;
        if(reinterpret_cast<BlockWrite>(io->WriteBlocks)(io,io->Media->MediaId,(g.target+(a-g.first))/512,size_t(b-a),const_cast<uint8_t*>(data+(a-offset)))!=EFI_SUCCESS)return false;
    }
    return true;
}
inline bool WriteOut(Job& j,size_t bytes) {
    const size_t count=(bytes+j.blockSize-1)/j.blockSize;
    for(size_t i=bytes;i<count*j.blockSize;++i)j.buffer[i]=0;
    if(R().on) {
        if(!PutMapped(j.io,j.lba*j.blockSize,j.buffer,count*j.blockSize)) {j.failed=true;j.status=EFI_DEVICE_ERROR;return false;}
        j.lba+=count;return true;
    }
    if(j.lba+count>j.blocks) {j.tooBig=true;return false;}
    j.status=reinterpret_cast<BlockWrite>(j.io->WriteBlocks)(j.io,j.io->Media->MediaId,j.lba,count*j.blockSize,j.buffer);
    if(j.status!=EFI_SUCCESS) {j.failed=true;return false;}
    j.lba+=count;return true;
}
inline bool Sink(void* context,const uint8_t* data,size_t size) {
    auto& j=*static_cast<Job*>(context);
    while(size) {
        size_t part=chunkBytes-j.fill;if(part>size)part=size;
        memcpy(j.buffer+j.fill,data,part);j.fill+=part;data+=part;size-=part;j.done+=part;
        if(j.fill==chunkBytes) {if(!WriteOut(j,chunkBytes))return false;j.fill=0;}
    }
    if(++j.rounds%16==0 && j.total)Progress(j.label,j.done,j.total);
    if(j.rounds%256==0 && EscapePressed()) {j.cancelled=true;return false;}
    Pump();
    return true;
}
// Firmware reads the partition table again (the old volumes go away).
inline efi_handle VolumeOfPartition(const uint8_t* unique);
inline void Reconnect(efi_handle disk) {
    auto* boot=U().host->boot;
    // r410 (helpers vr and s7, two Windows in a plan): Charon started from the plan's EFI partition after a restart
    // reads its own files from that partition; once the disk was reconnected (Windows' partitions made) its old handle
    // was gone, so copying Charon onto the second Windows' media failed at once ("na dysku Charona nie ma plików
    // dodatku czytnika ekranu") and that Windows never came back to Charon. Its partition is found again by its GUID.
    uint8_t own[16];bool onPartition=false;
    if(!hakefi::selfram::Active() && U().host->selfDevice) {
        size_t bytes=0;const uint8_t* p=DevicePathOf(U().host->selfDevice,bytes);
        for(size_t at=0;p && at+4<=bytes;) {
            const size_t length=size_t(p[at+2])|(size_t(p[at+3])<<8);if(length<4 || p[at]==0x7f)break;
            if(p[at]==4 && p[at+1]==1 && length>=42 && p[at+41]==2) {memcpy(own,p+at+24,16);onPartition=true;break;}
            at+=length;
        }
    }
    boot->DisconnectController(disk,nullptr,nullptr);
    boot->ConnectController(disk,nullptr,nullptr,true);
    // r408a: the volume handles of that disk are new now (or gone); Charon's own files are read from the
    // original medium only while it is still mounted, from the volume in memory otherwise.
    if(hakefi::selfram::Active()) {const efi_handle original=hakefi::selfram::OriginalDevice();U().host->selfDevice=original?original:hakefi::selfram::Handle();}
    else if(onPartition) {const efi_handle again=VolumeOfPartition(own);
        charontrace::Value("CHARON own volume after reconnect found",again?1:0);
        // r410 (s7 run F): the open root of the old volume went with it; the copy into memory (selfcopy::Step reads
        // host.root) then failed at once ("nie udało się skopiować Charona do pamięci") on a 12 GB machine.
        if(again) {U().host->selfDevice=again;if(efi_file_protocol* root=OpenRoot(again))U().host->root=root;}}
}
// Writes the resource at `url` onto the whole disk `d`, from its first block.
// `written` receives the image's size. False: said why.
inline bool UrlToDisk(const char* url,const disks::Disk& d,const char16_t* what,uint64_t* written) {
    static https::Client client;static https::Response probe,answer;auto& j=J();
    if(written)*written=0;
    if(d.hasCharon && !R().on && !hakefi::selfram::Active()) {Fail(T(u"To jest dysk, z którego działa Charon, a Charon nie jest jeszcze wczytany do pamięci. Zaczekaj chwilę i spróbuj ponownie.",u"This is the disk Charon is running from, and Charon is not yet loaded into memory. Wait a moment and try again."));return false;}
    if(d.readOnly || !d.io || !d.io->WriteBlocks) {Fail(T(u"Na tym dysku nie da się zapisywać.",u"This disk cannot be written to."));return false;}
    client.transport=net::tcp::Transport();
    const held::File* local=held::Find(url);   // r410: a plan fetched this image before it touched the disk; it lies in memory
    auto result=local?https::Result::ok:https::Probe(client,url,probe);
    if(result!=https::Result::ok) {net::HttpsFail(client,result);return false;}
    const uint64_t total=local?local->size:probe.totalLength!=https::unknownLength?probe.totalLength:probe.contentLength!=https::unknownLength?probe.contentLength:0;
    if(!total) {Fail(T(u"Serwer nie podał rozmiaru obrazu.",u"The server did not give the image's size."));return false;}
    if(!R().on && total>disks::Bytes(d)) {
        static char16_t text[300];Set(text,300,T(u"Obraz jest większy niż dysk: ",u"The image is larger than the disk: "));systools::PutSize(text,300,total,Pl());Fail(text);return false;}
    static Memory memory;memory.reserved=false;memory.extra=0;
    if(!Reserve(memory,chunkBytes+4096)) {Fail(T(u"Za mało pamięci RAM.",u"Not enough RAM."));return false;}
    j=Job{};j.io=d.io;j.blockSize=d.blockSize;j.blocks=d.blocks;j.buffer=memory.data;j.total=total;j.label=what;
    {static char16_t text[400];Set(text,400,local?T(u"Obraz jest już w pamięci; zapisuję go na dysk: ",u"The image is already in memory; writing it to the disk: ")
        :T(u"Pobieram obraz i zapisuję go na dysk: ",u"Downloading the image and writing it to the disk: "));systools::PutSize(text,400,total,Pl());
     Cat(text,400,T(u". Escape przerywa; dysk zostaje wtedy z niepełnym obrazem.",u". Escape stops it; the disk is then left with an incomplete image."));Say(text);install::HearOut(100);}
    uint64_t offset=0;
    if(local)held::Stream(*local,Sink,&j);   // r410: a stop sets j.failed, j.tooBig or j.cancelled; a short stream fails the size check below
    else result=https::Download(client,url,offset,probe.validator[0]?probe.validator:nullptr,Sink,&j,answer);
    bool ok=result==https::Result::ok && !j.failed && !j.tooBig && !j.cancelled && j.done==total;
    if(ok && j.fill)ok=WriteOut(j,j.fill);
    if(ok && d.io->FlushBlocks)reinterpret_cast<BlockFlush>(d.io->FlushBlocks)(d.io);
    Free(memory);
    if(ok) {Progress(what,total,total);if(written)*written=total;return true;}
    if(j.cancelled)Fail(T(u"Przerwano. Na dysku jest niepełny obraz; nie da się z niego uruchomić systemu.",u"Stopped. The disk holds an incomplete image; no system can start from it."));
    else if(R().on && R().failed)Fail(T(u"Tego obrazu nie da się zapisać w wolnym miejscu: nie ma on zwykłej tablicy partycji GPT z sektorami po 512 bajtów albo jego partycje nie mieszczą się w wolnym miejscu. Na dysku nic się nie zmieniło; mogły tylko przybyć puste partycje.",
        u"This image cannot be written into free space: it has no ordinary GPT partition table with 512-byte sectors, or its partitions do not fit in the free space. Nothing on the disk was changed, except that empty partitions may have been added."));
    else if(j.tooBig)Fail(T(u"Obraz nie zmieścił się na dysku.",u"The image did not fit on the disk."));
    else if(j.failed)Fail(T(u"Zapis na dysk nie powiódł się.",u"Writing to the disk failed."),j.status);
    else net::HttpsFail(client,result);
    return false;
}
// The volumes that lie on disk `d` (after Reconnect), FAT ones only: those firmware can open.
inline size_t VolumesOn(const disks::Disk& d,efi_handle* out,size_t maximum) {
    uint8_t* path=nullptr;size_t count=0;
    if(U().host->boot->HandleProtocol(d.handle,&media::pathGuid,(void**)&path)!=EFI_SUCCESS || !path)return 0;
    const size_t prefix=media::PathLength(path);if(prefix==SIZE_MAX)return 0;
    efi_handle volumes[32];const size_t n=Volumes(volumes,32);
    for(size_t i=0;i<n && count<maximum;++i) {
        size_t bytes=0;const uint8_t* own=DevicePathOf(volumes[i],bytes);
        if(!own || bytes<prefix)continue;
        bool under=true;for(size_t k=0;k<prefix && under;++k)under=own[k]==path[k];
        if(under)out[count++]=volumes[i];
    }
    return count;
}
// The volume on GPT partition `unique` (after Reconnect).
inline efi_handle VolumeOfPartition(const uint8_t* unique) {
    efi_handle volumes[32];const size_t n=Volumes(volumes,32);
    for(size_t i=0;i<n;++i) {
        size_t bytes=0;const uint8_t* p=DevicePathOf(volumes[i],bytes);if(!p)continue;
        for(size_t at=0;at+4<=bytes;) {
            const uint8_t type=p[at],sub=p[at+1];const size_t length=size_t(p[at+2])|(size_t(p[at+3])<<8);
            if(length<4 || type==0x7f)break;
            if(type==4 && sub==1 && length>=42 && p[at+41]==2) {bool same=true;for(int b=0;b<16 && same;++b)same=p[at+24+b]==unique[b];if(same)return volumes[i];}
            at+=length;
        }
    }
    return nullptr;
}
// The EFI system partition among them: the one with an \EFI folder.
inline efi_handle EspOn(const disks::Disk& d) {
    efi_handle list[16];const size_t n=VolumesOn(d,list,16);
    for(size_t i=0;i<n;++i) {
        FileRef ref;ref.volume=list[i];Set(ref.path,pathUnits,u"\\EFI");efi_file_protocol* file=nullptr;
        if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)==EFI_SUCCESS && file) {file->Close(file);return list[i];}
    }
    return nullptr;
}
// Charon (\EFI\HAK of its own disk, with the registration script) onto the
// new system's EFI partition, so the system can register the screen reader
// and the Charon boot manager on its first start. False: no room or no files.
// r409: ownHome: the disk was the one Charon itself lives on and has just been erased whole, so this
// copy IS Charon's home from now on: saved answer sets and plan state are carried along (from the
// copy in memory). Otherwise Charon is given to a new system and they stay behind (CharonPrivate.h).
inline bool CharonOnto(efi_handle esp,bool ownHome=false) {
    if(!esp || VolumeReadOnly(esp))return false;
    static FileRef from,to;from.volume=U().host->selfDevice;Set(from.path,pathUnits,u"\\EFI\\HAK");
    to.volume=esp;Set(to.path,pathUnits,u"\\EFI\\HAK");size_t copied=0;
    MakeFolders(esp,u"\\EFI\\HAK");
    Say(T(u"Kopiuję Charona na partycję systemową EFI nowego systemu.",u"Copying Charon to the new system's EFI system partition."));
    if((ownHome?CarryCharonTree(from,to,&copied):CopyCharonTree(from,to,&copied))!=EFI_SUCCESS)return false;   // r409: give: without \EFI\HAK\answers and \EFI\HAK\plan
    // settings that belong to the stick, not to the installed copy
    static const char16_t* const stale[]={u"\\EFI\\HAK\\continue.bin",u"\\EFI\\HAK\\continue-set",u"\\EFI\\HAK\\installed-by.txt",u"\\EFI\\HAK\\reader-only",u"\\EFI\\HAK\\windows-iso-url.txt"};
    for(const auto* name:stale) {FileRef ref;ref.volume=esp;Set(ref.path,pathUnits,name);efi_file_protocol* file=nullptr;
        if(OpenFile(ref,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0,&file)==EFI_SUCCESS && file)file->Delete(file);}
    // r406: Charon run from its boot disc is \EFI\BOOT\BOOTX64.EFI and has no HAKBOOT.EFI in its
    // folder; the installed copy gets the running program under that name (the boot entry
    // "Charon" and the reader started through the boot manager both point there).
    {FileRef manager;manager.volume=esp;Set(manager.path,pathUnits,u"\\EFI\\HAK\\HAKBOOT.EFI");efi_file_protocol* file=nullptr;
     if(OpenFile(manager,EFI_FILE_MODE_READ,0,&file)==EFI_SUCCESS && file)file->Close(file);
     else {static FileRef self;self.volume=U().host->selfDevice;Set(self.path,pathUnits,u"\\EFI\\BOOT\\BOOTX64.EFI");
        if(CopyFile(self,manager)!=EFI_SUCCESS)return false;}}
    return install::WriteText(esp,u"\\EFI\\HAK\\install-charon.sh",install::charonScript,sizeof(install::charonScript)-1);
}
// A disk to erase: any writable one of at least `minimum` bytes that is not
// Charon's own; usbOnly: removable/USB disks only (a stick for an ISO).
// r409: Charon into memory NOW. The move normally happens in the menu's own loop a few seconds after
// the start (BootMenu.h); someone who opens an installation sooner found the disk Charon lives on
// missing from the list of targets, with no word of why. Here the copy is finished and Charon moves
// onto its volume in memory before the list is made, as "Install Charon on this computer" does.
// False: it could not be done (said); the disk Charon runs from then stays out of the list.
inline bool EnsureInMemory() {
    if(hakefi::selfram::Active())return true;
    auto& host=*U().host;
    if(!hakefi::selfcopy::Ready() && !hakefi::selfcopy::Failed()) {
        Say(T(u"Kopiuję Charona do pamięci, żeby dysk, z którego działa, też mógł być celem instalacji.",u"Copying Charon into memory, so that the disk it runs from can be an installation target too."));
        unsigned rounds=0;
        while(hakefi::selfcopy::Step(host.boot,host.root,true)) {
            Pump();
            if(++rounds%32==0)Progress(T(u"Charon do pamięci",u"Charon into memory"),hakefi::selfcopy::Done(),hakefi::selfcopy::Total());
        }
        // r409: the bar ended on its last 32nd round ("59 / 62 MB" seen, with every file copied); it now ends on the whole.
        // (The numbers are binary megabytes, as everywhere in Charon: 66 000 000 bytes of files are 62.9 of them.)
        if(hakefi::selfcopy::Ready())Progress(T(u"Charon do pamięci",u"Charon into memory"),hakefi::selfcopy::Total(),hakefi::selfcopy::Total());
    }
    // r409: files or folders of \EFI\HAK that did not fit the copy's tables (4000 files, 512 folders, paths under 200
    // characters) are never left out in silence: whatever is installed from the copy in memory will lack them.
    if(hakefi::selfcopy::Ready() && hakefi::selfcopy::Skipped()) {
        static char16_t note[400];Set(note,400,T(u"Uwaga: kopia Charona w pamięci jest niepełna. Liczba plików i folderów, które się w niej nie zmieściły: ",u"Note: Charon's copy in memory is incomplete. Files and folders that did not fit in it: "));
        CatN(note,400,hakefi::selfcopy::Skipped());Cat(note,400,T(u". Charon zapisany z tej kopii na dysk będzie ich pozbawiony.",u". A Charon written to a disk from this copy will lack them."));Say(note);
    }
    if(hakefi::selfcopy::Ready() && hakefi::selfram::Move(host.boot,host.image,nullptr,nullptr)==EFI_SUCCESS && hakefi::selfram::Active()) {
        const efi_handle original=hakefi::selfram::OriginalDevice();host.selfDevice=original?original:hakefi::selfram::Handle();
        return true;
    }
    Say(T(u"Uwaga: nie udało się wczytać Charona do pamięci, więc dysku, z którego działa Charon, nie ma na liście celów. Zwykle brakuje na to pamięci RAM; inne dyski pozostają dostępne.",
          u"Note: Charon could not be loaded into memory, so the disk Charon runs from is not in the list of targets. Usually there is too little RAM for it; other disks remain available."));   // r409: no advice to make a Charon stick
    return false;
}
inline bool ChooseDisk(uint64_t minimum,bool usbOnly,size_t& chosen) {
    auto& s=disks::D();disks::Load();
    if(!usbOnly && !hakefi::selfram::Active())for(size_t i=0;i<s.count;++i)if(s.list[i].hasCharon && !s.list[i].readOnly) {EnsureInMemory();disks::Load();break;}   // r409
    static char16_t labels[disks::maxDisks][300];static ask::Choice list[disks::maxDisks];static size_t map[disks::maxDisks];size_t count=0;
    for(size_t i=0;i<s.count;++i) {
        const auto& d=s.list[i];
        if(d.readOnly || (d.hasCharon && !hakefi::selfram::Active()) || disks::Bytes(d)<minimum)continue;   // r408: Charon's own disk can be wiped once Charon runs from RAM
        if(usbOnly && !d.usb && !d.removable)continue;
        disks::DiskLabel(d,i,labels[count],300);list[count]={labels[count],labels[count],""};map[count]=i;++count;
    }
    if(!count) {
        static char16_t text[400];
        Set(text,400,usbOnly?T(u"Nie ma drugiego pendrive'a, który można wymazać: potrzeba co najmniej ",u"There is no second USB stick that can be erased: needed, at least ")
            :T(u"Nie ma dysku, który można wymazać: potrzeba co najmniej ",u"There is no disk that can be erased: needed, at least "));
        systools::PutSize(text,400,minimum,Pl());
        Cat(text,400,T(u". Dysk, z którego działa Charon, się nie liczy.",u". The disk Charon runs from does not count."));Fail(text);return false;}
    size_t index=0;
    if(!ask::Choose(usbOnly?T(u"Który pendrive wymazać?",u"Which USB stick should be erased?"):T(u"Który dysk wymazać?",u"Which disk should be erased?"),list,count,index))return false;
    chosen=map[index];return true;
}
// The typed word before a disk is erased.
inline bool ConfirmErase(const disks::Disk& d,size_t index,const char16_t* what) {
    static char16_t text[900];static char16_t label[300];disks::DiskLabel(d,index,label,300);
    Set(text,900,d.blank?T(u"Ten pusty dysk zostanie użyty w całości: ",u"The whole of this empty disk will be used: ")
        :T(u"CAŁY ten dysk zostanie wymazany, ze wszystkimi systemami i danymi: ",u"This WHOLE disk will be erased, with every system and all data on it: "));
    Cat(text,900,label);Cat(text,900,T(u". Trafi na niego: ",u". It will receive: "));Cat(text,900,what);
    Cat(text,900,T(u". Aby zacząć, wpisz słowo TAK i naciśnij Enter. Escape anuluje.",u". To start, type the word YES and press Enter. Escape cancels."));
    Say(text);
    static char16_t word[16];word[0]=0;
    if(!Prompt(T(u"Wpisz TAK, aby zacząć:",u"Type YES to start:"),word,15,nullptr,nullptr,false))return false;
    if(!SameFold(word,T(u"tak",u"yes"))) {Say(T(u"Nie wpisano potwierdzenia. Nic nie zostało zmienione.",u"No confirmation was typed. Nothing was changed."));return false;}
    return true;
}
}  // namespace images
}  // namespace ws
}  // namespace hakefi
#endif
