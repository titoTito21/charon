// r407 host test: secret typed input (VeraCrypt's password and PIM).
// Drives the real resident reader with the console writes VeraCrypt's loader
// makes (VeraCrypt-DCS Library/PasswordLib/ConsolePassword.c AskConsolePwdInt,
// Library/CommonLib/EfiConsole.c GetLineWithProgress / KeyWait / DcsMenuPrint)
// and checks everything the reader says, remembers or would braille.
//   clang++ -std=c++17 -fuse-ld=lld -I src/efi -I <dir holding src/efi/haiku as "efi">
//       -I tools/charon-compat-include tests/CharonSecretInputTests.cpp -o t.exe
#define private public
#include "../src/efi/CharonResident.h"
#undef private
#include <cstdio>
#include <string>
#include <vector>
namespace hakefi {
inline void Utf8(const char16_t* text,char* out,size_t capacity) {
    size_t o=0;
    for(;*text && o+4<capacity;++text) {
        const unsigned c=*text;
        if(c<0x80)out[o++]=char(c);
        else if(c<0x800) {out[o++]=char(0xc0|(c>>6));out[o++]=char(0x80|(c&63));}
        else {out[o++]=char(0xe0|(c>>12));out[o++]=char(0x80|((c>>6)&63));out[o++]=char(0x80|(c&63));}
    }
    if(capacity)out[o]=0;
}
}
using namespace hakefi;
static int failures=0;
static std::vector<std::string> said;
static std::string Row(size_t r);
static void Check(bool ok,const char* what) {
    std::printf("%s %s\n",ok?"PASS":"FAIL",what);if(ok)return;
    ++failures;
    for(auto& s:said)std::printf("    said: [%s]\n",s.c_str());
    for(size_t i=0;i<25;++i)if(!Row(i).empty())std::printf("    row %d: [%s]\n",int(i),Row(i).c_str());
}
static efi_status EFIAPI FakeSpeak(MenuVoice*,const char*,const char* text) {if(text[0] && std::string(text)!=" ")said.push_back(text);return EFI_SUCCESS;}
static efi_status EFIAPI FakePump(MenuVoice*,uint32_t) {return EFI_SUCCESS;}
static efi_status EFIAPI FakeStop(MenuVoice*) {return EFI_SUCCESS;}
static MenuVoice voice={1,sizeof(MenuVoice),FakeSpeak,FakePump,FakeStop};
static ResidentReader* reader=new ResidentReader();
static void Start(const char16_t* manager) {
    auto& r=*reader;
    r.options_=ResidentOptions{};r.options_.voice=&voice;
    r.rows_=25;r.columns_=80;r.ResetShadow();r.bootManager_=manager;r.tick_+=1000;
    r.historyCount_=0;r.historyNext_=0;said.clear();
}
// The reader's timer after the program went quiet.
static void Quiet() {
    auto& r=*reader;r.tick_+=10;
    if(r.anyDirty_)r.Flush();else r.CursorEcho();
}
// DCS prints through edk2's print library, which writes "\n" as "\r\n".
static void Out(const char16_t* text) {
    std::u16string s;
    for(;*text;++text) {if(*text==u'\n' && (s.empty() || s.back()!=u'\r'))s+=u'\r';s+=*text;}
    reader->Capture(s.c_str());
}
static void Key(uint16_t scan,char16_t ch) {Quiet();reader->NoteKey(scan,ch);}
// DCS: one accepted character, echoed as '*' (progress) or as itself (show).
static void Type(char16_t ch,bool show) {Key(0,ch);const char16_t one[2]={show?ch:u'*',0};Out(one);Quiet();}
static std::string Row(size_t r) {std::string s;for(size_t c=0;c<80;++c)s+=char(reader->text_[r][c]<128?reader->text_[r][c]:'?');while(!s.empty() && s.back()==' ')s.pop_back();return s;}
static bool AnySaid(const char* part) {for(auto& s:said)if(s.find(part)!=std::string::npos)return true;return false;}
static bool History(const char* part) {
    auto& r=*reader;
    for(size_t i=0;i<r.historyCount_;++i) {char t[1200];Utf8(r.HistoryEntry(i),t,sizeof(t));if(std::string(t).find(part)!=std::string::npos)return true;}
    return false;
}
static bool Screen(const char* part) {for(size_t r=0;r<25;++r)if(Row(r).find(part)!=std::string::npos)return true;return false;}
static const char* ScreenName() {
    static char t[200];auto& r=*reader;r.FindScreenKeys();
    if(!r.screenName_)return "";
    Utf8(r.screenName_->en,t,sizeof(t));return t;
}
int main() {
    auto& r=*reader;
    // ---- prompts ----------------------------------------------------------
    {using secretinput::OpenPrompt;
     auto open=[](const char16_t* s) {size_t n=0;while(s[n])++n;return OpenPrompt(s,n);};
     Check(!open(u"Password: "),"Password: is secret");
     Check(!open(u"PIM (Leave empty for default): ") && !open(u"Pim:"),"PIM prompts are secret");
     Check(!open(u"Enter the phrase for Anna> "),"an unknown prompt is secret");
     Check(!open(u""),"an empty prompt is secret");
     Check(!open(u"New password:") && !open(u"Confirm password:") && !open(u"TPM owner password:"),"other password prompts are secret");
     Check(!open(u"[x] New password:") && !open(u"Change password[N]?"),"a word for a secret wins over the open forms");
     Check(open(u"[dmzckbrvowshe]:"),"the rescue disk's menu selector is open");
     Check(open(u"True crypt mode [N]?") && open(u"Do you want to continue? [N]"),"yes/no questions are open");
     Check(open(u"Hash:") && open(u"[0] Select EFI boot volume:"),"hash and volume choices are open");
     Check(secretinput::PublicNotice(u" TPM locked!") && !secretinput::PublicNotice(u" TPM locked") && !secretinput::PublicNotice(u"hunter2"),"only DCS's own notices are public");}
    // ---- VeraCrypt password, default configuration (stars) ----------------
    Start(u"VeraCrypt");
    Out(u"Password: ");Quiet();
    Check(AnySaid("Password"),"the password prompt is read");
    Check(std::string(ScreenName())=="VeraCrypt: password prompt","key help names the password prompt");
    said.clear();
    Type(u'S',false);
    Check(said.size()==1 && said[0]=="star","one typed character: star, nothing else");
    Type(u'e',false);Type(u'k',false);
    Check(said.size()==3 && said[2]=="star","three characters: three stars");
    Key(0,8);Out(u"\b \b");Quiet();
    Check(said.size()==4 && said[3]=="deleted","Backspace: deleted");
    Type(u'c',false);
    // ---- F5 shows the password: DCS backspaces over the stars and prints it
    said.clear();
    Key(0x0f,0);Out(u"\b");Out(u"\b");Out(u"\b");Out(u"Sec");Quiet();
    Check(said.empty(),"F5 (show): nothing is read by itself");
    Check(Row(0)=="Password: ***","the screen model keeps stars for the shown password");
    Type(u'r',true);Type(u'3',true);Type(u't',true);
    Check(said.size()==3 && said[0]=="star" && said[2]=="star","typing while shown: stars only");
    Check(Row(0)=="Password: ******" && !Screen("Secr3t"),"shown characters never enter the screen model");
    // automatic and logged paths
    r.SayChanges();
    Check(!AnySaid("Sec") && !AnySaid("r3t"),"what changed (Insert+D) does not say the password");
    {r.reviewRow_=0;r.reviewCol_=0;r.markRow_=0;r.markCol_=0;r.reviewCol_=20;r.SpeakupCommand(u'C');}
    {char clip[600];Utf8(r.clip_,clip,sizeof(clip));Check(std::string(clip).find("Sec")==std::string::npos && std::string(clip).find("r3t")==std::string::npos,"the clipboard copy holds stars");}
    for(size_t i=0;i<25;++i)r.dirty_[i]=true;r.anyDirty_=true;for(size_t c=0;c<80;++c)r.prevRaw_[0][c]=0;
    Quiet();
    Check(!AnySaid("Sec") && !AnySaid("r3t"),"reading the whole screen again does not say the password");
    Check(!History("Sec") && !History("r3t"),"the speech history holds no password characters");
    // an explicit review command reads the shown password, and logs nothing
    said.clear();r.reviewRow_=0;r.reviewCol_=0;r.lastCommandKey_=0;
    r.Command(u'i');
    Check(AnySaid("Secr3t"),"review of the line (explicit) reads the shown password");
    Check(Row(0)=="Password: ******","the screen model is masked again after the review command");
    Check(!History("Secr3t") && !History("r3t"),"the reviewed password is not put into the speech history");
    // ---- notices at the caret stay readable -------------------------------
    said.clear();
    Key(0x12,0);Out(u" TPM locked!");Quiet();
    Check(AnySaid("locked"),"F8 notice is read");
    for(int i=0;i<12;++i)Out(u"\b \b");
    Quiet();
    Check(Row(0)=="Password: ******","the notice is removed, the stars stay");
    // ---- Enter: DCS removes the echo, prints one star and a new line -------
    said.clear();
    Key(0,13);for(int i=0;i<6;++i)Out(u"\b \b");Out(u"*");Out(u"\n");
    Out(u"PIM (Leave empty for default): ");Quiet();
    Check(AnySaid("PIM") && !AnySaid("Secr") ,"after Enter the PIM prompt is read, the password row is not");
    Check(std::string(ScreenName())=="VeraCrypt: PIM prompt","key help names the PIM prompt");
    said.clear();
    Type(u'4',true);Type(u'8',true);Type(u'5',true);
    Check(said.size()==3 && said[0]=="star" && said[1]=="star" && said[2]=="star","PIM digits shown on the screen: stars only");
    Check(Row(1)=="PIM (Leave empty for default): ***","the PIM row holds stars");
    Key(0,13);Out(u"\n");Out(u"Authorizing...\n\r");Quiet();
    Out(u"Authorization failed. Wrong password, PIM or hash.\n\r");Out(u"Password: ");Quiet();
    Check(AnySaid("Authorizing") && AnySaid("Authorization failed"),"Authorizing and the failure message are read");
    Check(!AnySaid("485") && !History("485"),"the PIM is neither said nor in the history");
    Check(std::string(ScreenName())=="VeraCrypt: authorization failed, the password prompt again","key help names the failure");
    // ---- a user's own PasswordMsg ------------------------------------------
    Start(u"VeraCrypt");
    Out(u"Tell me, friend> ");Quiet();said.clear();
    Type(u'x',true);
    Check(said.size()==1 && said[0]=="star" && Row(0)=="Tell me, friend> *","a custom prompt is secret too");
    // ---- open prompts of the loader ---------------------------------------
    Start(u"VeraCrypt");
    Out(u"(0) TEST ALL (1) SHA512 (2) WHIRLPOOL (3) SHA256 (4) BLAKE2S (5) STREEBOG\r\nHash: ");Quiet();said.clear();
    Type(u'3',true);
    Check(Row(1)=="Hash: 3" && AnySaid("3"),"the hash choice is not secret");
    Check(std::string(ScreenName())=="VeraCrypt: hash algorithm choice","key help names the hash prompt");
    Start(u"VeraCrypt Rescue Disk");
    Out(u"d) Decrypt OS\n");Out(u"m) Restore VeraCrypt loader to boot menu\n");Out(u"h) Help\n");Out(u"[dmh]:");Quiet();said.clear();
    Key(0,u'h');Out(u"h\n");Out(u"Rescue disk for VeraCrypt OS encryption\n\r");Quiet();
    Check(Row(3)=="[dmh]:h" && AnySaid("Rescue disk for VeraCrypt"),"the rescue disk menu letter and its answer are not masked");
    Check(std::string(ScreenName())=="VeraCrypt Rescue Disk menu","key help names the rescue disk menu");
    // KeyWait prints its countdown again after a key: program output, not an echo
    Start(u"VeraCrypt");
    Out(u"Press 'c' to configure, others to skip 9\r");Quiet();
    Key(0,u'x');Out(u"Press 'c' to configure, others to skip 9\r");Quiet();
    Check(Row(0)=="Press 'c' to configure, others to skip 9","a countdown printed again after a key is not masked");
    // ---- other programs are not affected ----------------------------------
    Start(u"GRUB");
    Out(u"grub> ");Quiet();said.clear();
    Type(u'l',true);
    Check(Row(0)=="grub> l" && !r.secret_.active,"GRUB's command line is not masked");
    Check(!managers::Find(u"GRUB")->secretInput && managers::Find(u"VeraCrypt")->secretInput && managers::Find(u"VeraCrypt Rescue Disk")->secretInput,"only VeraCrypt's profiles are secret-input");
    // ---- a screen that scrolls keeps the mask with its row -----------------
    Start(u"VeraCrypt");
    for(int i=0;i<24;++i)Out(u"line\n");
    Out(u"Password: ");Quiet();Type(u'a',true);Type(u'b',true);
    Check(Row(24)=="Password: **","entry on the last row");
    Key(0,13);Out(u"\b \b");Out(u"\b \b");Out(u"*");Out(u"\n");
    Check(Row(23)=="Password: *" && r.secret_.marks==1 && r.secret_.clear[23][10]==u'*',"the mask scrolled up with its row");
    // ---- profile files -----------------------------------------------------
    {managers::UserProfile u;
     const char text[]="name=MyCrypt\nsignature=MyCrypt loader\nsecretinput=yes\n";
     Check(managers::ParseUserProfile(text,sizeof(text)-1,u) && u.profile.secretInput,"secretinput=yes in a profile file");
     const char plain[]="name=MyLoader\nsignature=MyLoader v1\n";
     Check(managers::ParseUserProfile(plain,sizeof(plain)-1,u) && !u.profile.secretInput,"profiles are not secret by default");}
    std::printf(failures?"FAILED %d\n":"ALL PASSED\n",failures);
    return failures?1:0;
}
