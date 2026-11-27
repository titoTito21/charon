#pragma once
// Charon r409: several systems in one go - the dialogue that builds a plan, and the steps that
// need the installers. Everything here CALLS the single-system code (questions, generators,
// image writer, CharonDOS's formatter); none of that code is changed for it.
//
//   EntryQuestion()   "One system, or several systems side by side?" - asked by install::Dialogue
//                     through install::PlanDialogue(). Returns false at once when no combination
//                     is offered (see Offered()), so the usual dialogue is untouched.
//   Build()           the list (a ready set, or the user's own list), the shared answers once,
//                     each system's own questions, the disk, the layout, the pre-flight, the
//                     summary and the ONE typed word; then the plan runs (planrun::Continue).
//   Prepare()         step 0. Everything is validated and held in memory first; then the
//                     variable, the partition table, the EFI partition, the placeholders; and as
//                     the first act after the layout exists: Charon onto the new EFI partition
//                     (with its answer sets when the disk was Charon's own), the plan saved
//                     there, the entries "Charon" (first) and "Charon voice".
//   WriteDos / WriteImage / InstallDebian   the runners of the steps (Runners()).
//   FinishExtras()    what the finish step needs from here.
//
// What this version offers in a plan: CharonDOS (without a Windows 9x/XP profile), the ready
// images of Debian, Ubuntu and Fedora, Debian's installer and Windows (Microsoft's own images
// only; never an unofficial one); on ONE disk that is erased whole. Ubuntu's installer, a disk
// whose content is kept and several disks are not wired here (the plan core computes them; see
// tests/charon-r409-multiboot-results.txt).
//
// Windows in a plan (InstallWindows): its range of the layout is a placeholder until its step.
// Then the range is made free space, and - once the image's size is known - the Windows partition
// is made at its start and the installer's temporary media partition at its end
// (winpart::ProvidePlan); Setup installs into the partition by its position number
// (diskmode=partition), using the plan's EFI partition. At the first sign-in the media partition
// goes and Windows takes its space back, all inside the range. The password waits in the step's
// own file on the EFI partition (decision D4) and is erased when the step is done; the answer
// file itself lies on the media partition, is overwritten at the first sign-in, and Charon checks
// after the step that the partition is gone (CleanAfterWindows).
// r410 (helper bd): Ubuntu's installer in a plan (InstallUbuntu: autoinstall, storage layout direct, mode use_gap, the
// plan's EFI partition lends its label CIDATA), the shared data partition (detailed mode only, decision D7: exFAT
// written by step 0, CharonPlanExfat.h; each Linux mounts it at /media/data), typed sizes in the own list.
// GPL-2.0-or-later.
#include "CharonPlanExfat.h"
#if __has_include("CharonWsHypervisor.h")   // r410: agent 2's Charon Hypervisor step (hv::RunPlanStep, hv::kPartitionType)
#include "CharonWsHypervisor.h"
#endif
namespace hakefi {
namespace ws {
namespace planrun {

// ---- what can be in a plan -----------------------------------------------------------------
struct Offer {const char* id;plan::Kind kind;const install::Entry* entry;const char16_t* pl;const char16_t* en;};
inline const Offer* Offers(size_t& count) {
    static const Offer offers[]={
        {"dos",plan::Kind::charondos,&install::dosGroup[0],u"CharonDOS",u"CharonDOS"},
        {"win",plan::Kind::windows,&install::windowsGroup[0],u"Windows, wersja do wyboru",u"Windows, the version you choose"},
        {"debian",plan::Kind::debian,&install::linuxAutoGroup[0],u"Debian, instalator",u"Debian, its installer"},
        {"ubuntu",plan::Kind::ubuntu,&install::linuxAutoGroup[1],u"Ubuntu, instalator, wymaga 8 GB pamięci",u"Ubuntu, its installer, needs 8 GB of memory"},   // r410 (bd)
        {"idebian",plan::Kind::imageDebian,&install::imageGroup[0],u"Debian 13, gotowy obraz",u"Debian 13, ready-made image"},
        {"iubuntu",plan::Kind::imageUbuntu,&install::imageGroup[1],u"Ubuntu 26.04, gotowy obraz",u"Ubuntu 26.04, ready-made image"},
        {"ifedora",plan::Kind::imageFedora,&install::imageGroup[3],u"Fedora 45, gotowy obraz, bez mowy",u"Fedora 45, ready-made image, no speech"},
        {"hv",plan::Kind::hypervisor,nullptr,u"Charon Hypervisor: maszyny wirtualne, pobierany z sieci",u"Charon Hypervisor: virtual machines, downloaded from the network"},   // r410 (agent 2)
    };
    count=sizeof(offers)/sizeof(offers[0]);return offers;
}
inline const Offer* OfferOf(const char* id) {
    size_t n=0;const Offer* offers=Offers(n);
    for(size_t i=0;i<n;++i)if(plan::Eq(offers[i].id,id))return &offers[i];
    return nullptr;
}
// The ready sets. Two independent flags, kept separate on purpose so a set can be shown to the user
// without asserting a VM-verified install it has not had:
//   `offered` - the set appears in the normal release list (a shipping decision);
//   `proven`  - the set's FULL end-to-end install was run in a virtual machine and passed, as logged
//               in tests/charon-r409-multiboot-results.txt.
// The test switch (\EFI\HAK\plan-test) still shows every set regardless of either flag.
struct Preset {const char16_t* pl;const char16_t* en;const char* systems[3];bool proven;bool offered;};
inline const Preset* Presets(size_t& count) {
    static const Preset presets[]={
        {u"CharonDOS i Debian, gotowy obraz",u"CharonDOS and Debian, ready-made image",{"dos","idebian",nullptr},true,true},
        {u"CharonDOS i Debian, instalator",u"CharonDOS and Debian, its installer",{"dos","debian",nullptr},true,true},
        {u"Debian, instalator, i Ubuntu, gotowy obraz",u"Debian, its installer, and Ubuntu, ready-made image",{"debian","iubuntu",nullptr},true,true},   // r410: T5 run 2 (vr, r410a-ag5, a69v) RAN AND PASSED
        {u"Windows i Debian, instalator",u"Windows and Debian, its installer",{"win","debian",nullptr},true,true},
        {u"CharonDOS i Windows",u"CharonDOS and Windows",{"dos","win",nullptr},true,true},
        {u"CharonDOS, Windows i Debian, instalator",u"CharonDOS, Windows and Debian, its installer",{"dos","win","debian"},true,true},
        // r410 (user, through the coordinator): the Charon Hypervisor (agent 2) beside other systems; each shown only once its run passed
        {u"Windows i Charon Hypervisor",u"Windows and Charon Hypervisor",{"win","hv",nullptr},true,true},   // r410: h8 run 4c (a94, Windows + HV as an own list, same steps) RAN AND PASSED
        {u"Debian, instalator, i Charon Hypervisor",u"Debian, its installer, and Charon Hypervisor",{"debian","hv",nullptr},true,true},   // r410: h8 run 1 (a85) RAN AND PASSED, hostname by run 2 (a88)
        {u"Debian, gotowy obraz, i Charon Hypervisor",u"Debian, ready-made image, and Charon Hypervisor",{"idebian","hv",nullptr},true,true},   // r410: h8 run 3b (a93, 8 GB) RAN AND PASSED
        // r410 (user): OFFERED for the user to run on their own hardware; proven stays FALSE because the full
        // end-to-end triple install was NOT completed in the test harness (blocked only by the host's Wi-Fi/NAT
        // EFI-DHCP limit, not a Charon defect). What IS proven: the whole plan Q&A drive, the windows-iso-url
        // override, and the download-failure-leaves-disk-unchanged guarantee (in a VM); and the Charon Hypervisor
        // target boots+speaks (standalone, apkovl ovl22). See tests/charon-r410-install-results.txt and
        // charon-r409-multiboot-results.txt. To be verified end-to-end by the user on real hardware.
        {u"Windows, Debian, instalator, i Charon Hypervisor",u"Windows, Debian, its installer, and Charon Hypervisor",{"win","debian","hv"},false,true},
        {u"CharonDOS i Charon Hypervisor",u"CharonDOS and Charon Hypervisor",{"dos","hv",nullptr},true,true},   // r410: S7.5 and h8 run 2 (a88) RAN AND PASSED
    };
    count=sizeof(presets)/sizeof(presets[0]);return presets;
}
// The test switch: the file \EFI\HAK\plan-test on Charon's own volume shows every set and the
// own list, proven or not. It exists for the trial runs; a release medium does not carry it.
inline bool TestSwitch() {
    FileRef ref;ref.volume=U().host->selfDevice;Set(ref.path,pathUnits,u"\\EFI\\HAK\\plan-test");efi_file_protocol* file=nullptr;
    if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)!=EFI_SUCCESS || !file)return false;
    file->Close(file);return true;
}
inline bool Offered() {
    if(TestSwitch())return true;
    size_t n=0;const Preset* presets=Presets(n);
    for(size_t i=0;i<n;++i)if(presets[i].offered)return true;
    return false;
}
// r410 (bd): defined with the Ubuntu runner below.
inline bool HasData();
inline bool DebianDataLines(install::Text& t);
inline void ImageDataLines(install::Text& y);

// ---- answers between the question store and the plan ----------------------------------------
// The question store keeps the POINTER of a key, so keys that come out of the plan's store (whose
// records move) are copied into a pool of their own first.
inline const char* Intern(const char* key) {
    static char pool[16384];static size_t used=0;
    for(size_t at=0;at<used;at+=plan::Len(pool+at)+1)if(plan::Eq(pool+at,key))return pool+at;
    const size_t n=plan::Len(key)+1;
    if(used+n>sizeof(pool))return nullptr;
    memcpy(pool+used,key,n);used+=n;return pool+used-n;
}
// The keys of a [system.<id>] section that describe the system to the plan, not to an installer.
inline bool ModelKey(const char* key) {
    static const char* const keys[]={"kind","name","version","disk","size_gb","swap_gb","winre","image","image_mb","need_mb","speaks","esp_guid","seed_lba","programs","placed","iso_url","seed_hidden"};
    for(const char* k:keys)if(plan::Eq(k,key))return true;
    return false;
}
// Every answer of the question store into `section`; the clear password stays out (the hash goes).
inline bool Capture(const char* section) {
    auto& a=ask::A();bool ok=true;
    for(size_t i=0;i<a.count;++i)if(!plan::SecretKey(section,a.keys[i]))ok=Answers().Put(section,a.keys[i],ask::Get(a.keys[i])) && ok;
    return ok;
}
// The question store as it was when system `id` was asked: its generators read it as always.
inline bool Restore(const char* id) {
    char section[40];plan::SystemSection(id,section,sizeof(section));
    ask::Clear();bool ok=true;const size_t count=Answers().Count();
    for(size_t i=0;i<count;++i) {
        const char* s=nullptr;const char* k=nullptr;const char* v=nullptr;Answers().At(i,s,k,v);
        if(!plan::Eq(s,section) || ModelKey(k))continue;
        const char* key=Intern(k);
        ok=key && ask::Put(key,v) && ok;
    }
    return ok && ask::Complete();
}

// ---- the list --------------------------------------------------------------------------------
inline void SayList(const plan::Plan& p,char16_t* text,size_t cap) {
    Set(text,cap,T(u"Twoja lista: ",u"Your list: "));
    if(!p.count)Cat(text,cap,T(u"pusta",u"empty"));
    for(int i=0;i<p.count;++i) {
        static char16_t name[64];Widen(p.systems[i].name,name,64);
        if(i)Cat(text,cap,u", ");
        CatN(text,cap,size_t(i)+1);Cat(text,cap,u" ");Cat(text,cap,name);
    }
    Cat(text,cap,u". ");
}
// r410 (bw, T7): two systems of one kind must not share a name - it is what the summary says, and for two Windows
// it becomes each one's line in Windows' own text boot menu (bcdedit description). The second one is "Windows 11 (2)".
inline void UniqueName(plan::Plan& p,int index) {
    plan::System& s=p.systems[index];int same=0;
    char base[48];plan::Copy(base,sizeof(base),s.name);const size_t n=plan::Len(base);
    for(int i=0;i<index;++i) {
        const char* other=p.systems[i].name;
        size_t k=0;while(k<n && other[k]==base[k])++k;
        if(plan::Eq(other,base) || (k==n && other[n]==' ' && other[n+1]=='('))++same;
    }
    if(!same)return;
    plan::Out name(s.name,sizeof(s.name));name.S(base).S(" (").N(uint64_t(same)+1).S(")");
}
inline bool AddSystem(plan::Plan& p,const Offer& o) {
    if(p.count>=plan::kMaxSystems)return false;
    plan::System& s=p.systems[p.count];memset(&s,0,sizeof(s));
    plan::Copy(s.id,sizeof(s.id),o.id);s.kind=o.kind;s.disk=1;
    // A second system of one kind gets a number behind its id.
    for(int n=2;plan::FindSystem(p,s.id)>=0 && n<10;++n) {plan::Out id(s.id,sizeof(s.id));id.S(o.id).N(uint64_t(n));}
    // The spoken name ("Debian 13", "CharonDOS").
    plan::Copy(s.name,sizeof(s.name),o.kind==plan::Kind::debian?"Debian":o.kind==plan::Kind::ubuntu?"Ubuntu":o.kind==plan::Kind::imageDebian?"Debian 13":o.kind==plan::Kind::imageUbuntu?"Ubuntu 26.04"
        :o.kind==plan::Kind::imageFedora?"Fedora 45":o.kind==plan::Kind::windows?"Windows 11":o.kind==plan::Kind::hypervisor?"Charon Hypervisor":"CharonDOS");
    UniqueName(p,p.count);
    s.speaks=!o.entry || o.entry->speech!=install::Speech::none;   // r410: the hypervisor (no entry) speaks
    if(plan::IsImage(o.kind)) {s.imageMb=uint32_t(o.entry->bytes>>20);plan::Copy(s.image,sizeof(s.image),o.entry->url);}
    ++p.count;return true;
}
// The user's own list: add, remove, done. Refusals are said at once.
inline bool EditList(plan::Plan& p,const plan::Machine& machine) {
    size_t offers=0;const Offer* all=Offers(offers);
    for(;;) {
        static char16_t text[700];SayList(p,text,700);Cat(text,700,T(u"Co teraz?",u"What now?"));
        static const ask::Choice actions[]={{u"dodaj system",u"add a system","add"},{u"usuń ostatni system",u"remove the last system","remove"},{u"lista jest gotowa",u"the list is ready","done"}};
        size_t action=p.count>=2?2:0;
        if(!ask::Choose(text,actions,3,action))return false;
        if(action==0) {
            if(p.count>=plan::kMaxSystems) {Say(T(u"Plan może mieć najwyżej sześć systemów.",u"A plan can hold six systems at most."));continue;}
            static ask::Choice list[8];for(size_t i=0;i<offers;++i)list[i]={all[i].pl,all[i].en,all[i].id};
            size_t pick=0;if(!ask::Choose(T(u"Który system dodać?",u"Which system should be added?"),list,offers,pick))continue;
            if(all[pick].entry && all[pick].entry->notePl) {Say(Pl()?all[pick].entry->notePl:all[pick].entry->noteEn);install::HearOut(150);}
            if(all[pick].entry && !install::ConfirmSilent(*all[pick].entry))continue;
            AddSystem(p,all[pick]);
        } else if(action==1) {if(p.count)--p.count;}
        else {
            plan::Refusals report;plan::ValidatePlan(p,machine,report);
            if(!report.Refused())return true;
            static char why[2000];plan::Out out(why,sizeof(why));plan::SayAll(report,p,Pl(),false,out);SayText(why);install::HearOut(200);
        }
    }
}

// ---- machine and disk as the plan core sees them ---------------------------------------------
inline plan::Machine MachineForPlan(const disks::Disk* target) {
    plan::Machine m=MachineNow();
    m.network=install::EnsureNetwork();
    uint64_t bytes=0;
    if(systools::MemoryBytes(U().host->boot,bytes)==EFI_SUCCESS)m.ramMb=bytes>>20;
    auto& s=disks::D();uint8_t internal=0;
    for(size_t i=0;i<s.count;++i)if(!s.list[i].usb && !s.list[i].removable)++internal;
    m.internalDisks=internal;
    m.startMediumIsTarget=target && target->hasCharon;
    return m;
}
// r410 (kd, T8 kept disk): the free space on a FAT volume, in bytes (EFI_FILE_SYSTEM_INFO.FreeSpace, UEFI 2.10 13.5.16).
inline bool VolumeFreeBytes(efi_handle volume,uint64_t& bytes) {
    efi_file_protocol* root=volume?OpenRoot(volume):nullptr;if(!root)return false;
    alignas(8) static uint8_t info[512];size_t size=sizeof(info);
    const bool ok=root->GetInfo(root,&media::fsInfoGuid,&size,info)==EFI_SUCCESS && size>36;
    if(ok)bytes=systools::Le64(info+24);
    root->Close(root);return ok;
}
// The disk as the layout calculator sees it. r410 (kd, T8): with its partitions, the usable area and the free entries
// of its GPT (header at LBA 1: FirstUsableLBA at byte 40, LastUsableLBA at 48, NumberOfPartitionEntries at 80; UEFI
// 2.10 table 5.21), and the room on its EFI system partition - so that a plan can KEEP what is on the disk.
inline void DiskStateOf(const disks::Disk& d,plan::DiskState& state) {
    memset(&state,0,sizeof(state));
    state.sectorSize=d.blockSize;state.sectors=d.blocks;state.gpt=d.gpt;state.blank=d.blank;
    plan::Copy(state.serial,sizeof(state.serial),d.serial);plan::Copy(state.model,sizeof(state.model),d.model);
    if(!d.gpt || !d.io || d.blockSize<512 || d.blockSize>4096)return;
    alignas(16) static uint8_t header[4096];
    if(d.io->ReadBlocks(d.io,d.io->Media->MediaId,1,d.blockSize,header)!=EFI_SUCCESS)return;
    state.firstUsable=systools::Le64(header+40);state.lastUsable=systools::Le64(header+48);
    uint32_t entries=systools::Le32(header+80);if(entries>128)entries=128;
    state.entriesFree=entries>d.partCount?uint32_t(entries-d.partCount):0;
    for(size_t i=0;i<d.partCount && state.count<64;++i) {
        const auto& p=d.parts[i];plan::DiskPart& out=state.parts[state.count++];
        out.firstLba=p.first;out.lastLba=p.last;
        bool reserved=true;for(int k=0;k<16;++k)reserved=reserved && p.type[k]==seeddisk::kTypeLinuxReserved.bytes[k];
        out.type=p.kind==1?plan::PartType::efiSystem:p.kind==5?plan::PartType::microsoftReserved:p.kind==2?plan::PartType::basicData
            :p.kind==3?plan::PartType::linuxData:p.kind==6?plan::PartType::windowsRecovery:reserved?plan::PartType::placeholder:plan::PartType::other;
        if(p.kind==1 && !state.espFreeKnown) {   // the first EFI partition is the one ComputeLayout would share
            uint64_t bytes=0;
            if(VolumeFreeBytes(images::VolumeOfPartition(p.unique),bytes)) {state.espFreeKnown=true;state.espFreeMb=bytes>>20;}
        }
    }
}

// ---- the questions ------------------------------------------------------------------------------
// The password as it was typed, for the Windows steps only (Linux gets the hash). It lives in memory
// from the shared questions until step 0 has written it into each Windows step's own file on the
// new EFI partition (decision D4); every way out of Build() wipes it.
constexpr size_t secretBytes=128;
inline char* SharedSecret() {static char secret[secretBytes];return secret;}
inline void WipeSecret() {char* secret=SharedSecret();for(size_t i=0;i<secretBytes;++i)secret[i]=0;}
// The firmware's TCG2 protocol is there exactly when the computer has a TPM 2.0 the firmware runs.
inline bool Tpm2Present() {
    efi_guid guid={0x607f766c,0x7455,0x42be,{0x93,0x0b,0xe4,0xd7,0x6d,0xb2,0x72,0x0f}};void* found=nullptr;
    return U().host->boot->LocateProtocol(&guid,nullptr,&found)==EFI_SUCCESS && found;
}
// Asks whether an address answers (a HEAD request, redirections followed); a failure is said.
inline bool AddressAnswers(const char* url) {
    static https::Client client;static https::Response probe;client.transport=net::tcp::Transport();
    const auto result=https::Probe(client,url,probe);
    if(result!=https::Result::ok) {net::HttpsFail(client,result);return false;}
    return true;
}
// The shared answers, asked once: language, user, password (express); keyboard, time zone, name of
// the computer, full name as well (detailed). Left in the question store for the systems' own
// questions and recorded in [shared].
inline bool AskShared(bool quick) {
    ask::Clear();
    Say(T(u"Te odpowiedzi posłużą wszystkim systemom.",u"These answers are used for every system."));
    if(Pl() && !ask::Get("locale")[0])ask::Put("locale","pl_PL");
    // r410 (helper s6a, F1): in a detailed plan the site file is read BEFORE the shared main questions, so its [shared]
    // hostname, fullname, user and its [secrets] password are their starting values, as in a single installation.
    if(!quick) {
        detailed::Begin('s','s');detailed::SetMain(install::commonQuestions,sizeof(install::commonQuestions)/sizeof(install::commonQuestions[0]));
        sets::FindSoftFile();sets::LoadSite();
    }
    if(!ask::Run(install::commonQuestions,sizeof(install::commonQuestions)/sizeof(install::commonQuestions[0]),quick))return false;
    install::DeriveDefaults();install::HashPassword();
    if(!ask::Get("hash")[0]) {Fail(T(u"Nie udało się przygotować hasła.",u"The password could not be prepared."));return false;}
    if(!ask::Complete())return false;
    // r410: a detailed plan asks the shared detailed questions here, once for all its systems (the rows marked
    // shared in tools/charon-install/charon_questions.py); they are captured into [shared] with the rest.
    if(!quick) {
        // the site file was read above (Begin is not repeated: it would close the administration groups it opened)
        detailed::SetMain(nullptr,0);detailed::SaveHook()=nullptr;detailed::PickHook()=catalogue::Pick;detailed::NamesHook()=catalogue::Names;
        Say(T(u"Teraz pytania szczegółowe wspólne dla wszystkich systemów planu.",u"Now the detailed questions that are shared by every system of the plan."));
        if(!detailed::Run())return false;
    }
    plan::Copy(SharedSecret(),secretBytes,ask::Get("password"));
    bool ok=Capture("shared");
    ok=Answers().Put("shared","hostpattern",ask::Get("hostname")) && ok;
    return ok;
}
// r410: which detailed questions a system of a plan has (family and installation path of CharonWsDetailed.h).
inline void DetailContext(const plan::System& s,const Offer* o,char& family,char& path) {
    family='l';path='d';
    if(s.kind==plan::Kind::windows) {family='w';path='w';}
    else if(s.kind==plan::Kind::charondos) {family='x';path='x';}
    else if(s.kind==plan::Kind::ubuntu)path='u';
    else if(s.kind==plan::Kind::imageDebian)path='D';
    else if(s.kind==plan::Kind::imageUbuntu)path='U';
    else if(s.kind==plan::Kind::imageArch)path='A';
    else if(s.kind==plan::Kind::imageFedora)path='F';
    (void)o;
}
// r410: the per-system remainder of a detailed plan. The shared answers are already in the question store (for
// Windows under Windows' own keys); what this system cannot take of them is said by name; its summary speaks only
// what differs from the shared answers. The answer "detailed" tells the step that the carriers are wanted.
// r410 (helper s7, S7-F1): the site file is read once per system BEFORE its own main questions (Debian's SSH server,
// Windows' rows ...), so they start at the site's values as in a single installation; the same reading serves the
// detailed rows of SystemDetails, which then neither begins again nor reads the file a second time.
inline bool& SitePreloaded() {static bool on=false;return on;}
inline void PreloadSite(const plan::System& s,const Offer* o,const ask::Question* main,size_t count,bool quick) {
    SitePreloaded()=false;
    char family,path;DetailContext(s,o,family,path);
    if(quick || family=='x')return;
    detailed::Begin(family,path);detailed::SetMain(main,count);
    sets::FindSoftFile();sets::LoadSite();
    detailed::SetMain(nullptr,0);SitePreloaded()=true;
}
inline bool SystemDetails(const plan::System& s,const Offer* o,bool quick) {
    char family,path;DetailContext(s,o,family,path);
    const bool preloaded=SitePreloaded();SitePreloaded()=false;
    if(!preloaded)detailed::Begin(family,path);
    if(quick || family=='x')return true;
    if(path=='F') {   // r410 (bd): in a plan Fedora gets a bigger EFI partition inside its own range (PlanBiggerEsp)
        Say(T(u"Obraz Fedory ma partycję EFI o rozmiarze tylko 100 megabajtów. W planie Charon da Fedorze w jej miejscu na dysku nową partycję EFI o rozmiarze 256 megabajtów, więc odpowiedzi szczegółowe i zestawy programów działają jak w innych systemach.",
              u"The Fedora image has an EFI partition of only 100 megabytes. In a plan Charon gives Fedora a new EFI partition of 256 megabytes inside its own space on the disk, so the detailed answers and the software sets work as in the other systems."));
        install::HearOut(200);
    }
    if(family=='w')detailed::MapShared();
    static char16_t note[900];detailed::NotApplied(note,900);if(note[0]) {Say(note);detailed::Hear(300);}
    detailed::TakeBaseline();
    detailed::SaveHook()=nullptr;detailed::PickHook()=catalogue::Pick;detailed::NamesHook()=catalogue::Names;
    if(!preloaded) {sets::FindSoftFile();sets::LoadSite();}   // r410: the site's answers for this system's own rows
    if(!detailed::Run())return false;
    ask::Put("detailed","yes");
    // r410 (helper s6a, run 1): the gates of the administration rows and of the own software list are kept with the
    // system's answers; StepDetails opens them again, or Env()/Ini() would drop those rows at the step.
    ask::Put("admin_open",ask::AdminOpen()?"yes":"no");ask::Put("soft_file_on",detailed::SoftFileOn()?"yes":"no");
    return ask::Complete();
}
// r410: at a step of the plan, after Restore(): the generators carry the detailed answers again.
inline void StepDetails(const plan::System& s) {
    char family,path;DetailContext(s,nullptr,family,path);detailed::Begin(family,path);detailed::C().active=ask::Is("detailed","yes");
    ask::AdminOpen()=ask::Is("admin_open","yes");detailed::SoftFileOn()=ask::Is("soft_file_on","yes");   // r410 (s6a)
}
// r410 (helper s7, runs A and D): Escape at a system's FIRST question left the whole plan dialogue (back to Charon's
// environment), whereas everywhere else Escape goes back one question. AskSystem now sets this when it returns false
// because the user left at its first question; the caller then goes back to the previous system (or the shared
// answers) instead of ending the plan. Any other false (a refusal, a store that is full) ends the dialogue as before.
inline bool& SystemBack() {static bool back=false;return back;}
inline void CoverFreeForWindows(seeddisk::Disk& sd,uint64_t first,uint64_t last);   // r410 (h8, RUN G): below
inline bool AskSystemOnce(plan::Plan& p,int index,bool quick);
inline bool AskSystem(plan::Plan& p,int index,bool quick) {SystemBack()=false;return AskSystemOnce(p,index,quick);}
// One system's own questions, on top of the shared answers; recorded in [system.<id>].
inline bool AskSystemOnce(plan::Plan& p,int index,bool quick) {
    plan::System& s=p.systems[index];const Offer* o=OfferOf(s.id);
    if(!o) {size_t n=0;const Offer* all=Offers(n);for(size_t i=0;i<n;++i)if(all[i].kind==s.kind)o=&all[i];}
    char section[40];plan::SystemSection(s.id,section,sizeof(section));
    static char16_t intro[200],name[64];Widen(s.name,name,64);
    Set(intro,200,T(u"System ",u"System "));CatN(intro,200,size_t(index)+1);Cat(intro,200,T(u" z ",u" of "));CatN(intro,200,p.count);Cat(intro,200,u", ");Cat(intro,200,name);Cat(intro,200,u".");
    Say(intro);
    // The computer's name: the shared one, identical on every system of the plan (user decision 2026-11-24).
    {plan::Out host(s.host,sizeof(s.host));host.S(Answers().GetOr("shared","hostpattern","charon-pc"));}
    // The shared answers as the starting point (the clear password is gone; the hash is there).
    ask::Clear();
    {const size_t count=Answers().Count();
     for(size_t i=0;i<count;++i) {const char* sec=nullptr;const char* k=nullptr;const char* v=nullptr;Answers().At(i,sec,k,v);
        if(plan::Eq(sec,"shared") && !plan::Eq(k,"hostpattern")) {const char* key=Intern(k);if(key)ask::Put(key,v);}}}
    ask::Put("hostname",s.host);
    if(s.kind==plan::Kind::charondos) {
        size_t programs=0;if(!dosapps::AskGroup(programs,true)) {SystemBack()=true;return false;}
        s.needMb=512;
        return Answers().PutNumber(section,"programs",programs);
    }
    if(s.kind==plan::Kind::hypervisor) {
        // r410 (user, through the coordinator): the shared answers apply (language, keyboard, time zone, computer name,
        // speech); its virtual machines see the other systems' disks only when the user asks (default: no).
        static const ask::Choice disksChoice[]={{u"nie",u"no","no"},{u"tak, ale tylko do odczytu, gdy system go nie używa",u"yes, read-only, while that system is not running","yes"}};
        size_t pick=0;
        if(!quick && !ask::Choose(T(u"Czy maszyny wirtualne Charon Hypervisor mają widzieć dyski innych systemów tego komputera?",
                                    u"Should the virtual machines of Charon Hypervisor see the disks of this computer's other systems?"),disksChoice,2,pick)) {SystemBack()=true;return false;}
        ask::Put("hv_other_disks",disksChoice[pick].value);
        return Capture(section);
    }
    if(s.kind==plan::Kind::windows) {
        // The version: Microsoft's own images only (the list of the single-system dialogue).
        const size_t versionCount=sizeof(install::windowsVersions)/sizeof(install::windowsVersions[0]);
        static ask::Choice versions[sizeof(install::windowsVersions)/sizeof(install::windowsVersions[0])];
        for(size_t i=0;i<versionCount;++i)versions[i]={install::windowsVersions[i].pl,install::windowsVersions[i].en,install::windowsVersions[i].id};
      size_t version=0;   // r410 (a9w): kept across the loop, so coming back puts the cursor on the version chosen before
      for(;;) {   // r410 (s7): Escape at the first of Windows' own questions comes back here, to the version
        if(!ask::Choose(T(u"Która wersja Windows?",u"Which Windows version?"),versions,versionCount,version)) {SystemBack()=true;return false;}
        const install::WindowsVersion& chosen=install::windowsVersions[version];
        const bool ten=ask::Same(chosen.id,"10") || ask::Same(chosen.id,"ltsc10"),server=ask::Same(chosen.id,"server");
        plan::Copy(s.version,sizeof(s.version),chosen.id);
        plan::Copy(s.name,sizeof(s.name),ten?"Windows 10":server?"Windows Server 2025":"Windows 11");
        UniqueName(p,index);   // r410 (bw, T7)
        ask::Put("wversion",chosen.id);
        if(chosen.index) {char digits[2]={char('0'+chosen.index),0};ask::Put("windex",digits);}
        if(chosen.url) {
            Say(T(u"To oficjalny obraz wersji próbnej od Microsoftu: tylko po angielsku, działa 90 dni, a Server 180 dni, i nie da się jej zamienić na pełną wersję.",
                  u"This is Microsoft's official evaluation image: in English only, it works for 90 days, Server for 180 days, and it cannot be turned into a full version."));install::HearOut(200);
            ask::Put("wlocale","en-US");ask::Put("edition","auto");
        } else ask::Put("wlocale",Pl()?"pl-PL":"en-US");   // Microsoft's download in Charon's own language
        // Asked once for every system: name of the computer, user, password. Decided by the plan: the clock.
        ask::Put("password",SharedSecret());
        static ask::Question asked[sizeof(install::windowsQuestions)/sizeof(install::windowsQuestions[0])]={};size_t count=0;
        for(const auto& q:install::windowsQuestions) {
            if(ask::Same(q.key,"wlocale") || ask::Same(q.key,"hostname") || ask::Same(q.key,"user") || ask::Same(q.key,"password") || ask::Same(q.key,"clockutc"))continue;
            if(chosen.url && ask::Same(q.key,"edition"))continue;
            asked[count]=q;
            if(!server && asked[count].choices==install::windowsProfiles && asked[count].count>1)asked[count].count-=1;   // "Windows Server as a workstation"
            ++count;
        }
        PreloadSite(s,o,asked,count,quick);   // r410 (s7)
        if(!ask::Run(asked,count,quick)) {
            Say(T(u"Wracam do poprzedniego pytania.",u"Back to the previous question."));continue;
        }
        if(!install::ResolveWindowsReaders())return false;
        install::DropUnfitProfile();
        // A plan runs without the user; the two profiles with a step done by hand stay out of it.
        if(ask::Is("profile","ReviOS") || ask::Is("profile","AtlasOS")) {
            Say(T(u"Profile ReviOS i AtlasOS wymagają kroku wykonywanego ręcznie, a plan przebiega bez twojego udziału. Windows zainstaluje się bez profilu; profil zastosujesz później w zainstalowanym systemie.",
                  u"The ReviOS and AtlasOS profiles need a step done by hand, and a plan runs without you. Windows will be installed without a profile; you can apply the profile later in the installed system."));
            install::HearOut(200);ask::Put("profile","none");
        }
        ask::Put("clockutc","yes");   // every plan keeps the computer's clock in universal time
        // Windows 11 Setup refuses a computer without TPM 2.0 on a page nobody reads aloud: asked now.
        if(!ten && !server && !ask::Is("bypass","yes") && !Tpm2Present()) {
            if(!Confirm(T(u"Firmware tego komputera nie zgłasza modułu TPM 2.0, a instalator Windows 11 odmawia wtedy instalacji. Czy mam kazać instalatorowi pominąć sprawdzanie wymagań sprzętowych? Bez tego Windows 11 nie może być częścią planu.",
                          u"This computer's firmware reports no TPM 2.0, and Windows 11 Setup then refuses to install. Shall I tell Setup to skip the hardware requirement checks? Without that, Windows 11 cannot be part of the plan."))) {
                Say(T(u"Bez pominięcia sprawdzania sprzętu Windows 11 nie zainstaluje się na tym komputerze. Plan nie został utworzony; nic nie zostało zmienione.",
                      u"Without skipping the hardware checks Windows 11 will not install on this computer. The plan was not made; nothing was changed."));
                return false;}
            ask::Put("bypass","yes");
        }
        if(!ask::Complete())return false;
        if(!SystemDetails(s,o,quick))return false;
        return Capture(section);
      }
    }
    if(plan::IsImage(s.kind)) {
        // r410 (kd, run kd4): the Fedora image gets neither the desktop nor the reader question, as in the single path
        // (CharonWsInstall3.h imageQuestionsFedora, helper ls): its reader question offered Speakup after the sentence
        // that Fedora has no espeakup.
        if(s.kind==plan::Kind::imageFedora) {
            PreloadSite(s,o,install::imageQuestionsFedora,sizeof(install::imageQuestionsFedora)/sizeof(install::imageQuestionsFedora[0]),quick);
            if(!ask::Run(install::imageQuestionsFedora,sizeof(install::imageQuestionsFedora)/sizeof(install::imageQuestionsFedora[0]),quick)) {SystemBack()=true;return false;}
            ask::Put("desktop","");ask::Put("readers","");
        } else if(PreloadSite(s,o,install::imageQuestions,sizeof(install::imageQuestions)/sizeof(install::imageQuestions[0]),quick),!ask::Run(install::imageQuestions,sizeof(install::imageQuestions)/sizeof(install::imageQuestions[0]),quick)) {SystemBack()=true;return false;}
        if(!install::ResolveReaders(ask::Any("desktop")))return false;
        if(!ask::Complete())return false;
        static char probe[32000-256];install::Text y{probe,sizeof(probe),0,false};probe[0]=0;
        install::ImageUserData(y,ask::Is("speech","yes") && o->entry->speech!=install::Speech::none,install::DistroOf(*o->entry));
        const bool fits=!y.overflow;for(auto& c:probe)c=0;
        if(!fits) {install::AnswersDoNotFit();return false;}
    } else if(s.kind==plan::Kind::ubuntu) {   // r410 (bd): Ubuntu's installer, the questions of the single-system dialogue
        PreloadSite(s,o,install::ubuntuQuestions,sizeof(install::ubuntuQuestions)/sizeof(install::ubuntuQuestions[0]),quick);
        if(!ask::Run(install::ubuntuQuestions,sizeof(install::ubuntuQuestions)/sizeof(install::ubuntuQuestions[0]),quick)) {SystemBack()=true;return false;}
        if(!install::ResolveReaders(ask::Any("desktop")))return false;
        if(!ask::Complete())return false;
    } else {   // Debian's installer
        PreloadSite(s,o,install::debianQuestions,sizeof(install::debianQuestions)/sizeof(install::debianQuestions[0]),quick);
        if(!ask::Run(install::debianQuestions,sizeof(install::debianQuestions)/sizeof(install::debianQuestions[0]),quick)) {SystemBack()=true;return false;}
        if(!install::ResolveReaders(ask::Any("desktop")))return false;
        if(!ask::Complete())return false;
    }
    if(!SystemDetails(s,o,quick))return false;
    return Capture(section);
}

// ---- step 0: the disk ---------------------------------------------------------------------------
struct Prepared {const disks::Disk* disk;plan::Layout layout;bool ownHome;};
inline Prepared& Ready() {static Prepared prepared;return prepared;}
inline seeddisk::Disk SeedDisk(const disks::Disk& d) {return seeddisk::Disk{d.io,d.blockSize,d.blocks,images::SeedRead,images::SeedWrite};}
// The first step number of a system (for the name of its placeholder).
inline int StepOf(const plan::Run& run,int system) {for(int i=0;i<run.count;++i)if(run.steps[i].system==system)return i;return 0;}
inline bool Prepare(char* reason,size_t capacity) {
    auto& s=S();auto& ready=Ready();const disks::Disk& d=*ready.disk;plan::Layout& layout=ready.layout;
    plan::Out why(reason,capacity);
    static const seeddisk::Guid espType={{0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b}};
    // Fresh GUIDs for every range, decided before anything is written: the EFI partition's goes
    // into the variable first, so a start in the middle of this step finds "a plan stopped".
    int espRange=-1;
    for(int i=0;i<layout.count;++i) {
        seeddisk::Guid unique;images::RandomGuid(unique,300+uint64_t(i));memcpy(layout.ranges[i].guid,unique.bytes,16);
        if(layout.ranges[i].role==plan::Role::efiSystem || layout.ranges[i].role==plan::Role::dosEfi)espRange=i;
    }
    // r410 (kd, T8): a kept disk whose EFI partition is shared has no ESP range: the plan's EFI partition is that one.
    const disks::Part* keptEsp=nullptr;
    if(!layout.newTable && layout.espExisting)
        for(size_t i=0;i<d.partCount;++i)if(d.parts[i].kind==1 && d.parts[i].first==layout.espFirstLba)keptEsp=&d.parts[i];
    if(layout.newTable?espRange<0:(layout.espExisting?!keptEsp:espRange<0)) {why.S("the EFI partition of the plan is not in the layout");return false;}
    memcpy(s.esp,keptEsp?keptEsp->unique:layout.ranges[espRange].guid,16);
    s.bound.volume=nullptr;s.storage=Bind(s.bound);
    if(!plan::WritePointer(s.storage,s.plan,s.run,s.esp)) {why.S("the firmware did not take the variable CharonPlan");return false;}
    auto fail=[&](const char* text) {why.S(text);s.storage.setVariable(s.storage.context,nullptr,0);return false;};
    // The partition table and every range.
    seeddisk::Disk sd=SeedDisk(d);
    if(layout.newTable) {
        Say(T(u"Tworzę nową tablicę partycji.",u"Creating a new partition table."));
        seeddisk::Guid diskGuid;images::RandomGuid(diskGuid,299);
        if(!dossys::EmptyGpt(d.io,d.blocks,diskGuid))return fail("the partition table could not be written");
    } else {
        // r410 (kd, T8, spec 2.10): the disk's table stays; no existing partition is moved, retyped or written. Every
        // range of the plan must still lie in free space (a partition made since the dialogue: nothing is changed).
        static seeddisk::Partition parts[128];seeddisk::Info info;
        if(seeddisk::Inspect(sd,images::SeedWork(),seeddisk::kWorkBytes,parts,128,info)!=seeddisk::Error::None)return fail("the existing partitions of the disk are not as they were when the plan was made");
        for(int i=0;i<layout.count;++i)for(size_t k=0;k<info.partitionCount;++k) {
            const uint64_t first=layout.ranges[i].firstLba,last=first+layout.ranges[i].lbaCount-1;
            if(!(parts[k].lastLba<first || parts[k].firstLba>last))return fail("the existing partitions of the disk are not as they were when the plan was made");
        }
        Say(T(u"Zostawiam istniejące partycje bez zmian i dodaję partycje planu w wolnym miejscu.",u"Leaving the existing partitions unchanged and adding the plan's partitions in the free space."));
        LogLine("krok 0: dysk zachowany; istniejące partycje nie są ruszane, plan dodaje swoje w wolnym miejscu","step 0: the disk is kept; the existing partitions are not touched, the plan adds its own in the free space");
    }
    for(int i=0;i<layout.count;++i) {
        const plan::Range& r=layout.ranges[i];seeddisk::Guid unique;memcpy(unique.bytes,r.guid,16);seeddisk::AddResult added{};
        seeddisk::Error error=seeddisk::Error::None;
        if(r.placeholder) {
            static char name[64];static char16_t wide[64];plan::Out out(name,sizeof(name));plan::PlaceholderName(s.plan,r,r.system>=0?StepOf(s.run,r.system):0,out);Widen(name,wide,37);
            error=seeddisk::ReservePlaceholder(sd,images::SeedWork(),seeddisk::kWorkBytes,r.firstLba,r.lbaCount,unique,wide,added);
        } else {
            seeddisk::AddRequest request{};request.unique=unique;request.sizeBytes=r.lbaCount*d.blockSize;request.alignmentBytes=d.blockSize;
            request.placement=seeddisk::Placement::At;request.firstLba=r.firstLba;request.attributes=0;
            request.type=r.role==plan::Role::microsoftReserved?seeddisk::kTypeMicrosoftReserved:r.role==plan::Role::data || r.role==plan::Role::dos?seeddisk::kTypeBasicData:espType;
            request.name=r.role==plan::Role::dosEfi || r.role==plan::Role::dos?u"CharonDOS":r.role==plan::Role::data?u"DATA":r.role==plan::Role::microsoftReserved?u"Microsoft reserved partition":u"EFI system partition";
            error=seeddisk::AddPartition(sd,images::SeedWork(),seeddisk::kWorkBytes,request,added);
        }
        if(error!=seeddisk::Error::None)return fail("a partition of the plan could not be created");
    }
    // r410 (bd): the shared data partition gets its exFAT file system now, before any system (CharonPlanExfat.h).
    for(int i=0;i<layout.count;++i)if(layout.ranges[i].role==plan::Role::data) {
        if(d.blockSize!=512)return fail("the shared data partition needs 512-byte sectors");
        Say(T(u"Formatuję wspólną partycję na dane, exFAT.",u"Formatting the shared data partition, exFAT."));
        struct Place {media::BlockIo* io;uint64_t first;};static Place place;place={d.io,layout.ranges[i].firstLba};
        const plan::exfat::Sink sink{&place,[](void* c,uint64_t lba,const uint8_t* data,size_t bytes) {
            const Place& p=*static_cast<const Place*>(c);Pump();return dossys::Write(p.io,p.first+lba,data,bytes);}};
        uint32_t lo,hi;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));
        if(!plan::exfat::Format(sink,layout.ranges[i].firstLba,layout.ranges[i].lbaCount,u"DATA",lo^(hi*2654435761u)))return fail("the shared data partition could not be formatted");
        LogLine("krok 0: wspólna partycja na dane ma system plików exFAT o etykiecie DATA","step 0: the shared data partition has an exFAT file system labelled DATA");
    }
    if(!keptEsp) {
     Say(T(u"Formatuję partycję systemową EFI.",u"Formatting the EFI system partition."));
     uint32_t lo,hi;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));
     const plan::Range& r=layout.ranges[espRange];
     if(!dossys::FormatFat32(d.io,r.firstLba,r.lbaCount,r.role==plan::Role::dosEfi?"CHARONDOS":"CHARON",lo^hi))return fail("the EFI partition could not be formatted");}
    if(d.io->FlushBlocks)reinterpret_cast<images::BlockFlush>(d.io->FlushBlocks)(d.io);
    images::Reconnect(d.handle);
    const efi_handle volume=images::VolumeOfPartition(s.esp);
    if(!volume)return fail("the firmware does not see the new EFI partition");
    // FIRST after the layout exists: Charon, the plan, the entries.
    if(!images::CharonOnto(volume,ready.ownHome))return fail("Charon could not be copied onto the new EFI partition");
    {static FileRef a,b;a.volume=volume;Set(a.path,pathUnits,u"\\EFI\\HAK\\HAKBOOT.EFI");b.volume=volume;Set(b.path,pathUnits,u"\\EFI\\BOOT\\BOOTX64.EFI");
     // r410 (kd, T8): the fallback loader of a kept system's EFI partition (Windows' copy of its boot manager) stays.
     efi_file_protocol* there=nullptr;const bool kept=keptEsp && OpenFile(b,EFI_FILE_MODE_READ,0,&there)==EFI_SUCCESS && there;
     if(there)there->Close(there);
     if(!kept) {MakeFolders(volume,u"\\EFI\\BOOT");CopyFile(a,b);}}   // for firmware that has lost its entries: Charon, which shows every system
    s.bound.volume=volume;s.storage=Bind(s.bound);
    if(!plan::LayoutToStore(s.plan,layout,1,Answers()))return fail("the layout does not fit the plan file");
    if(plan::Save(s.storage,s.plan,s.run,Answers(),Work(),workBytes,s.esp)!=plan::Io::ok)return fail("the plan could not be written to the new EFI partition");
    efi_status status=dossys::PutOption(false,u"Charon",volume,u"\\EFI\\HAK\\HAKBOOT.EFI",true,nullptr,nullptr,true);
    if(status==EFI_UNSUPPORTED)status=dossys::PutOption(false,u"Charon",volume,u"\\EFI\\HAK\\HAKBOOT.EFI",true,nullptr);
    if(status!=EFI_SUCCESS) {why.S("the boot entry Charon could not be written");return false;}   // the plan is on the disk: the variable stays
    status=dossys::PutOption(true,u"Charon voice",volume,u"\\EFI\\HAK\\CHARONVOICE.EFI",true,nullptr,nullptr,true);
    if(status==EFI_UNSUPPORTED)status=dossys::PutOption(true,u"Charon voice",volume,u"\\EFI\\HAK\\CHARONVOICE.EFI",true,nullptr);
    if(status!=EFI_SUCCESS)LogLine("nie udało się zapisać opcji sterownika Charon voice","the driver option Charon voice could not be written");
    LogLine("krok 0: dysk jest przygotowany, Charon i plan są na nowej partycji EFI","step 0: the disk is prepared, Charon and the plan are on the new EFI partition");
    return true;
}

// ---- the runners ---------------------------------------------------------------------------------
// The disk and the layout of the plan in the session, read again (after a restart nothing of
// Build() is in memory).
inline bool Reload(char* reason,size_t capacity) {
    auto& ready=Ready();plan::Out why(reason,capacity);
    ready.disk=DiskOfPlan();
    if(!ready.disk) {why.S("the disk of the plan was not found");return false;}
    if(!plan::LayoutFromStore(Answers(),S().plan,1,ready.layout)) {why.S("the layout is missing from the plan file");return false;}
    return true;
}
// Makes the range of a system free space: its placeholder goes - and, when the step is tried
// again, whatever the failed attempt left INSIDE the range (the partitions of a half-written
// image). Nothing outside the range is touched; a partition that lies only partly inside it is
// not ours, and then nothing is changed at all (false).
inline bool FreeRange(const disks::Disk& d,const plan::Range& r) {
    seeddisk::Disk sd=SeedDisk(d);
    static seeddisk::Partition parts[128];seeddisk::Info info;
    if(seeddisk::Inspect(sd,images::SeedWork(),seeddisk::kWorkBytes,parts,128,info)!=seeddisk::Error::None)return false;
    const uint64_t first=r.firstLba,last=r.firstLba+r.lbaCount-1;
    for(size_t i=0;i<info.partitionCount;++i) {
        const bool inside=parts[i].firstLba>=first && parts[i].lastLba<=last;
        const bool apart=parts[i].lastLba<first || parts[i].firstLba>last;
        if(!inside && !apart)return false;
    }
    for(size_t i=0;i<info.partitionCount;++i) {
        if(parts[i].firstLba<first || parts[i].lastLba>last)continue;
        if(seeddisk::RemovePartition(sd,images::SeedWork(),seeddisk::kWorkBytes,parts[i].unique,parts[i].type,parts[i].firstLba)!=seeddisk::Error::None)return false;
    }
    if(d.io->FlushBlocks)reinterpret_cast<images::BlockFlush>(d.io->FlushBlocks)(d.io);
    return true;
}
inline bool FreeLeftover(disks::Disk& disk,const plan::Range& range) {return FreeRange(disk,range);}
// A system that was skipped, or that failed, after its step had opened its range (seen in VMware: a Windows
// download that broke left a Windows partition and a hole in the range; the next step, Debian's installer, then
// found two free ranges and refused): the range becomes one placeholder again, with the GUID the layout knows, so
// that "the biggest free space" is the next system's own range and the finish step can still ask about this one.
// Ranges of systems that are done, or still to come, are not looked at.
// One range of a system becomes its named placeholder again (everything inside it goes first).
inline bool ReserveOne(const disks::Disk& d,const plan::Range& r) {
    auto& s=S();seeddisk::Disk sd=SeedDisk(d);
    if(!FreeRange(d,r))return false;
    seeddisk::Guid unique;memcpy(unique.bytes,r.guid,16);seeddisk::AddResult added{};
    static char name[64];static char16_t wide[64];plan::Out out(name,sizeof(name));plan::PlaceholderName(s.plan,r,StepOf(s.run,r.system),out);Widen(name,wide,37);
    return seeddisk::ReservePlaceholder(sd,images::SeedWork(),seeddisk::kWorkBytes,r.firstLba,r.lbaCount,unique,wide,added)==seeddisk::Error::None;
}
inline bool ReserveAgain(const disks::Disk& d,const plan::Layout& layout) {
    auto& s=S();seeddisk::Disk sd=SeedDisk(d);static seeddisk::Partition parts[128];
    for(int i=0;i<layout.count;++i) {
        const plan::Range& r=layout.ranges[i];
        if(r.role!=plan::Role::system || r.system<0)continue;
        const plan::Result result=plan::ResultOf(s.run,r.system);
        if(result!=plan::Result::skipped && result!=plan::Result::failed)continue;
        seeddisk::Info info;
        if(seeddisk::Inspect(sd,images::SeedWork(),seeddisk::kWorkBytes,parts,128,info)!=seeddisk::Error::None)return false;
        bool whole=false;
        for(size_t k=0;k<info.partitionCount;++k)if(parts[k].firstLba==r.firstLba && parts[k].lastLba==r.firstLba+r.lbaCount-1)whole=true;
        if(whole)continue;   // still (or again) one partition over the whole range
        if(!ReserveOne(d,r))return false;
        LogLine("miejsce systemu, który się nie zainstalował, jest znowu jedną zarezerwowaną partycją zastępczą","the range of a system that was not installed is one reserved placeholder again");
    }
    if(d.io->FlushBlocks)reinterpret_cast<images::BlockFlush>(d.io->FlushBlocks)(d.io);
    return true;
}
// r410 (agent 1, T5 run bd1 FAILED: "the disk does not have exactly one free range for Debian's installer"): a ready
// image written earlier in the plan fills only the start of its range (and its seed the end); the room between is free
// until the image's first start grows its root partition into it. An installer that "takes the biggest free space"
// would see two free ranges - or take the image's. So before an installer step every free range that lies wholly
// inside the range of ANOTHER system is covered by a placeholder (kTypeLinuxReserved, a new unique GUID); the image's
// first start (UncoverGrowthRoom, Runners().beforeFirstStart) removes it again, so the image can grow.
inline bool PlaceholderOfRange(const seeddisk::Partition& p,const plan::Range& r) {for(int k=0;k<16;++k)if(p.unique.bytes[k]!=r.guid[k])return false;return true;}
inline bool CoverGrowthRoom(const disks::Disk& d,const plan::Layout& layout,int self) {
    seeddisk::Disk sd=SeedDisk(d);seeddisk::Gap gaps[16];size_t count=0;
    if(seeddisk::FreeGaps(sd,images::SeedWork(),seeddisk::kWorkBytes,0,uint64_t(1)<<20,gaps,16,count)!=seeddisk::Error::None || count>16)return false;
    for(size_t g=0;g<count;++g)for(int i=0;i<layout.count;++i) {
        const plan::Range& r=layout.ranges[i];
        if(r.role!=plan::Role::system || r.system<0 || r.system==self)continue;
        // r410 (kd, run kd5): the PART of a free range that lies inside another system's range is covered, not only a free
        // range wholly inside it - the Fedora image's old 100 MB EFI partition (removed by PlanBiggerEsp) left free room at
        // the START of Fedora's range, joined to Ubuntu's gap, and Ubuntu's installer took those 100 MiB of Fedora's range.
        const uint64_t from=gaps[g].firstLba>r.firstLba?gaps[g].firstLba:r.firstLba;
        const uint64_t gapEnd=gaps[g].firstLba+gaps[g].lbaCount,rangeEnd=r.firstLba+r.lbaCount,to=gapEnd<rangeEnd?gapEnd:rangeEnd;
        if(to<=from || (to-from)*d.blockSize<(uint64_t(1)<<20))continue;
        seeddisk::Guid unique;images::RandomGuid(unique,331+unsigned(g)*16+unsigned(i));seeddisk::AddResult added{};
        if(seeddisk::ReservePlaceholder(sd,images::SeedWork(),seeddisk::kWorkBytes,from,to-from,unique,u"Charon: growth room",added)!=seeddisk::Error::None)return false;
        charontrace::Value("CHARON plan: growth room covered, MiB",((to-from)*d.blockSize)>>20);
        LogLine("wolne miejsce wewnątrz zakresu innego systemu (na powiększenie obrazu) jest na czas instalatora przykryte partycją zastępczą","free space inside the range of another system (room for an image to grow) is covered by a placeholder while the installer runs");
    }
    if(d.io->FlushBlocks)reinterpret_cast<images::BlockFlush>(d.io->FlushBlocks)(d.io);
    return true;
}
inline void UncoverGrowthRoom(int system) {
    auto& s=S();auto& ready=Ready();if(system<0 || !plan::IsImage(s.plan.systems[system].kind))return;
    // r410 (T5 rerun: the placeholder stayed and Ubuntu's root kept 2.5 GB): after a restart nothing has loaded the disk
    // and the layout yet - Reload first.
    {char reason[160]="";if(!Reload(reason,sizeof(reason)) || !ready.disk) {LogLine("nie udało się zwolnić miejsca na powiększenie obrazu; system będzie miał tylko rozmiar obrazu","the room for the image to grow could not be freed; the system keeps the size of the image");return;}}
    const int range=plan::RangeOfSystem(ready.layout,system);if(range<0)return;
    const plan::Range& r=ready.layout.ranges[range];
    seeddisk::Disk sd=SeedDisk(*ready.disk);static seeddisk::Partition parts[128];seeddisk::Info info;
    if(seeddisk::Inspect(sd,images::SeedWork(),seeddisk::kWorkBytes,parts,128,info)!=seeddisk::Error::None)return;
    bool changed=false;
    for(size_t i=0;i<info.partitionCount;++i) {
        const auto& p=parts[i];
        if(p.firstLba<r.firstLba || p.lastLba>r.firstLba+r.lbaCount-1 || PlaceholderOfRange(p,r))continue;
        {bool reserved=true;for(int k=0;k<16;++k)reserved=reserved && p.type.bytes[k]==seeddisk::kTypeLinuxReserved.bytes[k];if(!reserved)continue;}
        if(seeddisk::RemovePartition(sd,images::SeedWork(),seeddisk::kWorkBytes,p.unique,p.type,p.firstLba)==seeddisk::Error::None) {changed=true;
            charontrace::Value("CHARON plan: growth room freed before the image's first start, MiB",((p.lastLba-p.firstLba+1)*ready.disk->blockSize)>>20);
            LogLine("miejsce na powiększenie obrazu jest znowu wolne przed jego pierwszym uruchomieniem","the room for the image to grow is free again before its first start");}
        else LogLine("nie udało się zwolnić miejsca na powiększenie obrazu; system będzie miał tylko rozmiar obrazu","the room for the image to grow could not be freed; the system keeps the size of the image");
    }
    if(changed) {if(ready.disk->io->FlushBlocks)reinterpret_cast<images::BlockFlush>(ready.disk->io->FlushBlocks)(ready.disk->io);images::Reconnect(ready.disk->handle);}
}
// r410 (bd): install::BiggerEsp (helper ls, CharonWsInstall3.h) for a ready image INSIDE a plan's range: the same steps
// (a 256 MiB FAT16 EFI partition with the old volume serial, so the image's fstab "UUID=XXXX-XXXX /boot/efi" finds it;
// \EFI copied; the old partition taken out of the table), but everything is looked for inside the range only: the new
// partition goes just before the image's CIDATA seed, behind the image's last partition. Any failure before the old
// partition is removed takes the new one out again. `unique` becomes the new partition's GUID.
inline bool PlanBiggerEsp(const disks::Disk& d,const plan::Range& r,uint8_t* unique) {
    using namespace seeddisk;
    if(d.blockSize!=512 || !d.io)return false;
    Disk disk{d.io,d.blockSize,d.blocks,images::SeedRead,images::SeedWrite};uint8_t* work=images::SeedWork();
    static Partition parts[128];Info info{};
    if(Inspect(disk,work,kWorkBytes,parts,128,info)!=Error::None)return false;
    const uint64_t lo=r.firstLba,hi=r.firstLba+r.lbaCount-1;
    const Partition* old=nullptr;const Partition* seed=nullptr;uint64_t below=0;
    for(size_t i=0;i<info.partitionCount;++i) {
        const auto& p=parts[i];if(p.firstLba<lo || p.lastLba>hi)continue;
        if(detail::Same(p.unique.bytes,unique,16))old=&p;
        if(p.name[0]==u'c' && p.name[1]==u'i' && p.name[2]==u'd' && p.name[3]==u'a' && p.name[4]==u't' && p.name[5]==u'a' && !p.name[6])seed=&p;
    }
    if(!old || !seed)return false;
    for(size_t i=0;i<info.partitionCount;++i)if(&parts[i]!=seed && parts[i].firstLba>=lo && parts[i].lastLba<=hi && parts[i].lastLba>below)below=parts[i].lastLba;
    const uint64_t count=(uint64_t(256)<<20)/512,first=(seed->firstLba-count)/2048*2048;
    if(seed->firstLba<count+2048 || first<=below || first<lo)return false;
    static uint8_t boot[512];
    if(!images::SeedRead(d.io,old->firstLba,boot,1) || boot[510]!=0x55 || boot[511]!=0xAA)return false;
    const bool fat32=boot[22]==0 && boot[23]==0;const uint8_t* sn=boot+(fat32?67:39);
    FatOptions options{};options.sectorSize=512;options.hiddenSectors=uint32_t(first);
    options.serial=uint32_t(sn[0])|uint32_t(sn[1])<<8|uint32_t(sn[2])<<16|uint32_t(sn[3])<<24;
    const Guid oldUnique=old->unique;const uint64_t oldFirst=old->firstLba;
    static char16_t name[37];for(int k=0;k<37;++k)name[k]=old->name[k];
    AddRequest request{};request.type=kTypeEfiSystem;images::RandomGuid(request.unique,7);request.name=name[0]?name:u"EFI System Partition";
    request.sizeBytes=count*512;request.alignmentBytes=0;request.placement=Placement::At;request.firstLba=first;request.attributes=0;
    AddResult added{};
    Error error=AddPartition(disk,work,kWorkBytes,request,added);
    charontrace::Value("CHARON plan: bigger esp added",(uint64_t(error)<<56)|added.firstLba);
    if(error!=Error::None)return false;
    const Guid made=request.unique;
    auto undo=[&]() {const auto e2=RemovePartition(disk,work,kWorkBytes,made,kTypeEfiSystem,first);charontrace::Value("CHARON plan: bigger esp undone",uint64_t(e2));images::Reconnect(d.handle);return false;};
    static Memory memory;memory.reserved=false;memory.extra=0;
    if(!Reserve(memory,size_t(count*512)))return undo();
    FatInfo fat{};
    error=BuildFat(memory.data,size_t(count*512),"EFI",nullptr,0,options,&fat);
    if(error==Error::None)error=seeddisk::WriteImage(disk,first,added.lbaCount,memory.data,size_t(1+2*fat.fatSectors+kFatRootEntries*32/512)*512);
    Free(memory);
    if(d.io->FlushBlocks)reinterpret_cast<images::BlockFlush>(d.io->FlushBlocks)(d.io);
    if(error!=Error::None)return undo();
    images::Reconnect(d.handle);
    const efi_handle to=images::VolumeOfPartition(made.bytes),from=images::VolumeOfPartition(oldUnique.bytes);
    if(!to || !from)return undo();
    FileRef a,b;a.volume=from;b.volume=to;Set(a.path,pathUnits,u"\\EFI");Set(b.path,pathUnits,u"\\EFI");size_t files=0;
    const efi_status copied=CopyTree(a,b,0,&files);
    charontrace::Value("CHARON plan: bigger esp copied",(uint64_t(copied)<<32)|files);
    if(copied!=EFI_SUCCESS || !files)return undo();
    error=RemovePartition(disk,work,kWorkBytes,oldUnique,kTypeEfiSystem,oldFirst);
    charontrace::Value("CHARON plan: bigger esp old removed",uint64_t(error));
    if(error!=Error::None)return undo();
    images::Reconnect(d.handle);
    memcpy(unique,made.bytes,16);
    return true;
}
// r410 (agent 2's Charon Hypervisor; revised design: nothing of it on the EFI partition). Its range becomes TWO
// partitions: P1 "Hypervisor", Microsoft basic data, FAT32 labelled CHARONHV, 2 GiB (Charon starts its kernel from there;
// not an ESP); P2 "Hypervisor data", Linux filesystem, the rest, NOT formatted - its first MiB is zeroed (the hypervisor
// formats it at its first start when its GUID is the one written to P1). Agent 2's hv::RunPlanStep (CharonWsHypervisor.h)
// then writes P1's files only; it speaks its own progress and failures. Without that header the step fails with a reason.
constexpr uint64_t hvBootBytes=uint64_t(2)<<30;
inline bool WriteHypervisor(int system,char* reason,size_t capacity) {
    plan::Out why(reason,capacity);auto& s=S();auto& ready=Ready();
#if __has_include("CharonWsHypervisor.h")
    if(!Reload(reason,capacity))return false;
    const int range=plan::RangeOfSystem(ready.layout,system);if(range<0) {why.S("the range of this system is not in the layout");return false;}
    const disks::Disk& d=*ready.disk;const plan::Range r=ready.layout.ranges[range];
    const uint64_t bootLbas=hvBootBytes/d.blockSize;
    if(r.lbaCount<=bootLbas+(uint64_t(1)<<20)/d.blockSize) {why.S("the partition of the Charon Hypervisor could not be made in its range");return false;}
    if(!FreeRange(d,r)) {why.S("the reserved range of this system is not on the disk as the plan left it");return false;}
    seeddisk::Disk sd=SeedDisk(d);seeddisk::Guid boot,data;images::RandomGuid(boot,340);images::RandomGuid(data,341);
    static const seeddisk::Guid basicData={{0xA2,0xA0,0xD0,0xEB,0xE5,0xB9,0x33,0x44,0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7}};   // EBD0A0A2-B9E5-4433-87C0-68B6B72699C7
    seeddisk::AddResult first{},second{};
    seeddisk::AddRequest one{};one.type=basicData;one.unique=boot;one.name=u"Hypervisor";one.sizeBytes=hvBootBytes;one.alignmentBytes=0;
    one.placement=seeddisk::Placement::At;one.firstLba=r.firstLba;one.attributes=0;
    seeddisk::AddRequest two{};two.type=seeddisk::kTypeLinuxData;two.unique=data;two.name=u"Hypervisor data";two.sizeBytes=(r.lbaCount-bootLbas)*d.blockSize;
    two.alignmentBytes=0;two.placement=seeddisk::Placement::At;two.firstLba=r.firstLba+bootLbas;two.attributes=0;
    if(seeddisk::AddPartition(sd,images::SeedWork(),seeddisk::kWorkBytes,one,first)!=seeddisk::Error::None
       || seeddisk::AddPartition(sd,images::SeedWork(),seeddisk::kWorkBytes,two,second)!=seeddisk::Error::None) {
        FreeRange(d,r);why.S("the partition of the Charon Hypervisor could not be made in its range");return false;}
    uint32_t lo=0,hi=0;for(int k=0;k<4;++k) {lo=(lo<<8)|boot.bytes[k];hi=(hi<<8)|boot.bytes[4+k];}
    if(!dossys::FormatFat32(d.io,first.firstLba,first.lbaCount,"CHARONHV",lo^hi) || !dossys::Zero(d.io,second.firstLba,(uint64_t(1)<<20)/d.blockSize)) {
        FreeRange(d,r);why.S("the partition of the Charon Hypervisor could not be made in its range");return false;}
    if(d.io->FlushBlocks)reinterpret_cast<images::BlockFlush>(d.io->FlushBlocks)(d.io);
    images::Reconnect(d.handle);s.bound.volume=images::VolumeOfPartition(s.esp);s.storage=Bind(s.bound);
    if(!Reload(reason,capacity))return false;
    const efi_handle bootVolume=images::VolumeOfPartition(boot.bytes);
    if(!bootVolume) {why.S("the partition of the Charon Hypervisor could not be made in its range");return false;}
    // r410 (helper h8, run 1; user point 6m): the step reads the system's answers (hostname = the plan's shared name,
    // hv_other_disks) from the question store, which at this step held nothing of the hypervisor (charon-hypervisor
    // was written instead of charon-pc): restored as for every other system's step.
    if(!Restore(s.plan.systems[system].id)) {why.S("the answers of this system could not be read back");return false;}
    if(!hv::RunPlanStep(*ready.disk,boot.bytes,data.bytes,bootVolume)) {why.S("the Charon Hypervisor could not be written");return false;}
    char section[40];plan::SystemSection(s.plan.systems[system].id,section,sizeof(section));
    char guid[40];plan::Out g(guid,sizeof(guid));plan::GuidText(boot.bytes,g);
    Answers().Put(section,"esp_guid",guid);   // the registry and LoaderOf find \boot\vmlinuz-lts on P1 by this
    return true;
#else
    (void)s;(void)ready;(void)system;why.S("this version of Charon has no installer for the Charon Hypervisor");return false;
#endif
}
inline bool WriteDos(int system,char* reason,size_t capacity) {
    if(plan::IsHypervisor(S().plan.systems[system].kind))return WriteHypervisor(system,reason,capacity);
    plan::Out why(reason,capacity);auto& s=S();
    char section[40];plan::SystemSection(s.plan.systems[system].id,section,sizeof(section));
    const efi_handle volume=s.bound.volume;   // on an erased disk CharonDOS's partition is the plan's EFI partition
    efi_handle package=nullptr;const char16_t* place=nullptr;
    if(!dossys::FindPackage(package,place)) {why.S("the CharonDOS package was not found on any disk (EFI, HAK, packages, CharonDOS)");return false;}
    Say(T(u"Kopiuję CharonDOS.",u"Copying CharonDOS."));
    static FileRef from,to;from.volume=package;Set(from.path,pathUnits,place);to.volume=volume;Set(to.path,pathUnits,u"\\EFI\\CharonDOS");size_t copied=0;
    if(CopyTree(from,to,0,&copied)!=EFI_SUCCESS) {why.S("CharonDOS could not be copied");return false;}
    MakeFolders(volume,u"\\EFI\\CharonDOS\\C");
    uint64_t programs=0;plan::ParseNumber(Answers().GetOr(section,"programs","0"),programs);
    size_t n=0;const ask::Choice* groups=dosapps::Groups(n);
    if(programs<n && !ask::Same(groups[programs].value,"none")) {
        LogLine("ten krok pobiera programy DOS teraz","this step downloads the DOS programs now");   // r410: said in the summary: their size is not known in advance
        if(install::EnsureNetwork())dosapps::Fetch(volume,u"\\EFI\\CharonDOS",size_t(programs));
        else LogLine("brak sieci: programy DOS nie zostały pobrane","no network: the DOS programs were not downloaded");
    }
    return true;
}
inline bool WriteImage(int system,char* reason,size_t capacity) {
    plan::Out why(reason,capacity);auto& s=S();auto& ready=Ready();
    if(!Reload(reason,capacity))return false;
    if(ready.disk->hasCharon && !hakefi::selfram::Active()) {   // the table of the disk Charon runs from changes: Charon goes into memory first
        if(!images::EnsureInMemory()) {why.S("Charon could not be copied into memory");return false;}
        if(!Reload(reason,capacity))return false;
    }
    const plan::System& sys=s.plan.systems[system];const Offer* o=nullptr;
    {size_t n=0;const Offer* all=Offers(n);for(size_t i=0;i<n;++i)if(all[i].kind==sys.kind)o=&all[i];}
    const int range=plan::RangeOfSystem(ready.layout,system);
    if(!o || range<0) {why.S("the image or its range is not known");return false;}
    const install::Entry& e=*o->entry;const disks::Disk& d=*ready.disk;const plan::Range& r=ready.layout.ranges[range];
    char section[40];plan::SystemSection(sys.id,section,sizeof(section));
    if(!Restore(sys.id)) {why.S("the answers of this system could not be read back");return false;}
    StepDetails(sys);   // r410
    // r410: the image waits in memory when the plan fetched everything first; then nothing is downloaded here.
    const bool upfront=held::Find(e.url)!=nullptr;
    if(upfront) {LogLine("obraz czekał w pamięci od pobierania z góry; ten krok niczego nie pobiera","the image waited in memory since the up-front download; this step downloads nothing");
        charontrace::Text("CHARON plan: image from memory, no download");}
    else {LogLine("ten krok pobiera swój obraz teraz","this step downloads its image now");charontrace::Text("CHARON plan: image is downloaded now");}
    if(!upfront && !install::EnsureNetwork()) {why.S("there is no network connection");return false;}
    // Its range becomes the free space the image is put into - and nothing else.
    if(!FreeRange(d,r)) {why.S("the reserved range of this system is not on the disk as the plan left it");return false;}
    auto& remap=images::R();remap=images::Remap{};
    remap.on=true;remap.disk=&d;remap.gapFirst=r.firstLba*d.blockSize;remap.gapEnd=(r.firstLba+r.lbaCount)*d.blockSize;
    uint64_t written=0;const char16_t* what=T(u"Obraz na dysk",u"Image to the disk");
    const bool wrote=e.way==install::Way::qcow?images::QcowToDisk(e.url,d,what,&written):images::UrlToDisk(e.url,d,what,&written);
    if(held::File* waited=held::Find(e.url))held::Drop(*waited);   // r410: written, or failed: a retry fetches it itself
    if(!wrote) {remap.on=false;why.S("the image could not be downloaded or written");return false;}
    static char userData[32000];static char metaData[200];
    install::Text y{userData,sizeof(userData),0,false};userData[0]=0;
    const bool speech=ask::Is("speech","yes") && e.speech!=install::Speech::none;
    install::ImageUserData(y,speech,install::DistroOf(e));
    ImageDataLines(y);   // r410 (bd)
    install::Text m{metaData,sizeof(metaData),0,false};metaData[0]=0;
    {uint32_t lo,hi;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));m.Add("instance-id: charon-");m.Number(lo);m.Add("\n");}
    m.Line("local-hostname: \"",ask::Get("hostname"),"\"");
    Say(T(u"Obraz jest już na dysku. Dodaję konfigurację: konto użytkownika, mowę i Charona.",u"The image is on the disk. Adding the configuration: the user account, speech and Charon."));
    const uint64_t seedLba=remap.seedLba;
    const bool configured=!y.overflow && images::SeedOnto(d,userData,y.size,metaData,m.size);
    for(auto& c:userData)c=0;
    const bool hasEsp=remap.hasEsp;uint8_t espUnique[16];memcpy(espUnique,remap.espUnique,16);
    remap.on=false;
    images::Reconnect(d.handle);
    // Reconnect gave the volumes new handles: the plan's EFI partition is found again.
    s.bound.volume=images::VolumeOfPartition(s.esp);s.storage=Bind(s.bound);
    if(!configured) {why.S("the configuration of the image could not be written to the disk");return false;}
    if(sys.kind==plan::Kind::imageFedora && hasEsp) {   // r410 (bd): Fedora's 100 MB EFI partition cannot hold Charon
        Say(T(u"Daję Fedorze większą partycję EFI, 256 megabajtów, w jej miejscu na dysku, i przenoszę na nią jej pliki rozruchowe.",
              u"Giving Fedora a bigger EFI partition, 256 megabytes, inside its space on the disk, and moving its boot files onto it."));
        const bool bigger=PlanBiggerEsp(d,r,espUnique);
        s.bound.volume=images::VolumeOfPartition(s.esp);s.storage=Bind(s.bound);
        if(!bigger) {why.S("Fedora's bigger EFI partition could not be made");return false;}
        LogLine("Fedora ma nową partycję EFI 256 MiB w swoim miejscu; stara partycja 100 MB została usunięta z tablicy","Fedora has a new 256 MiB EFI partition inside its range; the old 100 MB one was taken out of the table");
    }
    const efi_handle esp=hasEsp?images::VolumeOfPartition(espUnique):nullptr;
    const bool charon=esp && images::CharonOnto(esp,false) && install::WriteText(esp,u"\\EFI\\HAK\\speech.sh",install::speechScript,sizeof(install::speechScript)-1)
        && install::WriteText(esp,u"\\EFI\\HAK\\progress.sh",install::progressScript,sizeof(install::progressScript)-1)
        && install::WriteText(esp,u"\\EFI\\HAK\\firstboot.sh",install::firstbootScript,sizeof(install::firstbootScript)-1)
        && install::WriteText(esp,u"\\EFI\\HAK\\network.sh",install::networkScript,sizeof(install::networkScript)-1)
        && install::WriteText(esp,u"\\EFI\\HAK\\charon-tone",reinterpret_cast<const char*>(charonToneBinary),sizeof(charonToneBinary));
    if(!charon) {why.S("Charon's files could not be put on the image's EFI partition");return false;}
    {static char conf[400];const size_t bytes=install::NetworkConfNow(conf,sizeof(conf));
     if(bytes && !install::WriteText(esp,u"\\EFI\\HAK\\network.conf",conf,bytes))LogLine("nie udało się przekazać obrazowi ustawień Wi-Fi","the Wi-Fi settings could not be handed to the image");
     for(auto& c:conf)c=0;}
    install::WriteVoiceConf(esp,u"\\EFI\\HAK\\voice.conf");
    if(charon && detailed::Carried()) {static char env[16384];const size_t n=detailed::Env(env,sizeof(env));   // r410: the detailed answers (runcmd moves both)
        if(!n || !install::WriteText(esp,u"\\EFI\\HAK\\answers.env",env,n) || !install::WriteText(esp,u"\\EFI\\HAK\\personal.sh",install::personalScript,sizeof(install::personalScript)-1))
            LogLine("nie udało się przekazać obrazowi odpowiedzi szczegółowych","the detailed answers could not be handed to the image");
        for(auto& c:env)c=0;}
    // Where its loader and its seed are: needed for the first start, the registry and the clean-up.
    char guid[40];plan::Out text(guid,sizeof(guid));plan::GuidText(espUnique,text);
    Answers().Put(section,"esp_guid",guid);Answers().PutNumber(section,"seed_lba",seedLba);
    ask::Clear();
    return true;
}
// After an image's first start is done: its CIDATA seed partition goes, so that the next image's
// first start cannot read this one's seed (two volumes labelled CIDATA on one disk).
inline void RemoveSeed(int system) {
    auto& s=S();char section[40];plan::SystemSection(s.plan.systems[system].id,section,sizeof(section));
    uint64_t lba=0;if(!plan::ParseNumber(Answers().GetOr(section,"seed_lba",""),lba) || !lba)return;
    disks::Disk* d=DiskOfPlan();if(!d)return;
    for(size_t i=0;i<d->partCount;++i) {
        const auto& p=d->parts[i];
        if(p.first!=lba || !(p.name[0]==u'c' && p.name[1]==u'i' && p.name[2]==u'd'))continue;   // exactly the partition SeedOnto made there
        seeddisk::Disk sd=SeedDisk(*d);seeddisk::Guid unique;memcpy(unique.bytes,p.unique,16);
        if(seeddisk::RemovePartition(sd,images::SeedWork(),seeddisk::kWorkBytes,unique,seeddisk::kTypeBasicData,lba)==seeddisk::Error::None) {
            Answers().Put(section,"seed_lba","0");images::Reconnect(d->handle);
            s.bound.volume=images::VolumeOfPartition(s.esp);s.storage=Bind(s.bound);
            LogLine("usunięto partycję startową obrazu","an image's seed partition was removed");
        }
        return;
    }
}
// ---- r410: an installer's kernel and initrd that the plan fetched up front --------------------------
// They lie in the step's own folder on the plan's EFI partition (\EFI\HAK\plan\step<N>\linux, initrd.gz) from
// step 0 until the step is done. They hold no answers: the preseed file is made in memory at the step, as always.
inline void DebianUrl(bool initrd,bool speech,char* out,size_t capacity) {   // the very files install::RunDebian fetches
    plan::Out o(out,capacity);
    o.S("http://deb.debian.org/debian/dists/stable/main/installer-amd64/current/images/netboot/");
    o.S(!initrd?"debian-installer/amd64/linux":speech?"gtk/debian-installer/amd64/initrd.gz":"debian-installer/amd64/initrd.gz");
}
inline void UbuntuUrl(bool initrd,char* out,size_t capacity) {   // r410 (bd): the very files install::RunUbuntu fetches
    plan::Out o(out,capacity);
    for(const char16_t* p=install::ubuntuBase;*p;++p)o.Ch(char(*p));
    o.S(initrd?"netboot/amd64/initrd":"netboot/amd64/linux");
}
inline void InstallerPath(int step,const char* name,char16_t* wide,size_t units) {char path[160];plan::Out out(path,sizeof(path));plan::StepFilePath(step,name,out);Widen(path,wide,units);}
inline bool InstallerFilesThere(int step) {
    char a[160],b[160];plan::Out oa(a,sizeof(a)),ob(b,sizeof(b));plan::StepFilePath(step,"linux",oa);plan::StepFilePath(step,"initrd.gz",ob);
    return FileOn(S().bound.volume,a) && FileOn(S().bound.volume,b);
}
inline void RemoveInstallerFiles(int step) {
    static const char* const names[]={"linux","initrd.gz"};
    for(const char* name:names) {char path[160];plan::Out out(path,sizeof(path));plan::StepFilePath(step,name,out);
        if(S().storage.exists(S().storage.context,path))S().storage.remove(S().storage.context,path);}
}
// A whole file into page memory, with `extra` bytes of room behind it. False: nothing is held.
inline bool FileToMemory(efi_handle volume,const char16_t* path,Memory& m,size_t extra) {
    static FileRef ref;ref.volume=volume;Set(ref.path,pathUnits,path);efi_file_protocol* file=nullptr;
    m.data=nullptr;m.capacity=0;m.size=0;m.extra=extra;m.reserved=false;
    if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)!=EFI_SUCCESS || !file)return false;
    uint64_t size=0;file->SetPosition(file,~uint64_t(0));file->GetPosition(file,&size);file->SetPosition(file,0);
    bool ok=size && Reserve(m,size_t(size)+extra+4096);
    for(size_t at=0;ok && at<size;) {
        size_t part=size_t(size)-at;if(part>(size_t(4)<<20))part=size_t(4)<<20;
        size_t got=part;ok=file->Read(file,&got,m.data+at)==EFI_SUCCESS && got==part;at+=part;Pump();
    }
    file->Close(file);
    if(ok)m.size=size_t(size);else if(m.data)Free(m);
    return ok;
}
// install::RunDebian(false) with the kernel and the initrd read from the EFI partition instead of the network.
// Everything else is the same code: the preseed generator, Charon's archive, the start from memory.
inline bool RunDebianLocal(int step) {
    static Memory kernel,initrd;static char16_t path[pathUnits];
    install::CarryHome()=false;   // a plan's files are live on the EFI partition; nothing is carried over them
    const install::ArchivePlan archive=install::MeasureCharon();
    if(!archive.reader)Say(T(u"Uwaga: na dysku Charona nie ma plików czytnika ekranu; w nowym systemie Charona trzeba będzie zainstalować osobno.",
        u"Note: Charon's disk has no screen reader files; Charon will have to be installed in the new system separately."));
    Say(T(u"Pliki instalatora Debiana leżą na dysku od początku planu; niczego nie pobieram. Wczytuję je do pamięci.",
          u"The Debian installer's files have been on the disk since the start of the plan; I download nothing. Loading them into memory."));
    InstallerPath(step,"linux",path,pathUnits);
    if(!FileToMemory(S().bound.volume,path,kernel,0)) {Fail(T(u"Nie udało się odczytać jądra instalatora z partycji EFI.",u"The installer's kernel could not be read from the EFI partition."));return false;}
    InstallerPath(step,"initrd.gz",path,pathUnits);
    if(!FileToMemory(S().bound.volume,path,initrd,size_t(archive.bytes)+(64u<<10))) {Free(kernel);Fail(T(u"Nie udało się odczytać instalatora z partycji EFI.",u"The installer could not be read from the EFI partition."));return false;}
    const bool speech=ask::Is("speech","yes");
    static char preseed[48000];install::Text text{preseed,sizeof(preseed),0,false};preseed[0]=0;
    install::DebianPreseed(text);
    if(!DebianDataLines(text))text.overflow=true;   // r410 (bd): the shared data partition's fstab line
    if(text.overflow) {for(auto& c:preseed)c=0;Free(kernel);Free(initrd);install::AnswersDoNotFit();return false;}
    const bool packed=install::AppendCharon(initrd,"preseed.cfg",preseed,text.size);
    for(auto& c:preseed)c=0;
    if(!packed) {Free(kernel);Free(initrd);Fail(T(u"Nie udało się wczytać Charona i twoich odpowiedzi do pamięci.",u"Charon and your answers could not be loaded into memory."));return false;}
    static char16_t arguments[300];arguments[0]=0;
    Cat(arguments,300,u"auto=true priority=critical ");if(speech)Cat(arguments,300,u"speakup.synth=soft ");Cat(arguments,300,u"vga=788 --- quiet");
    Say(speech?T(u"Wszystko jest w pamięci. Uruchamiam instalator Debiana. Postęp odczytuje głos instalatora. Instalacja potrwa od kilkunastu minut do godziny. Na końcu komputer sam uruchomi się ponownie, z Charonem i czytnikiem ekranu.",
                 u"Everything is in memory. Starting the Debian installer. The installer's own voice reads the progress. The installation takes from a quarter of an hour to an hour. At the end the computer restarts by itself, with Charon and the screen reader.")
        :T(u"Wszystko jest w pamięci. Uruchamiam instalator Debiana bez mowy. Instalacja potrwa od kilkunastu minut do godziny. Na końcu komputer sam uruchomi się ponownie.",
           u"Everything is in memory. Starting the Debian installer without speech. The installation takes from a quarter of an hour to an hour. At the end the computer restarts by itself."));
    install::HearOut(200);
    net::ForgetJoined();
    install::StartFromMemory(kernel,initrd,u"Debian installer",arguments);
    for(size_t i=0;i<initrd.size;++i)initrd.data[i]=0;   // the answers were in there
    Free(kernel);Free(initrd);
    return false;
}
// Debian's installer: its range is made the ONE free space of the disk, and the installer is told
// to take the biggest free space. It does not return when the installer starts.
inline bool InstallDebian(int system,int step,char* reason,size_t capacity) {
    plan::Out why(reason,capacity);auto& s=S();auto& ready=Ready();
    held::DropAll();   // r410: an installer takes the machine; nothing fetched up front is needed in memory any more
    if(!Reload(reason,capacity))return false;
    if(ready.disk->hasCharon && !hakefi::selfram::Active()) {   // the table of the disk Charon runs from changes: Charon goes into memory first
        if(!images::EnsureInMemory()) {why.S("Charon could not be copied into memory");return false;}
        if(!Reload(reason,capacity))return false;
    }
    const plan::System& sys=s.plan.systems[system];
    if(sys.kind!=plan::Kind::debian) {why.S("this version of Charon has no unattended installer for this system in a plan");return false;}
    const int range=plan::RangeOfSystem(ready.layout,system);if(range<0) {why.S("the range of this system is not in the layout");return false;}
    const disks::Disk& d=*ready.disk;auto& all=disks::D();
    size_t index=SIZE_MAX;
    for(size_t i=0;i<all.count;++i) {
        if(&all.list[i]==&d)index=i;
        else if(all.list[i].blocks*all.list[i].blockSize==disks::Bytes(d)) {why.S("two disks have exactly the same size: Debian's installer could mix them up");return false;}
    }
    if(index==SIZE_MAX) {why.S("the disk of the plan was not found");return false;}
    if(!Restore(sys.id)) {why.S("the answers of this system could not be read back");return false;}
    StepDetails(sys);   // r410
    // r410: after a restart nothing has connected the network yet (seen in VMware: "the network is not ready" at
    // the download of the installer's kernel, and the plan stopped); the Windows and image steps always asked for it.
    if(!ReserveAgain(d,ready.layout)) {why.S("the range of a system that was not installed could not be reserved again");return false;}
    if(!FreeRange(d,ready.layout.ranges[range])) {why.S("the reserved range of this system is not on the disk as the plan left it");return false;}
    if(!CoverGrowthRoom(d,ready.layout,system)) {why.S("the free room of an image could not be covered for the installer");return false;}
    // Exactly one free range of 16 MiB or more must exist now.
    {seeddisk::Disk sd=SeedDisk(d);seeddisk::Gap gaps[4];size_t count=0;
     if(seeddisk::FreeGaps(sd,images::SeedWork(),seeddisk::kWorkBytes,0,uint64_t(16)<<20,gaps,4,count)!=seeddisk::Error::None || count!=1
        || gaps[0].firstLba!=ready.layout.ranges[range].firstLba || gaps[0].lbaCount>ready.layout.ranges[range].lbaCount) {why.S("the disk does not have exactly one free range for Debian's installer");return false;}}
    images::Reconnect(d.handle);
    s.bound.volume=images::VolumeOfPartition(s.esp);s.storage=Bind(s.bound);
    ask::Put("diskmode","free");
    ask::Put("plan","yes");   // DebianLateCommand: the last line reports the step to Charon
    install::Chosen().disk=index;install::Chosen().part=SIZE_MAX;
    if(!SaveNow()) {why.S("the plan could not be saved");return false;}
    // r410: kernel and initrd lie on the plan's EFI partition when the plan fetched everything first.
    {char section[40];plan::SystemSection(sys.id,section,sizeof(section));
     if(plan::Eq(Answers().GetOr(section,"placed",""),"yes") && InstallerFilesThere(step)) {
        LogLine("pliki instalatora Debiana leżą na partycji EFI od pobierania z góry; ten krok niczego nie pobiera","the Debian installer's files are on the EFI partition since the up-front download; this step downloads nothing");
        charontrace::Text("CHARON plan: Debian installer from local files, no download");
        RunDebianLocal(step);   // returns only when the installer could not be started
        why.S("Debian's installer could not be started from the files on the EFI partition");
        return false;
     }}
    // Asked only here: with the files already on the EFI partition (above) this step needs no firmware network,
    // and a firmware without an address after a restart must not stop it. A retry reserves the range again.
    if(!install::EnsureNetwork()) {why.S("there is no network connection");return false;}
    LogLine("ten krok pobiera pliki instalatora Debiana teraz","this step downloads the Debian installer's files now");
    charontrace::Text("CHARON plan: Debian installer is downloaded now");
    // r410 (bd): with the shared data partition the preseed needs its line (DebianDataLines), which only the local
    // path adds: the two files are fetched onto the EFI partition first and started from there.
    if(HasData()) {
        const bool speech=ask::Is("speech","yes");static char url[400];static char16_t wide[2][400];static FileRef files[2];
        for(int k=0;k<2;++k) {   // r410 (agent 3): both files at once (install::FetchPair)
            DebianUrl(k==1,speech,url,sizeof(url));Widen(url,wide[k],400);files[k].volume=s.bound.volume;InstallerPath(step,k?"initrd.gz":"linux",files[k].path,pathUnits);
            {static char16_t folder[pathUnits];size_t cut=0;for(size_t i=0;files[k].path[i];++i)if(files[k].path[i]==u'\\')cut=i;for(size_t i=0;i<cut;++i)folder[i]=files[k].path[i];folder[cut]=0;MakeFolders(s.bound.volume,folder);}
        }
        const bool ok=install::FetchPair(wide[0],nullptr,0,&files[0],T(u"jądro instalatora Debiana",u"the Debian installer's kernel"),wide[1],nullptr,0,&files[1],T(u"instalator Debiana",u"the Debian installer"));
        if(ok)RunDebianLocal(step);
        RemoveInstallerFiles(step);
        why.S("Debian's installer could not be downloaded or started");
        return false;
    }
    install::RunDebian(false);   // returns only when the installer could not be started
    why.S("Debian's installer could not be downloaded or started");
    return false;
}
// ---- r410 (bd): the shared data partition in each Linux, and Ubuntu's installer -------------------------------------
// The fstab line every Linux of the plan gets for the shared data partition (exFAT, label DATA, written by step 0).
// exFAT has no owners: uid/gid give the files to the plan's user (the first account, 1000, on every path Charon
// installs); nofail: a start never waits for it.
inline const char* DataFstab() {return "LABEL=DATA /media/data exfat defaults,nofail,uid=1000,gid=1000,umask=022 0 0";}
inline bool HasData() {return S().plan.data!=plan::DataFs::none;}
// `add` into the text at `at`. False: no room (the text is unchanged).
inline bool InsertText(install::Text& t,size_t at,const char* add) {
    const size_t n=plan::Len(add);
    if(at>t.size || t.size+n+1>t.capacity)return false;
    for(size_t i=t.size+1;i-- > at;)t.data[i+n]=t.data[i];
    for(size_t i=0;i<n;++i)t.data[at+i]=add[i];
    t.size+=n;return true;
}
inline size_t FindText(const install::Text& t,const char* what) {
    const size_t n=plan::Len(what);
    for(size_t i=0;i+n<=t.size;++i) {size_t k=0;while(k<n && t.data[i+k]==what[k])++k;if(k==n)return i;}
    return SIZE_MAX;
}
// Debian's preseed: the data partition's line goes into the new system first thing in late_command.
inline bool DebianDataLines(install::Text& t) {
    if(!HasData())return true;
    static const char marker[]="d-i preseed/late_command string ";
    const size_t at=FindText(t,marker);if(at==SIZE_MAX)return false;
    static char line[300];plan::Out o(line,sizeof(line));o.S("mkdir -p /target/media/data; echo '").S(DataFstab()).S("' >> /target/etc/fstab; ");
    return InsertText(t,at+sizeof(marker)-1,line);
}
// A ready image: cloud-init's mounts module writes the line and mounts it (cloudinit/config/cc_mounts.py: a device
// path is kept when it is a block device; LABEL= is not accepted there, the udev path is).
inline void ImageDataLines(install::Text& y) {
    if(!HasData())return;
    y.Line("mounts:");
    y.Line("  - [/dev/disk/by-label/DATA, /media/data, exfat, \"defaults,nofail,uid=1000,gid=1000,umask=022\", \"0\", \"0\"]");
}
// The CIDATA seed partition of a ready image written earlier in the plan (its first start comes at the end).
inline efi_handle SeedVolumeOf(int system) {
    auto& s=S();char section[40];plan::SystemSection(s.plan.systems[system].id,section,sizeof(section));
    uint64_t lba=0;if(!plan::ParseNumber(Answers().GetOr(section,"seed_lba",""),lba) || !lba)return nullptr;
    disks::Disk* d=DiskOfPlan();if(!d)return nullptr;
    for(size_t i=0;i<d->partCount;++i) {const auto& p=d->parts[i];
        if(p.first==lba && p.name[0]==u'c' && p.name[1]==u'i' && p.name[2]==u'd')return images::VolumeOfPartition(p.unique);}
    return nullptr;
}
inline bool LabelIs(efi_handle volume,const char16_t* label) {static char16_t now[64];now[0]=0;VolumeLabel(volume,now,64);return SameFold(now,label);}
// While Ubuntu's installer runs, the plan's EFI partition carries the label CIDATA (as in the single installation);
// an image's seed with the same label could be read by cloud-init in the live system instead, which would start an
// installation without answers. The seeds are named CHSEED for that step. False: a seed could not be renamed.
inline bool HideSeeds() {
    auto& s=S();
    for(int i=0;i<s.plan.count;++i)if(plan::IsImage(s.plan.systems[i].kind)) {
        const efi_handle v=SeedVolumeOf(i);if(!v || !LabelIs(v,u"CIDATA"))continue;
        if(install::SetVolumeLabel(v,u"CHSEED")!=EFI_SUCCESS)return false;
        char section[40];plan::SystemSection(s.plan.systems[i].id,section,sizeof(section));Answers().Put(section,"seed_hidden","yes");
        LogLine("partycja startowa obrazu ma na czas instalatora Ubuntu etykietę CHSEED","an image's seed partition is labelled CHSEED while Ubuntu's installer runs");
    }
    return true;
}
// Runners().beforeFirstStart: an image's seed is CIDATA again, and so is not the plan's EFI partition, whatever
// happened in between (an installer that stopped before it gave the label back).
inline void SeedBack(int system) {
    auto& s=S();
    if(s.bound.volume && LabelIs(s.bound.volume,u"CIDATA") && install::SetVolumeLabel(s.bound.volume,plan::HasCharonDos(s.plan)?u"CHARONDOS":u"CHARON")==EFI_SUCCESS)
        LogLine("partycja EFI planu ma znowu swoją etykietę","the plan's EFI partition has its own label again");
    if(system<0 || !plan::IsImage(s.plan.systems[system].kind))return;
    UncoverGrowthRoom(system);   // r410 (agent 1): the image grows into the room the installers saw covered
    const efi_handle v=SeedVolumeOf(system);
    if(v && !LabelIs(v,u"CIDATA")) {
        if(install::SetVolumeLabel(v,u"CIDATA")==EFI_SUCCESS)LogLine("partycja startowa obrazu ma znowu etykietę CIDATA","an image's seed partition is labelled CIDATA again");
        else LogLine("nie udało się przywrócić etykiety CIDATA partycji startowej obrazu; jego pierwsze uruchomienie nie znajdzie konfiguracji","the label CIDATA of an image's seed could not be restored; its first start will not find its configuration");
    }
}
// Ubuntu's installer (autoinstall). Its range is made the ONE free space of the disk (layout direct, mode use_gap);
// subiquity reuses the plan's EFI partition (common/storage/boot.py: an existing ESP is mounted, not made).
// The answers are written to the plan's EFI partition, which lends its label CIDATA, as RunUbuntu does with its work
// volume; the last late-command reports the step to Charon. It does not return when the installer starts.
inline bool InstallUbuntu(int system,int step,char* reason,size_t capacity) {
    plan::Out why(reason,capacity);auto& s=S();auto& ready=Ready();
    held::DropAll();
    if(!Reload(reason,capacity))return false;
    if(ready.disk->hasCharon && !hakefi::selfram::Active()) {
        if(!images::EnsureInMemory()) {why.S("Charon could not be copied into memory");return false;}
        if(!Reload(reason,capacity))return false;
    }
    const plan::System& sys=s.plan.systems[system];
    const int range=plan::RangeOfSystem(ready.layout,system);if(range<0) {why.S("the range of this system is not in the layout");return false;}
    const disks::Disk& d=*ready.disk;auto& all=disks::D();
    size_t index=SIZE_MAX;
    for(size_t i=0;i<all.count;++i) {
        if(&all.list[i]==&d)index=i;
        else if(!all.list[i].usb && !all.list[i].removable && disks::Bytes(all.list[i])>=disks::Bytes(d)) {why.S("Ubuntu's installer can only be pointed at the largest disk, and the plan's disk is not the largest");return false;}
    }
    if(index==SIZE_MAX) {why.S("the disk of the plan was not found");return false;}
    if(!Restore(sys.id)) {why.S("the answers of this system could not be read back");return false;}
    StepDetails(sys);
    if(!ReserveAgain(d,ready.layout)) {why.S("the range of a system that was not installed could not be reserved again");return false;}
    if(!FreeRange(d,ready.layout.ranges[range])) {why.S("the reserved range of this system is not on the disk as the plan left it");return false;}
    if(!CoverGrowthRoom(d,ready.layout,system)) {why.S("the free room of an image could not be covered for the installer");return false;}
    {seeddisk::Disk sd=SeedDisk(d);seeddisk::Gap gaps[4];size_t count=0;
     if(seeddisk::FreeGaps(sd,images::SeedWork(),seeddisk::kWorkBytes,0,uint64_t(16)<<20,gaps,4,count)!=seeddisk::Error::None || count!=1
        || gaps[0].firstLba!=ready.layout.ranges[range].firstLba || gaps[0].lbaCount>ready.layout.ranges[range].lbaCount) {why.S("the disk does not have exactly one free range for Ubuntu's installer");return false;}}
    images::Reconnect(d.handle);
    s.bound.volume=images::VolumeOfPartition(s.esp);s.storage=Bind(s.bound);
    const efi_handle volume=s.bound.volume;
    if(!volume) {why.S("the firmware does not see the plan's EFI partition");return false;}
    ask::Put("diskmode","free");ask::Put("plan","yes");
    install::Chosen().disk=index;install::Chosen().part=SIZE_MAX;
    install::CarryHome()=false;   // a plan's files are live on the EFI partition; nothing is carried over them
    char section[40];plan::SystemSection(sys.id,section,sizeof(section));
    static char image[220];plan::Copy(image,sizeof(image),Answers().GetOr(section,"iso_url",""));
    // The kernel and the initrd: from the EFI partition when the plan fetched them first, otherwise now.
    static Memory kernel,initrd;static char16_t path[pathUnits];
    const bool local=plan::Eq(Answers().GetOr(section,"placed",""),"yes") && InstallerFilesThere(step);
    if(local) {
        LogLine("pliki instalatora Ubuntu leżą na partycji EFI od pobierania z góry; ten krok pobiera tylko to, co instalator pobiera sam","the Ubuntu installer's files are on the EFI partition since the up-front download; this step fetches only what the installer fetches itself");
        charontrace::Text("CHARON plan: Ubuntu installer from local files, no download");
        Say(T(u"Pliki instalatora Ubuntu leżą na dysku od początku planu; wczytuję je do pamięci.",u"The Ubuntu installer's files have been on the disk since the start of the plan; loading them into memory."));
        InstallerPath(step,"linux",path,pathUnits);
        if(!FileToMemory(volume,path,kernel,0)) {why.S("the installer's kernel could not be read from the EFI partition");return false;}
        InstallerPath(step,"initrd.gz",path,pathUnits);
        if(!FileToMemory(volume,path,initrd,0)) {Free(kernel);why.S("the installer could not be read from the EFI partition");return false;}
    } else {
        if(!install::EnsureNetwork()) {why.S("there is no network connection");return false;}
        LogLine("ten krok pobiera pliki instalatora Ubuntu teraz","this step downloads the Ubuntu installer's files now");
        charontrace::Text("CHARON plan: Ubuntu installer is downloaded now");
        static char url[300];static char16_t wide[300],wide2[300];   // r410 (agent 3): kernel and initrd at once
        UbuntuUrl(false,url,sizeof(url));Widen(url,wide,300);
        UbuntuUrl(true,url,sizeof(url));Widen(url,wide2,300);
        if(!install::FetchPairToMemory(wide,kernel,0,T(u"jądro instalatora Ubuntu",u"the Ubuntu installer's kernel"),wide2,initrd,0,T(u"instalator Ubuntu",u"the Ubuntu installer"))) {why.S("Ubuntu's installer could not be downloaded or started");return false;}
    }
    if(!image[0] && !(install::EnsureNetwork() && install::UbuntuImageAddress(image,sizeof(image)))) {Free(kernel);Free(initrd);why.S("the address of the Ubuntu system image that matches the installer could not be found");return false;}
    // The answers: the single installation's generator; with the answer "plan" it adds the data partition's line and
    // the plan-done report itself (r410 kd, CharonWsInstall2.h UbuntuUserData).
    static char yaml[32000];install::Text text{yaml,sizeof(yaml),0,false};yaml[0]=0;
    install::UbuntuUserData(text,true,HasData()?DataFstab():nullptr);
    if(text.overflow || FindText(text,"install-charon.sh plan-done")==SIZE_MAX) {for(auto& c:yaml)c=0;Free(kernel);Free(initrd);install::AnswersDoNotFit();why.S("the answers do not fit in the installer's answer file");return false;}
    static char16_t oldLabel[64];VolumeLabel(volume,oldLabel,64);static uint8_t labelUtf8[64];const size_t labelBytes=Utf16ToUtf8(oldLabel,Len(oldLabel),labelUtf8,60);
    MakeFolders(volume,u"\\charon-install\\ubuntu");
    bool ok=install::WriteText(volume,u"\\user-data",yaml,text.size) && install::WriteText(volume,u"\\meta-data","instance-id: charon-plan\n",25)
        && install::WriteText(volume,u"\\charon-install\\ubuntu\\install-charon.sh",install::charonScript,sizeof(install::charonScript)-1)
        && install::WriteText(volume,u"\\charon-install\\ubuntu\\speech.sh",install::speechScript,sizeof(install::speechScript)-1)
        && install::WriteText(volume,u"\\charon-install\\ubuntu\\live-speech.sh",install::liveSpeechScript,sizeof(install::liveSpeechScript)-1)
        && install::WriteText(volume,u"\\charon-install\\ubuntu\\progress.sh",install::progressScript,sizeof(install::progressScript)-1)
        && install::WriteText(volume,u"\\charon-install\\ubuntu\\charon-tone",reinterpret_cast<const char*>(charonToneBinary),sizeof(charonToneBinary))
        && [&]() {static uint8_t wav[16384];const size_t n=install::GoodbyeWav(wav,sizeof(wav));return n && install::WriteText(volume,u"\\charon-install\\ubuntu\\goodbye.wav",reinterpret_cast<const char*>(wav),n);}()
        && install::WriteText(volume,u"\\charon-install\\ubuntu\\label.txt",reinterpret_cast<const char*>(labelUtf8),labelBytes);
    for(auto& c:yaml)c=0;
    if(ok && detailed::Carried()) {static char env[16384];const size_t n=detailed::Env(env,sizeof(env));
        ok=n && install::WriteText(volume,u"\\charon-install\\ubuntu\\answers-env.sh",env,n) && install::WriteText(volume,u"\\charon-install\\ubuntu\\personal.sh",install::personalScript,sizeof(install::personalScript)-1);
        for(auto& c:env)c=0;}
    if(ok)install::WriteVoiceConf(volume,u"\\charon-install\\ubuntu\\voice.conf");
    if(ok) {static char conf[400];const size_t bytes=install::NetworkConfNow(conf,sizeof(conf));
        if(bytes && install::WriteText(volume,u"\\charon-install\\ubuntu\\network.sh",install::networkScript,sizeof(install::networkScript)-1))
            install::WriteText(volume,u"\\charon-install\\ubuntu\\network.conf",conf,bytes);
        for(auto& c:conf)c=0;}
    if(!ok) {Free(kernel);Free(initrd);why.S("the answer files could not be written");return false;}
    if(!HideSeeds()) {Free(kernel);Free(initrd);why.S("the seed partition of an image could not be renamed for Ubuntu's installer");return false;}
    if(install::SetVolumeLabel(volume,u"CIDATA")!=EFI_SUCCESS) {Free(kernel);Free(initrd);SeedBack(-1);why.S("the plan's EFI partition could not be given the label CIDATA");return false;}
    if(!SaveNow()) {Free(kernel);Free(initrd);SeedBack(-1);why.S("the plan could not be saved");return false;}
    static char16_t arguments[500];Set(arguments,500,u"ip=dhcp iso-url=");
    {static char16_t wide[220];Widen(image,wide,220);Cat(arguments,500,wide);}
    Cat(arguments,500,u" subiquity-storage-version=2 autoinstall ds=nocloud ---");   // r410 (kd, run kd4): use_gap needs storage version 2 (CharonWsInstall2.h RunUbuntu)
    Say(ask::Is("speech","yes")?T(u"Uruchamiam instalator Ubuntu. Najpierw pobierze on do pamięci obraz systemu, prawie 3 gigabajty; przez te kilka minut będzie cisza. Potem instalator zacznie mówić, a Charon co dwie minuty przypomina, że instalacja trwa. Na końcu komputer sam uruchomi się ponownie.",
                                  u"Starting the Ubuntu installer. First it downloads the system image, almost 3 gigabytes, into memory; expect a few minutes of silence. Then the installer starts to speak, and every two minutes Charon says that the installation is still running. At the end the computer restarts by itself.")
        :T(u"Uruchamiam instalator Ubuntu bez mowy. Najpierw pobierze on obraz systemu, prawie 3 gigabajty. Na końcu komputer sam uruchomi się ponownie.",
           u"Starting the Ubuntu installer without speech. First it downloads the system image, almost 3 gigabytes. At the end the computer restarts by itself."));
    install::HearOut(200);
    net::ForgetJoined();
    install::StartFromMemory(kernel,initrd,u"Ubuntu installer",arguments);
    for(size_t i=0;i<initrd.size;++i)initrd.data[i]=0;
    Free(kernel);Free(initrd);
    SeedBack(-1);   // it did not start: the plan's EFI partition is CHARON again (the seeds get CIDATA before their start)
    why.S("Ubuntu's installer could not be started");
    return false;
}
// Where the installer's files of a Windows version come from, as the single-system dialogue sets it:
// the fixed address of an evaluation image, or Microsoft's download page in Charon's language.
inline const install::WindowsVersion* WindowsSource(const char* id) {
    const install::WindowsVersion* version=nullptr;
    for(const auto& v:install::windowsVersions)if(plan::Eq(id,v.id))version=&v;
    if(!version)return nullptr;
    winusb::Preset()=version->url;winusb::Product()=Pl()?version->pl:version->en;winusb::PresetSha256()=nullptr;install::UnofficialChosen()=false;
    auto& pick=ramdisk::P();pick.windows10=plan::Eq(id,"10");pick.language[0]=0;pick.locale[0]=0;
    return version;
}
// r410 (helper vr, ag6): the user's windows-iso-url.txt is read where Charon runs from; after a restart that is
// the plan's EFI partition, which has no copy of it. The address is recorded in the system's answers while the
// file is readable (the pre-flight, from Charon's own disk) and handed to winusb at every Windows step.
inline void CarryAddress(const char* section) {
    static char url[https::urlUnits];unsigned split=0;
    if(winusb::ReadOverride(url,sizeof(url),split)) {
        Answers().Put(section,"wurl",url);
        char t[12];plan::Out o(t,sizeof(t));o.N(uint64_t(split));Answers().Put(section,"wurl_split",split?t:"0");
    }
    plan::Copy(winusb::CarriedAddress(),https::urlUnits,Answers().GetOr(section,"wurl",""));
    uint64_t mb=0;plan::ParseNumber(Answers().GetOr(section,"wurl_split","0"),mb);winusb::CarriedSplitMb()=mb>=16 && mb<=4000?unsigned(mb):0;
}
// Windows: everything inside its own range (see the head of this file). It does not return when
// Setup starts.
inline bool InstallWindows(int system,int step,char* reason,size_t capacity) {
    plan::Out why(reason,capacity);auto& s=S();auto& ready=Ready();
    if(!Reload(reason,capacity))return false;
    const plan::System& sys=s.plan.systems[system];
    const int rangeIndex=plan::RangeOfSystem(ready.layout,system);if(rangeIndex<0) {why.S("the range of this system is not in the layout");return false;}
    const plan::Range r=ready.layout.ranges[rangeIndex];
    if(!Restore(sys.id)) {why.S("the answers of this system could not be read back");return false;}
    StepDetails(sys);   // r410
    // The password: the step's own file on the EFI partition.
    {char path[160];plan::Out p(path,sizeof(path));plan::StepFilePath(step,"answers.env",p);static char secret[secretBytes+1];size_t n=0;
     const bool have=s.storage.read(s.storage.context,path,secret,secretBytes,&n) && n;
     secret[have?n:0]=0;const bool kept=have && ask::Put("password",secret);for(auto& c:secret)c=0;
     if(!kept) {why.S("the password for Windows is no longer on the EFI partition");ask::Clear();return false;}}
    const install::WindowsVersion* version=WindowsSource(ask::Get("wversion"));
    {char section[40];plan::SystemSection(sys.id,section,sizeof(section));CarryAddress(section);}
    if(plan::CountWindows(s.plan)!=1)winusb::AddressReady()=false;   // an address settled in the pre-flight belongs to one Windows only
    if(!version) {why.S("the Windows version of the plan is not known to this version of Charon");ask::Clear();return false;}
    // The table of the disk Charon may be running from changes: Charon goes into memory first.
    if(ready.disk->hasCharon && !hakefi::selfram::Active()) {
        if(!images::EnsureInMemory()) {why.S("Charon could not be copied into memory");ask::Clear();return false;}
        if(!Reload(reason,capacity)) {ask::Clear();return false;}
    }
    const disks::Disk& d=*ready.disk;
    // Its range: everything in it goes - the placeholder, or what a failed attempt left - except a
    // complete media partition of an earlier attempt, which is used again without a download.
    // Nothing outside the range is touched; a partition that lies only partly inside is not ours.
    uint64_t mediaFirst=0;
    {seeddisk::Disk sd=SeedDisk(d);static seeddisk::Partition parts[128];seeddisk::Info info;
     if(seeddisk::Inspect(sd,images::SeedWork(),seeddisk::kWorkBytes,parts,128,info)!=seeddisk::Error::None) {why.S("the partition table could not be read");ask::Clear();return false;}
     const uint64_t first=r.firstLba,last=r.firstLba+r.lbaCount-1;
     for(size_t i=0;i<info.partitionCount;++i) {
        const bool inside=parts[i].firstLba>=first && parts[i].lastLba<=last,apart=parts[i].lastLba<first || parts[i].firstLba>last;
        if(!inside && !apart) {why.S("the reserved range of this system is not on the disk as the plan left it");ask::Clear();return false;}
     }
     for(size_t i=0;i<info.partitionCount;++i) {
        if(parts[i].firstLba<first || parts[i].lastLba>last)continue;
        if(!mediaFirst && parts[i].firstLba>first && SameFold(parts[i].name,u"Charon Windows media")) {
            const efi_handle volume=images::VolumeOfPartition(parts[i].unique.bytes);
            if(volume && FileOn(volume,"\\charon\\Charon-MediaCleanup.ps1") && install::HasWindowsImage(volume)) {mediaFirst=parts[i].firstLba;continue;}
        }
        if(seeddisk::RemovePartition(sd,images::SeedWork(),seeddisk::kWorkBytes,parts[i].unique,parts[i].type,parts[i].firstLba)!=seeddisk::Error::None) {
            why.S("a partition inside the range of this system could not be removed");ask::Clear();return false;}
     }
     if(mediaFirst) {
        seeddisk::Guid unique;images::RandomGuid(unique,320);seeddisk::AddResult added{};
        seeddisk::AddRequest request{};request.type=seeddisk::kTypeBasicData;request.unique=unique;request.name=u"Basic data partition";
        request.sizeBytes=(mediaFirst-first)*d.blockSize;request.alignmentBytes=0;request.placement=seeddisk::Placement::At;request.firstLba=first;request.attributes=0;
        if(seeddisk::AddPartition(sd,images::SeedWork(),seeddisk::kWorkBytes,request,added)!=seeddisk::Error::None) {why.S("the Windows partition could not be made in its range");ask::Clear();return false;}
        dossys::Zero(d.io,first,2048);
     }
     CoverFreeForWindows(sd,first,last);
     if(d.io->FlushBlocks)reinterpret_cast<images::BlockFlush>(d.io->FlushBlocks)(d.io);}
    images::Reconnect(d.handle);
    s.bound.volume=images::VolumeOfPartition(s.esp);s.storage=Bind(s.bound);
    // r410: the media is complete when the plan fetched everything first (PlaceWindows); only otherwise is the
    // network needed here, and the log says that this step downloads.
    if(mediaFirst) {LogLine("nośnik instalatora Windows jest gotowy od początku planu; ten krok niczego nie pobiera","the Windows installer's media is ready since the start of the plan; this step downloads nothing");
        charontrace::Text("CHARON plan: Windows media ready, no download");}
    else {LogLine("ten krok pobiera obraz Windows teraz","this step downloads the Windows image now");charontrace::Text("CHARON plan: Windows image is downloaded now");
        if(!install::EnsureNetwork()) {why.S("there is no network connection");ask::Clear();return false;}}
    if(!Reload(reason,capacity)) {ask::Clear();return false;}
    const disks::Disk& now=*ready.disk;const size_t index=size_t(&now-disks::D().list);
    install::Chosen().disk=index;install::Chosen().part=SIZE_MAX;
    if(mediaFirst)for(size_t i=0;i<now.partCount;++i)if(now.parts[i].first==r.firstLba)install::Chosen().part=i;
    if(mediaFirst && install::Chosen().part==SIZE_MAX) {why.S("the firmware does not see the Windows partition");ask::Clear();return false;}
    // The answers the generator and RunWindows read for a step of a plan.
    {char digits[8];plan::Out out(digits,sizeof(digits));out.N(uint64_t(step));ask::Put("planstep",digits);}
    ask::Put("plan","yes");ask::Put("diskmode","partition");ask::Put("clockutc","yes");
    ask::Put("plantwo",plan::CountWindows(s.plan)>=2?"yes":"no");ask::Put("planname",sys.name);
    // r410 (T7, agent 1): Windows Setup makes the Windows it installs the default of the shared boot store, so the
    // menu of Windows started "Windows 11 (2)" although the plan's default was "Windows 11". The LAST Windows of the
    // plan sets the default of Windows' menu at its first logon: the plan's default system when it is a Windows,
    // otherwise the first Windows of the plan (CharonWsPlanWindows.h FirstLogon, menuDefault).
    {ask::Put("planwindefault","");const int self=int(&sys-s.plan.systems);bool last=true;int first=-1,chosen=-1;
     for(int k=0;k<s.plan.count;++k)if(plan::IsWindows(s.plan.systems[k].kind)) {if(first<0)first=k;if(k>self)last=false;
         if(s.plan.defaultId[0] && plan::Eq(s.plan.systems[k].id,s.plan.defaultId))chosen=k;}
     if(chosen<0)chosen=first;
     if(last && plan::CountWindows(s.plan)>=2 && chosen>=0)ask::Put("planwindefault",chosen==self?"{current}":s.plan.systems[chosen].name);}
    if(!ask::Complete()) {why.S("the answers of this step could not be kept");ask::Clear();return false;}
    winpart::InPlan()=winpart::PlanRange{true,r.firstLba,r.lbaCount};
    winpart::Deferred()=true;
    if(!SaveNow()) {winpart::InPlan().on=false;winpart::Deferred()=false;ask::Clear();why.S("the plan could not be saved");return false;}
    const bool started=install::RunWindows(false);   // on success Setup has the machine and this does not come back
    winpart::InPlan().on=false;winpart::Deferred()=false;ask::Clear();
    // The media was made on this disk, so the volumes have new handles.
    s.bound.volume=images::VolumeOfPartition(s.esp);s.storage=Bind(s.bound);
    if(started)return true;
    why.S("Windows Setup could not be prepared or started; the reason was said");
    return false;
}
// After the Windows step is done: the media partition must be gone (the answer file with the
// password lay on it). Windows removes it at the first sign-in; when it is still there, Charon
// overwrites the answer file and removes the partition itself. A free range of 16 MiB or more
// left inside Windows' range is covered by a placeholder, so that an installer that takes "the
// biggest free space" later in the plan cannot land in it.
// r410 (helper h8, RUN G): the space the plan leaves free (outside every system's range) was taken by Windows Setup
// of the second Windows for its recovery partition (1006 MB at the end of the disk, while the summary said "Wolne
// zostaje 1006 megabajtów"). While a Windows installs, every free range of 16 MiB or more outside its own range is
// covered by a placeholder "Charon plan: kept free"; the placeholders are removed when Windows is back (below) and
// at the plan's finish, so the space is free again as the summary said.
constexpr const char16_t* kKeptFree=u"Charon plan: kept free";
inline void CoverFreeForWindows(seeddisk::Disk& sd,uint64_t first,uint64_t last) {
    seeddisk::Gap gaps[8];size_t count=0;
    if(seeddisk::FreeGaps(sd,images::SeedWork(),seeddisk::kWorkBytes,0,uint64_t(16)<<20,gaps,8,count)!=seeddisk::Error::None)return;
    for(size_t i=0;i<count;++i) {
        if(gaps[i].firstLba+gaps[i].lbaCount-1>=first && gaps[i].firstLba<=last)continue;   // inside (or touching) Windows' own range
        seeddisk::Guid unique;images::RandomGuid(unique,350+uint64_t(i));seeddisk::AddResult added{};
        if(seeddisk::ReservePlaceholder(sd,images::SeedWork(),seeddisk::kWorkBytes,gaps[i].firstLba,gaps[i].lbaCount,unique,kKeptFree,added)==seeddisk::Error::None)
            LogLine("wolne miejsce poza zakresem Windows przykryte na czas instalacji Windows","free space outside the range of Windows covered while Windows installs");
    }
}
// True when something was removed (the caller flushes and reconnects).
inline bool UncoverFree(seeddisk::Disk& sd) {
    static seeddisk::Partition parts[128];seeddisk::Info info;bool changed=false;
    if(seeddisk::Inspect(sd,images::SeedWork(),seeddisk::kWorkBytes,parts,128,info)!=seeddisk::Error::None)return false;
    for(size_t i=0;i<info.partitionCount;++i)
        if(SameFold(parts[i].name,kKeptFree) && seeddisk::RemovePartition(sd,images::SeedWork(),seeddisk::kWorkBytes,parts[i].unique,parts[i].type,parts[i].firstLba)==seeddisk::Error::None)changed=true;
    return changed;
}
inline void CleanAfterWindows(int system) {
    auto& s=S();auto& ready=Ready();char reason[120]="";
    if(!Reload(reason,sizeof(reason)))return;
    const int rangeIndex=plan::RangeOfSystem(ready.layout,system);if(rangeIndex<0)return;
    const plan::Range r=ready.layout.ranges[rangeIndex];const disks::Disk& d=*ready.disk;
    seeddisk::Disk sd=SeedDisk(d);static seeddisk::Partition parts[128];seeddisk::Info info;bool changed=false;
    if(seeddisk::Inspect(sd,images::SeedWork(),seeddisk::kWorkBytes,parts,128,info)!=seeddisk::Error::None)return;
    const uint64_t first=r.firstLba,last=r.firstLba+r.lbaCount-1;
    for(size_t i=0;i<info.partitionCount;++i) {
        if(parts[i].firstLba<first || parts[i].lastLba>last || !SameFold(parts[i].name,u"Charon Windows media"))continue;
        LogLine("partycja nośnika Windows została po kroku; Charon ją usuwa","the Windows media partition was still there after the step; Charon removes it");
        if(const efi_handle volume=images::VolumeOfPartition(parts[i].unique.bytes)) {
            Bound b{volume};static char zero[65536];
            static const char* const files[]={"\\Autounattend.xml","\\sources\\Autounattend.xml"};
            for(const char* path:files) {
                size_t length=0;if(!StoreExists(&b,path))continue;
                if(StoreRead(&b,path,zero,sizeof(zero),&length)) {memset(zero,0,length);StoreWrite(&b,path,zero,length);}
                StoreRemove(&b,path);
            }
        }
        if(seeddisk::RemovePartition(sd,images::SeedWork(),seeddisk::kWorkBytes,parts[i].unique,parts[i].type,parts[i].firstLba)==seeddisk::Error::None)changed=true;
        else LogLine("nie udało się usunąć partycji nośnika Windows","the Windows media partition could not be removed");
    }
    if(UncoverFree(sd)) {changed=true;LogLine("wolne miejsce planu znów jest wolne","the plan's free space is free again");}   // r410 (h8, RUN G)
    seeddisk::Gap gaps[8];size_t count=0;
    if(seeddisk::FreeGaps(sd,images::SeedWork(),seeddisk::kWorkBytes,0,uint64_t(16)<<20,gaps,8,count)==seeddisk::Error::None)
        for(size_t i=0;i<count;++i) {
            if(gaps[i].firstLba<first || gaps[i].firstLba+gaps[i].lbaCount-1>last)continue;
            seeddisk::Guid unique;images::RandomGuid(unique,330+uint64_t(i));seeddisk::AddResult added{};
            if(seeddisk::ReservePlaceholder(sd,images::SeedWork(),seeddisk::kWorkBytes,gaps[i].firstLba,gaps[i].lbaCount,unique,u"Charon plan: free after Windows",added)==seeddisk::Error::None) {
                changed=true;LogLine("wolne miejsce w zakresie Windows zostało przykryte partycją zastępczą; program do partycji może je oddać Windows","free space left inside the range of Windows was covered by a placeholder; a partition tool can give it to Windows");}
        }
    if(!changed)return;
    if(d.io->FlushBlocks)reinterpret_cast<images::BlockFlush>(d.io->FlushBlocks)(d.io);
    images::Reconnect(d.handle);
    s.bound.volume=images::VolumeOfPartition(s.esp);s.storage=Bind(s.bound);
}
// ---- r410: a plan downloads EVERYTHING FIRST ---------------------------------------------------------
// The rules are pure (CharonPlanFetch.h); the design is work/r410a-evidence/ef/DESIGN.txt. In short:
//   MakeFetchList   before the summary: every file Charon itself fetches, with its size (one HEAD request each),
//                   and where it waits - in memory, or (said by name and size) fetched only after the disk is
//                   prepared. Holding a file on Charon's own medium is not built: the room given is memory only.
//   FetchUpfront    after the typed word, BEFORE the disk is touched: the files into memory, one after another
//                   (the HTTP client is a blocking call), with one progress over all of them. A failure leaves
//                   the disk unchanged: the caller releases the memory and the plan does not start.
//   PlaceAll        right after step 0, in the same start of Charon: Debian's kernel and initrd onto the plan's EFI
//                   partition, every Windows' installer media made and filled. Ready images are written by their
//                   own steps, which run in this same start and take the bytes from memory (ws::held).
struct Wanted {plan::FetchList list;char urls[plan::kMaxFetch][https::urlUnits];bool ready;};
inline Wanted& Fetches() {static Wanted wanted;return wanted;}
// A line about the up-front phase where it can be read afterwards although no plan file exists yet: the serial
// trace, and \EFI\HAK\plan-upfront.log on Charon's own medium when that can be written.
inline void UpfrontNote(const char* text) {
    charontrace::Text("CHARON upfront:");charontrace::Text(text);
    const efi_handle volume=U().host->selfDevice;
    if(volume && !VolumeReadOnly(volume))install::WriteText(volume,u"\\EFI\\HAK\\plan-upfront.log",text,plan::Len(text));
}
inline bool SizeOf(const char* url,uint64_t& bytes) {
    static https::Client client;static https::Response probe;client.transport=net::tcp::Transport();
    const auto result=https::Probe(client,url,probe);
    if(result!=https::Result::ok) {net::HttpsFail(client,result);return false;}
    bytes=probe.totalLength!=https::unknownLength?probe.totalLength:probe.contentLength!=https::unknownLength?probe.contentLength:0;
    return true;
}
inline bool ListHas(const char* list,const char* value) {
    const size_t n=plan::Len(value);
    for(const char* p=list;*p;) {
        const char* e=p;while(*e && *e!=',')++e;
        if(size_t(e-p)==n) {bool same=true;for(size_t i=0;i<n;++i)if(p[i]!=value[i])same=false;if(same)return true;}
        p=*e?e+1:e;
    }
    return false;
}
// False: an address does not answer (said); the plan does not start.
constexpr const char* kHvPlaceholder="charon-hypervisor:memory";   // r410 (h8): the name of the hypervisor's reserved memory
inline bool MakeFetchList(bool network) {
    auto& w=Fetches();auto& s=S();memset(&w.list,0,sizeof(w.list));w.ready=false;
    if(!network)return true;   // without a network nothing is promised; the steps say what they miss
    Say(T(u"Sprawdzam adresy i rozmiary plików, które pobiorę.",u"Checking the addresses and sizes of the files I will download."));
    auto add=[&w](plan::FetchWhat what,int system,const char* url,bool probe)->bool {
        const int index=w.list.count;uint64_t bytes=0;
        if(index>=plan::kMaxFetch)return false;
        w.urls[index][0]=0;
        if(url && !plan::Copy(w.urls[index],https::urlUnits,url))return false;
        if(url && probe && !SizeOf(url,bytes))return false;
        return plan::AddFetch(w.list,what,system,bytes,bytes!=0);
    };
    for(int i=0;i<s.plan.count;++i) {
        const plan::System& sys=s.plan.systems[i];char section[40];plan::SystemSection(sys.id,section,sizeof(section));bool ok=true;
        if(sys.kind==plan::Kind::debian) {
            const bool speech=plan::Eq(Answers().GetOr(section,"speech",""),"yes");static char url[400];
            DebianUrl(false,speech,url,sizeof(url));ok=add(plan::FetchWhat::kernel,i,url,true);
            DebianUrl(true,speech,url,sizeof(url));ok=ok && add(plan::FetchWhat::initrd,i,url,true);
        } else if(sys.kind==plan::Kind::ubuntu) {   // r410 (bd): kernel and initrd up front; the system image's address is
            static char url[400],image[220];         // settled now (no firmware network is needed at the step) and kept
            UbuntuUrl(false,url,sizeof(url));ok=add(plan::FetchWhat::kernel,i,url,true);
            UbuntuUrl(true,url,sizeof(url));ok=ok && add(plan::FetchWhat::initrd,i,url,true);
            ok=ok && install::UbuntuImageAddress(image,sizeof(image)) && Answers().Put(section,"iso_url",image);
        } else if(plan::IsImage(sys.kind)) {
            const Offer* o=nullptr;size_t n=0;const Offer* all=Offers(n);for(size_t k=0;k<n;++k)if(all[k].kind==sys.kind)o=&all[k];
            ok=o && add(plan::FetchWhat::image,i,o->entry->url,true);
        } else if(plan::IsWindows(sys.kind)) {
            // The same resolver the step uses. With two Windows in a plan the address is settled again at each one's
            // turn, so their images are not promised up front (said: fetched after the disk is prepared).
            const bool one=plan::CountWindows(s.plan)==1;
            CarryAddress(section);
            ok=WindowsSource(sys.version) && winusb::ResolveAddress(Pl()?"pl-PL":"en-US");
            if(ok)ok=one?add(plan::FetchWhat::windows,i,winusb::I().url,true):AddressAnswers(winusb::I().url) && add(plan::FetchWhat::windows,i,nullptr,false);
            if(ok && ListHas(Answers().GetOr(section,"wreaders",""),"nvda")) {
                static char nvda[400];size_t k=0;for(;install::nvdaUrl[k] && k+1<sizeof(nvda);++k)nvda[k]=char(install::nvdaUrl[k]);
                nvda[k]=0;
                // r410 (helper bw, T7): two Windows asked for the same installer; the second item took the first one's
                // reservation (held::Reserved matches by address), so the first Windows dropped the only complete copy
                // and the second Windows' step downloaded NVDA again. One item serves every Windows (PlaceWindows).
                bool already=false;for(int e=0;e<w.list.count;++e)already=already || (w.list.items[e].what==plan::FetchWhat::reader && plan::Eq(w.urls[e],nvda));
                if(!already)ok=add(plan::FetchWhat::reader,i,nvda,true);
            }
        } else if(plan::IsHypervisor(sys.kind)) {   // r410: agent 2 fetches and checks its own files (Alpine, QEMU)
#if __has_include("CharonWsHypervisor.h")
            ok=plan::AddFetch(w.list,plan::FetchWhat::hypervisor,i,hv::PlanBytes(),hv::PlanBytes()!=0);w.urls[w.list.count-1][0]=0;
#endif
        } else if(sys.kind==plan::Kind::charondos) {
            uint64_t programs=0;plan::ParseNumber(Answers().GetOr(section,"programs","0"),programs);
            size_t n=0;const ask::Choice* groups=dosapps::Groups(n);
            if(programs<n && !ask::Same(groups[programs].value,"none"))ok=add(plan::FetchWhat::dosPrograms,i,nullptr,false);
        }
        if(!ok) {
            Say(T(u"Adres pobierania nie odpowiada, więc plan nie ruszy. Na dysku nic się nie zmieniło.",u"The download address does not answer, so the plan does not start. Nothing on the disk has changed."));install::HearOut(300);
            return false;}
    }
    // r410 (VMware, 16:30): holding a file of 3 GB or 8 GB in memory FAILED although the memory map's total said it
    // fits (AllocatePages of the 64 MiB pieces; 6, 8 and 12 GB machines), and the plan then stopped. Proven so far:
    // about 1.1 GB held. Session 3 capped the promise at 1792 MiB of free memory; session 4 replaces the cap by a real
    // reservation (below). A file that cannot be reserved takes the announced path "fetched after the disk is
    // prepared", which has run in machines for the Windows image and is the r409 path for ready images.
    // r410 (block D, session 4): the promise is no longer a guess from the memory map. Every file that the map says
    // fits is given its memory NOW, before the summary (held::Make, which also asks for memory above 4 GiB by
    // address); a file whose memory cannot be had is moved to "fetched after the disk is prepared" and the summary
    // says so with its size. The memory stays reserved until the download fills it or the dialogue ends.
    held::DropAll();
    const plan::FetchRoom room{ramdisk::FreeMemory(),false,0};   // memory only: see the head of this section
    plan::AssignHolds(w.list,room);
    bool tried[plan::kMaxFetch]={};
    // r410 (helper h8, run 3: Debian image + hypervisor in 8 GB): the hypervisor's files are held by hv::FetchUpfront
    // itself, so nothing kept memory for them and after the 3 GB image no 64 MB piece was left ("not enough RAM for a
    // 64 MB piece", after the image's 8 minutes). Its memory is now taken FIRST, as a placeholder in 64 MB pieces; a
    // file that then does not fit goes "after the disk is prepared" as usual; the placeholder is given back right
    // before hv::FetchUpfront asks for the same amount.
    for(int k=0;k<w.list.count;++k)if(w.list.items[k].hold==plan::Hold::ram && w.list.items[k].what==plan::FetchWhat::hypervisor)
        charontrace::Value(held::Make(kHvPlaceholder,w.list.items[k].bytes)?"CHARON upfront: memory reserved for the hypervisor, MiB":"CHARON upfront: memory for the hypervisor could not be reserved, MiB",w.list.items[k].bytes>>20);
    for(int round=0;round<w.list.count;++round) {   // smallest first, as AssignHolds: the one that misses is the largest
        int i=-1;
        for(int k=0;k<w.list.count;++k)if(!tried[k] && w.list.items[k].hold==plan::Hold::ram && w.list.items[k].what!=plan::FetchWhat::hypervisor && (i<0 || w.list.items[k].bytes<w.list.items[i].bytes))i=k;
        if(i<0)break;
        tried[i]=true;plan::FetchItem& item=w.list.items[i];
        if(!held::Make(w.urls[i],item.bytes)) {
            item.hold=plan::Hold::later;
            charontrace::Value("CHARON upfront: memory could not be reserved, file goes later, MiB",item.bytes>>20);
        } else charontrace::Value("CHARON upfront: memory reserved, MiB",item.bytes>>20);
    }
    charontrace::Value("CHARON upfront: free memory, MiB",room.freeRam>>20);
    charontrace::Value("CHARON upfront: address probes tried again",uint64_t(https::ProbeRetries()));   // r410 (helper pw)
    charontrace::Value("CHARON upfront: files in memory",uint64_t(plan::FetchCount(w.list,plan::Hold::ram)));
    charontrace::Value("CHARON upfront: files after the disk is prepared",uint64_t(plan::FetchCount(w.list,plan::Hold::later)));
    w.ready=true;return true;
}
inline const char16_t* FetchLabel() {return T(u"Pobieranie wszystkiego z góry",u"Downloading everything first");}
// r410 (agent 3, user 2026-11-26: "several at once"): the files held in memory are fetched in ONE batch of
// net::many::Download (CharonWsDownloads.h): up to 4 kept-alive connections at once, every file straight into the
// memory reserved for it before the summary (held::Reserved), NVDA checked against Charon's SHA-256 as soon as it is
// complete. Spoken: what is fetched (one file: its line as before; several: how many at once), then each file's name
// as it completes ("Downloaded: X, and 2 more." when names come faster than speech), one combined progress for all.
// Charon Hypervisor's own files follow in hv::FetchUpfront (its packages are a batch of their own).
struct UpfrontRun {int index[plan::kMaxFetch];char16_t name[plan::kMaxFetch][160];uint64_t all,before;bool sizeWrong,notExpected;};
inline UpfrontRun& Upfront() {static UpfrontRun run;return run;}
inline bool UpfrontBegin(net::many::Item& it,uint64_t total) {
    const int i=Upfront().index[reinterpret_cast<uintptr_t>(it.user)];auto& w=Fetches();
    if(total!=w.list.items[i].bytes) {Upfront().sizeWrong=true;return false;}
    held::File* file=held::Reserved(w.urls[i],total);   // r410: reserved before the summary
    if(!file)file=held::Make(w.urls[i],total);
    if(!file) {it.noMemory=true;return false;}
    return true;
}
inline bool UpfrontPut(net::many::Item& it,uint64_t offset,const uint8_t* data,size_t size) {
    const int i=Upfront().index[reinterpret_cast<uintptr_t>(it.user)];
    held::File* file=held::Reserved(Fetches().urls[i],Fetches().list.items[i].bytes);   // (Find gives complete files only)
    return file && held::Put(*file,offset,data,size);
}
inline void UpfrontDrop(net::many::Item&) {}   // the held memory stays reserved; a new attempt writes it again from 0
inline bool UpfrontCheck(net::many::Item& it) {
    const int i=Upfront().index[reinterpret_cast<uintptr_t>(it.user)];auto& w=Fetches();const plan::FetchItem& item=w.list.items[i];
    held::File* file=held::Reserved(w.urls[i],item.bytes);
    if(!file || it.received!=item.bytes) {Upfront().sizeWrong=true;return false;}
    if(item.what==plan::FetchWhat::reader) {   // NVDA: the SHA-256 Charon pins (install::nvdaSha256)
        Sha256 hash;for(size_t k=0;k<file->pieceCount;++k)hash.Update(file->pieces[k],held::PieceSize(*file,k));
        uint8_t got[32];hash.Final(got);static const char hex[]="0123456789abcdef";bool same=true;
        for(int k=0;k<32 && same;++k)same=install::nvdaSha256[k*2]==hex[got[k]>>4] && install::nvdaSha256[k*2+1]==hex[got[k]&15];
        if(!same) {Upfront().notExpected=true;return false;}
    }
    file->complete=true;
    charontrace::Value("CHARON upfront: file complete, bytes",item.bytes);
    return true;
}
inline void UpfrontProgress(void*,uint64_t done,uint64_t) {Progress(FetchLabel(),Upfront().before+done,Upfront().all);}
// Fetches every file whose place is memory. False: `reason` (a text of plan::Reason) and `failedItem` say what
// broke; a network failure was said in full by net::HttpsFail. The caller releases what is held.
inline bool FetchUpfront(char* reason,size_t capacity,int& failedItem) {
    auto& w=Fetches();plan::Out why(reason,capacity);failedItem=-1;
    auto& u=Upfront();u.all=plan::UpfrontBytes(w.list);u.before=0;u.sizeWrong=u.notExpected=false;
    charontrace::Text("CHARON upfront: download begins; the disk has not been touched");
    int count=0,hypervisor=-1;
    for(int i=0;i<w.list.count;++i) {
        const plan::FetchItem& item=w.list.items[i];
        if(item.hold!=plan::Hold::ram)continue;
        if(item.what==plan::FetchWhat::hypervisor) {hypervisor=i;continue;}
        u.index[count++]=i;
    }
    if(count) {
        static net::many::Item items[plan::kMaxFetch];
        for(int k=0;k<count;++k) {
            const int i=u.index[k];const plan::FetchItem& item=w.list.items[i];
            static char line[240];plan::Out o(line,sizeof(line));plan::FetchName(S().plan,item,Pl(),o);Widen(line,u.name[k],160);
            memset(&items[k],0,sizeof(items[k]));
            items[k].url=w.urls[i];items[k].size=item.bytes;items[k].begin=UpfrontBegin;items[k].put=UpfrontPut;items[k].drop=UpfrontDrop;
            items[k].check=UpfrontCheck;items[k].name=u.name[k];items[k].user=reinterpret_cast<void*>(uintptr_t(k));
            charontrace::Text("CHARON upfront: file");charontrace::Text(w.urls[i]);
        }
        if(count==1) {static char line[300];plan::Out o(line,sizeof(line));plan::FetchFileLine(S().plan,w.list,u.index[0],Pl(),o);SayText(line);}
        else {static char line[120];plan::Out o(line,sizeof(line));o.S(Pl()?"Pobieram naraz plików: ":"Downloading files at once: ").N(uint64_t(count));o.S(".");SayText(line);}
        install::HearOut(40);
        net::many::Options o{};o.connections=4;o.perHost=4;
        o.progress=UpfrontProgress;o.prefix=T(u"Pobrano: ",u"Downloaded: ");o.period=true;o.sayRest=hypervisor<0;
        const net::many::Outcome r=net::many::Download(items,size_t(count),o);
        for(int k=0;k<count;++k)u.before+=w.list.items[u.index[k]].bytes;
        if(r.failed) {
            const net::many::Item& f=*r.firstFailure;failedItem=u.index[&f-items];
            if(r.cancelled)why.S("the download before the disk was changed was stopped with Escape");
            else if(f.noMemory)why.S("there is not enough memory for the files of the plan");
            else if(f.checkFailed && u.notExpected)why.S("a downloaded file is not the one that was expected");
            else if(u.sizeWrong || (f.result==https::Result::stopped && !f.writeFailed))why.S("a downloaded file does not have the announced size");
            else {net::many::Explain(f);why.S("a file could not be downloaded before the disk was changed");}
            return false;
        }
    }
    if(hypervisor>=0) {
#if __has_include("CharonWsHypervisor.h")
        const int i=hypervisor;failedItem=i;
        {static char line[300];plan::Out o(line,sizeof(line));plan::FetchFileLine(S().plan,w.list,i,Pl(),o);SayText(line);install::HearOut(40);}
        if(held::File* placeholder=held::Reserved(kHvPlaceholder,w.list.items[i].bytes))held::Drop(*placeholder);   // r410 (h8): its memory, for hv's own pieces
        if(!hv::FetchUpfront([](void*,uint64_t done){Progress(FetchLabel(),Upfront().before+done,Upfront().all);},nullptr)) {why.S("a file could not be downloaded before the disk was changed");return false;}
#else
        failedItem=hypervisor;why.S("a file could not be downloaded before the disk was changed");return false;
#endif
    }
    failedItem=-1;
    if(u.all)Progress(FetchLabel(),u.all,u.all);
    charontrace::Text("CHARON upfront: download complete");
    return true;
}
// A held file into a file of a FAT volume (replacing it). False: the file is not left half written.
inline bool HeldToFile(const held::File& f,efi_handle volume,const char16_t* path) {
    static char16_t folder[pathUnits];size_t cut=0;
    for(size_t i=0;path[i];++i)if(path[i]==u'\\')cut=i;
    if(cut && cut<pathUnits) {for(size_t i=0;i<cut;++i)folder[i]=path[i];folder[cut]=0;if(MakeFolders(volume,folder)!=EFI_SUCCESS)return false;}
    efi_file_protocol* file=winusb::OpenForWrite(volume,path,true);
    if(!file)return false;
    bool ok=true;
    for(size_t i=0;i<f.pieceCount && ok;++i) {
        const size_t whole=held::PieceSize(f,i);
        for(size_t at=0;at<whole && ok;) {
            size_t part=whole-at;if(part>(size_t(4)<<20))part=size_t(4)<<20;
            size_t wrote=part;ok=file->Write(file,&wrote,f.pieces[i]+at)==EFI_SUCCESS && wrote==part;at+=part;Pump();
        }
    }
    if(ok) {if(file->Flush)file->Flush(file);file->Close(file);}
    else file->Delete(file);
    return ok;
}
// Debian's kernel and initrd onto the plan's EFI partition, into the folder of its step.
inline bool PlaceDebian(int system) {
    auto& w=Fetches();auto& s=S();const int step=StepOf(s.run,system);
    char section[40];plan::SystemSection(s.plan.systems[system].id,section,sizeof(section));
    int found=0;bool ok=true,late=false;
    for(int i=0;i<w.list.count && ok;++i) {
        const plan::FetchItem& item=w.list.items[i];
        if(item.system!=system || (item.what!=plan::FetchWhat::kernel && item.what!=plan::FetchWhat::initrd))continue;
        static char16_t path[pathUnits];InstallerPath(step,item.what==plan::FetchWhat::initrd?"initrd.gz":"linux",path,pathUnits);
        if(held::File* f=held::Find(w.urls[i])) {ok=HeldToFile(*f,s.bound.volume,path);held::Drop(*f);}
        else {   // it did not fit in memory (said in the summary): fetched now, after the disk was prepared
            static char16_t url[400];Widen(w.urls[i],url,400);late=true;
            {static char16_t folder[pathUnits];size_t cut=0;for(size_t k=0;path[k];++k)if(path[k]==u'\\')cut=k;
             for(size_t k=0;k<cut;++k)folder[k]=path[k];folder[cut]=0;MakeFolders(s.bound.volume,folder);}
            ok=install::EnsureNetwork() && install::Fetch(url,s.bound.volume,path,T(u"plik instalatora systemu Linux",u"a file of the Linux installer"));
        }
        ++found;
    }
    if(found!=2 || !ok) {
        RemoveInstallerFiles(step);
        LogLine("krok 0: nie udało się położyć plików instalatora systemu Linux na partycji EFI; jego krok pobierze je sam","step 0: the Linux installer's files could not be put on the EFI partition; its step will download them itself");
        return false;}
    Answers().Put(section,"placed","yes");
    if(late)LogLine("krok 0: pliki instalatora systemu Linux pobrane po przygotowaniu dysku, przed pierwszą instalacją; leżą na partycji EFI","step 0: the Linux installer's files were downloaded after the disk was prepared, before the first installation; they are on the EFI partition");
    else LogLine("krok 0: jądro i instalator systemu Linux, pobrane z góry, leżą na partycji EFI","step 0: the Linux installer's kernel and initrd, downloaded up front, are on the EFI partition");
    return true;
}
// A Windows of the plan: its range is opened, the Windows partition and the temporary media partition are made
// (winpart::ProvidePlan) and the media is filled - from memory when the image was fetched up front, otherwise from
// the network now (that was said before the confirmation). The step later finds the complete media partition and
// downloads nothing (InstallWindows, "a complete media partition ... is used again").
inline bool PlaceWindows(int system) {
    auto& s=S();auto& ready=Ready();auto& w=Fetches();char reason[160]="";
    const plan::System& sys=s.plan.systems[system];char section[40];plan::SystemSection(sys.id,section,sizeof(section));
    if(!Reload(reason,sizeof(reason)))return false;
    const int rangeIndex=plan::RangeOfSystem(ready.layout,system);if(rangeIndex<0)return false;
    const plan::Range r=ready.layout.ranges[rangeIndex];
    if(!Restore(sys.id))return false;
    if(!WindowsSource(ask::Get("wversion"))) {ask::Clear();return false;}
    CarryAddress(section);
    if(plan::CountWindows(s.plan)!=1)winusb::AddressReady()=false;   // settled again for this Windows
    // Test discs only (r410, proof of CarryAddress): the file \EFI\HAK\media-place-fail-test on Charon's own volume
    // makes step 0 leave every Windows without media, so its step downloads after a restart from the plan's EFI
    // partition. A release medium does not carry the file.
    {FileRef ref;ref.volume=U().host->selfDevice;Set(ref.path,pathUnits,u"\\EFI\\HAK\\media-place-fail-test");efi_file_protocol* file=nullptr;
     if(ref.volume && OpenFile(ref,EFI_FILE_MODE_READ,0,&file)==EFI_SUCCESS && file) {file->Close(file);
        charontrace::Text("CHARON test: Windows media of step 0 left out");ask::Clear();
        for(int i=0;i<Fetches().list.count;++i)
            if(Fetches().list.items[i].system==system && Fetches().list.items[i].what==plan::FetchWhat::windows)
                if(held::File* f=held::Find(Fetches().urls[i]))held::Drop(*f);
        LogLine("krok 0: nie udało się przygotować nośnika instalatora Windows; krok Windows spróbuje sam i wtedy pobiera","step 0: the Windows installer's media could not be made; the Windows step will try by itself and downloads then");
        return false;}}
    const char* imageUrl=nullptr;const char* readerUrl=nullptr;
    const bool wantsNvda=ListHas(Answers().GetOr(section,"wreaders",""),"nvda");
    for(int i=0;i<w.list.count;++i) {
        if(w.list.items[i].system==system && w.list.items[i].what==plan::FetchWhat::windows && w.urls[i][0])imageUrl=w.urls[i];
        if(wantsNvda && w.list.items[i].what==plan::FetchWhat::reader)readerUrl=w.urls[i];   // one item for every Windows
    }
    const bool inMemory=imageUrl && held::Find(imageUrl);
    if(inMemory) {LogLine("krok 0: obraz Windows czekał w pamięci od pobierania z góry; zapisuję nośnik instalatora","step 0: the Windows image waited in memory since the up-front download; writing the installer's media");
        charontrace::Text("CHARON plan: Windows media from memory");}
    else {LogLine("krok 0: obraz Windows nie czekał w pamięci; pobieram go teraz, po przygotowaniu dysku, przed pierwszą instalacją","step 0: the Windows image did not wait in memory; downloading it now, after the disk was prepared, before the first installation");
        charontrace::Text("CHARON plan: Windows image is downloaded now, after the disk was prepared, before the first installation");}
    bool ok=inMemory || install::EnsureNetwork();
    if(ok)ok=FreeRange(*ready.disk,r);
    if(ok) {
        images::Reconnect(ready.disk->handle);
        s.bound.volume=images::VolumeOfPartition(s.esp);s.storage=Bind(s.bound);
        ok=Reload(reason,sizeof(reason));
    }
    efi_handle media=nullptr;
    if(ok) {
        install::Chosen().disk=size_t(ready.disk-disks::D().list);install::Chosen().part=SIZE_MAX;
        ask::Put("diskmode","partition");
        auto& state=winpart::W();state.made=false;state.changed=false;state.whole=false;state.io=nullptr;state.disk=nullptr;state.volume=nullptr;
        winpart::InPlan()=winpart::PlanRange{true,r.firstLba,r.lbaCount};
        // r410 (T7, agent 1): a second Windows of the same image does not download it again: its media partition is
        // made to the same size and the first Windows' media files are copied onto it (CopyTree; the first media
        // partition is still there - each Windows removes only its own at its first logon).
        static seeddisk::Guid firstMedia;static char firstUrl[https::urlUnits];static uint64_t firstTotal=0;static bool haveFirst=false;
        bool copied=false;
        if(haveFirst && !inMemory && winusb::ResolveAddress(Pl()?"pl-PL":"en-US") && plan::Eq(winusb::I().url,firstUrl)) {
            if(images::VolumeOfPartition(firstMedia.bytes)) {
                Say(T(u"Ten sam obraz Windows jest już na nośniku poprzedniego Windows; kopiuję go na dysku zamiast pobierać jeszcze raz.",
                      u"The same Windows image is already on the previous Windows' media; I copy it on the disk instead of downloading it again."));
                const efi_handle made=winpart::ProvidePlan(firstTotal);
                // vr's run: ProvidePlan reconnects the disk, so the first media's handle is looked up only now
                const efi_handle from=made?images::VolumeOfPartition(firstMedia.bytes):nullptr;
                static FileRef a,b;a.volume=from;Set(a.path,pathUnits,u"\\");b.volume=made;Set(b.path,pathUnits,u"\\");size_t files=0;
                const efi_status st=made && from?CopyTree(a,b,0,&files):EFI_NOT_FOUND;
                if(st==EFI_SUCCESS) {copied=true;ok=true;
                    charontrace::Value("CHARON plan: Windows media copied from the previous Windows, files",uint64_t(files));
                    LogLine("krok 0: nośnik drugiego Windows skopiowany z nośnika pierwszego, bez pobierania","step 0: the second Windows' media was copied from the first one's, no download");}
                else {
                    charontrace::Value("CHARON plan: Windows media copy failed, status",uint64_t(st));
                    charontrace::Value("CHARON plan: Windows media copy failed, files copied",uint64_t(files));
                    // vr's run: winpart::Undo removed only the media partition and the download's ProvidePlan then found the
                    // Windows partition in the way - the whole range is made free again before the download
                    if(made)winpart::Undo();
                    auto& again=winpart::W();again.made=false;again.changed=false;again.volume=nullptr;again.io=nullptr;again.disk=nullptr;
                    char why[160]="";
                    if(Reload(why,sizeof(why)) && FreeRange(*ready.disk,r)) {images::Reconnect(ready.disk->handle);Reload(why,sizeof(why));
                        install::Chosen().disk=size_t(ready.disk-disks::D().list);install::Chosen().part=SIZE_MAX;}
                    LogLine("krok 0: kopiowanie nośnika Windows nie udało się; pobieram obraz","step 0: copying the Windows media failed; downloading the image");
                }
            }
            winusb::AddressReady()=true;   // Make below uses the address just settled
        }
        if(!copied)ok=winusb::Make(nullptr,Pl()?"pl-PL":"en-US",winpart::ProvidePlan);
        winpart::InPlan().on=false;
        media=ok?state.volume:nullptr;
        if(ok && !copied && plan::CountWindows(s.plan)>=2) {firstMedia=state.unique;plan::Copy(firstUrl,sizeof(firstUrl),winusb::I().url);firstTotal=winusb::I().total;haveFirst=true;}
        if(ok) {MakeFolders(media,u"\\charon");ok=media && install::WriteText(media,u"\\charon\\Charon-MediaCleanup.ps1",winpart::cleanupScript,sizeof(winpart::cleanupScript)-1);}
        if(!ok)winpart::Undo();
    }
    if(ok && readerUrl) {   // NVDA's installer, fetched up front and checked: RunWindows finds it on the media and downloads nothing
        if(held::File* f=held::Find(readerUrl)) {
            static char16_t path[pathUnits];Set(path,pathUnits,u"\\charon\\readers\\");CatAscii(path,pathUnits,install::nvdaFile);
            if(!HeldToFile(*f,media,path))LogLine("krok 0: nie udało się zapisać NVDA na nośniku Windows; krok Windows pobierze go sam","step 0: NVDA could not be written to the Windows media; the Windows step will download it itself");
        }
    }
    if(imageUrl)if(held::File* f=held::Find(imageUrl))held::Drop(*f);
    // NVDA's installer stays held while another Windows of the plan still waits for its media (67 MB).
    {bool laterWindows=false;for(int k=system+1;k<s.plan.count;++k)laterWindows=laterWindows || plan::IsWindows(s.plan.systems[k].kind);
     if(readerUrl && !laterWindows)if(held::File* f=held::Find(readerUrl))held::Drop(*f);}
    ask::Clear();
    // The partitions of the disk were read again: new handles.
    s.bound.volume=images::VolumeOfPartition(s.esp);s.storage=Bind(s.bound);
    if(!ok) {
        if(Reload(reason,sizeof(reason)) && ReserveOne(*ready.disk,r)) {
            if(ready.disk->io->FlushBlocks)reinterpret_cast<images::BlockFlush>(ready.disk->io->FlushBlocks)(ready.disk->io);
            images::Reconnect(ready.disk->handle);s.bound.volume=images::VolumeOfPartition(s.esp);s.storage=Bind(s.bound);
        }
        LogLine("krok 0: nie udało się przygotować nośnika instalatora Windows; krok Windows spróbuje sam i wtedy pobiera","step 0: the Windows installer's media could not be made; the Windows step will try by itself and downloads then");
        return false;}
    Answers().Put(section,"placed","yes");
    LogLine("krok 0: nośnik instalatora Windows jest gotowy","step 0: the Windows installer's media is ready");
    return true;
}
inline void PlaceAll() {
    auto& s=S();if(!Fetches().ready)return;
    for(int i=0;i<s.plan.count;++i)if(plan::IsInstallerLinux(s.plan.systems[i].kind))PlaceDebian(i);   // r410 (bd): Ubuntu too
    for(int i=0;i<s.plan.count;++i)if(plan::IsWindows(s.plan.systems[i].kind))PlaceWindows(i);
}
// r410 (user answer 2): the order of the boot entries behind Charon's own. Two systems: one choice between the
// two orders; more: the first, the second, ... The first choice offered is today's order (the system that starts
// by itself first). False: Escape.
inline bool AskBootOrder(plan::Plan& p) {
    int offered[plan::kMaxSystems];const int n=plan::DefaultBootSequence(p,offered);
    if(n<2)return true;
    static char16_t names[plan::kMaxSystems][64];
    for(int i=0;i<p.count;++i)Widen(p.systems[i].name,names[i],64);
    int chosen[plan::kMaxSystems];
    if(n==2) {
        static char16_t labels[2][160];static ask::Choice orders[2];
        for(int k=0;k<2;++k) {Set(labels[k],160,names[offered[k]]);Cat(labels[k],160,T(u", potem ",u", then "));Cat(labels[k],160,names[offered[1-k]]);orders[k]={labels[k],labels[k],""};}
        size_t pick=0;
        if(!ask::Choose(T(u"W jakiej kolejności mają stać wpisy rozruchowe systemów, po wpisie Charona?",u"In which order should the boot entries of the systems stand, after Charon's own entry?"),orders,2,pick))return false;
        chosen[0]=offered[pick];chosen[1]=offered[1-pick];
    } else {
        int left[plan::kMaxSystems];int leftCount=n;
        for(int i=0;i<n;++i)left[i]=offered[i];
        for(int at=0;at+1<n;++at) {
            static ask::Choice list[plan::kMaxSystems];
            for(int k=0;k<leftCount;++k)list[k]={names[left[k]],names[left[k]],""};
            static char16_t question[200];Set(question,200,T(u"Który wpis rozruchowy ma stać na miejscu ",u"Which boot entry should stand in place "));CatN(question,200,size_t(at)+1);
            Cat(question,200,T(u" po wpisie Charona?",u" after Charon's own entry?"));
            size_t pick=0;
            if(!ask::Choose(question,list,size_t(leftCount),pick))return false;
            chosen[at]=left[pick];
            for(int k=int(pick);k+1<leftCount;++k)left[k]=left[k+1];
            --leftCount;
        }
        chosen[n-1]=left[0];
    }
    return plan::SetBootOrder(p,chosen,n);
}
inline bool InstallAny(int system,int step,char* reason,size_t capacity) {
    const plan::Kind kind=S().plan.systems[system].kind;
    return plan::IsWindows(kind)?InstallWindows(system,step,reason,capacity):kind==plan::Kind::ubuntu?InstallUbuntu(system,step,reason,capacity)   // r410 (bd)
        :InstallDebian(system,step,reason,capacity);
}

// A step has just been found done at a start of Charon: what follows from it here.
inline void AfterStepDone(int step) {
    const auto& run=S().run;
    if(run.steps[step].kind==plan::StepKind::firstStart)RemoveSeed(run.steps[step].system);
    if(run.steps[step].kind==plan::StepKind::install)RemoveInstallerFiles(step);   // r410: kernel and initrd of an installer, fetched up front
    if(run.steps[step].kind==plan::StepKind::install && run.steps[step].system>=0 && plan::IsWindows(S().plan.systems[run.steps[step].system].kind))
        CleanAfterWindows(run.steps[step].system);
}

// ---- what the finish step needs from here ---------------------------------------------------------
inline void FinishExtras(plan::Out& trouble) {
    auto& s=S();const efi_handle esp=s.bound.volume;
    // Charon's own entries point at the plan's EFI partition again (an image's first start
    // registered the copy on its own partition), and CharonDOS gets the entry it did not get
    // while the plan ran.
    efi_status status=dossys::PutOption(false,u"Charon",esp,u"\\EFI\\HAK\\HAKBOOT.EFI",true,nullptr,nullptr,true);
    if(status==EFI_UNSUPPORTED)status=dossys::PutOption(false,u"Charon",esp,u"\\EFI\\HAK\\HAKBOOT.EFI",true,nullptr);
    if(status!=EFI_SUCCESS)plan::Trouble(trouble,"the boot entry Charon could not be written",Pl());
    status=dossys::PutOption(true,u"Charon voice",esp,u"\\EFI\\HAK\\CHARONVOICE.EFI",true,nullptr,nullptr,true);
    if(status==EFI_UNSUPPORTED)status=dossys::PutOption(true,u"Charon voice",esp,u"\\EFI\\HAK\\CHARONVOICE.EFI",true,nullptr);
    if(status!=EFI_SUCCESS)plan::Trouble(trouble,"the reader add-on could not be registered",Pl());
    // Windows Setup writes its boot manager over the fallback loader \\EFI\\BOOT\\BOOTX64.EFI, where step 0 had put
    // Charon for firmware that loses its entries (decision D1: Charon's menu first). Put back; Windows starts from
    // its own entry and from \\EFI\\Microsoft\\Boot, which are not touched.
    for(int i=0;i<s.plan.count;++i)if(plan::IsWindows(s.plan.systems[i].kind) && plan::ResultOf(s.run,i)==plan::Result::done) {
        static FileRef a,b;a.volume=esp;Set(a.path,pathUnits,u"\\EFI\\HAK\\HAKBOOT.EFI");b.volume=esp;Set(b.path,pathUnits,u"\\EFI\\BOOT\\BOOTX64.EFI");
        if(CopyFile(a,b)!=EFI_SUCCESS)plan::Trouble(trouble,"Charon could not be put back as the fallback loader",Pl());
        break;
    }
    for(int step=0;step<s.run.count;++step)if(s.run.steps[step].kind==plan::StepKind::install)RemoveInstallerFiles(step);   // r410: also of a step that was skipped
    for(int i=0;i<s.plan.count;++i) {
        const plan::System& sys=s.plan.systems[i];
        if(sys.kind==plan::Kind::charondos && plan::ResultOf(s.run,i)==plan::Result::done) {
            status=dossys::PutOption(false,u"CharonDOS",esp,u"\\EFI\\CharonDOS\\CHARONDOS.EFI",false,u"Charon",Pl()?u"pl":nullptr);
            if(status!=EFI_SUCCESS)plan::Trouble(trouble,"the boot entry CharonDOS could not be written",Pl());
        }
        if(plan::IsImage(sys.kind)) {
            const char* path=nullptr;const efi_handle volume=LoaderOfSystem(i,path);
            if(volume && volume!=esp)for(int step=0;step<s.run.count;++step)if(s.run.steps[step].system==i)MarkImageVolume(volume,step,false);
            RemoveSeed(i);
        }
    }
}

// ---- r410: saved answer sets of a plan (\EFI\HAK\answers\<name>.ini; the text is made and read by CharonPlanSets.h) ----
inline plan::Store& SetScratch() {static char bytes[65536];static plan::Store store(bytes,sizeof(bytes));return store;}
inline char* SetBuffer() {static char text[49152];return text;}
constexpr size_t setBufferBytes=49152;
inline void SayRefusals(const plan::Refusals& report,const plan::Plan& p,const char16_t* pl,const char16_t* en) {
    static char text[3000];plan::Out out(text,sizeof(text));plan::SayAll(report,p,Pl(),false,out);
    static char16_t wide[3400];Set(wide,3400,T(pl,en));static char16_t more[3000];Widen(text,more,3000);
    // r410 (T9 negative run): "...; nic nie zostało zmienione. ... Nic nie zostało zmienione." - said once: a reason
    // that ends with the sentence itself has it cut here (the caller's sentence already says it).
    {const char16_t* tail=T(u" Nic nie zostało zmienione.",u" Nothing was changed.");size_t n=0,k=0;while(more[n])++n;while(tail[k])++k;
     if(n>=k) {bool same=true;for(size_t i=0;i<k && same;++i)same=more[n-k+i]==tail[i];if(same)more[n-k]=0;}}
    Cat(wide,3400,u" ");Cat(wide,3400,more);
    Fail(wide);install::HearOut(300);
}
// "Save this plan as an answer set?" - asked in the detailed mode only, after the disk is known and before the
// summary and the typed word. `d` is the chosen disk (its serial goes into a set for unattended use).
inline void OfferSaveSet(const plan::Plan& p,const disks::Disk& d,bool detailedMode) {
    const efi_handle volume=U().host->selfDevice;
    if(!volume || VolumeReadOnly(volume))return;   // a disc: nothing to save to, and no question about it
    const bool serial=d.serial[0]!=0;
    static const ask::Choice kinds[]={{u"nie",u"no","no"},{u"tak, zapisz odpowiedzi tego planu",u"yes, save the answers of this plan","yes"},
        {u"tak, do instalacji bez pytań na dysku o tym numerze seryjnym",u"yes, for an installation without questions on the disk with this serial number","unattended"}};
    size_t kind=0;
    if(!ask::Choose(T(u"Zapisać ten plan jako zestaw odpowiedzi na nośniku Charona?",u"Save this plan as an answer set on Charon's medium?"),kinds,serial?3:2,kind) || kind==0)return;
    static char16_t name[32];name[0]=0;
    if(!Prompt(T(u"Nazwa zestawu, małe litery i cyfry:",u"Name of the set, lower-case letters and digits:"),name,31) || !name[0])return;
    static char narrow[32];size_t n=0;bool good=true;
    for(;name[n];++n) {const char16_t c=speechtext::LowerCase(name[n]);if(!((c>=u'a' && c<=u'z') || (c>=u'0' && c<=u'9') || c==u'-'))good=false;name[n]=c;narrow[n]=char(c);}
    narrow[n]=0;
    if(!good) {Fail(T(u"W nazwie zestawu dozwolone są małe litery bez polskich znaków, cyfry i myślnik. Zestaw nie został zapisany.",u"Only lower-case letters without accents, digits and the hyphen are allowed in the name of a set. The set was not saved."));return;}
    static const ask::Choice passwords[]={{u"nie, pytaj o hasło przy wczytaniu",u"no, ask for the password when the set is loaded","no"},{u"tak, zapisz hasło jawnym tekstem",u"yes, store the password in clear text","yes"}};
    size_t with=0;
    if(!ask::Choose(T(u"Zapisać w pliku także hasło?",u"Store the password in the file too?"),passwords,2,with))return;
    if(with==1 && !ask::ConfirmRisk(T(u"Hasło będzie stało w pliku jawnym tekstem. Każdy, kto dostanie ten nośnik, może je przeczytać.",
                                      u"The password will stand in the file in clear text. Anyone who gets this medium can read it.")))return;
    plan::Out out(SetBuffer(),setBufferBytes);
    const bool made=plan::SetText(p,Answers(),narrow,detailedMode,kind==2,d.serial,with==1?SharedSecret():nullptr,SetScratch(),out);
    bool ok=made;
    if(made) {
        static FileRef ref;ref.volume=volume;Set(ref.path,pathUnits,sets::Folder());MakeFolders(volume,ref.path);
        Cat(ref.path,pathUnits,u"\\");Cat(ref.path,pathUnits,name);Cat(ref.path,pathUnits,u".ini");
        ok=WriteWhole(ref,reinterpret_cast<const uint8_t*>(SetBuffer()),out.length)==EFI_SUCCESS;
    }
    for(size_t i=0;i<setBufferBytes;++i)SetBuffer()[i]=0;
    if(!ok) {Fail(made?T(u"Nie udało się zapisać pliku zestawu.",u"The set file could not be written."):T(u"Odpowiedzi nie mieszczą się w pliku zestawu. Nic nie zapisano.",u"The answers do not fit in a set file. Nothing was saved."));return;}
    static char16_t said[300];Set(said,300,T(u"Zapisano plan jako zestaw odpowiedzi ",u"Saved the plan as the answer set "));Cat(said,300,name);
    Cat(said,300,kind==2?T(u". Przy wczytaniu Charon zapyta tylko o to, czego w nim nie ma, i zawsze o słowo potwierdzenia.",u". When it is loaded, Charon asks only for what it does not hold, and always for the confirmation word.")
        :T(u". Wspólne odpowiedzi i odpowiedzi każdego systemu są w nim osobno.",u". The shared answers and each system's own answers stand in it separately."));
    Play(Icon::done);Say(said);install::HearOut(150);
}
// Offers the saved PLANS (sets that name two or more systems). 0: none or declined, the dialogue asks as always;
// 1: a plan and its answers are loaded (`unattended` says whether the file asks for a run without questions);
// -1: Escape, or a file that was refused (said) - the caller leaves, nothing was changed.
inline int OfferPlanSets(plan::Plan& p,bool& unattended) {
    unattended=false;
    const efi_handle volume=U().host->selfDevice;if(!volume)return 0;
    static FileRef folder;folder.volume=volume;Set(folder.path,pathUnits,sets::Folder());
    static Listing list;if(ReadFolder(folder,list)!=EFI_SUCCESS)return 0;
    static ask::Choice choices[17];static char16_t names[16][nameUnits];static char16_t labels[16][nameUnits+40];size_t count=0;
    choices[count++]=ask::Choice{u"nie, zapytaj mnie o wszystko",u"no, ask me everything",""};
    for(size_t i=0;i<list.count && count<17;++i) {
        const Entry& e=list.entries[i];const size_t len=Len(e.name);
        if(e.directory || len<5 || !SameFold(e.name+len-4,u".ini"))continue;
        static FileRef ref;ref.volume=volume;Set(ref.path,pathUnits,sets::Folder());Cat(ref.path,pathUnits,u"\\");Cat(ref.path,pathUnits,e.name);
        uint8_t* data=nullptr;size_t size=0;
        if(ReadWhole(ref,setBufferBytes,&data,&size)!=EFI_SUCCESS || !data)continue;
        plan::Refusals quiet;plan::ParseIni(reinterpret_cast<const char*>(data),size,SetScratch(),quiet);
        const plan::SetInfo info=plan::Inspect(SetScratch());SetScratch().Wipe();
        for(size_t k=0;k<size;++k)data[k]=0;
        Release(data);
        if(!info.isSet || !info.isPlan)continue;
        Set(names[count-1],nameUnits,e.name);names[count-1][len-4]=0;
        Set(labels[count-1],nameUnits+40,names[count-1]);Cat(labels[count-1],nameUnits+40,T(u", systemów: ",u", systems: "));CatN(labels[count-1],nameUnits+40,size_t(info.systems));
        if(info.unattended)Cat(labels[count-1],nameUnits+40,T(u", bez pytań",u", without questions"));
        choices[count]=ask::Choice{labels[count-1],labels[count-1],""};++count;
    }
    if(count==1)return 0;
    size_t index=0;
    if(!ask::Choose(T(u"Wczytać zapisany plan?",u"Load a saved plan?"),choices,count,index))return -1;
    if(!index)return 0;
    static FileRef ref;ref.volume=volume;Set(ref.path,pathUnits,sets::Folder());Cat(ref.path,pathUnits,u"\\");Cat(ref.path,pathUnits,names[index-1]);Cat(ref.path,pathUnits,u".ini");
    uint8_t* data=nullptr;size_t size=0;
    if(ReadWhole(ref,setBufferBytes,&data,&size)!=EFI_SUCCESS || !data) {Fail(T(u"Nie udało się odczytać pliku zestawu.",u"The set file could not be read."));return -1;}
    plan::Refusals report;plan::SetInfo info{};
    const bool ok=plan::LoadSetText(reinterpret_cast<const char*>(data),size,Answers(),p,report,info);
    for(size_t k=0;k<size;++k)data[k]=0;
    Release(data);
    if(!ok) {
        SayRefusals(report,p,u"Tego zestawu nie da się wczytać; nic nie zostało zmienione.",u"This set cannot be loaded; nothing was changed.");
        Answers().Wipe();return -1;}
    // Every system must be one this version offers in a plan, and a system that does not speak is confirmed as always.
    for(int i=0;i<p.count;++i) {
        const Offer* o=nullptr;size_t n=0;const Offer* all=Offers(n);for(size_t k=0;k<n;++k)if(all[k].kind==p.systems[i].kind)o=&all[k];
        if(!o) {Fail(T(u"Zestaw zawiera system, którego ta wersja Charona nie instaluje w planie. Nic nie zostało zmienione.",u"The set holds a system this version of Charon does not install in a plan. Nothing was changed."));Answers().Wipe();return -1;}
        if(!info.unattended && o->entry && !install::ConfirmSilent(*o->entry)) {Answers().Wipe();return -1;}
        // Windows 11 on a computer without TPM 2.0: the set must already say that the checks are skipped, or the user says so now.
        if(p.systems[i].kind==plan::Kind::windows && !plan::Eq(p.systems[i].version,"10") && !plan::Eq(p.systems[i].version,"ltsc10") && !plan::Eq(p.systems[i].version,"server")
           && !plan::Eq(plan::Resolve(Answers(),p.systems[i].id,"bypass"),"yes") && !Tpm2Present()) {
            if(info.unattended || !Confirm(T(u"Firmware tego komputera nie zgłasza modułu TPM 2.0, a zestaw nie każe instalatorowi Windows 11 pomijać sprawdzania sprzętu. Czy mam to dodać?",
                                             u"This computer's firmware reports no TPM 2.0, and the set does not tell Windows 11 Setup to skip the hardware checks. Shall I add that?"))) {
                Fail(T(u"Bez pominięcia sprawdzania sprzętu Windows 11 nie zainstaluje się na tym komputerze. Plan nie został utworzony; nic nie zostało zmienione.",
                       u"Without skipping the hardware checks Windows 11 will not install on this computer. The plan was not made; nothing was changed."));Answers().Wipe();return -1;}
            char section[40];plan::SystemSection(p.systems[i].id,section,sizeof(section));Answers().Put(section,"bypass","yes");
        }
    }
    // The password: from the file only when its author put it there (said); otherwise asked now, as in the dialogue.
    ask::Clear();
    {const size_t all=Answers().Count();
     for(size_t i=0;i<all;++i) {const char* sec=nullptr;const char* k=nullptr;const char* v=nullptr;Answers().At(i,sec,k,v);
        if(plan::Eq(sec,"shared")) {const char* key=Intern(k);if(key)ask::Put(key,v);}}}
    if(info.passwordInside) {
        ask::Put("password",Answers().GetOr("secrets","password",""));
        Say(T(u"Uwaga: ten plik zawiera hasło jawnym tekstem; użyję go.",u"Note: this file holds a password in clear text; I will use it."));install::HearOut(100);
    } else {
        static ask::Question one[1]={};bool found=false;
        for(const auto& q:install::commonQuestions)if(ask::Same(q.key,"password")) {one[0]=q;found=true;}
        if(!found || !ask::Run(one,1,false)) {ask::Clear();Answers().Wipe();return -1;}
    }
    install::HashPassword();
    const bool hashed=ask::Get("hash")[0]!=0;
    plan::Copy(SharedSecret(),secretBytes,ask::Get("password"));
    const bool stored=hashed && plan::FinishLoad(Answers(),p,ask::Get("hash"));
    ask::Clear();
    if(!stored) {Fail(T(u"Nie udało się przygotować hasła.",u"The password could not be prepared."));Answers().Wipe();return -1;}
    unattended=info.unattended;
    static char16_t said[400];Set(said,400,T(u"Wczytano plan ",u"Loaded the plan "));Cat(said,400,names[index-1]);Cat(said,400,T(u", systemów: ",u", systems: "));CatN(said,400,size_t(p.count));
    Cat(said,400,T(u". O nic z niego nie pytam ponownie; przed startem usłyszysz podsumowanie i wpiszesz słowo potwierdzenia.",u". I ask nothing of it again; before the start you will hear the summary and type the confirmation word."));
    Say(said);install::HearOut(200);
    return 1;
}
// ---- r410 (bd): typed sizes (own list) and the shared data partition (detailed mode, decision D7) ------------------
// A number of gigabytes, typed; the proposal stands in the field (Enter takes it). False: Escape.
inline bool AskGigabytes(const char16_t* question,uint32_t proposed,uint32_t& value) {
    static char16_t text[16];
    for(;;) {
        Set(text,16,u"");CatN(text,16,proposed);
        if(!Prompt(question,text,10))return false;
        uint64_t n=0;bool digits=text[0]!=0;
        for(size_t i=0;text[i];++i) {if(text[i]<u'0' || text[i]>u'9') {digits=false;break;}n=n*10+uint64_t(text[i]-u'0');if(n>1000000)digits=false;}
        if(digits) {value=uint32_t(n);return true;}
        Fail(T(u"Wpisz liczbę gigabajtów samymi cyframi.",u"Type the number of gigabytes in digits only."));
    }
}
// After the disk is chosen, before the layout is computed. A size below the system's minimum or above what the disk
// leaves is refused at once, with the bounds, and asked again: the summary never carries a size that will not fit.
inline bool AskSizesAndData(plan::Plan& p,const disks::Disk& d,bool ownList,bool detailedMode,plan::Existing existing=plan::Existing::erase) {   // r410 (kd): existing=keep for a kept disk
    plan::DiskState state;DiskStateOf(d,state);
    if(ownList)for(int i=0;i<p.count;++i) {
        plan::System& sys=p.systems[i];
        if(sys.kind==plan::Kind::charondos)continue;
        sys.sizeGb=0;
        uint32_t smallest=0,proposed=0,largest=0;
        if(!plan::SizeBounds(p,1,state,existing,i,smallest,proposed,largest))return true;   // the layout says why, below
        static char16_t question[300],name[64];Widen(sys.name,name,64);
        Set(question,300,T(u"Rozmiar dla systemu ",u"Size for "));Cat(question,300,name);Cat(question,300,T(u" w gigabajtach. Najmniej ",u" in gigabytes. Smallest "));
        CatN(question,300,smallest);Cat(question,300,T(u", proponuję ",u", proposed "));CatN(question,300,proposed);
        Cat(question,300,T(u", najwięcej ",u", largest possible "));CatN(question,300,largest);Cat(question,300,u":");
        for(;;) {
            uint32_t typed=0;if(!AskGigabytes(question,proposed,typed))return false;
            if(typed<smallest || typed>largest) {
                static char16_t no[300];Set(no,300,typed<smallest?T(u"Za mało: ",u"Too little: "):T(u"Za dużo: ",u"Too much: "));Cat(no,300,name);
                Cat(no,300,typed<smallest?T(u" potrzebuje co najmniej ",u" needs at least "):T(u" może dostać najwyżej ",u" can get at most "));
                CatN(no,300,typed<smallest?smallest:largest);Cat(no,300,T(u" gigabajtów na tym dysku. Wpisz inny rozmiar.",u" gigabytes on this disk. Type another size."));
                Fail(no);install::HearOut(150);continue;}
            sys.sizeGb=typed;break;
        }
    }
    if(!detailedMode)return true;
    static const ask::Choice yesNo[]={{u"nie",u"no","no"},{u"tak, exFAT",u"yes, exFAT","exfat"}};
    size_t pick=0;
    if(!ask::Choose(T(u"Dodać wspólną partycję na dane, którą odczyta i zapisze każdy system? Linux zobaczy ją w folderze /media/data.",
                      u"Add a shared data partition that every system can read and write? Linux shows it in the folder /media/data."),yesNo,2,pick))return false;
    p.data=pick?plan::DataFs::exfat:plan::DataFs::none;p.dataSizeGb=0;
    if(!pick)return true;
    // Its bounds: 1 GiB, or everything the systems leave (their minimums or typed sizes).
    plan::Plan trial=p;plan::Layout layout;plan::Refusals report;
    for(int i=0;i<trial.count;++i)if(!trial.systems[i].sizeGb && trial.systems[i].kind!=plan::Kind::charondos) {uint64_t mn=0,st=0;plan::SizeLimits(trial.systems[i],mn,st);trial.systems[i].sizeGb=uint32_t((mn+1023)/1024);}
    if(!plan::ComputeLayout(trial,1,state,existing,layout,report))return true;   // said by the layout below
    uint32_t largest=0;
    for(int i=0;i<layout.count;++i)if(layout.ranges[i].role==plan::Role::data)largest=uint32_t(plan::RangeMb(layout,layout.ranges[i])/1024);
    static char16_t question[300];
    Set(question,300,T(u"Jej rozmiar w gigabajtach, 0 to cała reszta dysku. Najwięcej ",u"Its size in gigabytes, 0 for all the rest of the disk. Largest possible "));CatN(question,300,largest);Cat(question,300,u":");
    for(;;) {
        uint32_t typed=0;if(!AskGigabytes(question,0,typed))return false;
        if(typed && (typed<1 || typed>largest)) {
            static char16_t no[200];Set(no,200,T(u"Za dużo: na tym dysku wspólna partycja może mieć najwyżej ",u"Too much: on this disk the shared partition can have at most "));CatN(no,200,largest);
            Cat(no,200,T(u" gigabajtów. Wpisz inny rozmiar.",u" gigabytes. Type another size."));Fail(no);install::HearOut(150);continue;}
        p.dataSizeGb=typed;return true;
    }
}
// ---- r410 (kd, T8): a disk whose systems the plan keeps (spec 2.10) -----------------------------------------------
// Offered only for a plan without Windows and without CharonDOS (Windows Setup beside an existing EFI partition and
// CharonDOS as an ordinary FAT partition are not proven), on a GPT disk with partitions and a free range that holds
// the plan's minimums. Nothing of the kept partitions is moved, retyped or written; the plan works in the largest
// free range (plan::ComputeLayout with Existing::keep), the existing EFI partition is shared when Charon and the new
// loaders fit, else the plan makes its own small one there.
inline bool KeepAllowed(const plan::Plan& p) {
    for(int i=0;i<p.count;++i)if(plan::IsWindows(p.systems[i].kind) || p.systems[i].kind==plan::Kind::charondos)return false;
    return true;
}
inline bool KeepCandidate(const disks::Disk& d,uint64_t neededBytes) {
    return !d.readOnly && !(d.hasCharon && !hakefi::selfram::Active()) && d.gpt && !d.blank && d.partCount && d.freeBlocks*d.blockSize>=neededBytes;
}
// "Zostają bez zmian: partycja systemowa EFI, 300 MB; dane Windows, 59 GB; ..." - what the plan keeps.
inline void KeptText(const disks::Disk& d,char16_t* out,size_t cap) {
    Set(out,cap,T(u"Zostają bez zmian: ",u"Kept unchanged: "));
    for(size_t i=0;i<d.partCount;++i) {
        if(i)Cat(out,cap,u"; ");
        Cat(out,cap,disks::KindName(d.parts[i].kind));Cat(out,cap,u", ");
        systools::PutSize(out,cap,(d.parts[i].last-d.parts[i].first+1)*d.blockSize,Pl());
    }
    Cat(out,cap,T(u". Wolne miejsce, w którym stanie plan: ",u". Free space the plan goes into: "));systools::PutSize(out,cap,d.freeBlocks*d.blockSize,Pl());Cat(out,cap,u".");
}
// The disk of the plan: a kept disk (when one qualifies) or a disk erased whole (images::ChooseDisk). False: Escape.
// r410 (user answer 6e, through agent 2): Charon Hypervisor ALONE never erases a disk. It goes into the largest free
// space beside the systems that are there (a kept disk: the existing EFI partition is used, every other partition stays)
// or onto a disk that is empty; when no disk has the room, that is said before anything else and nothing is changed.
// r410 (helper h8, run 6): the refusal came only after the language, user and password questions. True: some disk
// has the room ChooseHypervisorTarget asks for (the same rule), so the questions are worth asking.
inline bool HypervisorRoomAnywhere(uint64_t neededMb) {
    auto& all=disks::D();const uint64_t need=(neededMb+2048)<<20;
    for(size_t i=0;i<all.count;++i) {
        const auto& d=all.list[i];
        if(d.readOnly || (d.hasCharon && !hakefi::selfram::Active()))continue;
        if((d.blank?disks::Bytes(d):d.gpt?d.freeBlocks*d.blockSize:0)>=need)return true;
    }
    return false;
}
inline bool ChooseHypervisorTarget(uint64_t neededMb,size_t& index,bool& keep) {
    keep=false;auto& all=disks::D();const uint64_t need=(neededMb+2048)<<20;
    static char16_t labels[disks::maxDisks][400];static ask::Choice list[disks::maxDisks];static size_t map[disks::maxDisks];static bool kept[disks::maxDisks];size_t count=0;
    uint64_t largest=0;
    for(size_t i=0;i<all.count && count<disks::maxDisks;++i) {
        const auto& d=all.list[i];
        if(d.readOnly || (d.hasCharon && !hakefi::selfram::Active()))continue;
        const uint64_t room=d.blank?disks::Bytes(d):d.gpt?d.freeBlocks*d.blockSize:0;
        if(room>largest)largest=room;
        if(room<need)continue;
        static char16_t label[300];disks::DiskLabel(d,i,label,300);
        if(d.blank)Set(labels[count],400,T(u"pusty dysk, nic na nim nie ma: ",u"an empty disk, nothing is on it: "));
        else {Set(labels[count],400,T(u"wolne miejsce obok istniejących systemów, ",u"free space beside the existing systems, "));systools::PutSize(labels[count],400,room,Pl());Cat(labels[count],400,u": ");}
        Cat(labels[count],400,label);
        list[count]={labels[count],labels[count],d.blank?"empty":"keep"};map[count]=i;kept[count]=!d.blank;++count;
    }
    if(!count) {
        static char16_t no[900];
        Set(no,900,T(u"Charon Hypervisor instaluje się tylko w wolnym miejscu obok istniejących systemów albo na pustym dysku; żadnego dysku nie wymazuje. Potrzebuje ",
                      u"Charon Hypervisor installs only into free space beside the existing systems or onto an empty disk; it erases no disk. It needs "));
        systools::PutSize(no,900,need,Pl());
        Cat(no,900,T(u", a największe wolne miejsce na dyskach tego komputera to ",u", and the largest free space on this computer's disks is "));systools::PutSize(no,900,largest,Pl());
        Cat(no,900,T(u". Miejsce w Windows zrobi skrypt Charon-MakeRoom.ps1 z folderu EFI, HAK, addon na nośniku Charona: uruchom go w Windows jako administrator i wpisz słowo ZMNIEJSZ. Potem uruchom tę instalację ponownie. Nic nie zostało zmienione.",
                      u". The script Charon-MakeRoom.ps1 in the folder EFI, HAK, addon on Charon's medium makes room in Windows: run it in Windows as an administrator and type the word SHRINK. Then start this installation again. Nothing was changed."));
        Say(no);install::HearOut(300);return false;
    }
    size_t pick=0;
    if(!ask::Choose(T(u"Gdzie zainstalować Charon Hypervisor?",u"Where should Charon Hypervisor be installed?"),list,count,pick))return false;
    index=map[pick];keep=kept[pick];
    if(keep) {static char16_t text[1200];KeptText(all.list[index],text,1200);Say(text);install::HearOut(150);}
    return true;
}
inline bool ChooseTarget(const plan::Plan& p,uint64_t neededMb,size_t& index,bool& keep) {
    keep=false;auto& all=disks::D();
    static char16_t labels[disks::maxDisks+1][400];static ask::Choice list[disks::maxDisks+1];static size_t map[disks::maxDisks+1];size_t count=0;
    for(size_t i=0;i<all.count && count<disks::maxDisks;++i) {
        const auto& d=all.list[i];
        if(!KeepCandidate(d,neededMb<<20))continue;
        static char16_t label[300];disks::DiskLabel(d,i,label,300);
        Set(labels[count],400,T(u"zachowaj to, co jest na dysku, i użyj jego wolnego miejsca, ",u"keep what is on the disk and use its free space, "));systools::PutSize(labels[count],400,d.freeBlocks*d.blockSize,Pl());
        Cat(labels[count],400,u": ");Cat(labels[count],400,label);
        list[count]={labels[count],labels[count],"keep"};map[count]=i;++count;
    }
    if(count && !KeepAllowed(p)) {
        Say(T(u"Na dysku z systemami jest dość wolnego miejsca, ale zachowanie istniejących systemów ta wersja oferuje tylko w planie bez Windows i bez CharonDOS. Ten plan zainstaluję na dysku wymazanym w całości.",
              u"A disk with systems has enough free space, but this version offers keeping the existing systems only in a plan without Windows and without CharonDOS. This plan goes onto a disk erased whole."));
        install::HearOut(150);count=0;
    }
    if(!count)return images::ChooseDisk((neededMb+2048)<<20,false,index);
    list[count]={T(u"wymaż cały dysk, wybiorę który",u"erase a whole disk, I will choose which"),T(u"wymaż cały dysk, wybiorę który",u"erase a whole disk, I will choose which"),"erase"};
    size_t pick=0;
    if(!ask::Choose(T(u"Gdzie zainstalować plan?",u"Where should the plan be installed?"),list,count+1,pick))return false;
    if(pick==count)return images::ChooseDisk((neededMb+2048)<<20,false,index);
    index=map[pick];keep=true;
    static char16_t kept[1200];KeptText(all.list[index],kept,1200);Say(kept);install::HearOut(150);
    return true;
}
// The boot entries of the systems that stay on a kept disk: every Boot#### in BootOrder whose device path names a
// partition of that disk (an HD() node, UEFI 2.10 10.3.5.1) and that is not Charon's. Up to 8.
struct KeptEntries {uint16_t ids[8];char16_t names[8][64];size_t count;};
inline KeptEntries& Kept() {static KeptEntries kept;return kept;}
inline void FindKeptEntries(const disks::Disk& d) {
    auto& k=Kept();k.count=0;
    static uint16_t order[256];static uint8_t option[4096];const size_t n=BootOrder(order,256);
    for(size_t i=0;i<n && k.count<8;++i) {
        const size_t size=BootOption(order[i],option,sizeof(option));if(size<8)continue;
        if(dossys::SameDescription(option,size,u"Charon") || dossys::SameDescription(option,size,u"CharonDOS"))continue;
        size_t at=6;while(at+1<size && (option[at] || option[at+1]))at+=2;
        const size_t textEnd=at;at+=2;const size_t listBytes=size_t(option[4])|(size_t(option[5])<<8);if(at+listBytes>size)continue;
        bool ours=false;
        for(size_t q=at;q+4<=at+listBytes;) {
            const size_t length=size_t(option[q+2])|(size_t(option[q+3])<<8);if(length<4 || q+length>at+listBytes || option[q]==0x7f)break;
            if(option[q]==4 && option[q+1]==1 && length>=42 && option[q+41]==2)
                for(size_t p=0;p<d.partCount;++p)if(disks::SameBytes(option+q+24,d.parts[p].unique,16))ours=true;
            q+=length;
        }
        if(!ours)continue;
        size_t c=0;for(size_t b=6;b+1<textEnd && c<63;b+=2)k.names[k.count][c++]=char16_t(option[b]|(option[b+1]<<8));
        k.names[k.count][c]=0;k.ids[k.count++]=order[i];
    }
}
// Detailed mode: where the kept systems' entries stand - behind the plan's systems (as they stand now, the default)
// or right after Charon's own entry. Recorded in [kept] entries=hex,... first=yes|no for the finish step.
inline bool AskKeptPlace(bool detailedMode) {
    auto& k=Kept();
    static char list[64];plan::Out ids(list,sizeof(list));
    for(size_t i=0;i<k.count;++i) {if(i)ids.Ch(',');ids.Hex(k.ids[i],4);}
    if(!k.count)return true;
    size_t pick=0;
    if(detailedMode) {
        static char16_t question[400];Set(question,400,T(u"Gdzie mają stać wpisy rozruchowe systemów, które zostają na dysku: ",u"Where should the boot entries of the systems that stay on the disk stand: "));
        for(size_t i=0;i<k.count;++i) {if(i)Cat(question,400,u", ");Cat(question,400,k.names[i]);}
        Cat(question,400,u"?");
        static const ask::Choice places[]={{u"za nowymi systemami",u"behind the new systems","after"},{u"zaraz po wpisie Charona, przed nowymi systemami",u"right after Charon's own entry, before the new systems","first"}};
        if(!ask::Choose(question,places,2,pick))return false;
    }
    return Answers().Put("kept","entries",list) && Answers().Put("kept","first",pick==1?"yes":"no");
}
// ---- the dialogue ---------------------------------------------------------------------------------
// r410 (agent 2's Charon Hypervisor): a plan whose systems are given, not asked (the single installation of the
// hypervisor is a one-system plan); BuildWith() sets it for one Build().
inline const char*& ForcedSystem() {static const char* id=nullptr;return id;}
inline void Build() {
    auto& s=S();const bool test=TestSwitch();
    struct Wiper {~Wiper() {WipeSecret();}} wiper;   // the typed password never outlives the dialogue
    WipeSecret();
    Answers().Wipe();plan::Clear(s.plan);
    {uint32_t lo,hi;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));s.plan.id=(lo^(hi*2654435761u))|1;}
    if(!images::EnsureInMemory()) {/* said; Charon's own disk then stays out of the list */}
    disks::Load();
    plan::Machine machine=MachineForPlan(nullptr);
    size_t mode=0;bool fileUnattended=false;bool ownList=false;   // r410 (bd)
    const char* forced=ForcedSystem();ForcedSystem()=nullptr;
    const bool hvAlone=forced && plan::Eq(forced,"hv");   // r410 (user answer 6e): never erases a disk
    if(hvAlone) {   // r410 (h8, run 6): no room anywhere -> said now, before any question
        plan::System probe{};probe.kind=plan::Kind::hypervisor;uint64_t minimum=0,standard=0;plan::SizeLimits(probe,minimum,standard);
        if(!HypervisorRoomAnywhere(minimum)) {size_t unused=0;bool kept=false;ChooseHypervisorTarget(minimum,unused,kept);return;}
    }
    const int fromSet=forced?0:OfferPlanSets(s.plan,fileUnattended);
    if(fromSet<0)return;
    // r410 (known issue 10): a plan with Windows on a computer with two or more internal disks is refused as soon as
    // its systems are known, not after the whole dialogue (Windows Setup numbers the disks itself).
    auto manyDisks=[&]() {
        bool windows=false;for(int i=0;i<s.plan.count;++i)windows=windows || s.plan.systems[i].kind==plan::Kind::windows;
        if(!windows || machine.internalDisks<=1)return false;
        plan::Refusals refusals;refusals.Add(plan::Code::windowsManyDisks,-1,-1,machine.internalDisks);
        SayRefusals(refusals,s.plan,u"Tego zestawu nie zainstaluję na tym komputerze; nic nie zostało zmienione. Odłącz pozostałe dyski wewnętrzne albo wybierz zestaw bez Windows.",
            u"I will not install this set on this computer; nothing was changed. Disconnect the other internal disks or choose a set without Windows.");
        Answers().Wipe();return true;
    };
    if(fromSet==1 && manyDisks())return;
    if(fromSet==0) {
    // The list.
    size_t presetCount=0;const Preset* presets=Presets(presetCount);
    static ask::Choice sets[16];static size_t map[16];size_t shown=0;   // r410: 13 sets and the own list
    for(size_t i=0;i<presetCount;++i)if(presets[i].offered || test) {sets[shown]={presets[i].pl,presets[i].en,""};map[shown++]=i;}
    const size_t own=shown;
    if(test)sets[shown++]={u"własna lista",u"my own list","own"};
    size_t set=0;
    if(forced) {const Offer* o=OfferOf(forced);if(!o || !AddSystem(s.plan,*o))return;}
    else if(!ask::Choose(T(u"Który zestaw?",u"Which set?"),sets,shown,set))return;
    else if(test && set==own) {if(!EditList(s.plan,machine))return;ownList=true;}
    else for(const char* id:presets[map[set]].systems)if(id) {const Offer* o=OfferOf(id);if(!o || (o->entry && !install::ConfirmSilent(*o->entry)))return;AddSystem(s.plan,*o);}
    if(manyDisks())return;
    // How many questions; the shared answers; each system's own.
    static const ask::Choice modes[]={{u"ekspresowo: wspólne odpowiedzi raz, reszta domyślnie",u"express: shared answers once, defaults for the rest","quick"},
        {u"szczegółowo: także pytania każdego systemu",u"detailed: also every system's own questions","detailed"}};
    if(!ask::Choose(T(u"Ile pytań?",u"How many questions?"),modes,2,mode))return;
    if(!AskShared(mode==0))return;
    // r410 (s7): Escape at a system's first question goes back to the previous system, at the first system to the shared answers.
    for(int i=0;i<s.plan.count;) {
        if(AskSystem(s.plan,i,mode==0)) {++i;continue;}
        if(!SystemBack()) {ask::Clear();return;}
        if(i>0) {Say(T(u"Wracam do poprzedniego systemu.",u"Back to the previous system."));--i;continue;}
        Say(T(u"Wracam do wspólnych odpowiedzi.",u"Back to the shared answers."));
        if(!AskShared(mode==0)) {ask::Clear();return;}
    }
    ask::Clear();
    // What starts by itself (not asked with CharonDOS: it starts first).
    if(!plan::HasCharonDos(s.plan)) {
        static ask::Choice starts[plan::kMaxSystems+1];static char16_t names[plan::kMaxSystems][64];
        for(int i=0;i<s.plan.count;++i) {Widen(s.plan.systems[i].name,names[i],64);starts[i]={names[i],names[i],""};}
        starts[s.plan.count]={u"żaden: menu Charona czeka na mnie",u"none: Charon's menu waits for me","menu"};
        size_t start=0;if(!ask::Choose(T(u"Który system ma się uruchamiać, gdy nic nie zrobisz?",u"Which system should start when you do nothing?"),starts,size_t(s.plan.count)+1,start))return;
        plan::Copy(s.plan.defaultId,sizeof(s.plan.defaultId),start<size_t(s.plan.count)?s.plan.systems[start].id:"menu");
        if(mode==1 && start<size_t(s.plan.count)) {   // r410: how long Charon waits before that system starts ([plan] timeout)
            static const ask::Choice waits[]={{u"5 sekund",u"5 seconds","5"},{u"2 sekundy",u"2 seconds","2"},{u"10 sekund",u"10 seconds","10"},{u"30 sekund",u"30 seconds","30"}};
            static const uint32_t seconds[]={5,2,10,30};size_t wait=0;
            if(!ask::Choose(T(u"Jak długo Charon czeka, zanim uruchomi ten system?",u"How long does Charon wait before it starts that system?"),waits,4,wait))return;
            s.plan.timeout=seconds[wait];
        }
        if(mode==1 && !AskBootOrder(s.plan))return;   // r410 (user answer 2): the express mode keeps today's order
    } else {
        {static char dosFirst[300];plan::Out o(dosFirst,sizeof(dosFirst));o.S(Pl()?"Jako pierwszy uruchamia się CharonDOS. ":"CharonDOS starts first. ");
         plan::DosCommandsText(s.plan,Pl(),o);SayText(dosFirst);}   // r410: only the commands of this plan's systems
        if(mode==1)Say(T(u"Kolejność wpisów rozruchowych jest w planie z CharonDOS stała: CharonDOS, Charon, potem pozostałe; o nią nie pytam.",
                        u"In a plan with CharonDOS the order of the boot entries is fixed: CharonDOS, Charon, then the others; I do not ask about it."));
    }
    }   // fromSet==0: the questions were asked; a loaded set brought the plan and every answer
    // The disk: one disk, erased whole.
    uint64_t needed=0;
    for(int i=0;i<s.plan.count;++i) {uint64_t minimum=0,standard=0;plan::SizeLimits(s.plan.systems[i],minimum,standard);needed+=minimum;}
    size_t index=0;bool keep=false;
    if(fromSet==1 && (fileUnattended || s.plan.disks[0].present)) {
        // r410: a set that names its disk: the disk is found by its serial number and by nothing else (no serial in
        // the file, no disk with it, or two such disks: refused, nothing is touched). The typed word still follows.
        static plan::DiskState states[disks::maxDisks];auto& all=disks::D();
        for(size_t i=0;i<all.count;++i)DiskStateOf(all.list[i],states[i]);
        int indexes[plan::kMaxDiskRules]={-1,-1,-1,-1};plan::Refusals refusals;
        if(!plan::AuthoriseDisks(s.plan,states,all.count,indexes,refusals) || indexes[0]<0) {
            SayRefusals(refusals,s.plan,u"Zestaw nie pasuje do dysków tego komputera; nic nie zostało zmienione.",u"The set does not fit the disks of this computer; nothing was changed.");
            Answers().Wipe();return;}
        index=size_t(indexes[0]);
        const auto& found=all.list[index];
        if(found.readOnly || (found.hasCharon && !hakefi::selfram::Active()) || disks::Bytes(found)<((needed+2048)<<20)) {
            Fail(T(u"Dysk wskazany w zestawie jest za mały, tylko do odczytu albo jest dyskiem, z którego działa Charon. Nic nie zostało zmienione.",
                   u"The disk the set names is too small, read-only, or the disk Charon runs from. Nothing was changed."));install::HearOut(200);Answers().Wipe();return;}
    } else if(hvAlone) {if(!ChooseHypervisorTarget(needed,index,keep))return;
    } else if(!ChooseTarget(s.plan,needed,index,keep))return;   // r410 (kd, T8): or a kept disk
    const disks::Disk& d=disks::D().list[index];
    auto& ready=Ready();ready.disk=&d;ready.ownHome=d.hasCharon;
    machine=MachineForPlan(&d);
    const plan::Existing existing=keep?plan::Existing::keep:plan::Existing::erase;
    Kept().count=0;if(keep)FindKeptEntries(d);
    if(fromSet==0 && !AskSizesAndData(s.plan,d,ownList,mode==1,existing))return;   // r410 (bd): typed sizes, shared data partition
    if(keep && !AskKeptPlace(fromSet==0 && mode==1))return;   // r410 (kd): where the kept systems' entries stand
    // Pre-flight and layout: nothing has been written.
    plan::Refusals report;plan::ValidatePlan(s.plan,machine,report);
    plan::DiskState state;DiskStateOf(d,state);
    const bool fits=plan::ComputeLayout(s.plan,1,state,existing,ready.layout,report);
    static char text[6000];
    if(!fits || report.Refused()) {
        plan::Out out(text,sizeof(text));plan::SayAll(report,s.plan,Pl(),false,out);
        out.S(Pl()?" Na dysku nic się nie zmieniło.":" Nothing on the disk has changed.");SayText(text);install::HearOut(300);return;}
    {plan::Out out(text,sizeof(text));plan::SayAll(report,s.plan,Pl(),true,out);
     if(text[0]) {SayText(text);install::HearOut(200);if(!Confirm(T(u"Kontynuować mimo to?",u"Continue anyway?")))return;}}
    // r410: every file Charon fetches itself, its size and where it waits; every address must answer before the
    // disk changes (this replaces the r409 check of the image and Windows addresses, and now covers Debian's too).
    if(!MakeFetchList(machine.network))return;
    // r410: the answers of a detailed plan can be kept as a set (shared part once, each system's part).
    if(fromSet==0 && mode==1)OfferSaveSet(s.plan,d,true);
    // The summary and the one typed word.
    {plan::Out out(text,sizeof(text));
     plan::Summary(s.plan,ready.layout,machine,disks::Bytes(d)>>30,d.blank,Answers().GetOr("shared","user",""),Pl(),out,Fetches().ready?&Fetches().list:nullptr);
     if(keep) {   // r410 (kd, T8): what stays, by name, and what Charon leaves to the kept systems
        static char16_t kept[1200];KeptText(d,kept,1200);static char narrow[1600];
        size_t k=0;for(size_t i=0;kept[i] && k+4<sizeof(narrow);++i) {const char16_t c=kept[i];
            if(c<0x80)narrow[k++]=char(c);else if(c<0x800) {narrow[k++]=char(0xc0|(c>>6));narrow[k++]=char(0x80|(c&0x3f));}
            else {narrow[k++]=char(0xe0|(c>>12));narrow[k++]=char(0x80|((c>>6)&0x3f));narrow[k++]=char(0x80|(c&0x3f));}}
        narrow[k]=0;out.S(" ").S(narrow);
        for(size_t i=0;i<Kept().count;++i) {
            static char name[200];size_t n=0;for(size_t c=0;Kept().names[i][c] && n+4<sizeof(name);++c) {const char16_t w=Kept().names[i][c];if(w<0x80)name[n++]=char(w);}
            name[n]=0;
            out.S(Pl()?" Wpis rozruchowy ":" The boot entry ").S(name).S(Pl()?" zostaje i jest w menu Charona.":" stays and is in Charon's menu.");
        }
        bool keptWindows=false;for(size_t i=0;i<d.partCount;++i)keptWindows=keptWindows || d.parts[i].kind==2;
        if(keptWindows)out.S(Pl()?" Zachowanych systemów Charon nie zmienia, także ich ustawienia zegara; Windows liczy czas lokalnie, a nowe systemy w czasie uniwersalnym, więc po przełączeniu zegar Windows może się różnić o różnicę stref, dopóki w Windows nie ustawisz czasu uniwersalnego."
            :" Charon does not change the kept systems, nor their clock setting; Windows keeps local time and the new systems universal time, so after a switch Windows' clock may be off by the time-zone difference until you set universal time in Windows.");
     }
     if(plan::CountWindows(s.plan))out.S(Pl()?" Pliki instalatora Windows trafią na tymczasową partycję na końcu miejsca przeznaczonego na Windows; Windows usunie ją przy pierwszym logowaniu i odzyska to miejsce."
        :" The Windows installer's files go to a temporary partition at the end of the space given to Windows; Windows removes it at the first sign-in and takes the space back.");
     out.S(Pl()?" Aby zacząć cały plan, wpisz słowo TAK i naciśnij Enter. Escape anuluje. Jeszcze nic nie zostało zmienione."
        :" To start the whole plan, type the word YES and press Enter. Escape cancels. Nothing has been changed yet.");
     SayText(text);}
    static char16_t word[16];word[0]=0;
    if(!Prompt(T(u"Wpisz TAK, aby zacząć:",u"Type YES to start:"),word,15,nullptr,nullptr,false) || !SameFold(word,T(u"tak",u"yes"))) {
        held::DropAll();   // r410: the memory reserved for the files is given back
        Say(T(u"Nie wpisano potwierdzenia. Nic nie zostało zmienione.",u"No confirmation was typed. Nothing was changed."));Answers().Wipe();return;}
    if(U().host->ensureReader && !U().host->ensureReader(U().host->context))
        Say(T(u"Uwaga: nie udało się włączyć dodatku czytnika ekranu w firmware.",u"Note: the screen reader add-on could not be turned on in the firmware."));
    plan::PlanToStore(s.plan,Answers());
    char reason[160]="";
    // r410, phase 1: everything that waits in memory is fetched NOW, before the disk is touched. A failure ends
    // here: no variable, no partition table, nothing on the disk has changed.
    plan::Upfront phases{};phases.present=Fetches().ready;phases.began=Now();
    if(Fetches().ready) {
        int failedItem=-1;
        if(!FetchUpfront(reason,sizeof(reason),failedItem)) {
            held::DropAll();
            static char note[700];plan::Out o(note,sizeof(note));
            o.S(Pl()?"Pobieranie plików przed zmianą dysku nie powiodło się: ":"Downloading the files before the disk is changed failed: ").S(plan::Reason(reason,Pl()));
            if(failedItem>=0) {o.S(Pl()?"; plik: ":"; file: ");plan::FetchName(s.plan,Fetches().list.items[failedItem],Pl(),o);}
            o.S(Pl()?". Plan nie ruszył. Na dysku nic się nie zmieniło.":". The plan did not start. Nothing on the disk has changed.");
            UpfrontNote(note);
            static char16_t wide[700];Widen(note,wide,700);Fail(wide);install::HearOut(300);
            Answers().Wipe();return;}
        phases.files=uint32_t(plan::UpfrontCount(Fetches().list));phases.mb=plan::UpfrontBytes(Fetches().list)>>20;
        phases.later=uint32_t(plan::FetchCount(Fetches().list,plan::Hold::later));
        if(phases.files) {Play(Icon::done);Say(T(u"Wszystkie pliki są pobrane. Teraz zmieniam dysk.",u"Every file is downloaded. Now I change the disk."));install::HearOut(60);}
    }
    phases.ended=Now();
    // Step 0, then the plan runs.
    charontrace::Text("CHARON plan: step 0 begins; the disk is changed from here on");
    plan::Begin(s.plan,s.run,Now());
    plan::MarkLaunched(s.run,0,Now());
    if(!Prepare(reason,sizeof(reason))) {
        held::DropAll();
        static char16_t wide[200];Widen(plan::Reason(reason,Pl()),wide,200);
        static char16_t all[500];Set(all,500,T(u"Przygotowanie dysku się nie powiodło: ",u"The disk could not be prepared: "));Cat(all,500,wide);
        if(ready.layout.newTable)Cat(all,500,T(u". Plan nie ruszył; dysk może mieć już nową, pustą tablicę partycji.",u". The plan did not start; the disk may already hold a new, empty partition table."));
        else Cat(all,500,T(u". Plan nie ruszył; istniejące partycje dysku nie zostały zmienione, mogły dojść partycje planu.",u". The plan did not start; the disk's existing partitions were not changed, the plan's partitions may have been added."));
        Fail(all);install::HearOut(300);return;}
    plan::MarkDone(s.run,0,Now());
    // Decision D4: the password for each Windows step waits in the step's own file on the new EFI
    // partition; plan::Resume overwrites and deletes it when the step is done, the finish step at the latest.
    for(int i=0;i<s.run.count;++i) {
        const auto& step=s.run.steps[i];
        if(step.kind!=plan::StepKind::install || step.system<0 || !plan::IsWindows(s.plan.systems[step.system].kind))continue;
        if(!plan::WriteStepFile(s.storage,s.run,i,"answers.env",SharedSecret(),plan::Len(SharedSecret())))LogLine("nie udało się zapisać hasła dla kroku Windows; ten krok się nie powiedzie i można go pominąć","the password for the Windows step could not be stored; that step will fail and can be skipped");
    }
    WipeSecret();
    if(!SaveNow())return;
    s.loaded=true;
    // r410, phase 3: what was fetched goes to where each step will find it (same start of Charon; the ready images
    // are written by their own steps, which follow at once and take them from memory).
    PlaceAll();
    phases.placed=Now();
    if(phases.present)plan::UpfrontToStore(phases,Answers());
    if(!SaveNow())return;
    charontrace::Text("CHARON plan: files are in place; the steps follow");
    Continue(machine);
}
// Asked by install::Dialogue before the group question. True: the plan dialogue took over.
inline bool EntryQuestion() {
    // r410: a plan waits for a decision (Escape at its question left it waiting): the install entry says so and
    // offers its question again; it does not build another plan or a single installation over the waiting one.
    if(Pending(dossys::Rt())) {
        static const ask::Choice waiting[]={{u"wróć do pytania planu",u"go back to the plan's question","plan"},{u"nic teraz nie rób",u"do nothing now","none"}};
        size_t pick=0;
        if(ask::Choose(T(u"Plan instalacji kilku systemów czeka na twoją decyzję; dopóki czeka, nie zaczynam innej instalacji. Co teraz?",
                         u"A plan of several systems is waiting for your decision; while it waits I start no other installation. What now?"),waiting,2,pick) && pick==0)
            AtStart(MachineNow());
        return true;
    }
    if(!Offered())return false;
    // r410 (agent 2's Charon Hypervisor): its single installation is a one-system plan (BuildWith). Shown with the test
    // switch until a run of it in a virtual machine is recorded (as every plan set).
    static const ask::Choice kinds[]={{u"jeden system",u"one system","one"},
        {u"kilka systemów obok siebie, instalowanych po kolei bez twojego udziału",u"several systems side by side, installed one after another without you","several"},
        {u"Charon Hypervisor: maszyny wirtualne obok innych systemów",u"Charon Hypervisor: virtual machines beside other systems","hv"}};
    size_t kind=0;
    if(!ask::Choose(T(u"Jeden system czy kilka systemów obok siebie?",u"One system, or several systems side by side?"),kinds,TestSwitch()?3:2,kind) || kind==0)return false;
    if(kind==2)ForcedSystem()="hv";
    Build();
    return true;
}
// Called once when the workspace opens: the dialogue's hook and the runners.
inline void Register() {
    install::PlanDialogue()=EntryQuestion;
    auto& r=Runners();r.prepare=Prepare;r.writeDos=WriteDos;r.writeImage=WriteImage;r.install=InstallAny;
    r.beforeFirstStart=SeedBack;   // r410 (bd)
}

}  // namespace planrun
}  // namespace ws
}  // namespace hakefi
