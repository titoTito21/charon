#pragma once
// Charon r409: a multi-system plan inside the EFI program - the binding of the plan core
// (CharonPlanModel.h, CharonPlanLayout.h, CharonPlanState.h; pure, host tested) to firmware:
// the plan's files on its EFI system partition, the variable CharonPlan, the boot order.
//
//   Pending()    is a plan waiting? (the variable; asked by the boot menu at every start)
//   AtStart()    what a start of Charon with a waiting plan does: load, look at the markers the
//                installed system left, say where the plan is, then pass through to the system
//                whose installer is in the middle, start the first run of a written image, ask
//                after a failure, or finish
//   Finish()     the finish step: spare placeholders removed, unused ranges asked about, boot
//                order, Charon's "Continue" target, systems registry, CharonDOS defaults, clock
//                file, report, clean-up of the answer files
//
// The dialogue that builds a plan and the steps that need the installers (disk preparation,
// CharonDOS, ready images, Debian) are in CharonWsPlanDialogue.h, which registers them through
// Runners(). Without the variable CharonPlan every entry point here returns at once.
// What has run in a machine and what is compiled only: tests/charon-r409-multiboot-results.txt.
// GPL-2.0-or-later.
#include "CharonPlanState.h"
#include "CharonPlanSets.h"    // r410: saved answer sets of a plan
#include "CharonClockZone.h"
#include "../dos/launch_select.h"   // BOOT.CFG rules shared with CharonDOS (pure)
namespace hakefi {
namespace ws {
namespace planrun {

inline efi_guid& CharonVars() {static efi_guid g={0x7a3c5e21,0x4b9d,0x4f1e,{0xa6,0x52,0x1c,0x8e,0x93,0x0d,0x27,0xb5}};return g;}
inline void Widen(const char* in,char16_t* out,size_t cap) {plan::Utf16(in,out,cap);}

// ---- Storage on one volume ------------------------------------------------------------
struct Bound {efi_handle volume;};
inline void Ref(void* context,const char* path,FileRef& ref) {ref.volume=static_cast<Bound*>(context)->volume;Widen(path,ref.path,pathUnits);}
inline bool StoreExists(void* context,const char* path) {
    static FileRef ref;Ref(context,path,ref);efi_file_protocol* file=nullptr;
    if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)!=EFI_SUCCESS || !file)return false;
    file->Close(file);return true;
}
inline bool StoreRead(void* context,const char* path,char* buffer,size_t capacity,size_t* length) {
    static FileRef ref;Ref(context,path,ref);uint8_t* data=nullptr;size_t size=0;
    if(ReadWhole(ref,capacity,&data,&size)!=EFI_SUCCESS || !data)return false;
    const bool fits=size<=capacity;
    if(fits) {memcpy(buffer,data,size);*length=size;}
    memset(data,0,size);Release(data);return fits;
}
inline bool StoreWrite(void* context,const char* path,const char* data,size_t length) {
    static FileRef ref;Ref(context,path,ref);
    // The folders of the path first.
    static char16_t folder[pathUnits];size_t cut=0;
    for(size_t i=0;ref.path[i];++i)if(ref.path[i]==u'\\')cut=i;
    if(cut) {for(size_t i=0;i<cut;++i)folder[i]=ref.path[i];folder[cut]=0;if(MakeFolders(ref.volume,folder)!=EFI_SUCCESS)return false;}
    return WriteWhole(ref,reinterpret_cast<const uint8_t*>(data),length)==EFI_SUCCESS;
}
inline bool StoreRemove(void* context,const char* path) {
    static FileRef ref;Ref(context,path,ref);efi_file_protocol* file=nullptr;
    if(OpenFile(ref,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0,&file)!=EFI_SUCCESS || !file)return !StoreExists(context,path);
    return file->Delete(file)==EFI_SUCCESS;
}
// The log is short; the whole of it is read and written again.
inline bool StoreAppend(void* context,const char* path,const char* data,size_t length) {
    static char all[32768];size_t have=0;
    if(StoreExists(context,path) && !StoreRead(context,path,all,sizeof(all)-length-1,&have))have=0;   // a log that grew too long starts again
    if(have+length>sizeof(all))return false;
    memcpy(all+have,data,length);
    return StoreWrite(context,path,all,have+length);
}
// FAT has no atomic rename through the file protocol that every driver honours: copy, then delete.
// plan::Load takes the valid copy with the highest sequence number, whichever of the two survives.
inline bool StoreRename(void* context,const char* from,const char* to) {
    static char copy[131072];size_t length=0;
    if(StoreExists(context,to) || !StoreRead(context,from,copy,sizeof(copy),&length))return false;
    const bool ok=StoreWrite(context,to,copy,length);
    memset(copy,0,length);
    return ok && StoreRemove(context,from);
}
inline bool VarGet(void*,uint8_t* buffer,size_t capacity,size_t* length) {
    size_t size=capacity;
    if(dossys::Rt()->GetVariable((char16_t*)u"CharonPlan",&CharonVars(),nullptr,&size,buffer)!=EFI_SUCCESS)return false;
    *length=size;return true;
}
inline bool VarSet(void*,const uint8_t* data,size_t length) {
    // Non-volatile, boot services, runtime: the installed systems may read it, and it survives restarts.
    const efi_status status=dossys::Rt()->SetVariable((char16_t*)u"CharonPlan",&CharonVars(),7,length,const_cast<uint8_t*>(data));
    return status==EFI_SUCCESS || (!length && status==EFI_NOT_FOUND);
}
inline plan::Storage Bind(Bound& bound) {return plan::Storage{&bound,StoreRead,StoreWrite,StoreAppend,StoreRename,StoreRemove,StoreExists,VarGet,VarSet};}

// True when the variable names a plan. Cheap: one GetVariable, no file access.
inline bool Pending(efi_runtime_services* runtime) {
    uint8_t bytes[64];size_t size=sizeof(bytes);plan::Pointer pointer;
    return runtime && runtime->GetVariable((char16_t*)u"CharonPlan",&CharonVars(),nullptr,&size,bytes)==EFI_SUCCESS && plan::UnpackPointer(bytes,size,pointer);
}

// ---- small firmware helpers -----------------------------------------------------------
inline uint64_t Now() {
    efi_time t{};auto* runtime=dossys::Rt();
    if(!runtime || !runtime->GetTime || runtime->GetTime(&t,nullptr)!=EFI_SUCCESS)return 0;
    return plan::Seconds(t.Year,t.Month,t.Day,t.Hour,t.Minute,t.Second);
}
inline void SayText(const char* utf8) {static char16_t wide[3000];Widen(utf8,wide,3000);Say(wide);}
// The numbers of BootOrder; 0 when it cannot be read.
inline size_t BootOrder(uint16_t* order,size_t capacity,uint32_t* attributes=nullptr) {
    size_t size=capacity*2;
    if(dossys::Rt()->GetVariable((char16_t*)u"BootOrder",&dossys::GlobalVars(),attributes,&size,order)!=EFI_SUCCESS || (size&1))return 0;
    return size/2;
}
inline size_t BootOption(uint16_t id,uint8_t* option,size_t capacity) {
    char16_t name[16];dossys::VarName(u"Boot",id,name);size_t size=capacity;
    return dossys::Rt()->GetVariable(name,&dossys::GlobalVars(),nullptr,&size,option)==EFI_SUCCESS?size:0;
}
// The entry with exactly that description ("Charon", "CharonDOS").
inline bool EntryNamed(const char16_t* description,uint16_t& id) {
    static uint16_t order[256];static uint8_t option[4096];const size_t count=BootOrder(order,256);
    for(size_t i=0;i<count;++i) {const size_t size=BootOption(order[i],option,sizeof(option));if(size && dossys::SameDescription(option,size,description)) {id=order[i];return true;}}
    return false;
}
// The entry whose device path names that partition and that loader file (letter case ignored).
inline bool EntryOfLoader(const uint8_t* partition,const char* path,uint16_t& id) {
    static uint16_t order[256];static uint8_t option[4096];const size_t count=BootOrder(order,256);
    for(size_t i=0;i<count;++i) {
        const size_t size=BootOption(order[i],option,sizeof(option));if(size<8)continue;
        size_t at=6;while(at+1<size && (option[at] || option[at+1]))at+=2;
        at+=2;const size_t list=size_t(option[4])|(size_t(option[5])<<8);if(at+list>size)continue;
        bool disk=false,file=false;
        for(size_t p=at;p+4<=at+list;) {
            const size_t length=size_t(option[p+2])|(size_t(option[p+3])<<8);if(length<4 || p+length>at+list || option[p]==0x7f)break;
            if(option[p]==4 && option[p+1]==1 && length>=42 && option[p+41]==2) {disk=true;for(int b=0;b<16;++b)if(option[p+24+b]!=partition[b])disk=false;}
            if(option[p]==4 && option[p+1]==4) {
                size_t k=0;file=true;
                for(size_t c=p+4;c+1<p+length;c+=2,++k) {
                    const char16_t have=char16_t(option[c]|(option[c+1]<<8));if(!have)break;
                    if(!path[k] || plan::Lower(char(have))!=plan::Lower(path[k]) || have>127) {file=false;break;}
                }
                if(file && path[k])file=false;
            }
            p+=length;
        }
        if(disk && file) {id=order[i];return true;}
    }
    return false;
}
// BootNext = Charon's own entry: the next start comes back to Charon even when an installer has
// just put itself first. Used once by the firmware and deleted by it.
inline bool BootNextCharon() {
    uint16_t id=0;if(!EntryNamed(u"Charon",id))return false;
    return dossys::Rt()->SetVariable((char16_t*)u"BootNext",&dossys::GlobalVars(),7,2,&id)==EFI_SUCCESS;
}
// Writes BootOrder with `first` in front (plan::FinishOrder) and reads it back.
inline bool PutFirst(const uint16_t* first,size_t firstCount) {
    static uint16_t old[256],next[256],check[256];uint32_t attributes=7;
    const size_t count=BootOrder(old,256,&attributes);if(!count)return false;
    const size_t n=plan::FinishOrder(old,count,first,firstCount,next,256);if(!n)return false;
    if(dossys::Rt()->SetVariable((char16_t*)u"BootOrder",&dossys::GlobalVars(),attributes,n*2,next)!=EFI_SUCCESS)return false;
    if(BootOrder(check,256)!=n)return false;
    for(size_t i=0;i<n;++i)if(check[i]!=next[i])return false;
    return true;
}
// The GPT partition GUID behind a volume.
inline bool PartitionOf(efi_handle volume,uint8_t* unique) {
    size_t bytes=0;const uint8_t* p=DevicePathOf(volume,bytes);if(!p)return false;
    for(size_t at=0;at+4<=bytes;) {
        const size_t length=size_t(p[at+2])|(size_t(p[at+3])<<8);if(length<4 || p[at]==0x7f)break;
        if(p[at]==4 && p[at+1]==1 && length>=42 && p[at+41]==2) {memcpy(unique,p+at+24,16);return true;}
        at+=length;
    }
    return false;
}
inline bool FileOn(efi_handle volume,const char* path) {Bound b{volume};return StoreExists(&b,path);}
// Where a system's loader is: on the plan's EFI partition, or - a ready image keeps its own small
// EFI partition inside its range - on that partition, whose GUID was recorded when the image was
// written (`ownEsp`, GUID text; never guessed from what other volumes happen to hold).
inline efi_handle LoaderOf(plan::Kind kind,efi_handle esp,const char* ownEsp,const char*& path) {
    const char* candidates[4];const int n=plan::LoaderCandidates(kind,candidates);
    efi_handle volume=esp;
    if(plan::IsImage(kind) || plan::IsHypervisor(kind)) {   // r410: the hypervisor's loader is on its P1 (esp_guid)
        uint8_t unique[16];
        if(!ownEsp || !plan::ParseGuid(ownEsp,unique))return nullptr;
        volume=images::VolumeOfPartition(unique);
    }
    if(!volume)return nullptr;
    for(int i=0;i<n;++i)if(FileOn(volume,candidates[i])) {path=candidates[i];return volume;}
    return nullptr;
}

// What the finish step and the report need to know about this machine, read now.
inline plan::Machine MachineNow() {
    plan::Machine m{};uint8_t value=0;size_t size=1;
    m.secureBoot=dossys::Rt()->GetVariable((char16_t*)u"SecureBoot",&dossys::GlobalVars(),nullptr,&size,&value)==EFI_SUCCESS && value==1;
    // The reader add-on leaves this mark when the firmware loaded it from Driver####.
    uint8_t mark[8];size=sizeof(mark);
    const efi_status loaded=dossys::Rt()->GetVariable((char16_t*)u"CharonVoiceLoaded",&CharonVars(),nullptr,&size,mark);
    m.driverOptions=loaded==EFI_SUCCESS || loaded==EFI_BUFFER_TOO_SMALL;
    m.runningFromRam=selfram::Active();
    m.network=true;m.internalDisks=1;
    return m;
}

// ---- the session ------------------------------------------------------------------------
// Plain data: the session is zero-initialised static storage (no run-time constructor).
struct Session {
    plan::Plan plan;plan::Run run;
    Bound bound;plan::Storage storage;
    uint8_t esp[16];bool loaded;
};
constexpr size_t workBytes=131072;
inline char* Work() {static char work[workBytes];return work;}
inline plan::Store& Answers() {static char bytes[65536];static plan::Store store(bytes,sizeof(bytes));return store;}
// The loader of system `index` of the plan in the session.
inline efi_handle LoaderOfSystem(int index,const char*& path);
inline Session& S() {static Session session;return session;}
inline efi_handle LoaderOfSystem(int index,const char*& path) {
    const auto& system=S().plan.systems[index];char section[40];
    return LoaderOf(system.kind,S().bound.volume,Answers().GetOr(plan::SystemSection(system.id,section,sizeof(section)),"esp_guid",""),path);
}
// r410 (helper h8, run 6): the boot entry Charon makes for the Charon Hypervisor (its default start, or its place in a
// chosen order) pointed at \boot\vmlinuz-lts with NO load options, and Alpine's kernel stopped without its initramfs.
// It gets the same options as the hypervisor's entry in Charon's device list (CharonHypervisor.h, AddEntries).
inline const char16_t* HypervisorOptions(efi_handle volume) {
    static char16_t options[400];size_t at=0;char16_t serial[16]={};
    const bool bySerial=::hakefi::hvlist::FatSerial(U().host->boot,volume,serial);
    ::hakefi::hvlist::Put(options,400,at,u"initrd=\\boot\\initramfs-lts modules=loop,squashfs,sd-mod,usb-storage,vfat,nvme,ahci modloop=/boot/modloop-lts alpine_dev=");
    if(bySerial) {::hakefi::hvlist::Put(options,400,at,u"UUID=");::hakefi::hvlist::Put(options,400,at,serial);}
    else ::hakefi::hvlist::Put(options,400,at,u"LABEL=CHARONHV");
    ::hakefi::hvlist::Put(options,400,at,Pl()?u" quiet charonhv.lang=pl":u" quiet charonhv.lang=en");
    return options;
}
// What the dialogue registers in phase B2: Charon's own steps and the start of an installer.
// Each returns true when its work is done; false with `reason` filled when it failed.
struct StepRunners {
    bool (*prepare)(char* reason,size_t capacity);
    bool (*writeDos)(int system,char* reason,size_t capacity);
    bool (*writeImage)(int system,char* reason,size_t capacity);
    bool (*install)(int system,int step,char* reason,size_t capacity);   // starts the installer; returns only when that failed
    void (*beforeFirstStart)(int system);   // r410 (bd): may be null; an image's seed gets its label CIDATA back
};
inline StepRunners& Runners() {static StepRunners runners{};return runners;}

inline bool SaveNow() {
    auto& s=S();
    const plan::Io io=plan::Save(s.storage,s.plan,s.run,Answers(),Work(),workBytes,s.esp);
    if(io!=plan::Io::ok) {Fail(T(u"Nie udało się zapisać stanu planu na partycji EFI.",u"The state of the plan could not be written to the EFI partition."));return false;}
    return true;
}
inline void LogLine(const char* text) {plan::Log(S().storage,Now(),text);}
// r410: the log is in the language of the interface, like the spoken lines that are written into it.
inline void LogLine(const char* pl,const char* en) {LogLine(Pl()?pl:en);}
// The step number for the hooks of the installed systems (install-charon.sh, Install-Charon.ps1).
inline bool WriteCurrent(int step) {
    char digits[8];plan::Out out(digits,sizeof(digits));out.N(uint64_t(step));
    return S().storage.write(S().storage.context,"\\EFI\\HAK\\plan\\current",digits,out.length);
}
// Removes everything a plan keeps on its EFI partition: the plan files, the markers and the answer
// files of every step (overwritten with zeros first: they may hold a password). Used when a plan
// is discarded without its finish step.
inline void DiscardFiles(const plan::Storage& storage) {
    static const char* const answerFiles[]={"Autounattend.xml","autounattend.xml","preseed.cfg","user-data","meta-data","answers.ini","answers.env","linux","initrd.gz"};   // r410: the last two are an installer's kernel and initrd, fetched up front
    static char zero[65536];
    for(int step=0;step<plan::kMaxSteps;++step) {
        for(const char* name:answerFiles) {
            char path[160];plan::Out out(path,sizeof(path));plan::StepFilePath(step,name,out);
            if(!storage.exists(storage.context,path))continue;
            size_t length=0;
            if(storage.read(storage.context,path,zero,sizeof(zero),&length)) {memset(zero,0,length);storage.write(storage.context,path,zero,length);}
            storage.remove(storage.context,path);
        }
        for(int failed=0;failed<2;++failed) {char path[96];plan::Out out(path,sizeof(path));plan::MarkerPath(step,failed!=0,out);storage.remove(storage.context,path);}
    }
    static const char* const planFiles[]={plan::kPlanFile,plan::kPlanNew,plan::kPlanBackup,"\\EFI\\HAK\\plan\\current"};
    for(const char* path:planFiles)storage.remove(storage.context,path);
}
// A plan that is abandoned without its finish step (its file is unreadable, or its disk is gone).
// Decided rule (2026-11-23):
//   1. every volume that carries a plan's files or marks loses them - the plan's own EFI
//      partition and the EFI partition of every ready image (there the mark is what makes the
//      image's start-up script keep Charon first and report to a plan that no longer exists);
//   2. the entries "Charon" and "Charon voice" are pointed at the plan's EFI partition when it
//      is there and holds Charon: an image's first start had registered the copy on the image's
//      own partition, and that partition goes when the user later removes that system. When the
//      plan's partition is not there, the entries are left as they are: Charon was started
//      somehow, and an entry that works is not replaced by a guess;
//   3. no partition is removed and no system is touched: what was installed stays, reserved
//      ranges stay as named placeholders ("Charon plan N: ...") that any partition tool shows.
inline void DiscardEverywhere(efi_handle planVolume) {
    efi_handle volumes[32];const size_t count=Volumes(volumes,32);
    for(size_t i=0;i<count;++i) {
        Bound b{volumes[i]};plan::Storage there=Bind(b);
        if(there.exists(there.context,plan::kPlanFile) || there.exists(there.context,plan::kPlanBackup) || there.exists(there.context,plan::kPlanNew)
            || there.exists(there.context,plan::kPlanCurrent))DiscardFiles(there);
    }
    if(planVolume && FileOn(planVolume,"\\EFI\\HAK\\HAKBOOT.EFI")) {
        efi_status status=dossys::PutOption(false,u"Charon",planVolume,u"\\EFI\\HAK\\HAKBOOT.EFI",true,nullptr,nullptr,true);
        if(status==EFI_UNSUPPORTED)dossys::PutOption(false,u"Charon",planVolume,u"\\EFI\\HAK\\HAKBOOT.EFI",true,nullptr);
        status=dossys::PutOption(true,u"Charon voice",planVolume,u"\\EFI\\HAK\\CHARONVOICE.EFI",true,nullptr,nullptr,true);
        if(status==EFI_UNSUPPORTED)dossys::PutOption(true,u"Charon voice",planVolume,u"\\EFI\\HAK\\CHARONVOICE.EFI",true,nullptr);
    }
}
// Loads the plan the variable points to. False: nothing to do (no variable), or the user was
// told why the plan cannot go on.
inline bool Open() {
    auto& s=S();s.loaded=false;
    Bound none{nullptr};plan::Storage variable=Bind(none);plan::Pointer pointer;
    if(!plan::ReadPointer(variable,pointer))return false;
    memcpy(s.esp,pointer.espGuid,16);
    s.bound.volume=images::VolumeOfPartition(pointer.espGuid);
    s.storage=Bind(s.bound);
    plan::Refusals report;
    const plan::Io io=s.bound.volume?plan::Load(s.storage,Work(),workBytes,Answers(),s.plan,s.run,report):plan::Io::none;
    if(io==plan::Io::ok) {s.loaded=true;return true;}
    // The variable names a plan whose file cannot be read: say so, offer to discard it.
    static char text[600];plan::Out out(text,sizeof(text));plan::PointerLostText(pointer,Pl(),out);SayText(text);
    if(Confirm(T(u"Usunąć ten plan? Zainstalowane systemy zostają.",u"Discard this plan? The installed systems stay."))) {
        DiscardEverywhere(s.bound.volume);
        variable.setVariable(variable.context,nullptr,0);
        Say(T(u"Plan usunięty.",u"The plan was discarded."));
    }
    return false;
}

// A ready image keeps its own small EFI partition, and its first-start script looks for the plan
// THERE (install-charon.sh: EFI/HAK/plan/plan.txt, current). So Charon leaves a mark on that
// partition before it starts the image, fetches the marker the image wrote, and removes the mark
// when the step is over.
inline void MarkImageVolume(efi_handle volume,int step,bool on) {
    Bound b{volume};plan::Storage there=Bind(b);
    if(!on) {
        static const char* const marks[]={plan::kPlanFile,"\\EFI\\HAK\\plan\\current"};
        for(const char* path:marks)there.remove(there.context,path);
        for(int failed=0;failed<2;++failed) {char path[96];plan::Out out(path,sizeof(path));plan::MarkerPath(step,failed!=0,out);there.remove(there.context,path);}
        return;
    }
    static const char stub[]="; a multi-system plan is running; its state is on the plan's own EFI partition\r\n";
    char digits[8];plan::Out out(digits,sizeof(digits));out.N(uint64_t(step));
    there.write(there.context,plan::kPlanFile,stub,sizeof(stub)-1);
    there.write(there.context,"\\EFI\\HAK\\plan\\current",digits,out.length);
}
// Before Resume: the marker of the current step, when its system wrote it on its own EFI partition.
inline void ImportMarkers() {
    auto& s=S();const int step=plan::Current(s.run);
    if(step<0 || s.run.steps[step].system<0)return;
    const auto& system=s.plan.systems[s.run.steps[step].system];
    if(!plan::IsImage(system.kind))return;
    const char* loader=nullptr;const efi_handle volume=LoaderOfSystem(s.run.steps[step].system,loader);
    if(!volume || volume==s.bound.volume)return;
    Bound b{volume};plan::Storage there=Bind(b);static char text[4096];
    for(int failed=0;failed<2;++failed) {
        char path[96];plan::Out out(path,sizeof(path));plan::MarkerPath(step,failed!=0,out);size_t length=0;
        if(there.exists(there.context,path) && there.read(there.context,path,text,sizeof(text),&length) && s.storage.write(s.storage.context,path,text,length))
            MarkImageVolume(volume,step,false);
    }
}
// Starts the loader of a system (pass-through, or the first start of a written image). Returns
// when the loader came back; false when it could not be found.
inline bool StartSystem(int step) {
    auto& s=S();const auto& system=s.plan.systems[s.run.steps[step].system];
    const char* path=nullptr;const efi_handle volume=LoaderOfSystem(s.run.steps[step].system,path);
    if(!volume)return false;
    static char16_t wide[200],title[64];Widen(path,wide,200);Widen(system.name,title,64);
    held::DropAll();   // r410: nothing fetched up front is needed once a system starts
    WriteCurrent(step);
    if(volume!=s.bound.volume)MarkImageVolume(volume,step,true);
    if(!BootNextCharon())LogLine("nie udało się ustawić BootNext na Charona","BootNext could not be set to Charon");
    static char line[200];plan::Out out(line,sizeof(line));out.S(Pl()?"krok ":"step ").N(uint64_t(step)).S(Pl()?": uruchamiam ":": starting ").S(path);LogLine(line);
    StartProgram(volume,wide,title,nullptr);
    LogLine("program rozruchowy wrócił","the loader came back");
    return true;
}

// Charon's "Continue" target as a file on a volume, in the format BootMenu.h SaveContinue writes
// (and CharonDOS's CHARON command): the mark continue-set first, then continue.bin = "HAKCONT1",
// the entry's number, the size and the bytes of its Boot#### option, "HEND". Written to the
// plan's EFI partition, so it is right whatever medium Charon was started from for the finish.
inline bool WriteContinue(efi_handle volume,uint16_t id) {
    static uint8_t record[4096+32];const size_t size=BootOption(id,record+14,4000);
    if(!size)return false;
    memcpy(record,"HAKCONT1",8);record[8]=uint8_t(id);record[9]=uint8_t(id>>8);
    for(int i=0;i<4;++i)record[10+i]=uint8_t(size>>(8*i));
    memcpy(record+14+size,"HEND",4);
    Bound b{volume};const char one=1;
    return StoreWrite(&b,"\\EFI\\HAK\\continue-set",&one,1) && StoreWrite(&b,"\\EFI\\HAK\\continue.bin",reinterpret_cast<const char*>(record),18+size);
}
// Defined by the dialogue (CharonWsPlanDialogue.h): the entries the plan did not write while it
// ran (CharonDOS), Charon's own entries pointed back at the plan's EFI partition, the marks on
// the images' EFI partitions removed, the clock file where CharonDOS reads it.
inline void FinishExtras(plan::Out& trouble);
inline void AfterStepDone(int step);
inline bool FreeLeftover(disks::Disk& disk,const plan::Range& range);   // everything inside the range goes (placeholder, or what a failed attempt left)

// ---- the finish step ----------------------------------------------------------------------
inline disks::Disk* DiskOfPlan() {
    disks::Load();auto& d=disks::D();
    for(size_t i=0;i<d.count;++i)for(size_t p=0;p<d.list[i].partCount;++p)if(disks::SameBytes(d.list[i].parts[p].unique,S().esp,16))return &d.list[i];
    return nullptr;
}
inline bool RemoveRange(disks::Disk& disk,const plan::Range& range) {
    static uint8_t table[seeddisk::kWorkBytes];
    seeddisk::Disk io{disk.io,disk.blockSize,disk.blocks,images::SeedRead,images::SeedWrite};
    seeddisk::Guid unique;memcpy(unique.bytes,range.guid,16);
    return seeddisk::RemovePartition(io,table,sizeof(table),unique,seeddisk::kTypeLinuxReserved,range.firstLba)==seeddisk::Error::None;
}
inline bool Finish(const plan::Machine& machine) {
    auto& s=S();const int step=plan::Current(s.run);
    if(step<0 || s.run.steps[step].kind!=plan::StepKind::finish)return false;
    plan::MarkLaunched(s.run,step,Now());if(!SaveNow())return false;
    static char problems[1200];plan::Out trouble(problems,sizeof(problems));
    plan::Layout layout;const bool haveLayout=plan::LayoutFromStore(Answers(),s.plan,1,layout);
    disks::Disk* disk=DiskOfPlan();
    uint32_t freed=0;int64_t clockShift=0;
    // 1. Spare placeholders go; unused ranges are asked about (unattended: [plan] leftover).
    if(haveLayout && disk) {
        for(int i=0;i<layout.count;++i)if(layout.ranges[i].role==plan::Role::spare && !RemoveRange(*disk,layout.ranges[i]))plan::Trouble(trouble,"a spare placeholder could not be removed",Pl());
        int left[plan::kMaxRanges];const int n=plan::Leftovers(s.run,layout,left,plan::kMaxRanges);
        for(int i=0;i<layout.count;++i)if(layout.ranges[i].role==plan::Role::system && layout.ranges[i].system>=0 && plan::MayHoldSystem(s.run,layout.ranges[i].system)) {
            static char line[240];plan::Out l(line,sizeof(line));
            l.S(Pl()?"miejsce systemu ":"the range of ").S(plan::NameOf(s.plan,layout.ranges[i].system)).S(Pl()?" może zawierać zainstalowany system: nie jest proponowane do zwolnienia i nic z niego nie usunięto":" may hold an installed system: it is not offered for freeing and nothing was removed from it");
            LogLine(line);
        }
        for(int i=0;i<n;++i) {
            bool free=s.plan.leftover==plan::Leftover::free;
            if(!s.plan.unattended) {
                static char text[400];plan::Out out(text,sizeof(text));plan::LeftoverText(s.plan,layout,left[i],Pl(),out);
                static char16_t wide[400];Widen(text,wide,400);
                static const ask::Choice choices[]={{u"zostaw zarezerwowane",u"keep it reserved","keep"},{u"zwolnij",u"free it","free"}};
                size_t chosen=0;free=ask::Choose(wide,choices,2,chosen) && chosen==1;
            }
            if(!free)continue;
            if(!FreeLeftover(*disk,layout.ranges[left[i]]))plan::Trouble(trouble,"a reserved range could not be freed",Pl());
            else {
                layout.ranges[left[i]].role=plan::Role::spare;   // freed: the report must not call it reserved (seen in VMware)
                if(left[i]<32)freed|=uint32_t(1)<<left[i];
                static char line[240];plan::Out l(line,sizeof(line));
                l.S(Pl()?"zwolniono zarezerwowane miejsce systemu ":"freed the reserved range of ").S(plan::NameOf(s.plan,layout.ranges[left[i]].system)).S(Pl()?", megabajtów: ":", megabytes: ").N(plan::RangeMb(layout,layout.ranges[left[i]]));
                LogLine(line);
            }
        }
        // r410 (h8, RUN G): a placeholder that kept the plan's free space from Windows Setup (CoverFreeForWindows) and
        // was not removed because that Windows never came back goes now: the summary promised the space free.
        {seeddisk::Disk sd{disk->io,disk->blockSize,disk->blocks,images::SeedRead,images::SeedWrite};
         static seeddisk::Partition parts[128];seeddisk::Info info;
         if(seeddisk::Inspect(sd,images::SeedWork(),seeddisk::kWorkBytes,parts,128,info)==seeddisk::Error::None)
            for(size_t i=0;i<info.partitionCount;++i)
                if(SameFold(parts[i].name,u"Charon plan: kept free")
                   && seeddisk::RemovePartition(sd,images::SeedWork(),seeddisk::kWorkBytes,parts[i].unique,parts[i].type,parts[i].firstLba)!=seeddisk::Error::None)
                    plan::Trouble(trouble,"a placeholder over the plan's free space could not be removed",Pl());
         if(disk->io->FlushBlocks)reinterpret_cast<images::BlockFlush>(disk->io->FlushBlocks)(disk->io);}
        images::Reconnect(disk->handle);
        s.bound.volume=images::VolumeOfPartition(s.esp);s.storage=Bind(s.bound);   // the volumes have new handles now
    } else plan::Trouble(trouble,"the layout or the disk of the plan was not found",Pl());
    FinishExtras(trouble);
    // 2. Boot order and what starts by itself.
    // r410: when this Charon was not started from the plan's EFI partition, the wait cannot be stored in its
    // settings; the report then names the usual wait, which is the one in force (not the one that was asked for).
    // r410 (known issue 1, seen in VMware: Charon runs plans from memory, so the wait was never stored): when this
    // Charon is not the one on the plan's partition, the wait goes into the settings of the Charon there.
    const bool ownPartition=U().host->selfDevice==s.bound.volume && U().host->planContinue;
    auto storeWait=[&](uint8_t seconds,bool wait) {
        if(ownPartition)return true;   // stored with the Continue target below / by planContinue(wait)
        return U().host->planWaitOn && U().host->planWaitOn(U().host->context,s.bound.volume,seconds,wait);
    };
    plan::Machine told=machine;told.waitNotStored=!ownPartition && !U().host->planWaitOn;
    const plan::FinishRule rule=plan::FinishOf(s.plan,s.run,told);
    const bool restarts=plan::FinishRestarts(s.plan,s.run,told);static char targetName[40];
    plan::Copy(targetName,sizeof(targetName),rule.target>=0?s.plan.systems[rule.target].name:"");
    uint16_t charon=0,dos=0,first[2+plan::kMaxSystems+8];size_t firstCount=0;
    uint16_t entryOf[plan::kMaxSystems]={};bool entryFound[plan::kMaxSystems]={};   // r410: the systems' own boot entries, for the chosen order
    const bool haveCharon=EntryNamed(u"Charon",charon),haveDos=EntryNamed(u"CharonDOS",dos);
    if(rule.dosFirst && haveDos)first[firstCount++]=dos;
    if(haveCharon)first[firstCount++]=charon;
    if(!firstCount || !PutFirst(first,firstCount))plan::Trouble(trouble,"the boot order could not be written",Pl());
    const char* firstLabel=rule.dosFirst && haveDos?"CharonDOS":"Charon";
    s.storage.write(s.storage.context,"\\EFI\\HAK\\first-entry",firstLabel,plan::Len(firstLabel));
    // 3. Registry, Charon's Continue target, CharonDOS defaults.
    static plan::Place places[plan::kMaxSystems];memset(places,0,sizeof(places));
    for(int i=0;i<s.plan.count;++i) {
        if(plan::ResultOf(s.run,i)!=plan::Result::done)continue;
        const char* path=nullptr;const efi_handle volume=LoaderOfSystem(i,path);uint8_t unique[16];
        if(!volume || !PartitionOf(volume,unique)) {plan::Trouble(trouble,"the loader of a system was not found",Pl(),s.plan.systems[i].name);continue;}
        plan::Out part(places[i].partition,sizeof(places[i].partition));plan::GuidText(unique,part);
        plan::Copy(places[i].path,sizeof(places[i].path),path);
        if(s.plan.systems[i].kind!=plan::Kind::charondos)entryFound[i]=EntryOfLoader(unique,path,entryOf[i]);   // r410
        // r410 (kd, run kd5: a Fedora image as the default system -> "Uwaga: nie udało się ustawić systemu, który uruchamia
        // się sam"): a ready image has no boot entry of its own; the entry the chosen order makes below came too late for
        // the Continue target. It is made here, before the target is set (same call as below).
        if(s.plan.systems[i].kind!=plan::Kind::charondos && !entryFound[i] && i==rule.target) {
            static char16_t name16[48],path16[96];size_t k=0;
            for(;s.plan.systems[i].name[k] && k+1<48;++k)name16[k]=char16_t(uint8_t(s.plan.systems[i].name[k]));name16[k]=0;
            for(k=0;path[k] && k+1<96;++k)path16[k]=char16_t(path[k]);path16[k]=0;
            const char16_t* data=plan::IsHypervisor(s.plan.systems[i].kind)?HypervisorOptions(volume):nullptr;   // r410 (h8 run 6)
            efi_status made=dossys::PutOption(false,name16,volume,path16,false,nullptr,data,true);
            if(made==EFI_UNSUPPORTED)made=dossys::PutOption(false,name16,volume,path16,false,nullptr,data);
            if(made==EFI_SUCCESS)entryFound[i]=EntryOfLoader(unique,path,entryOf[i]);
            if(entryFound[i])LogLine("wpis rozruchowy systemu utworzony przez Charona, żeby mógł się uruchamiać sam","a boot entry was made by Charon for a system so that it can start by itself");
        }
        if(i==rule.target) {
            uint16_t id=0;
            const bool found=s.plan.systems[i].kind==plan::Kind::charondos?haveDos && ((id=dos),true):entryFound[i] && ((id=entryOf[i]),true);
            if(!found || !WriteContinue(s.bound.volume,id))plan::Trouble(trouble,"the system that starts by itself could not be set",Pl());
            // The wait is one of Charon's settings: stored through the menu when Charon runs from the plan's partition.
            else if(ownPartition)U().host->planContinue(U().host->context,id,uint8_t(rule.timeout>60?60:rule.timeout),false);
            else if(!told.waitNotStored && !storeWait(uint8_t(rule.timeout>60?60:rule.timeout),false))told.waitNotStored=true;
        }
    }
    // r410 (user answer 2): the order of the boot entries that was chosen in the detailed mode: behind Charon's own
    // entry (and CharonDOS's) come the systems' entries in that order, then every other entry as it stood.
    // r410 (kd, T8): on a kept disk the kept systems' entries ([kept] entries) may stand right after Charon's (first=yes).
    const bool keptFirst=plan::Eq(Answers().GetOr("kept","first",""),"yes");
    if((plan::BootOrderChosen(s.plan) || keptFirst) && firstCount) {
        int sequence[plan::kMaxSystems];const int count=plan::BootSequence(s.plan,sequence);
        static char line[300];plan::Out l(line,sizeof(line));l.S(Pl()?"kolejność wpisów rozruchowych po Charonie:":"order of the boot entries after Charon:");
        if(keptFirst)for(const char* at=Answers().GetOr("kept","entries","");*at;) {
            uint16_t id=0;size_t n=0;
            for(;at[n] && at[n]!=',';++n) {const char c=at[n];id=uint16_t(id*16+(c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:0));}
            static uint8_t option[4096];
            if(n && firstCount<2+8 && BootOption(id,option,sizeof(option))) {first[firstCount++]=id;l.S(" Boot").Hex(id,4);}
            at+=n;if(*at==',')++at;
        }
        for(int k=0;k<count;++k) {
            const int i=sequence[k];
            if(plan::ResultOf(s.run,i)!=plan::Result::done)continue;
            // r410 (T5 rerun, agent 1): a ready image has no boot entry of its own (Charon started it from its loader), so
            // a chosen order found nothing to move and the note kept Charon waiting for a key. The entry is made now.
            if(!entryFound[i]) {
                const char* loader=nullptr;const efi_handle volume=LoaderOfSystem(i,loader);
                if(volume && loader) {
                    static char16_t name16[48],path16[96];size_t k=0;
                    for(;s.plan.systems[i].name[k] && k+1<48;++k)name16[k]=char16_t(uint8_t(s.plan.systems[i].name[k]));name16[k]=0;
                    for(k=0;loader[k] && k+1<96;++k)path16[k]=char16_t(loader[k]);path16[k]=0;
                    const char16_t* data=plan::IsHypervisor(s.plan.systems[i].kind)?HypervisorOptions(volume):nullptr;   // r410 (h8 run 6)
                    efi_status made=dossys::PutOption(false,name16,volume,path16,false,nullptr,data,true);
                    if(made==EFI_UNSUPPORTED)made=dossys::PutOption(false,name16,volume,path16,false,nullptr,data);
                    uint8_t unique[16];
                    if(made==EFI_SUCCESS && PartitionOf(volume,unique))entryFound[i]=EntryOfLoader(unique,loader,entryOf[i]);
                    if(entryFound[i])LogLine("wpis rozruchowy systemu utworzony przez Charona, żeby stanął w wybranej kolejności","a boot entry was made by Charon for a system so that it stands in the chosen order");
                }
            }
            if(!entryFound[i]) {plan::Trouble(trouble,"the boot entry of a system was not found, so it keeps its place in the boot order",Pl(),s.plan.systems[i].name);continue;}
            first[firstCount++]=entryOf[i];l.Ch(' ').S(s.plan.systems[i].name).S(" (Boot").Hex(entryOf[i],4).Ch(')');
        }
        if(!PutFirst(first,firstCount))plan::Trouble(trouble,"the chosen order of the boot entries could not be written",Pl());
        else LogLine(line);
    }
    if(rule.menuWaits) {
        if(ownPartition)U().host->planContinue(U().host->context,0,0,true);
        else if(!storeWait(0,true))told.waitNotStored=true;
    }
    if(told.waitNotStored && rule.timeout!=5)
        plan::Trouble(trouble,"the wait before the default system starts could not be stored in the settings of Charon on the plan's EFI partition, so it keeps its usual value",Pl());
    if(told.waitNotStored)LogLine(Pl()?"czas oczekiwania przed startem domyślnego systemu nie został zapisany":"the wait before the default system starts was not stored");
    else if(!ownPartition)LogLine(Pl()?"czas oczekiwania przed startem domyślnego systemu zapisany w ustawieniach Charona na partycji EFI planu":"the wait before the default system starts is stored in the settings of Charon on the plan's EFI partition");
    {
        efi_time t{};dossys::Rt()->GetTime(&t,nullptr);
        static char date[16],fresh[2000],merged[8192],old[6000];plan::Out d(date,sizeof(date));
        d.N(t.Year).Ch('-').Ch(char('0'+t.Month/10)).Ch(char('0'+t.Month%10)).Ch('-').Ch(char('0'+t.Day/10)).Ch(char('0'+t.Day%10));
        plan::Out lines(fresh,sizeof(fresh));plan::RegistryLines(s.plan,s.run,places,date,lines);
        size_t oldLength=0;if(!s.storage.read(s.storage.context,plan::kRegistryFile,old,sizeof(old),&oldLength))oldLength=0;
        plan::Out all(merged,sizeof(merged));plan::MergeRegistry(old,oldLength,fresh,lines.length,all);
        if(all.overflow || lines.overflow || !s.storage.write(s.storage.context,plan::kRegistryFile,merged,all.length))plan::Trouble(trouble,"the systems registry could not be written",Pl());
    }
    if(plan::HasCharonDos(s.plan)) {
        static char wanted[200],cfg[1024],next[1200];plan::Out out(wanted,sizeof(wanted));plan::DosDefaults(s.plan,s.run,Answers(),out);
        size_t have=0;if(!s.storage.read(s.storage.context,"\\EFI\\CharonDOS\\BOOT.CFG",cfg,sizeof(cfg),&have))have=0;
        bool ok=true;
        static const char* const keys[2]={"winnt","linux"};
        for(const char* key:keys) {
            char value[96];launch_cfg_get(wanted,out.length,key,value,sizeof(value));
            const long n=launch_cfg_set(cfg,have,key,value,next,sizeof(next));
            if(n<0 || size_t(n)>sizeof(cfg)) {ok=false;break;}
            memcpy(cfg,next,size_t(n));have=size_t(n);
        }
        if(!ok || (have && !s.storage.write(s.storage.context,"\\EFI\\CharonDOS\\BOOT.CFG",cfg,have)))plan::Trouble(trouble,"the defaults of WINNT and LINUX could not be written",Pl());
    }
    // 4. The clock: universal time in every system; Charon and CharonDOS show local time.
    if(plan::ClockUtc(s.plan)) {
        const char* zone=Answers().GetOr("shared","timezone","");static char text[80];
        // r410: the first installed system switched the clock from local to universal time, unless the clock kept
        // universal time before the plan (clock.txt of an earlier plan on this partition). The report needs the size
        // of that step back to give the duration of the first system.
        {static char before[256];size_t had=0;const bool was=s.storage.read(s.storage.context,"\\EFI\\HAK\\clock.txt",before,sizeof(before),&had) && clockzone::Parse(before,had).universal;
         const clockzone::Zone* z=clockzone::Find(zone,plan::Len(zone));efi_time t{};
         if(!was && z && dossys::Rt()->GetTime(&t,nullptr)==EFI_SUCCESS)
            clockShift=(int64_t(z->offset)+(clockzone::Summer(*z,clockzone::Days(t.Year,t.Month,t.Day)*1440+t.Hour*60+t.Minute)?60:0))*60;}
        const size_t n=clockzone::Find(zone,plan::Len(zone))?clockzone::Text(zone,text,sizeof(text)):0;
        if(!n || !s.storage.write(s.storage.context,"\\EFI\\HAK\\clock.txt",text,n))plan::Trouble(trouble,"the time zone for Charon's clock could not be stored",Pl());
    }
    // 5. Report, spoken and written; answer files erased; the plan closed.
    // The answer files go BEFORE the report is made, so that the report says what is true (seen in VMware: a
    // skipped Windows step's password file was erased by the closing below, after the report had already
    // warned that it could not be erased). A file that cannot be erased keeps the plan open, as before.
    for(int i=0;i<s.run.count;++i)if(s.run.steps[i].files[0])plan::EraseStepFiles(s.storage,s.run,i,Work(),workBytes);
    plan::MarkDone(s.run,step,Now());
    static char report[6000];plan::Out out(report,sizeof(report));
    const plan::Upfront upfront=plan::UpfrontFromStore(Answers());   // r410: the phases before the first system
    plan::Report(s.plan,s.run,layout,told,Answers().GetOr("shared","user",""),Pl(),out,freed,clockShift,&upfront);
    if(problems[0])out.S(Pl()?"Uwaga: ":"Note: ").S(problems).S(".\r\n");
    const int dirty=plan::CloseFiles(s.storage,s.plan,s.run,Answers(),report,Work(),workBytes);
    if(dirty>=0) {
        Fail(T(u"Nie udało się wymazać pliku odpowiedzi z hasłem z partycji EFI. Plan nie został zamknięty.",u"An answer file with a password could not be erased from the EFI partition. The plan was not closed."));
        s.run.steps[step].state=plan::StepState::failed;s.run.finished=false;SaveNow();return false;
    }
    Answers().Wipe();s.loaded=false;
    SayText(report);
    auto* input=U().host->system->ConIn;
    auto keyWaits=[&]() {Pump();return input && input->WaitForKey && U().host->boot->CheckEvent(input->WaitForKey)==EFI_SUCCESS;};
    auto hearOut=[&](unsigned tenths) {   // true: a key came
        unsigned idle=0;   // r410: 1.5 s of unbroken silence, as detailed::Hear (a long text has gaps between pieces)
        // r410 (vr, h8b): on silent machines the restart sentence came after the full cap (180 s). No voice at all:
        // nothing to wait for. The trace says what the wait saw (effects present, idle at start, tenths waited).
        auto* fx0=U().host->effects;
        const bool known=fx0 && fx0->SpeechIdle,noVoice=!U().host->voice;
        charontrace::Value("CHARON plan hear-out: effects, idle at start",(known?2:0)|(known && fx0->SpeechIdle(fx0->context)?1:0)|(noVoice?4:0));
        unsigned i=0;
        for(;i<tenths;++i) {
            if(keyWaits())return true;
            U().host->boot->Stall(100000);auto* fx=U().host->effects;
            idle=(fx && fx->SpeechIdle?fx->SpeechIdle(fx->context):noVoice)?idle+1:0;
            if(i>5 && idle>=15)break;
        }
        charontrace::Value("CHARON plan hear-out: tenths waited",i);
        return keyWaits();
    };
    // r410: every system is installed and the plan has a default system: the plan ends in it by itself. The report
    // is heard out, the restart is announced, and any key within the wait keeps Charon here instead.
    if(restarts && !problems[0] && !hearOut(1800)) {
        static char16_t line[300],name[64];Widen(targetName,name,64);
        Set(line,300,T(u"Za 15 sekund uruchamiam komputer ponownie; wystartuje ",u"In 15 seconds I restart the computer; it will start "));Cat(line,300,name);
        Cat(line,300,T(u". Dowolny klawisz otwiera zamiast tego środowisko Charona.",u". Any key opens the Charon workspace instead."));Say(line);
        bool key=false;
        for(unsigned i=0;i<150 && !key;++i) {key=keyWaits();if(!key)U().host->boot->Stall(100000);}
        if(!key) {
            charontrace::Text("CHARON plan: finished; restarting into the default system");
            auto* runtime=U().host->system->RuntimeServices;
            if(runtime && runtime->ResetSystem)runtime->ResetSystem(EfiResetCold,EFI_SUCCESS,0,nullptr);
        }
        WaitKey();return true;   // a key (taken here), or a firmware whose restart came back
    }
    // Otherwise Charon waits for a key, without a time limit; the sentence is said again every minute with a sound.
    for(;;) {
        Say(T(u"Naciśnij dowolny klawisz, aby przejść do środowiska Charona.",u"Press any key to go on to the Charon workspace."));
        bool key=false;
        for(unsigned i=0;i<600 && !key;++i) {key=keyWaits();if(!key)U().host->boot->Stall(100000);}
        if(key)break;
        Play(Icon::error);
    }
    WaitKey();
    return true;
}

// ---- a start of Charon with a waiting plan -------------------------------------------------
// Returns true when the plan took the turn (the workspace then leaves to the menu).
inline bool Continue(const plan::Machine& machine);
inline bool AtStartInner(const plan::Machine& machine);
inline bool AtStart(const plan::Machine& machine) {
    // Everything the plan says stands on the screen too, for the review keys and for a sighted helper.
    ScriptBegin(T(u" Charon: plan instalacji kilku systemów",u" Charon: a plan of several systems"));
    ReviewMode(true);
    const bool took=AtStartInner(machine);
    ScriptEnd();
    return took;
}
inline bool AtStartInner(const plan::Machine& machine) {
    if(!Open())return false;
    auto& s=S();
    ImportMarkers();
    const int before=plan::Current(s.run);
    const bool changed=plan::Resume(s.plan,s.run,s.storage,Now(),Work(),workBytes);
    if(changed && before>=0 && s.run.steps[before].state==plan::StepState::done)AfterStepDone(before);
    if(changed && !SaveNow())return true;
    return Continue(machine);
}
// r410 (user decision 2026-11-24): the question after a failed step waits WITHOUT a time limit. While nobody
// answers, a sound is given and the whole question is said again every minute. The first key goes to the list.
inline bool ChooseWaiting(const char* question,const ask::Choice* choices,size_t count,size_t& index) {
    auto* input=U().host->system->ConIn;
    static char16_t first[300];Set(first,300,T(u"Co teraz? ",u"What now? "));Cat(first,300,Pl()?choices[0].pl:choices[0].en);
    Cat(first,300,T(u", 1 z ",u", 1 of "));CatN(first,300,count);Cat(first,300,T(u". Strzałki wybierają, Enter zatwierdza.",u". The arrows choose, Enter confirms."));
    Say(first);
    for(;;) {
        bool key=false;
        for(unsigned i=0;i<600 && !key;++i) {
            Pump();key=input && input->WaitForKey && U().host->boot->CheckEvent(input->WaitForKey)==EFI_SUCCESS;
            if(!key)U().host->boot->Stall(100000);
        }
        if(key)return ask::Choose(T(u"Co teraz?",u"What now?"),choices,count,index);
        Play(Icon::error);SayText(question);
        for(unsigned i=0;i<300;++i) {   // the question is heard out (a key ends it), then the first choice again
            U().host->boot->Stall(100000);Pump();auto* fx=U().host->effects;
            if(i>5 && fx && fx->SpeechIdle && fx->SpeechIdle(fx->context))break;
            if(input && input->WaitForKey && U().host->boot->CheckEvent(input->WaitForKey)==EFI_SUCCESS)break;
        }
        Say(first);
    }
}
// Runs the plan from where it stands until it hands the machine over, waits for the user, or ends.
inline bool Continue(const plan::Machine& machine) {
    auto& s=S();
    for(int guard=0;guard<plan::kMaxSteps*3;++guard) {
        const plan::Action action=plan::Next(s.run);
        static char text[900];plan::Out out(text,sizeof(text));
        switch(action.what) {
            case plan::Do::complete:return true;
            case plan::Do::askFailure:
            case plan::Do::askPaused: {
                if(action.what==plan::Do::askFailure) {plan::FailureText(s.plan,s.run,Pl(),out);LogLine(text);} else plan::PausedText(Pl(),out);
                SayText(text);
                plan::Decision decision=plan::Policy(s.plan,s.run);
                if(!s.plan.unattended || action.what==plan::Do::askPaused) {
                    static const ask::Choice failed[]={{u"spróbuj ponownie",u"try again","retry"},{u"pomiń ten system i kontynuuj",u"skip this system and continue","skip"},{u"zakończ tutaj",u"stop here","stop"}};
                    static const ask::Choice paused[]={{u"kontynuuj",u"continue","retry"},{u"pomiń ten system",u"skip this system","skip"},{u"zakończ tutaj",u"stop here","stop"}};
                    size_t chosen=0;
                    if(action.what==plan::Do::askFailure?!ChooseWaiting(text,failed,3,chosen):!ask::Choose(T(u"Co teraz?",u"What now?"),paused,3,chosen))return true;   // Escape: nothing decided, the plan waits
                    decision=chosen==0?plan::Decision::retry:chosen==1?plan::Decision::skip:plan::Decision::stop;
                }
                if(action.what==plan::Do::askPaused && decision==plan::Decision::retry)s.run.paused=false;
                else if(!plan::Decide(s.run,decision,Now())) {Say(T(u"Tego kroku nie można pominąć.",u"This step cannot be skipped."));continue;}
                if(!SaveNow())return true;
                continue;
            }
            case plan::Do::finish:Finish(machine);return true;
            default:break;
        }
        plan::Announce(s.plan,s.run,Pl(),out);SayText(text);LogLine(text);
        // Space or Escape within the announcement pauses the plan (three seconds are given).
        {auto* input=U().host->system->ConIn;bool pause=false;
         for(unsigned i=0;i<30 && !pause;++i) {
            Pump();efi_input_key key{};
            if(input && input->ReadKeyStroke(input,&key)==EFI_SUCCESS)pause=key.UnicodeChar==u' ' || key.ScanCode==0x17;
            else U().host->boot->Stall(100000);
         }
         if(pause) {s.run.paused=true;LogLine("wstrzymano klawiszem","paused by a key");if(!SaveNow())return true;continue;}}
        const int step=action.step;char reason[160]="";bool ok=false,own=false;
        if(action.what==plan::Do::passThrough) {
            if(!SaveNow())return true;
            if(!StartSystem(step)) {plan::MarkFailed(s.run,step,plan::Fail::charon,"the system's boot loader was not found",Now());SaveNow();continue;}
            return true;   // the loader came back: the menu is shown; the next start counts again
        }
        if(action.what==plan::Do::launchFirstStart) {
            plan::MarkLaunched(s.run,step,Now());if(!SaveNow())return true;
            if(Runners().beforeFirstStart)Runners().beforeFirstStart(s.run.steps[step].system);
            if(!StartSystem(step)) {plan::MarkFailed(s.run,step,plan::Fail::charon,"the image's boot loader was not found",Now());SaveNow();continue;}
            return true;
        }
        // Charon's own steps and the start of an installer: the dialogue's part (phase B2).
        auto& r=Runners();
        const bool have=action.what==plan::Do::prepareDisk?r.prepare!=nullptr:action.what==plan::Do::writeDos?r.writeDos!=nullptr
            :action.what==plan::Do::writeImage?r.writeImage!=nullptr:r.install!=nullptr;
        if(!have) {Fail(T(u"Ta wersja Charona nie potrafi wykonać tego kroku planu. Nic nie zostało zmienione.",u"This version of Charon cannot run this step of the plan. Nothing has been changed."));return true;}
        plan::MarkLaunched(s.run,step,Now());if(!SaveNow())return true;
        WriteCurrent(step);
        switch(action.what) {
            case plan::Do::prepareDisk:own=true;ok=r.prepare(reason,sizeof(reason));break;
            case plan::Do::writeDos:own=true;ok=r.writeDos(action.system,reason,sizeof(reason));break;
            case plan::Do::writeImage:own=true;ok=r.writeImage(action.system,reason,sizeof(reason));break;
            default:BootNextCharon();ok=r.install(action.system,step,reason,sizeof(reason));break;
        }
        if(own && ok)plan::MarkDone(s.run,step,Now());
        else if(!ok)plan::MarkFailed(s.run,step,plan::Fail::charon,reason,Now());
        else return true;   // an installer that returned without an error: the next start decides
        if(!SaveNow())return true;
    }
    return true;
}

}  // namespace planrun
}  // namespace ws
}  // namespace hakefi
