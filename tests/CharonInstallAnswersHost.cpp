// r409: host harness for Charon's install dialogue and answer-file generators. GPL-2.0-or-later.
// Built and driven by tests/charon-install-answers-host.py; see its header for what is checked.
//
// What is Charon's OWN code here (unchanged text of src/efi):
//   - CharonWsAsk.h, whole: the answer store and the dialogue engine (Run, Choose, Pick, Secret);
//   - CharonWsReaders.h, whole: reader and desktop tables, ResolveReaders, ReaderPackages, ReaderArguments;
//   - CharonCrypt.h, CharonSha256.h, CharonPrivate.h, CharonSelfCopy.h, whole;
//   - cut out by the runner (charon-*-cuts.inc): the question tables, DeriveDefaults, DebianPreseed with
//     DebianLateCommand, UbuntuUserData, ResolveWindowsReaders, WindowsAnswers, ConfirmSilent, ImageUserData
//     with DistroOf and DesktopPackages, the CharonDOS ProfileText, SkipInArchive and HakTree, and from
//     CharonWsCore.h Prompt, Confirm, CopyTree and CopyCharonTree.
// What is a stand-in: the rest of CharonWsCore.h (tests/charon-install-host/CharonWsCore.h: scripted
// keys, a transcript instead of speech, host folders as volumes), the disk list (one fake disk), and
// the few lines of install::Classic, install::RunImage and install::RunWindows that sit between the
// questions and the generator (Flow* below mirror them; those functions as a whole need a network,
// disks and firmware). The password hash uses Charon's Sha256Crypt with a fixed salt instead of a
// salt from the clock.
#define CHARON_INSTALL_HOST 1
#include "CharonWsReaders.h"   // the copy in the build folder: CharonWsAsk.h -> the stand-in CharonWsCore.h
#include "CharonWsDetailed.h"   // r410: the copy in the build folder (groups of the detailed mode, carriers)
#include "CharonSelfCopy.h"
#include "CharonWsPlanWindows.h"   // r409: WindowsAnswers calls it for a step of a multi-system plan
#include <algorithm>
#include <deque>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace hakefi {
namespace ws {
namespace disks {
struct Disk {uint64_t blocks;uint32_t blockSize;};
struct Set {Disk list[4];size_t count;};
inline Set& D() {static Set set;return set;}
}  // namespace disks
namespace install {
using ask::Choice;using ask::Kind;using ask::Question;
#include "charon-install-cuts.inc"
}  // namespace install
namespace dossys {
#include "charon-dos-cuts.inc"
}  // namespace dossys
namespace selfinstall {
#include "charon-selfinstall-cuts.inc"
}  // namespace selfinstall
}  // namespace ws
}  // namespace hakefi
using namespace hakefi;
using namespace hakefi::ws;

// ---- which questions were asked ---------------------------------------------------
static const ask::Question* currentTable=nullptr;static size_t currentCount=0;
static std::vector<std::string> askedKeys;
static bool StartsWith(const char16_t* text,const char16_t* prefix) {
    if(!prefix || !prefix[0])return false;
    for(size_t i=0;prefix[i];++i)if(text[i]!=prefix[i])return false;
    return true;
}
void hakefi::ws::HostSaid(const char16_t* text) {
    for(size_t i=0;i<currentCount;++i) {
        const auto& q=currentTable[i];
        if(StartsWith(text,Pl()?q.pl:q.en)) {if(askedKeys.empty() || askedKeys.back()!=q.key)askedKeys.push_back(q.key);return;}
    }
}
static bool RunTable(const ask::Question* table,size_t count,bool quick) {
    currentTable=table;currentCount=count;const bool ok=ask::Run(table,count,quick);currentTable=nullptr;currentCount=0;return ok;
}
#define TABLE(t) t,sizeof(t)/sizeof(t[0])

// ---- text helpers -------------------------------------------------------------------
static std::string Utf8(const std::u16string& s) {
    std::string out(s.size()*3+8,'\0');const size_t n=Utf16ToUtf8(s.c_str(),s.size(),reinterpret_cast<uint8_t*>(&out[0]),out.size());out.resize(n);return out;
}
static std::u16string Utf16(const std::string& s) {
    std::u16string out(s.size()+4,u'\0');const size_t n=Utf8ToUtf16(reinterpret_cast<const uint8_t*>(s.data()),s.size(),&out[0],out.size());out.resize(n);return out;
}
static void WriteFile(const std::string& path,const std::string& bytes) {
    std::ofstream f(path,std::ios::binary);f.write(bytes.data(),std::streamsize(bytes.size()));
    if(!f) {printf("error=cannot write %s\n",path.c_str());exit(2);}
}
// Key script: ENTER ESC UP DOWN LEFT RIGHT HOME END SPACE F1 BS, or "text" (UTF-8; \" and \\ inside).
static void QueueKeys(const std::string& script) {
    auto& keys=H().keys;size_t i=0;
    auto push=[&](uint16_t scan,char16_t ch) {keys.push_back(Key{scan,ch,false,false,false});};
    while(i<script.size()) {
        if(script[i]==' ' || script[i]=='\t') {++i;continue;}
        if(script[i]=='"') {
            std::string text;++i;
            while(i<script.size() && script[i]!='"') {if(script[i]=='\\' && i+1<script.size())++i;text+=script[i++];}
            ++i;
            for(char16_t c:Utf16(text))push(0,c);
            continue;
        }
        size_t e=i;while(e<script.size() && script[e]!=' ' && script[e]!='\t')++e;
        const std::string word=script.substr(i,e-i);i=e;
        if(word=="ENTER")push(0,13);else if(word=="ESC")push(scEsc,0);else if(word=="UP")push(scUp,0);else if(word=="DOWN")push(scDown,0);
        else if(word=="LEFT")push(scLeft,0);else if(word=="RIGHT")push(scRight,0);else if(word=="HOME")push(scHome,0);else if(word=="END")push(scEnd,0);
        else if(word=="SPACE")push(0,u' ');else if(word=="F1")push(scF1,0);else if(word=="BS")push(0,8);
        else if(word=="F2")push(scF2,0);else if(word=="F3")push(scF3,0);else if(word=="F10")push(scF10,0);else if(word=="DEL")push(scDelete,0);
        else {printf("error=unknown key %s\n",word.c_str());exit(2);}
    }
}

// ---- one vector -----------------------------------------------------------------------
struct Vector {
    std::string name,flow,mode="quick",keys;bool polish=false;
    std::vector<std::pair<std::string,std::string>> puts;   // stored after the dialogue (what the flow itself stores)
    std::map<std::string,std::string> args;
    std::string Arg(const char* key,const char* fallback) const {auto it=args.find(key);return it==args.end()?fallback:it->second;}
};
static std::deque<std::string>& Pool() {static std::deque<std::string> pool;return pool;}   // a deque: the pointers handed out stay valid when it grows   // key strings must outlive the store
static const char* Keep(const std::string& s) {Pool().push_back(s);return Pool().back().c_str();}
static Vector ReadVector(const std::string& path) {
    std::ifstream f(path,std::ios::binary);if(!f) {printf("error=cannot read %s\n",path.c_str());exit(2);}
    Vector v;std::string line;
    while(std::getline(f,line)) {
        if(!line.empty() && line.back()=='\r')line.pop_back();
        if(line.empty() || line[0]=='#')continue;
        auto pair=[&](const std::string& text) {const size_t eq=text.find('=');if(eq==std::string::npos) {printf("error=bad line %s\n",line.c_str());exit(2);}
            return std::make_pair(text.substr(0,eq),text.substr(eq+1));};
        if(line.rfind("put ",0)==0)v.puts.push_back(pair(line.substr(4)));
        else if(line.rfind("arg ",0)==0) {auto p=pair(line.substr(4));v.args[p.first]=p.second;}
        else {
            auto p=pair(line);
            if(p.first=="lang")v.polish=p.second=="pl";else if(p.first=="flow")v.flow=p.second;else if(p.first=="mode")v.mode=p.second;
            else if(p.first=="keys")v.keys+=(v.keys.empty()?"":" ")+p.second;
            else {printf("error=unknown line %s\n",line.c_str());exit(2);}
        }
    }
    return v;
}
static void HashPassword() {   // install::HashPassword with a fixed salt (the real one takes it from the clock)
    static char hash[96];
    if(crypt::Sha256Crypt(ask::Get("password"),"charonhosttests1",hash,sizeof(hash)))ask::Put("hash",hash);
}
static void ApplyPuts(const Vector& v) {for(const auto& p:v.puts)ask::Put(Keep(p.first),p.second.c_str());}
// install::Classic for Debian (0) and Ubuntu (1), from the questions to the generator.
static bool FlowLinux(const Vector& v,int system,install::Text& text) {
    const bool quick=v.mode=="quick";
    if(v.mode=="guided") {text.Line("# Written by Charon: the guided installation only brings Charon along.");install::DebianLateCommand(text,false);return true;}
    if(Pl() && !ask::Get("locale")[0])ask::Put("locale","pl_PL");
    if(!RunTable(TABLE(install::commonQuestions),quick))return false;
    if(system==0?!RunTable(TABLE(install::debianQuestions),quick):!RunTable(TABLE(install::ubuntuQuestions),quick))return false;
    if(!install::ResolveReaders(ask::Any("desktop")))return false;
    install::DeriveDefaults();HashPassword();
    if(!ask::Get("hash")[0])return false;
    detailed::Begin('l',system==0?'d':'u');   // r410, as install::Classic
    if(!quick && !detailed::Run())return false;
    ApplyPuts(v);
    if(!ask::Complete())return false;
    auto& d=disks::D();d.count=1;d.list[0].blocks=strtoull(v.Arg("disk_blocks","250069680").c_str(),nullptr,10);d.list[0].blockSize=512;install::Chosen().disk=0;install::Chosen().part=SIZE_MAX;
    if(system==0)install::DebianPreseed(text);else install::UbuntuUserData(text,v.Arg("largest","1")=="1");
    return true;
}
// install::Classic for Windows and the part of install::RunWindows before WindowsAnswers.
static bool FlowWindows(const Vector& v,install::Text& text) {
    const bool quick=v.mode=="quick";
    if(v.args.count("wversion")) {ask::Put("wversion",Keep(v.Arg("wversion","")));if(v.args.count("windex"))ask::Put("windex",Keep(v.Arg("windex","")));}
    const size_t skip=v.Arg("language_asked","0")=="1"?1:0;
    if(skip)ask::Put("wlocale",Keep(v.Arg("wlocale","en-US")));
    // r409 (Windows profiles), as in install::Classic: the last profile ("Windows Server as a workstation") is left out
    // when the fetched media is known not to be Windows Server; ReviOS/AtlasOS on a version they refuse are dropped aloud.
    static ask::Question asked[sizeof(install::windowsQuestions)/sizeof(install::windowsQuestions[0])];const size_t total=sizeof(asked)/sizeof(asked[0]);
    const bool notServer=ask::Get("wversion")[0] && !ask::Is("wversion","server");
    for(size_t i=0;i<total;++i) {asked[i]=install::windowsQuestions[i];if(notServer && asked[i].choices==install::windowsProfiles && asked[i].count>1)asked[i].count-=1;}
    if(!RunTable(asked+skip,total-skip,quick))return false;
    if(!install::ResolveWindowsReaders())return false;
    install::DropUnfitProfile();
    detailed::Begin('w','w');   // r410, as install::Classic
    if(!quick && !detailed::Run())return false;
    ApplyPuts(v);
    ask::Put("nvdaready","no");ask::Put("nvdasetup","no");
    if(ask::Has("wreaders","nvda") && v.Arg("nvda_on_media","1")=="1") {ask::Put("nvdaready","yes");ask::Put("nvdasetup","yes");}
    if(!ask::Complete())return false;
    install::WindowsAnswers(text,unsigned(strtoul(v.Arg("partition","3").c_str(),nullptr,10)),v.Arg("esp","0")=="1");
    return true;
}
// install::Dialogue (the confirmation of a system without speech) and install::RunImage up to ImageUserData.
static bool FlowImage(const Vector& v,install::Text& text,std::string& note) {
    const size_t index=strtoul(v.Arg("entry","0").c_str(),nullptr,10);
    if(index>=sizeof(install::imageGroup)/sizeof(install::imageGroup[0]))return false;
    const install::Entry& e=install::imageGroup[index];
    const bool confirmed=install::ConfirmSilent(e);
    note+=std::string("confirm-silent=")+(e.speech!=install::Speech::none?"not-asked":confirmed?"yes":"no")+"\n";
    if(!confirmed)return false;
    const bool quick=v.mode=="quick";
    if(Pl() && !ask::Get("locale")[0])ask::Put("locale","pl_PL");
    if(!RunTable(TABLE(install::commonQuestions),quick))return false;
    if(!RunTable(TABLE(install::imageQuestions),quick))return false;
    if(install::DistroOf(e)=='f' && ask::Any("desktop")) {Say(T(u"Do obrazu Fedory Charon nie dodaje pulpitów; zostaje system z samą konsolą.",u"Charon adds no desktops to the Fedora image; it stays a console-only system."));ask::Put("desktop","");}
    if(!install::ResolveReaders(ask::Any("desktop")))return false;
    install::DeriveDefaults();HashPassword();
    if(!ask::Get("hash")[0])return false;
    {const char distro=install::DistroOf(e);detailed::Begin('l',distro=='d'?'D':distro=='u'?'U':distro=='a'?'A':'F');}   // r410, as install::RunImage
    if(!quick && !detailed::Run())return false;
    ApplyPuts(v);
    if(!ask::Complete())return false;
    const bool speech=ask::Is("speech","yes") && e.speech!=install::Speech::none;
    install::ImageUserData(text,speech,install::DistroOf(e));
    return true;
}
// dossys::MakeProfile's text (the dialogue of dossys::Run needs disks and volumes; its choices are the arguments).
static bool FlowDos(const Vector& v,std::string& out) {
    const std::string id=v.Arg("profile","WIN98");const dossys::Profile* p=nullptr;
    for(const auto& candidate:dossys::profiles)if(candidate.id && id==candidate.id)p=&candidate;
    if(!p)return false;
    const std::string reader=v.Arg("reader","none");std::string readerFile;
    for(const auto& r:dossys::readers9x)if(reader==r.id && r.file)readerFile=r.file;
    if(reader==dossys::readerXp.id)readerFile=dossys::readerXp.file;
    auto& s=dossys::Setup();s.automatic=v.Arg("setup_auto","0")=="1";
    snprintf(s.key,sizeof s.key,"%s",v.Arg("setup_key","").c_str());snprintf(s.name,sizeof s.name,"%s",v.Arg("setup_name","User").c_str());
    snprintf(s.computer,sizeof s.computer,"%s",v.Arg("setup_computer","CHARON").c_str());
    const std::string medium=v.Arg("medium","SETUP.ISO"),ready=v.Arg("ready_format","");
    static char cfg[dossys::profileTextBytes];size_t n=0;cfg[0]=0;
    ApplyPuts(v);   // r410: dos_mode / dos_lang (CHARONPC.CFG lang= follows the CharonDOS language answer)
#ifdef CHARON_HOST_DOS_FLOPPY
    const std::string floppy=v.Arg("floppy","");
    dossys::CfgOverflow()=false;
    dossys::ProfileText(cfg,sizeof cfg,n,*p,reader.c_str(),readerFile.c_str(),medium.empty()?nullptr:medium.c_str(),ready.empty()?nullptr:ready.c_str(),floppy.c_str());
    if(dossys::CfgOverflow())return false;
#else
    dossys::ProfileText(cfg,sizeof cfg,n,*p,reader.c_str(),readerFile.c_str(),medium.empty()?nullptr:medium.c_str(),ready.empty()?nullptr:ready.c_str());
#endif
    out.assign(cfg,n);return true;
}
static int RunVector(const std::string& path,const std::string& outDir) {
    Vector v=ReadVector(path);
    {const size_t slash=path.find_last_of("/\\");v.name=path.substr(slash==std::string::npos?0:slash+1);const size_t dot=v.name.rfind('.');if(dot!=std::string::npos)v.name.resize(dot);}
    U().host->pl=v.polish;ask::Clear();detailed::Begin(0,0);QueueKeys(v.keys);
    static char buffer[262144];install::Text text{buffer,sizeof(buffer),0,false};buffer[0]=0;
    std::string out,note,carriedText;bool ok=false;
    if(v.flow=="debian")ok=FlowLinux(v,0,text);
    else if(v.flow=="ubuntu")ok=FlowLinux(v,1,text);
    else if(v.flow=="windows")ok=FlowWindows(v,text);
    else if(v.flow=="image")ok=FlowImage(v,text,note);
    else if(v.flow=="dos")ok=FlowDos(v,out);
    else if(v.flow=="networkconf") {   // install::NetworkConf: network.conf (the names are those of src/linux/network.sh)
        install::NetworkConf(text,v.Arg("mac","").c_str(),v.Arg("ssid","dom").c_str(),v.Arg("open","0")=="1",v.Arg("key","").c_str());
        ok=!text.overflow;out.assign(buffer,text.size);}
    else if(v.flow=="voiceconf") {   // install::VoiceConf: /var/lib/charon/voice.conf (the names are those of src/linux/speech.sh)
        install::VoiceConf(text,v.Arg("synth","tvox").c_str(),v.Arg("voice","").c_str(),unsigned(strtoul(v.Arg("rate","10").c_str(),nullptr,10)),
            unsigned(strtoul(v.Arg("pitch","10").c_str(),nullptr,10)),unsigned(strtoul(v.Arg("punctuation","1").c_str(),nullptr,10)));
        ok=!text.overflow;out.assign(buffer,text.size);}
    else {printf("error=unknown flow %s\n",v.flow.c_str());return 2;}
    if(v.flow!="dos" && v.flow!="voiceconf" && v.flow!="networkconf")out.assign(buffer,text.size);
    // r410: what carries the detailed answers that differ from the defaults (nothing when all are defaults)
    if(ok && detailed::Carried()) {static char carried[16384];const bool windows=v.flow=="windows";
        const size_t n=windows?detailed::Ini(carried,sizeof(carried)):detailed::Env(carried,sizeof(carried));
        carriedText+=windows?"==== \\charon\\answers.ini ====\n":"==== /var/lib/charon/answers.env ====\n";carriedText.append(carried,n);
        static char set[32768];detailed::MainTables none{};const size_t m=detailed::SetText(none,"host-test",false,set,sizeof(set));
        carriedText+="\n==== a saved answer set (without the system's main questions) ====\n";carriedText.append(set,m);
        // the set loads back into an empty store with the same detailed answers
        static char before[16384];std::string was(carried,n);(void)before;
        const detailed::Context kept=detailed::C();ask::Clear();detailed::C()=kept;detailed::Loaded loaded;set[m]=0;
        const bool good=detailed::LoadSet(none,set,m,loaded);
        const size_t n2=windows?detailed::Ini(carried,sizeof(carried)):detailed::Env(carried,sizeof(carried));
        note+=std::string("set-reload=")+(good && loaded.unknown==0 && loaded.bad==0?"ok":"BAD")+" applied="+std::to_string(loaded.applied)+"\n";(void)n2;}

    std::string answers="result="+std::string(ok?"written":"left")+"\n"+note+"asked=";
    for(size_t i=0;i<askedKeys.size();++i)answers+=(i?",":"")+askedKeys[i];
    answers+="\n";
    {auto& a=ask::A();for(size_t i=0;i<a.count;++i)answers+=std::string(a.keys[i])+"="+ask::Get(a.keys[i])+"\n";}
    if(text.overflow)answers+="overflow=generator\n";
    if(ask::Overflow())answers+=std::string("overflow=store:")+ask::LostKey()+"\n";
    answers+="keys-left="+std::to_string(H().keys.size())+"\n";
    std::string transcript;for(const auto& line:H().said)transcript+=Utf8(line)+"\n";
    WriteFile(outDir+"/"+v.name+".out",ok?out:std::string());
    WriteFile(outDir+"/"+v.name+".answers",answers);
    WriteFile(outDir+"/"+v.name+".carried",carriedText);   // r410: empty when every detailed answer is a default
    WriteFile(outDir+"/"+v.name+".transcript",transcript);
    printf("vector=%s\nresult=%s\nbytes=%zu\n",v.name.c_str(),ok?"written":"left",ok?out.size():size_t(0));
    return 0;
}

// ---- the answer store (defect 2) --------------------------------------------------------
static int failures=0;
static void Check(const char* name,bool ok) {printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;}
static int StoreTests() {
    U().host->pl=false;ask::Clear();
    Check("store: the constants hold the planned dialogue (384 keys, 511-byte values, 32 long values of 4095 bytes)",
        ask::maxAnswers>=384 && ask::valueUnits>=512 && ask::maxLongAnswers>=32 && ask::longUnits>=4096);
    // 1. more keys and longer values than the store of r408 (48 keys of 159 bytes) could take
    std::vector<std::string> keys;for(size_t i=0;i<ask::maxAnswers+1;++i)keys.push_back("key"+std::to_string(i));
    bool all=true;for(size_t i=0;i<ask::maxAnswers;++i)all=ask::Put(keys[i].c_str(),("value"+std::to_string(i)).c_str()) && all;
    Check("store: every one of the maxAnswers keys is stored",all && !ask::Overflow());
    bool same=true;for(size_t i=0;i<ask::maxAnswers;++i)same=same && ("value"+std::to_string(i))==ask::Get(keys[i].c_str());
    Check("store: all of them read back unchanged (the 49th no longer overwrites the 48th)",same);
    Check("store: one key too many is refused",!ask::Put(keys[ask::maxAnswers].c_str(),"x"));
    Check("store: ... and recorded with its key",ask::Overflow() && std::string(ask::LostKey())==keys[ask::maxAnswers]);
    Check("store: ... and the refused key reads as empty, the last slot is intact",!ask::Get(keys[ask::maxAnswers].c_str())[0] && ("value"+std::to_string(ask::maxAnswers-1))==ask::Get(keys[ask::maxAnswers-1].c_str()));
    Check("store: an existing key can still be changed when the table is full",ask::Put(keys[3].c_str(),"changed") && ask::Is(keys[3].c_str(),"changed"));
    {static char16_t before[8];const size_t failed=H().failed.size();Check("store: Complete() says so by voice and returns false",!ask::Complete() && H().failed.size()==failed+1
        && Utf8(H().failed.back()).find(keys[ask::maxAnswers])!=std::string::npos);(void)before;}
    ask::Clear();
    Check("store: Clear() empties the store and the overflow record",!ask::Overflow() && !ask::Get("key0")[0] && ask::A().count==0 && ask::Complete());
    // 2. value lengths
    const std::string short159(159,'a'),short511(ask::valueUnits-1,'b'),long512(ask::valueUnits,'c'),long4095(ask::longUnits-1,'d'),tooLong(ask::longUnits,'e');
    Check("store: 159 bytes (the old limit)",ask::Put("v",short159.c_str()) && short159==ask::Get("v"));
    Check("store: 511 bytes in the key's own slot",ask::Put("v",short511.c_str()) && short511==ask::Get("v") && !ask::A().longOf[0]);
    Check("store: 512 bytes go to a long slot, uncut",ask::Put("v",long512.c_str()) && long512==ask::Get("v") && ask::A().longOf[0]);
    Check("store: 4095 bytes, uncut",ask::Put("v",long4095.c_str()) && long4095==ask::Get("v"));
    Check("store: 4096 bytes are refused, not cut; the previous value stays",!ask::Put("v",tooLong.c_str()) && long4095==ask::Get("v") && ask::Overflow() && std::string(ask::LostKey())=="v");
    ask::Clear();
    Check("store: a long value replaced by a short one frees its long slot",ask::Put("v",long512.c_str()) && ask::Put("v","short") && ask::Is("v","short") && !ask::A().longOf[0] && !ask::A().longUsed[0]);
    ask::Clear();
    std::vector<std::string> longKeys;for(size_t i=0;i<ask::maxLongAnswers+1;++i)longKeys.push_back("long"+std::to_string(i));
    all=true;for(size_t i=0;i<ask::maxLongAnswers;++i)all=ask::Put(longKeys[i].c_str(),(long512+std::to_string(i)).c_str()) && all;
    same=true;for(size_t i=0;i<ask::maxLongAnswers;++i)same=same && (long512+std::to_string(i))==ask::Get(longKeys[i].c_str());
    Check("store: 32 long values at once",all && same && !ask::Overflow());
    Check("store: a 33rd long value is refused and recorded",!ask::Put(longKeys[ask::maxLongAnswers].c_str(),long512.c_str()) && ask::Overflow() && !ask::Get(longKeys[ask::maxLongAnswers].c_str())[0]);
    ask::Clear();
    // 3. check-box values and aliasing
    const std::string list="orca,"+std::string(600,'x')+",speakup";
    Check("store: Has() finds values of a long check-box answer",ask::Put("readers",list.c_str()) && ask::Has("readers","orca") && ask::Has("readers","speakup") && !ask::Has("readers","fenrir"));
    Check("store: a value copied from another key (fullname from user)",ask::Put("user","anna") && ask::Put("fullname",ask::Get("user")) && ask::Is("fullname","anna"));
    Check("store: a key stored onto itself keeps its value",ask::Put("readers",ask::Get("readers")) && list==ask::Get("readers") && ask::Put("user",ask::Get("user")) && ask::Is("user","anna"));
    ask::Clear();
    // 4. the dialogue: an answer that does not fit is said and asked again, never dropped
    {static const ask::Question one[]={{"note",true,ask::Kind::text,u"Notatka:",u"Note:","",nullptr,0,nullptr,nullptr}};
     for(size_t i=0;i<ask::maxLongAnswers;++i)ask::Put(longKeys[i].c_str(),long512.c_str());   // no long slot left
     std::u16string typed(250,u'\u20ac');   // 250 euro signs: 750 bytes of UTF-8
     for(char16_t c:typed)H().keys.push_back(Key{0,c,false,false,false});
     H().keys.push_back(Key{0,13,false,false,false});                         // Enter: does not fit -> said, asked again
     QueueKeys("\"short\" ENTER");
     const size_t failed=H().failed.size();const bool ran=ask::Run(one,1,false);
     Check("dialogue: a typed answer that does not fit is reported by voice and asked again",ran && H().failed.size()==failed+1 && ask::Is("note","short") && !ask::Overflow() && H().keys.empty());
    }
    ask::Clear();
    {static const ask::Question many[ask::maxAnswers+1]={};
     static ask::Question table[ask::maxAnswers+1];for(size_t i=0;i<ask::maxAnswers+1;++i)table[i]={keys[i].c_str(),false,ask::Kind::yesNo,u"p",u"q","no",nullptr,0,nullptr,nullptr};
     const size_t failed=H().failed.size();
     Check("dialogue: a table whose defaults do not fit the store is refused by voice",!ask::Run(table,ask::maxAnswers+1,true) && H().failed.size()==failed+1);(void)many;}
    ask::Clear();
    // 5. a generator's text that does not fit its buffer is flagged (the callers refuse to start)
    {static char small[64];install::Text t{small,sizeof(small),0,false};disks::D().count=1;disks::D().list[0]={1000,512};install::Chosen().disk=0;
     ask::Put("locale","en_US");install::DebianPreseed(t);Check("generator: Text reports overflow instead of writing past its buffer",t.overflow && t.size<sizeof(small));}
    ask::Clear();
    printf("failures=%d\n",failures);
    return failures?1:0;
}

// ---- copies of \EFI\HAK (defect 1) ---------------------------------------------------------
static void ListTree(const std::filesystem::path& root,const std::filesystem::path& at,std::vector<std::string>& out) {
    std::error_code error;
    for(const auto& e:std::filesystem::directory_iterator(at,error)) {
        if(e.is_directory(error))ListTree(root,e.path(),out);
        else out.push_back(std::filesystem::relative(e.path(),root,error).generic_string());
    }
}
static int CopyTests(const std::string& source,const std::string& work) {
    static HostVolume from{source},to{work+"/copytree-volume"},measure{source};
    std::filesystem::create_directories(to.root);
    U().host->selfDevice=&from;
    // 1. CopyCharonTree: Windows media, ready images, CharonDOS (CharonWsInstall2.h, CharonWsImages.h)
    {static FileRef a,b;a.volume=&from;Set(a.path,pathUnits,u"\\EFI\\HAK");b.volume=&to;Set(b.path,pathUnits,u"\\charon\\EFI\\HAK");size_t copied=0;
     MakeFolders(&to,u"\\charon\\EFI");
     const efi_status status=CopyCharonTree(a,b,&copied);
     printf("copytree-status=%llu\ncopytree-files=%zu\n",(unsigned long long)status,copied);
     std::vector<std::string> names;ListTree(to.root/"charon",to.root/"charon",names);std::sort(names.begin(),names.end());
     for(const auto& n:names)printf("copytree:%s\n",n.c_str());}
    // 2. HakTree: the cpio archive of a Debian installation (CharonWsInstall.h)
    {static uint8_t archive[1<<20];crypt::Cpio cpio{archive,sizeof(archive),0,0,false};uint64_t bytes=0;size_t files=0;
     const bool ok=install::HakTree(&cpio,bytes,files);cpio.End();
     printf("cpio-ok=%d\ncpio-files=%zu\ncpio-overflow=%d\n",ok?1:0,files,cpio.overflow?1:0);
     std::vector<std::string> names;
     for(size_t at=0;at+110<=cpio.size;) {   // newc: 110 bytes of header, name, data, each padded to 4
         auto hex=[&](size_t field) {return size_t(strtoul(std::string(reinterpret_cast<const char*>(archive+at+6+field*8),8).c_str(),nullptr,16));};
         const size_t mode=hex(1),fileSize=hex(6),nameSize=hex(11);const std::string name(reinterpret_cast<const char*>(archive+at+110),nameSize?nameSize-1:0);
         if(name=="TRAILER!!!")break;
         if((mode&0170000)==0100000)names.push_back(name);
         at+=(110+nameSize+3)/4*4;at+=(fileSize+3)/4*4;
     }
     std::sort(names.begin(),names.end());for(const auto& n:names)printf("cpio:%s\n",n.c_str());
     uint64_t measured=0;size_t measuredFiles=0;install::HakTree(nullptr,measured,measuredFiles);printf("cpio-measured-files=%zu\n",measuredFiles);}
    // 3. selfcopy::List: Charon's copy in memory, from which "Install Charon on this computer" and the volume in memory are made
    {efi_file_protocol* root=OpenRoot(&measure);const bool ok=selfcopy::List(U().host->boot,root);root->Close(root);
     printf("selfcopy-ok=%d\n",ok?1:0);std::vector<std::string> names;
     for(size_t i=0;i<selfcopy::S().count;++i) {std::string n=Utf8(selfcopy::S().items[i].path);std::replace(n.begin(),n.end(),'\\','/');names.push_back(n.substr(1));}
     std::sort(names.begin(),names.end());for(const auto& n:names)printf("selfcopy:%s\n",n.c_str());
     // "Install Charon on this computer" writes the items its Wanted() accepts (CharonWsSelfInstall.h, cut out unchanged)
     std::vector<std::string> given;
     for(size_t i=0;i<selfcopy::S().count;++i)if(selfinstall::Wanted(selfcopy::S().items[i],true,true)) {std::string n=Utf8(selfcopy::S().items[i].path);std::replace(n.begin(),n.end(),'\\','/');given.push_back(n.substr(1));}
     std::sort(given.begin(),given.end());for(const auto& n:given)printf("selfinstall:%s\n",n.c_str());}
    // 3c. self-install short of room (item 7): the further voice packs are the first to be left out
    {auto pack=[](const char* label,const char16_t* path) {selfcopy::Item item{};Set(item.path,selfcopy::pathUnits,path);
         printf("voicepack:%s=%s\n",label,selfinstall::Wanted(item,true,true)?"kept":"left-out");};
     snprintf(selfinstall::KeptVoice(),64,"%s","rhvoice-magda");selfinstall::FurtherVoices()=false;
     pack("rhvoice-pl.chsp",u"\\EFI\\HAK\\synths\\rhvoice\\rhvoice-pl.chsp");pack("RHVOICE-EN.CHSP",u"\\EFI\\HAK\\SYNTHS\\RHVOICE\\RHVOICE-EN.CHSP");
     pack("rhvoice-magda.chsp",u"\\EFI\\HAK\\synths\\rhvoice\\rhvoice-magda.chsp");pack("rhvoice-natan.chsp",u"\\EFI\\HAK\\synths\\rhvoice\\rhvoice-natan.chsp");
     pack("rhvoice-slt.chsp",u"\\EFI\\HAK\\synths\\rhvoice\\rhvoice-slt.chsp");pack("rhvoice.efi",u"\\EFI\\HAK\\synths\\rhvoice\\rhvoice.efi");
     pack("rhvoice-voices.tsv",u"\\EFI\\HAK\\synths\\rhvoice\\rhvoice-voices.tsv");pack("espeak-ng.chsp",u"\\EFI\\HAK\\synths\\espeak\\espeak-ng.chsp");
     selfinstall::FurtherVoices()=true;pack("all-wanted:rhvoice-natan.chsp",u"\\EFI\\HAK\\synths\\rhvoice\\rhvoice-natan.chsp");}
    // 3b. CarryCharonTree: Charon written back to its own disk after that disk was erased whole
    {static HostVolume home{work+"/carry-volume"};std::filesystem::create_directories(home.root);
     static FileRef a,b;a.volume=&from;Set(a.path,pathUnits,u"\\EFI\\HAK");b.volume=&home;Set(b.path,pathUnits,u"\\EFI\\HAK");size_t copied=0;
     MakeFolders(&home,u"\\EFI");printf("carry-status=%llu\n",(unsigned long long)CarryCharonTree(a,b,&copied));
     std::vector<std::string> names;ListTree(home.root,home.root,names);std::sort(names.begin(),names.end());
     for(const auto& n:names)printf("carry:%s\n",n.c_str());}
    // 4. the list itself, and its shell form (the Ubuntu installer copies \EFI\HAK with a shell command)
    for(const char* name:hakprivate::names)printf("private:%s\n",name);
    {std::string command;hakprivate::ShellRemove("@HAK@",[&](const char* piece) {command+=piece;});printf("shell-remove=%s\n",command.c_str());}
    return 0;
}

int main(int argc,char** argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    const std::string command=argc>1?argv[1]:"";
    if(command=="run" && argc==4)return RunVector(argv[2],argv[3]);
    if(command=="store")return StoreTests();
    if(command=="copy" && argc==4)return CopyTests(argv[2],argv[3]);
    printf("usage: run VECTOR OUTDIR | store | copy VOLUME WORKDIR\n");
    return 2;
}
