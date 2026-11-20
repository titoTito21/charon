#pragma once
// r400: Charon workspace ("Środowisko Charona") - a small speaking environment
// in the spirit of Emacs with Emacspeak: buffers, a speaking text editor, a
// Dired-style file manager, a launcher for UEFI programs, packages, a command
// palette (M-x) and small utilities. This header holds what every view
// shares: the host interface, keys, the screen, speech helpers, prompts and
// file helpers. Nothing here writes firmware variables. GPL-2.0-or-later.
#include <stddef.h>
#include <stdint.h>
#include "BootMenuVoice.h"
#include "CharonCues.h"
#include "CharonKeyNotify.h"
#include "CharonMedia.h"
#include "CharonSpeechText.h"
#include "CharonSystemTools.h"
#include "CharonMessageMode.h"
#include "CharonProgress.h"
#include "CharonMenuVoiceMark.h"
namespace hakefi {
namespace ws {
// What the boot menu lends to the workspace.
struct Host {
    efi_system_table* system;efi_boot_services* boot;
    efi_file_protocol* root;       // Charon's volume (favourites, notes, packages)
    efi_handle selfDevice;efi_handle image;   // Charon's image handle (parent of loaded drivers)
    MenuVoice* voice;AudioEffects* effects;
    bool pl,sounds,positions;
    void* context;
    void (*say)(void*,const char16_t*);   // speaks now, cutting earlier speech
    // Starts a program from an in-memory load option with the resident reader;
    // returns when the program returns; `report` says how it ended.
    void (*launch)(void*,const char16_t* name,const uint8_t* option,size_t size,char16_t* report,size_t capacity);
    void (*synthsChanged)(void*);          // packages installed or removed synthesizers
    bool (*ensureReader)(void*);           // turns the firmware reader add-on (Driver####) on; false: it could not
    // r401: the progress setting of the boot manager (0 off, 1 beeps, 2 speech
    // every 10%, 3 both) and the explicit mode, for Progress() below.
    uint8_t progress;bool explicitMessages;
    uint8_t formatFlags;                   // the formatting settings: bit 0 voice, bit 1 sound icons (CharonFormatting.h)
    void (*sayAuthored)(void*,const char16_t*);   // speaks a finished phrase as it is (no restyling)
    // Screen review inside Charon's own dialogues: the screen reader of the boot
    // manager is switched on silent, serving only its review keys. False: not available.
    bool (*review)(void*,bool on);
};
// Where the boot menu should go when the workspace closes.
enum class Leave : uint8_t {menu,devices,order,settings,firmware,shell,tools,continueBoot,restart,shutdown};
struct Key {uint16_t scan;char16_t ch;bool ctrl,alt,shift;};
constexpr uint16_t scUp=1,scDown=2,scRight=3,scLeft=4,scHome=5,scEnd=6,scInsert=7,scDelete=8,scPageUp=9,scPageDown=10,
    scF1=11,scF2=12,scF3=13,scF4=14,scF5=15,scF6=16,scF7=17,scF8=18,scF9=19,scF10=20,scEsc=23;
inline size_t Len(const char16_t* s) {size_t n=0;while(s && s[n])++n;return n;}
inline void Set(char16_t* out,size_t cap,const char16_t* text) {if(cap) {out[0]=0;systools::Put(out,cap,text);}}
inline void Cat(char16_t* out,size_t cap,const char16_t* text) {systools::Put(out,cap,text);}
inline void CatN(char16_t* out,size_t cap,uint64_t value) {systools::PutNumber(out,cap,value);}
inline void CatC(char16_t* out,size_t cap,char16_t c) {char16_t t[2]={c,0};systools::Put(out,cap,t);}
inline void CatAscii(char16_t* out,size_t cap,const char* text) {for(;text && *text;++text)CatC(out,cap,(char16_t)(unsigned char)*text);}
// Case and Polish diacritics folded, for matching names typed by the user.
inline char16_t Fold(char16_t c) {
    c=speechtext::LowerCase(c);
    switch(c) {case u'ą':return u'a';case u'ć':return u'c';case u'ę':return u'e';case u'ł':return u'l';case u'ń':return u'n';
        case u'ó':return u'o';case u'ś':return u's';case u'ź':case u'ż':return u'z';}
    return c;
}
inline bool SameFold(const char16_t* a,const char16_t* b) {
    for(size_t i=0;;++i) {if(Fold(a[i])!=Fold(b[i]))return false;if(!a[i])return true;}
}
inline bool StartsFold(const char16_t* text,const char16_t* prefix) {
    for(size_t i=0;prefix[i];++i)if(Fold(text[i])!=Fold(prefix[i]))return false;
    return true;
}
inline bool ContainsFold(const char16_t* text,const char16_t* part) {
    if(!part[0])return true;
    for(size_t i=0;text[i];++i)if(StartsFold(text+i,part))return true;
    return false;
}
inline bool EndsFold(const char16_t* text,const char16_t* suffix) {
    const size_t n=Len(text),m=Len(suffix);
    return n>=m && SameFold(text+n-m,suffix);
}
inline int CompareFold(const char16_t* a,const char16_t* b) {
    for(size_t i=0;;++i) {const char16_t x=Fold(a[i]),y=Fold(b[i]);if(x!=y || !x)return int(x)-int(y);}
}
struct Ui {
    Host* host;efi_simple_text_output_protocol* out;TextInputEx* ex;efi_event timer;
    size_t cols,rows;bool audioFailed;
    uint32_t shadow[64];      // what each row shows (hash), to redraw changes only
    Leave leave;bool quit;
    bool review;                          // r401: the reader is on for review; it pumps the voice from its timer
    int lastPercent;const char16_t* progressWhat;uint64_t progressDone;
    efi_event backPump;                   // r401: a timer that keeps speech flowing during long work (see BackgroundPump)
};
inline Ui& U() {static Ui ui;return ui;}
inline bool Pl() {return U().host->pl;}
inline const char16_t* T(const char16_t* pl,const char16_t* en) {return U().host->pl?pl:en;}
// r401: while the reader serves the review keys its timer (TPL_CALLBACK) uses
// the same voice; Charon's own calls into the voice must not be interrupted by it.
struct VoiceGuard {
    efi_tpl old=0;bool raised=false;
    VoiceGuard() {if(U().review || U().backPump) {old=U().host->boot->RaiseTPL(TPL_NOTIFY);raised=true;}}
    ~VoiceGuard() {if(raised)U().host->boot->RestoreTPL(old);}
};
inline void Shown(const char16_t* text);
// r401: speech used to break up ("cut mercilessly") whenever Charon was busy
// for long - an encrypted connection being set up, a request to a server, a
// write to a slow stick - because the sound card is fed only when Charon gets
// round to it. While this is on, a timer feeds it every 20 ms whatever Charon
// is doing; Charon's own calls into the voice are then made at a raised
// priority (VoiceGuard) so the two never interleave.
inline void EFIAPI BackPumpNotify(efi_event,void*) {
    auto& u=U();
    if(VoiceSwitching())return;   // a synthesizer is being changed: its module may be half loaded
    if(u.host && !u.audioFailed && u.host->voice)u.host->voice->Pump(u.host->voice,480);
}
inline void BackgroundPump(bool on) {
    auto& u=U();auto* boot=u.host->boot;
    if(on && !u.backPump) {
        efi_event event=nullptr;
        if(boot->CreateEvent(EVT_TIMER|EVT_NOTIFY_SIGNAL,TPL_CALLBACK,BackPumpNotify,nullptr,&event)!=EFI_SUCCESS || !event)return;
        if(boot->SetTimer(event,TimerPeriodic,200000)!=EFI_SUCCESS) {boot->CloseEvent(event);return;}
        u.backPump=event;
    } else if(!on && u.backPump) {
        const efi_event event=u.backPump;u.backPump=nullptr;
        boot->SetTimer(event,TimerCancel,0);boot->CloseEvent(event);
    }
}
inline void Pump() {
    auto& u=U();VoiceGuard guard;
    if(!u.audioFailed && u.host->voice && u.host->voice->Pump(u.host->voice,480)!=EFI_SUCCESS)u.audioFailed=true;
}
inline void Say(const char16_t* text) {if(text && text[0]) {Shown(text);VoiceGuard guard;U().host->say(U().host->context,text);}}
inline void Tone(unsigned hertz,unsigned ms) {
    auto* e=U().host->effects;
    if(U().host->sounds && e && e->Tone) {VoiceGuard guard;e->Tone(e->context,hertz,ms);}
}
inline void Cue(MenuCue cue) {
    auto* e=U().host->effects;
    if(U().host->sounds && e && e->QueueCue) {VoiceGuard guard;e->QueueCue(e->context,cue);}
}
// Auditory icons (oscillator tones, no files), as Emacspeak marks events.
enum class Icon {edge,open,close,mark,save,deleted,error,yank,search,done,task};
inline void IconTone(unsigned hertz,unsigned ms) {
    auto* e=U().host->effects;
    if((U().host->sounds || (U().host->formatFlags&2)) && e && e->Tone) {VoiceGuard guard;e->Tone(e->context,hertz,ms);}
}
// r401: the voice marker of a list position, when voice formatting is on.
inline void PositionVoice(char16_t* out,size_t cap,size_t index) {
    if(!(U().host->formatFlags&1) || cap<menuvoice::markUnits+1)return;
    const size_t w=menuvoice::PositionMark(index,out);out[w]=0;
}
inline void Play(Icon icon) {
#define Tone IconTone
    switch(icon) {
        case Icon::edge:Tone(220,60);break;case Icon::open:Tone(660,40);break;case Icon::close:Tone(440,40);break;
        case Icon::mark:Tone(1200,30);break;case Icon::save:Tone(880,90);break;case Icon::deleted:Tone(330,40);break;
        case Icon::error:Tone(160,180);break;case Icon::yank:Tone(990,40);break;case Icon::search:Tone(1500,25);break;
        case Icon::done:Tone(1040,120);break;case Icon::task:Tone(520,20);break;
    }
#undef Tone
}
inline void Fail(const char16_t* text,efi_status status=EFI_SUCCESS) {
    char16_t line[400];Set(line,400,text);
    if(status!=EFI_SUCCESS) {
        Cat(line,400,T(u" Kod błędu ",u" Error code "));CatN(line,400,uint64_t(status)&0xffff);Cat(line,400,u".");
    }
    Play(Icon::error);Say(line);
}
// One screen row, padded; rows that did not change are not written again.
inline void Row(size_t row,const char16_t* text,size_t attribute=7) {
    auto& u=U();if(row>=u.rows || !u.out)return;
    static char16_t line[260];size_t limit=u.cols-1;if(limit>258)limit=258;
    size_t n=0;uint32_t hash=2166136261u^uint32_t(attribute);
    while(n<limit && text && text[n]) {char16_t c=text[n];if(c<32 || c==127)c=u' ';line[n++]=c;}
    if(n && line[n-1]>=0xd800 && line[n-1]<=0xdbff)--n;
    while(n<limit)line[n++]=u' ';
    line[n]=0;
    for(size_t i=0;i<n;++i)hash=(hash^line[i])*16777619u;
    if(row<64) {if(u.shadow[row]==hash)return;u.shadow[row]=hash;}
    u.out->SetAttribute(u.out,attribute);u.out->SetCursorPosition(u.out,0,row);u.out->OutputString(u.out,line);
}
inline void Forget() {for(auto& h:U().shadow)h=0;}
inline void Reshape() {
    auto& u=U();u.cols=80;u.rows=25;
    if(u.out && u.out->Mode && u.out->QueryMode)u.out->QueryMode(u.out,u.out->Mode->Mode,&u.cols,&u.rows);
    if(u.cols<20)u.cols=20;
    if(u.rows<8)u.rows=8;
    if(u.rows>64)u.rows=64;
    Forget();
    if(u.out) {u.out->SetAttribute(u.out,7);u.out->ClearScreen(u.out);}
}
// Title, body rows 1..rows-3, the mode line and the echo line (Emacs layout).
inline size_t BodyRows() {return U().rows-3;}
inline void Title(const char16_t* text) {Row(0,text,0x0f);}
inline void ModeLine(const char16_t* text) {Row(U().rows-2,text,0x70);}
inline void Echo(const char16_t* text) {Row(U().rows-1,text,7);}
// r401: a dialogue that only spoke now also stands on the screen: everything
// Charon says is written into the body rows (wrapped, scrolling), so a sighted
// helper can read it and the review keys have text to review.
struct Transcript {bool on;size_t count;char16_t lines[48][200];};
inline Transcript& Script() {static Transcript t;return t;}
inline void ScriptDraw() {
    auto& t=Script();const size_t body=BodyRows();const size_t first=t.count>body?t.count-body:0;
    for(size_t r=0;r<body;++r)Row(1+r,first+r<t.count?t.lines[first+r]:u"",7);
}
inline void Shown(const char16_t* text) {
    auto& t=Script();if(!t.on || !text)return;
    static char16_t plain[1700];
    {size_t k=0;for(size_t i=0;text[i] && k+1<1700;++i) {
        if(text[i]==0x001b && text[i+1]==u'f') {size_t skip=2;while(skip<14 && text[i+skip])++skip;i+=skip-1;if(text[i+1]==u' ')++i;continue;}
        plain[k++]=text[i];}
     plain[k]=0;text=plain;}
    size_t width=U().cols>2?U().cols-2:20;if(width>198)width=198;
    size_t at=0;const size_t n=Len(text);
    while(at<n) {
        while(at<n && text[at]==u' ')++at;
        if(at>=n)break;
        size_t end=at+width;
        if(end>=n)end=n;
        else {size_t cut=end;while(cut>at && text[cut]!=u' ')--cut;if(cut>at)end=cut;}
        if(t.count==48) {for(size_t i=1;i<48;++i)for(size_t k=0;k<200;++k)t.lines[i-1][k]=t.lines[i][k];t.count=47;}
        size_t k=0;for(size_t i=at;i<end && k<199;++i)t.lines[t.count][k++]=text[i];
        t.lines[t.count][k]=0;++t.count;at=end;
    }
    ScriptDraw();
}
inline void ScriptBegin(const char16_t* title) {
    auto& t=Script();t.on=true;t.count=0;Forget();
    if(U().out) {U().out->SetAttribute(U().out,7);U().out->ClearScreen(U().out);}
    BackgroundPump(true);
    Title(title);ModeLine(T(u" Escape cofa. F1 powtarza pytanie. Klawisze przeglądu czytnika czytają ten ekran.",u" Escape goes back. F1 repeats the question. The reader's review keys read this screen."));
}
inline void ScriptEnd() {Script().on=false;BackgroundPump(false);}
// Screen review by the boot manager's reader (see Host::review).
inline void ReviewMode(bool on) {
    auto& u=U();if(!u.host->review || on==u.review)return;
    if(on)u.review=u.host->review(u.host->context,true);
    else {u.host->review(u.host->context,false);u.review=false;}
}
inline void Cursor(bool visible,size_t column=0,size_t row=0) {
    auto& u=U();if(!u.out)return;
    if(visible) {u.out->SetAttribute(u.out,7);u.out->SetCursorPosition(u.out,column<u.cols-1?column:u.cols-2,row);}
    if(u.out->EnableCursor)u.out->EnableCursor(u.out,visible);
}
// Waits for a key; speech keeps flowing meanwhile.
inline Key WaitKey() {
    auto& u=U();auto* boot=u.host->boot;auto* input=u.host->system->ConIn;
    for(;;) {
        Key key{};bool got=false;
        if(u.ex && u.ex->ReadKeyStrokeEx) {
            KeyData data{};
            if(u.ex->ReadKeyStrokeEx(u.ex,&data)==EFI_SUCCESS) {
                key.scan=data.key.ScanCode;key.ch=data.key.UnicodeChar;got=true;
                if(data.state.shift&0x80000000u) {
                    key.ctrl=(data.state.shift&0x0c)!=0;key.alt=(data.state.shift&0x30)!=0;key.shift=(data.state.shift&0x03)!=0;}
            }
        } else {
            efi_input_key plain{};
            if(input->ReadKeyStroke(input,&plain)==EFI_SUCCESS) {key.scan=plain.ScanCode;key.ch=plain.UnicodeChar;got=true;}
        }
        if(got) {
            if(!key.scan && !key.ch)continue;   // a modifier alone
            // Control characters: Ctrl+letter arrives as 1..26 on most firmware.
            if(!key.scan && key.ch<32 && key.ch!=8 && key.ch!=9 && key.ch!=13 && key.ch!=10) {
                key.ctrl=true;key.ch=key.ch<27?char16_t(u'a'+key.ch-1):key.ch==31?u'/':key.ch==0?u' ':char16_t(u'@'+key.ch);}
            else if(key.ctrl && key.ch>=u'A' && key.ch<=u'Z')key.ch=char16_t(key.ch+32);
            if(key.alt && key.ch>=u'A' && key.ch<=u'Z' && !key.shift)key.ch=char16_t(key.ch+32);
            return key;
        }
        efi_event events[2]={u.ex && u.ex->WaitForKeyEx?u.ex->WaitForKeyEx:input->WaitForKey,u.timer};size_t index=0;
        if(WaitMenuVoiceEvent(boot,u.audioFailed?nullptr:u.host->voice,events,u.timer?2:1,1,index)!=EFI_SUCCESS) {
            boot->Stall(10000);}
        if(index==1)Pump();
    }
}
// A key waiting already (to stop long work with Escape)?
inline bool EscapePressed() {
    auto& u=U();auto* input=u.host->system->ConIn;Pump();
    if(u.ex && u.ex->ReadKeyStrokeEx) {KeyData d{};return u.ex->ReadKeyStrokeEx(u.ex,&d)==EFI_SUCCESS && d.key.ScanCode==scEsc;}
    efi_input_key k{};return input->ReadKeyStroke(input,&k)==EFI_SUCCESS && k.ScanCode==scEsc;
}
inline bool Printable(const Key& key) {return !key.scan && key.ch>=32 && key.ch!=127 && !key.ctrl && !key.alt;}
inline bool IsCancel(const Key& key) {return key.scan==scEsc || (key.ctrl && key.ch==u'g');}
// Name of one character as NVDA says it ("spacja", "duże A").
inline void CharText(char16_t c,char16_t* out,size_t cap) {
    out[0]=0;speechtext::Out o{out,cap};
    if(c==u'\n') {o.Puts(T(u"nowy wiersz",u"new line"));return;}
    if(c==u'\t') {o.Puts(u"tab");return;}
    if(c==u' ') {o.Puts(T(u"spacja",u"space"));return;}
    speechtext::CharName(c,Pl(),true,o);
}
// The echo of a typed key is spoken but not written into the transcript (the prompt row shows it).
inline void SayEcho(const char16_t* text) {auto& t=Script();const bool on=t.on;t.on=false;Say(text);t.on=on;}
inline void SayChar(char16_t c) {char16_t text[96];CharText(c,text,96);SayEcho(text);}
// The echo line as a minibuffer. Typed characters are echoed in speech;
// `complete` (optional) is called after each change and may speak a match;
// Up/Down are passed to it as -1/+1 with the text unchanged. False: cancelled.
using PromptHook=void (*)(const char16_t* text,int move,void* context);
inline bool Prompt(const char16_t* label,char16_t* text,size_t cap,PromptHook hook=nullptr,void* context=nullptr,bool announce=true) {
    size_t n=Len(text);
    static char16_t line[400];
    if(announce) {Set(line,400,label);if(n) {Cat(line,400,u" ");Cat(line,400,text);}Say(line);}
    for(;;) {
        Set(line,400,label);Cat(line,400,u" ");Cat(line,400,text);Echo(line);
        Cursor(true,Len(label)+1+n,U().rows-1);
        const Key key=WaitKey();
        if(IsCancel(key)) {Cursor(false);Echo(u"");Play(Icon::close);Say(T(u"Anulowano",u"Cancelled"));return false;}
        if(!key.scan && key.ch==13) {Cursor(false);Echo(u"");return true;}
        if(!key.scan && key.ch==8) {
            if(!n) {Play(Icon::edge);continue;}
            const char16_t gone=text[--n];text[n]=0;Play(Icon::deleted);
            if(hook)hook(text,0,context);else SayChar(gone);
            continue;
        }
        if(key.scan==scUp || key.scan==scDown || (!key.scan && key.ch==9)) {
            if(hook)hook(text,key.scan==scUp?-1:1,context);else Say(n?text:T(u"puste",u"empty"));
            n=Len(text);continue;
        }
        if(key.ctrl && key.ch==u'u') {n=0;text[0]=0;Say(T(u"Wyczyszczono",u"Cleared"));if(hook)hook(text,0,context);continue;}
        if(Printable(key)) {
            if(n+1>=cap) {Play(Icon::edge);continue;}
            text[n++]=key.ch;text[n]=0;
            if(hook)hook(text,0,context);else SayChar(key.ch);
        }
    }
}
// A yes/no question; only T/Y confirms.
inline bool Confirm(const char16_t* question) {
    static char16_t line[500];Set(line,500,question);Cat(line,500,T(u" T tak, N nie.",u" Y yes, N no."));
    Echo(line);Say(line);
    for(;;) {
        const Key key=WaitKey();
        if(Printable(key) && (Fold(key.ch)==u't' || Fold(key.ch)==u'y')) {Echo(u"");return true;}
        if(IsCancel(key) || (Printable(key) && Fold(key.ch)==u'n') || (!key.scan && key.ch==13)) {
            Echo(u"");Say(T(u"Nie",u"No"));return false;}
    }
}
// ---- Files -------------------------------------------------------------
constexpr size_t pathUnits=512;
struct FileRef {efi_handle volume;char16_t path[pathUnits];};   // path starts with a backslash
inline efi_file_protocol* OpenRoot(efi_handle volume) {
    efi_simple_file_system_protocol* fs=nullptr;efi_file_protocol* root=nullptr;
    if(!volume || U().host->boot->HandleProtocol(volume,&media::fsGuid,(void**)&fs)!=EFI_SUCCESS || !fs)return nullptr;
    return fs->OpenVolume(fs,&root)==EFI_SUCCESS?root:nullptr;
}
inline efi_status OpenFile(const FileRef& ref,uint64_t mode,uint64_t attributes,efi_file_protocol** file) {
    *file=nullptr;efi_file_protocol* root=OpenRoot(ref.volume);if(!root)return EFI_NOT_FOUND;
    efi_status status;
    if(ref.path[0]==u'\\' && !ref.path[1]) {*file=root;return EFI_SUCCESS;}
    status=root->Open(root,file,ref.path,mode,attributes);root->Close(root);
    if(status==EFI_SUCCESS && !*file)status=EFI_NOT_FOUND;
    return status;
}
inline bool JoinPath(char16_t* out,size_t cap,const char16_t* folder,const char16_t* name) {
    const size_t a=Len(folder),b=Len(name);
    if(a+b+2>=cap)return false;
    Set(out,cap,folder);if(a>1 || !a)Cat(out,cap,u"\\");Cat(out,cap,name);return true;
}
inline const char16_t* BaseName(const char16_t* path) {
    const char16_t* base=path;for(size_t i=0;path[i];++i)if(path[i]==u'\\')base=path+i+1;
    return base;
}
inline void ParentOf(const char16_t* path,char16_t* out,size_t cap) {
    Set(out,cap,path);size_t n=Len(out);
    while(n>1 && out[n-1]!=u'\\')--n;
    if(n>1)--n;
    out[n?n:1]=0;if(!n)out[0]=u'\\';
}
// The file info of an open file (static buffer; valid until the next call).
inline const efi_file_info* InfoOf(efi_file_protocol* file) {
    static efi_guid infoGuid=EFI_FILE_INFO_GUID;alignas(8) static uint8_t buffer[1200];size_t size=sizeof(buffer);
    if(file->GetInfo(file,&infoGuid,&size,buffer)!=EFI_SUCCESS)return nullptr;
    return reinterpret_cast<const efi_file_info*>(buffer);
}
inline void VolumeLabel(efi_handle volume,char16_t* out,size_t cap) {
    out[0]=0;efi_file_protocol* root=OpenRoot(volume);
    if(root) {
        alignas(8) uint8_t info[512]={};size_t size=sizeof(info);
        if(root->GetInfo(root,&media::fsInfoGuid,&size,info)==EFI_SUCCESS && size>36)
            for(size_t k=0;k+1<cap && 36+k*2+1<size;++k) {const char16_t c=char16_t(systools::Le16(info+36+k*2));if(!c)break;out[k]=c<32?u' ':c;out[k+1]=0;}
        root->Close(root);
    }
    if(!out[0])Set(out,cap,volume==U().host->selfDevice?T(u"dysk Charona",u"Charon's disk"):T(u"bez etykiety",u"no label"));
}
inline bool VolumeReadOnly(efi_handle volume) {
    bool readOnly=true;efi_file_protocol* root=OpenRoot(volume);
    if(root) {
        alignas(8) uint8_t info[512]={};size_t size=sizeof(info);
        if(root->GetInfo(root,&media::fsInfoGuid,&size,info)==EFI_SUCCESS && size>36)readOnly=info[8]!=0;
        root->Close(root);
    }
    return readOnly;
}
// Every mounted file system. Returns the count.
inline size_t Volumes(efi_handle* out,size_t maximum) {
    auto* boot=U().host->boot;efi_handle* handles=nullptr;size_t n=0,count=0;
    if(boot->LocateHandleBuffer(ByProtocol,&media::fsGuid,nullptr,&n,&handles)!=EFI_SUCCESS || !handles)return 0;
    // Charon's own disk first.
    for(size_t i=0;i<n && count<maximum;++i)if(handles[i]==U().host->selfDevice)out[count++]=handles[i];
    for(size_t i=0;i<n && count<maximum;++i)if(handles[i]!=U().host->selfDevice)out[count++]=handles[i];
    boot->FreePool(handles);return count;
}
inline void* Allocate(size_t bytes) {
    void* p=nullptr;
    return U().host->boot->AllocatePool(EfiLoaderData,bytes?bytes:1,&p)==EFI_SUCCESS?p:nullptr;
}
inline void Release(void* p) {if(p)U().host->boot->FreePool(p);}
// Large blocks (downloads kept in memory, archives): whole pages.
struct Memory {uint8_t* data;size_t capacity,size,extra;bool reserved;};   // extra: room asked for after the content; reserved: memory an operating system must leave alone (a RAM disk)
inline bool Reserve(Memory& m,size_t bytes) {
    efi_physical_addr address=0;m.data=nullptr;m.capacity=0;m.size=0;
    const size_t pages=(bytes+4095)/4096;
    if(!pages || U().host->boot->AllocatePages(AllocateAnyPages,m.reserved?EfiReservedMemoryType:EfiLoaderData,pages,&address)!=EFI_SUCCESS || !address)return false;
    m.data=reinterpret_cast<uint8_t*>(address);m.capacity=pages*4096;return true;
}
inline void Free(Memory& m) {
    if(m.data)U().host->boot->FreePages(efi_physical_addr(reinterpret_cast<uintptr_t>(m.data)),m.capacity/4096);
    m.data=nullptr;m.capacity=0;m.size=0;
}
// A progress line on the echo row: a bar and the percentage.
inline void ProgressLine(const char16_t* what,uint64_t done,uint64_t total) {
    static char16_t line[200];const unsigned percent=total?unsigned(done*100/total):0;
    Set(line,200,what);Cat(line,200,u" [");
    for(unsigned i=0;i<30;++i)Cat(line,200,i*100<percent*30?u"█":u"░");
    Cat(line,200,u"] ");CatN(line,200,percent);Cat(line,200,u"%  ");
    CatN(line,200,done>>20);Cat(line,200,u" / ");CatN(line,200,total>>20);Cat(line,200,u" MB");
    Row(U().rows-1,line,7);
}
// r401: progress of a long task of Charon itself: the bar on the screen and
// the very same sound and speech as the reader gives for the bars it sees in
// boot managers (CharonProgress.h), by the same setting.
inline void Progress(const char16_t* what,uint64_t done,uint64_t total) {
    auto& u=U();ProgressLine(what,done,total);
    if(!total)return;
    if(what!=u.progressWhat || done<u.progressDone)u.lastPercent=-1;
    u.progressWhat=what;u.progressDone=done;
    const int percent=int(done*100/total);const uint8_t mode=u.host->progress;
    if(!mode || percent==u.lastPercent)return;
    const int previous=u.lastPercent;u.lastPercent=percent;
    auto* e=u.host->effects;
    if((mode&1) && e && e->Tone) {VoiceGuard guard;e->Tone(e->context,progressfb::Hertz(percent),progressfb::toneMs);}
    if((mode&2) && progressfb::SpeakAt(previous,percent)) {
        char16_t text[64]={};progressfb::Phrase(percent,u.host->pl,u.host->explicitMessages,unsigned(ExplicitVariety()),text);
        VoiceGuard guard;
        if(u.host->sayAuthored)u.host->sayAuthored(u.host->context,text);else u.host->say(u.host->context,text);
    }
}
// Whole file into pool memory (caller releases). Files above `limit` fail
// with EFI_BAD_BUFFER_SIZE.
inline efi_status ReadWhole(const FileRef& ref,size_t limit,uint8_t** data,size_t* size) {
    *data=nullptr;*size=0;efi_file_protocol* file=nullptr;
    auto status=OpenFile(ref,EFI_FILE_MODE_READ,0,&file);if(status!=EFI_SUCCESS)return status;
    const auto* info=InfoOf(file);
    if(!info || (info->Attribute&EFI_FILE_DIRECTORY)) {file->Close(file);return EFI_INVALID_PARAMETER;}
    const uint64_t length=info->FileSize;
    if(length>limit) {file->Close(file);return EFI_BAD_BUFFER_SIZE;}
    uint8_t* bytes=static_cast<uint8_t*>(Allocate(size_t(length)+2));
    if(!bytes) {file->Close(file);return EFI_OUT_OF_RESOURCES;}
    size_t done=0;
    while(done<length) {
        size_t part=size_t(length)-done;if(part>65536)part=65536;
        status=file->Read(file,&part,bytes+done);
        if(status!=EFI_SUCCESS || !part)break;
        done+=part;Pump();
    }
    file->Close(file);
    if(status!=EFI_SUCCESS) {Release(bytes);return status;}
    bytes[done]=0;bytes[done+1]=0;*data=bytes;*size=done;return EFI_SUCCESS;
}
// Replaces a file's contents: the old file is deleted first (UEFI files
// cannot be truncated), then created again and written.
inline efi_status WriteWhole(const FileRef& ref,const uint8_t* data,size_t size) {
    efi_file_protocol* file=nullptr;
    if(OpenFile(ref,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0,&file)==EFI_SUCCESS) {
        const auto* info=InfoOf(file);
        if(info && (info->Attribute&EFI_FILE_DIRECTORY)) {file->Close(file);return EFI_INVALID_PARAMETER;}
        const auto gone=file->Delete(file);   // closes the handle either way
        if(gone!=EFI_SUCCESS)return EFI_WRITE_PROTECTED;
    }
    auto status=OpenFile(ref,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0,&file);
    if(status!=EFI_SUCCESS)return status;
    size_t done=0;
    while(done<size) {
        size_t part=size-done;if(part>65536)part=65536;const size_t asked=part;
        status=file->Write(file,&part,const_cast<uint8_t*>(data+done));
        if(status!=EFI_SUCCESS)break;
        if(part!=asked) {status=EFI_VOLUME_FULL;break;}
        done+=part;Pump();
    }
    if(status==EFI_SUCCESS && file->Flush)status=file->Flush(file);
    file->Close(file);
    return status;
}
// Folders along `path` on a volume, created when missing.
inline efi_status MakeFolders(efi_handle volume,const char16_t* path) {
    efi_file_protocol* root=OpenRoot(volume);if(!root)return EFI_NOT_FOUND;
    static char16_t part[pathUnits];efi_status status=EFI_SUCCESS;
    for(size_t i=1;;++i) {
        if(path[i]==u'\\' || !path[i]) {
            if(i>1 && i<pathUnits) {
                for(size_t k=0;k<i;++k)part[k]=path[k];
                part[i]=0;efi_file_protocol* dir=nullptr;
                status=root->Open(root,&dir,part,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);
                if(status!=EFI_SUCCESS)status=root->Open(root,&dir,part,EFI_FILE_MODE_READ,0);   // read-only media: it may exist
                if(status!=EFI_SUCCESS || !dir)break;
                dir->Close(dir);
            }
            if(!path[i])break;
        }
    }
    root->Close(root);return status;
}
// A directory listing in pool-free static storage, sorted: folders first.
constexpr size_t maxEntries=512,nameUnits=128;
struct Entry {char16_t name[nameUnits];bool directory,marked;uint64_t size;efi_time modified;};
struct Listing {Entry entries[maxEntries];size_t count;bool truncated;};
inline int CompareEntry(const Entry& a,const Entry& b) {
    if(a.directory!=b.directory)return a.directory?-1:1;
    return CompareFold(a.name,b.name);
}
inline efi_status ReadFolder(const FileRef& folder,Listing& list) {
    list.count=0;list.truncated=false;efi_file_protocol* dir=nullptr;
    auto status=OpenFile(folder,EFI_FILE_MODE_READ,0,&dir);if(status!=EFI_SUCCESS)return status;
    alignas(8) static uint8_t buffer[2048];
    for(unsigned guard=0;guard<100000;++guard) {
        size_t size=sizeof(buffer);status=dir->Read(dir,&size,buffer);
        if(status==EFI_BUFFER_TOO_SMALL) {list.truncated=true;status=EFI_SUCCESS;break;}   // cannot step over it portably
        if(status!=EFI_SUCCESS || !size)break;
        const auto* info=reinterpret_cast<const efi_file_info*>(buffer);const char16_t* name=info->FileName;
        if(name[0]==u'.' && (!name[1] || (name[1]==u'.' && !name[2])))continue;
        if(Len(name)>=nameUnits) {list.truncated=true;continue;}
        if(list.count>=maxEntries) {list.truncated=true;break;}
        auto& e=list.entries[list.count++];Set(e.name,nameUnits,name);
        e.directory=(info->Attribute&EFI_FILE_DIRECTORY)!=0;e.marked=false;e.size=info->FileSize;e.modified=info->ModificationTime;
    }
    dir->Close(dir);
    for(size_t i=1;i<list.count;++i) {
        static Entry moving;moving=list.entries[i];size_t j=i;
        while(j && CompareEntry(moving,list.entries[j-1])<0) {list.entries[j]=list.entries[j-1];--j;}
        list.entries[j]=moving;
    }
    return status;
}
// Copies one file. Progress ticks rise with the position.
inline efi_status CopyFile(const FileRef& from,const FileRef& to) {
    efi_file_protocol* in=nullptr;efi_file_protocol* out=nullptr;
    auto status=OpenFile(from,EFI_FILE_MODE_READ,0,&in);if(status!=EFI_SUCCESS)return status;
    const auto* info=InfoOf(in);const uint64_t total=info?info->FileSize:0;
    if(OpenFile(to,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0,&out)==EFI_SUCCESS) {
        if(out->Delete(out)!=EFI_SUCCESS) {in->Close(in);return EFI_WRITE_PROTECTED;}
        out=nullptr;
    }
    status=OpenFile(to,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0,&out);
    if(status!=EFI_SUCCESS) {in->Close(in);return status;}
    static uint8_t block[65536];uint64_t done=0;unsigned rounds=0;
    for(;;) {
        size_t part=sizeof(block);status=in->Read(in,&part,block);
        if(status!=EFI_SUCCESS || !part)break;
        const size_t asked=part;status=out->Write(out,&part,block);
        if(status!=EFI_SUCCESS)break;
        if(part!=asked) {status=EFI_VOLUME_FULL;break;}
        done+=part;Pump();
        if(++rounds%16==0)Tone(400+unsigned(total?done*800/total:0),20);
    }
    in->Close(in);
    if(status==EFI_SUCCESS && out->Flush)status=out->Flush(out);
    if(status!=EFI_SUCCESS) {out->Delete(out);return status;}
    out->Close(out);return EFI_SUCCESS;
}
// Copies a folder with everything below it (depth-first, bounded depth).
inline efi_status CopyTree(const FileRef& from,const FileRef& to,unsigned depth,size_t* files) {
    if(depth>8)return EFI_UNSUPPORTED;
    auto status=MakeFolders(to.volume,to.path);if(status!=EFI_SUCCESS)return status;
    efi_file_protocol* dir=nullptr;status=OpenFile(from,EFI_FILE_MODE_READ,0,&dir);if(status!=EFI_SUCCESS)return status;
    // Children are read one at a time; the pool copy keeps recursion off the stack.
    struct Frame {FileRef a,b;uint8_t info[1200];};
    Frame* f=static_cast<Frame*>(Allocate(sizeof(Frame)));
    if(!f) {dir->Close(dir);return EFI_OUT_OF_RESOURCES;}
    for(unsigned guard=0;guard<20000;++guard) {
        size_t size=sizeof(f->info);status=dir->Read(dir,&size,f->info);
        if(status!=EFI_SUCCESS || !size)break;
        const auto* info=reinterpret_cast<const efi_file_info*>(f->info);const char16_t* name=info->FileName;
        if(name[0]==u'.' && (!name[1] || (name[1]==u'.' && !name[2])))continue;
        f->a.volume=from.volume;f->b.volume=to.volume;
        if(!JoinPath(f->a.path,pathUnits,from.path,name) || !JoinPath(f->b.path,pathUnits,to.path,name)) {status=EFI_BAD_BUFFER_SIZE;break;}
        if(info->Attribute&EFI_FILE_DIRECTORY)status=CopyTree(f->a,f->b,depth+1,files);
        else {status=CopyFile(f->a,f->b);if(status==EFI_SUCCESS && files)++*files;}
        if(status!=EFI_SUCCESS)break;
    }
    Release(f);dir->Close(dir);return status;
}
// Deletes a file, or a folder with everything below it.
inline efi_status DeleteTree(const FileRef& target,unsigned depth) {
    if(depth>8)return EFI_UNSUPPORTED;
    if(target.path[0]==u'\\' && !target.path[1])return EFI_ACCESS_DENIED;   // never a whole volume
    efi_file_protocol* file=nullptr;
    auto status=OpenFile(target,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0,&file);if(status!=EFI_SUCCESS)return status;
    const auto* info=InfoOf(file);
    if(info && (info->Attribute&EFI_FILE_DIRECTORY)) {
        struct Frame {FileRef child;uint8_t info[1200];};
        Frame* f=static_cast<Frame*>(Allocate(sizeof(Frame)));
        if(!f) {file->Close(file);return EFI_OUT_OF_RESOURCES;}
        for(unsigned guard=0;guard<20000;++guard) {
            file->SetPosition(file,0);   // entries vanish as they are deleted
            bool found=false;
            for(unsigned skip=0;skip<4;++skip) {
                size_t size=sizeof(f->info);status=file->Read(file,&size,f->info);
                if(status!=EFI_SUCCESS || !size)break;
                const char16_t* name=reinterpret_cast<const efi_file_info*>(f->info)->FileName;
                if(name[0]==u'.' && (!name[1] || (name[1]==u'.' && !name[2])))continue;
                found=true;break;
            }
            if(!found)break;
            f->child.volume=target.volume;
            if(!JoinPath(f->child.path,pathUnits,target.path,reinterpret_cast<const efi_file_info*>(f->info)->FileName)) {status=EFI_BAD_BUFFER_SIZE;break;}
            status=DeleteTree(f->child,depth+1);
            if(status!=EFI_SUCCESS)break;
            Pump();
        }
        Release(f);
        if(status!=EFI_SUCCESS && status!=EFI_NOT_FOUND) {file->Close(file);return status;}
    }
    return file->Delete(file)==EFI_SUCCESS?EFI_SUCCESS:EFI_WRITE_PROTECTED;
}
// UTF-8 text <-> UTF-16 (invalid bytes become U+FFFD).
inline size_t Utf8ToUtf16(const uint8_t* in,size_t size,char16_t* out,size_t cap,bool* invalid=nullptr) {
    size_t at=0;
    for(size_t i=0;i<size && at+2<cap;) {
        uint32_t c=in[i];size_t extra=c<0x80?0:(c&0xe0)==0xc0?1:(c&0xf0)==0xe0?2:(c&0xf8)==0xf0?3:SIZE_MAX;
        if(extra==SIZE_MAX || i+extra>=size+(extra?0:1)) {out[at++]=0xfffd;if(invalid)*invalid=true;++i;continue;}
        if(extra) {
            c&=extra==1?0x1f:extra==2?0x0f:0x07;bool ok=true;
            for(size_t k=1;k<=extra;++k) {if((in[i+k]&0xc0)!=0x80) {ok=false;break;}c=(c<<6)|(in[i+k]&0x3f);}
            if(!ok) {out[at++]=0xfffd;if(invalid)*invalid=true;++i;continue;}
        }
        i+=extra+1;
        if(c>=0x10000) {c-=0x10000;out[at++]=char16_t(0xd800+(c>>10));out[at++]=char16_t(0xdc00+(c&0x3ff));}
        else out[at++]=char16_t(c);
    }
    out[at]=0;return at;
}
inline size_t Utf16ToUtf8(const char16_t* in,size_t length,uint8_t* out,size_t cap) {
    size_t at=0;
    for(size_t i=0;i<length && at+4<cap;++i) {
        uint32_t c=in[i];
        if(c>=0xd800 && c<=0xdbff && i+1<length && in[i+1]>=0xdc00 && in[i+1]<=0xdfff) {c=0x10000+((c-0xd800)<<10)+(in[i+1]-0xdc00);++i;}
        if(c<0x80)out[at++]=uint8_t(c);
        else if(c<0x800) {out[at++]=uint8_t(0xc0|(c>>6));out[at++]=uint8_t(0x80|(c&0x3f));}
        else if(c<0x10000) {out[at++]=uint8_t(0xe0|(c>>12));out[at++]=uint8_t(0x80|((c>>6)&0x3f));out[at++]=uint8_t(0x80|(c&0x3f));}
        else {out[at++]=uint8_t(0xf0|(c>>18));out[at++]=uint8_t(0x80|((c>>12)&0x3f));out[at++]=uint8_t(0x80|((c>>6)&0x3f));out[at++]=uint8_t(0x80|(c&0x3f));}
    }
    return at;
}
// A generic speaking list: the label callback fills one row; `spoken` may
// differ from what is drawn. Returns the key that the list did not handle.
struct ListView {
    size_t count,index,top;
    void (*label)(size_t row,char16_t* out,size_t cap,bool spoken,void* context);
    void* context;
};
inline void SayRow(ListView& v) {
    static char16_t text[700];text[0]=0;
    if(!v.count) {Say(T(u"Lista jest pusta",u"The list is empty"));return;}
    PositionVoice(text,700,v.index);
    {const size_t at=Len(text);v.label(v.index,text+at,640-at,true,v.context);}
    if(U().host->positions) {Cat(text,700,u", ");CatN(text,700,v.index+1);Cat(text,700,T(u" z ",u" of "));CatN(text,700,v.count);}
    Say(text);
}
inline void DrawList(ListView& v) {
    const size_t body=BodyRows();
    if(v.index<v.top)v.top=v.index;
    if(v.index>=v.top+body)v.top=v.index-body+1;
    static char16_t text[300];
    for(size_t r=0;r<body;++r) {
        const size_t i=v.top+r;
        if(i<v.count) {text[0]=i==v.index?u'►':u' ';text[1]=u' ';text[2]=0;v.label(i,text+2,296,false,v.context);Row(1+r,text,i==v.index?0x70:7);}
        else Row(1+r,!v.count && !r?T(u"  (pusto)",u"  (empty)"):u"");
    }
}
// Arrows, Home/End, pages and Emacs C-n/C-p. True when the key was a move.
inline bool MoveList(ListView& v,const Key& key) {
    const size_t before=v.index;bool move=true;const size_t body=BodyRows();
    if(key.scan==scUp || (key.ctrl && key.ch==u'p')) {if(v.index)--v.index;}
    else if(key.scan==scDown || (key.ctrl && key.ch==u'n')) {if(v.index+1<v.count)++v.index;}
    else if(key.scan==scHome || (key.alt && key.ch==u'<'))v.index=0;
    else if(key.scan==scEnd || (key.alt && key.ch==u'>'))v.index=v.count?v.count-1:0;
    else if(key.scan==scPageUp || (key.alt && key.ch==u'v'))v.index=v.index>body?v.index-body:0;
    else if(key.scan==scPageDown || (key.ctrl && key.ch==u'v'))v.index=v.index+body<v.count?v.index+body:(v.count?v.count-1:0);
    else move=false;
    if(move) {
        if(before==v.index)Play(Icon::edge);
        else Cue(MenuCue::focus);
        SayRow(v);
    }
    return move;
}
}  // namespace ws
}  // namespace hakefi
