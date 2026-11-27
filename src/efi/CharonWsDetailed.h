#pragma once
// r410: the detailed mode of the install dialogue.
//
// After a system's own questions (they are unchanged), the detailed mode shows a list of GROUPS
// of further questions. Nothing forces the user through a group: every question has a default,
// and the default is what the express mode installs. In the list: Up/Down move, Enter opens a
// group, D reads the group's changed answers, F3 searches all questions, F10 (or the last line)
// ends. In a question: F2 says the default again, Delete goes back to it, F1 repeats, Escape
// goes back one question. Administration groups appear only after the gate was answered yes.
//
// The questions themselves are data: tools/charon-install/charon_questions.py writes
// CharonWsQuestions.h (the tables), the key lists the scripts in the installed system accept,
// and the test vectors. Only answers that DIFFER from their defaults travel to the installed
// system: answers.env + personal.sh (Linux), answers.ini + Charon-Personal.ps1 (Windows). With
// every answer at its default nothing is carried and the generated answer files are byte for byte
// those of the express mode.
// GPL-2.0-or-later.
#include "CharonWsAsk.h"
#ifndef CHARON_INSTALL_HOST
#include "CharonVoiceRest.h"   // hakefi::VoiceResting (Hear)
#endif
namespace hakefi {
namespace ws {
namespace detailed {
using ask::Question;using ask::Choice;using ask::Kind;
struct Group {const char* id;const char16_t* pl;const char16_t* en;uint8_t level;};
// What is being installed: family 'l' Linux, 'w' Windows, 'x' CharonDOS; path 'd' Debian's installer,
// 'u' Ubuntu's installer, 'D' 'U' 'A' 'F' the ready images of Debian, Ubuntu, Arch, Fedora, 'w', 'x'.
struct Context {char family;char path;bool active;};
inline Context& C() {static Context context{0,0,false};return context;}
inline bool& BaselineOn();
// r410 (es): the user's own list file \EFI\HAK\catalogue\software.txt is on the medium (set by sets::Offer).
inline bool& SoftFileOn() {static bool on=false;return on;}
inline bool SoftFile() {return SoftFileOn();}
inline void Begin(char family,char path) {C()=Context{family,path,false};ask::AdminOpen()=false;BaselineOn()=false;SoftFileOn()=false;}
// Conditions the generated tables use.
inline bool On(const char* paths) {for(;*paths;++paths)if(*paths==C().path)return true;return false;}
inline bool AnyDesk() {return ask::Any("desktop");}
inline bool Desk(const char* name) {return ask::Has("desktop",name);}
// Which desktops were chosen, by the words in their package names (gnome-desktop, task-kde-desktop, kubuntu-desktop,
// ubuntu-mate-desktop ...). Letters: g GNOME, k KDE Plasma, m MATE, x Xfce, c Cinnamon. A question whose setting
// exists on none of the chosen desktops is not asked (LXQt and i3 have none of these settings).
inline bool DeskWord(const char* word) {
    const char* text=ask::Get("desktop");
    for(size_t i=0;text[i];++i) {size_t k=0;while(word[k] && text[i+k]==word[k])++k;if(!word[k])return true;}
    return false;
}
inline bool DeskIs(char letter) {
    switch(letter) {
        case 'm':return DeskWord("mate");
        case 'c':return DeskWord("cinnamon");
        case 'x':return DeskWord("xfce") || DeskWord("xubuntu");
        case 'k':return DeskWord("kde") || DeskWord("kubuntu") || DeskWord("plasma");
        case 'g':return DeskWord("gnome") || (DeskWord("ubuntu-desktop") && !DeskWord("mate"));
        default:return false;
    }
}
inline bool DeskAny(const char* letters) {for(;*letters;++letters)if(DeskIs(*letters))return true;return false;}
inline bool Is(const char* key,const char* value) {return ask::Is(key,value);}
inline bool Has(const char* key,const char* value) {return ask::Has(key,value);}
}  // namespace detailed
}  // namespace ws
}  // namespace hakefi
#include "CharonWsQuestions.h"   // generated: detailed::groups[], linuxRows[], windowsRows[], dosRows[]
namespace hakefi {
namespace ws {
namespace detailed {
inline void Table(const Question*& rows,size_t& count) {
    if(C().family=='s') {rows=sharedRows;count=sizeof(sharedRows)/sizeof(sharedRows[0]);}   // the shared block of a plan
    else if(C().family=='w') {rows=windowsRows;count=sizeof(windowsRows)/sizeof(windowsRows[0]);}
    else if(C().family=='x') {rows=dosRows;count=sizeof(dosRows)/sizeof(dosRows[0]);}
    else {rows=linuxRows;count=sizeof(linuxRows)/sizeof(linuxRows[0]);}
}
// Asked for this system at all (dependencies on other answers are looked at when it is asked).
inline bool Changed(const Question& q) {
    const char* v=ask::Get(q.key);
    if(ask::Find(q.key)==SIZE_MAX)return false;
    return !ask::Same(v,q.fallback?q.fallback:"");
}
inline size_t CountChanged() {
    const Question* rows;size_t n;Table(rows,n);size_t changed=0;
    for(size_t i=0;i<n;++i)if(ask::Applies(rows[i]) && Changed(rows[i]))++changed;
    return changed;
}
// ---- a plan of several systems: shared answers, asked once ------------------------------------
inline const Question* RowOf(const Question* rows,size_t count,const char* key) {for(size_t i=0;i<count;++i)if(ask::Same(rows[i].key,key))return &rows[i];return nullptr;}
inline const Question* SharedRow(const char* key) {return RowOf(sharedRows,sizeof(sharedRows)/sizeof(sharedRows[0]),key);}
// What a system inherited from the shared block: its summary speaks only what differs from this.
struct Base {bool on;char value[96];};
inline Base* Baseline() {static Base base[ask::maxAnswers];return base;}
inline bool& BaselineOn() {static bool on=false;return on;}
inline void TakeBaseline() {
    const Question* rows;size_t n;Table(rows,n);Base* base=Baseline();
    for(size_t i=0;i<n && i<ask::maxAnswers;++i) {
        base[i].on=Changed(rows[i]);const char* v=ask::Get(rows[i].key);size_t k=0;for(;v[k] && k<95;++k)base[i].value[k]=v[k];base[i].value[k]=0;
        if(v[k])base[i].on=false;   // too long to compare: spoken again rather than hidden wrongly
    }
    BaselineOn()=true;
}
inline bool Inherited(const Question* rows,size_t i) {return BaselineOn() && i<ask::maxAnswers && Baseline()[i].on && ask::Same(Baseline()[i].value,ask::Get(rows[i].key));}
// Windows in a plan: the shared answers under Windows' own keys and values (the generated maps).
inline void MapShared() {
    for(const auto& m:sharedMaps) {
        const Question* shared=SharedRow(m.key);
        if(!shared || ask::Find(m.key)==SIZE_MAX || !Changed(*shared))continue;
        const char* value=ask::Get(m.key);const char* mapped=m.from[0]?nullptr:value;
        for(size_t i=0;i<8 && m.from[i];++i)if(ask::Same(m.from[i],value))mapped=m.to[i];
        if(mapped)ask::Put(m.windows,mapped);
    }
    const char* zone=ask::Get("timezone");
    for(const auto& z:zoneMaps)if(ask::Same(z.iana,zone))ask::Put("wtimezone",z.windows);
}
// The shared answers this system cannot take, by the questions' own words (empty: all of them apply).
inline void NotApplied(char16_t* out,size_t cap) {
    out[0]=0;const Question* rows;size_t n;Table(rows,n);bool any=false;
    for(const auto& q:sharedRows) {
        if(ask::Find(q.key)==SIZE_MAX || !Changed(q))continue;
        bool applied=false;
        if(C().family=='w') {
            for(const auto& m:sharedMaps)if(ask::Same(m.key,q.key)) {
                const char* value=ask::Get(q.key);applied=!m.from[0];
                for(size_t i=0;i<8 && m.from[i];++i)if(ask::Same(m.from[i],value))applied=true;
                const Question* w=RowOf(rows,n,m.windows);if(!w || !ask::Applies(*w))applied=false;
            }
        } else {const Question* own=RowOf(rows,n,q.key);applied=own && ask::Applies(*own);}
        if(applied)continue;
        if(!any)Set(out,cap,T(u"W tym systemie nie da się zastosować tych wspólnych odpowiedzi: ",u"These shared answers cannot be applied in this system: "));
        else Cat(out,cap,u"; ");
        Cat(out,cap,Pl()?q.pl:q.en);any=true;
        {size_t n2=Len(out);if(n2 && (out[n2-1]==u':' || out[n2-1]==u'?'))out[n2-1]=0;}   // the question's name, without its own mark
    }
    if(any)Cat(out,cap,u". ");
}
// True: this installation carries answers of the detailed mode to the installed system.
inline bool Carried() {return C().active && CountChanged()!=0;}
inline bool Inherited(const Question* rows,size_t i);
// r410 (helper ls): a set's programs are named, not given by their catalogue ids (catalogue::Names).
using NamesFn=bool (*)(const Question& q,char16_t* out,size_t cap);
inline NamesFn& NamesHook() {static NamesFn hook=nullptr;return hook;}
// The value as it is said: the text of the chosen entry, "set" for a secret.
inline void ValueText(const Question& q,char16_t* out,size_t cap) {
    out[0]=0;const char* v=ask::Get(q.key);
    {const char* k=q.key;if(k[0]=='w' && k[1]=='_')k+=2;
     if(k[0]=='s' && k[1]=='e' && k[2]=='t' && k[3]=='_' && q.kind!=Kind::yesNo && NamesHook() && NamesHook()(q,out,cap))return;}
    if(q.kind==Kind::secret || (q.flags&ask::flagSecret)) {Set(out,cap,v[0]?T(u"ustawione",u"set"):T(u"nie ustawione",u"not set"));return;}
    if(q.kind==Kind::yesNo) {Set(out,cap,ask::Same(v,"yes")?T(u"tak",u"yes"):T(u"nie",u"no"));return;}
    if(q.kind==Kind::choice) {for(size_t k=0;k<q.count;++k)if(ask::Same(q.choices[k].value,v)) {Set(out,cap,Pl()?q.choices[k].pl:q.choices[k].en);return;}}
    if(q.kind==Kind::multi) {bool any=false;
        for(size_t k=0;k<q.count;++k)if(ask::Has(q.key,q.choices[k].value)) {if(any)Cat(out,cap,u", ");Cat(out,cap,Pl()?q.choices[k].pl:q.choices[k].en);any=true;}
        if(!any)Set(out,cap,T(u"nic",u"nothing"));return;}
    static char16_t wide[ask::typedUnits];ask::Wide(v,wide,ask::typedUnits);Set(out,cap,wide[0]?wide:T(u"puste",u"empty"));
}
inline const Group* GroupOf(const char* id) {
    for(const auto& g:groups)if(ask::Same(g.id,id))return &g;
    return nullptr;
}
// "N answers differ from the defaults: <group>: <question> <value>. ..." (nothing when none differ).
inline void Summary(char16_t* out,size_t cap,const char* onlyGroup=nullptr) {
    out[0]=0;const Question* rows;size_t n;Table(rows,n);const size_t changed=CountChanged();
    if(!onlyGroup) {
        if(!changed) {Set(out,cap,T(u"Wszystkie odpowiedzi szczegółowe mają wartości domyślne. ",u"Every detailed answer has its default. "));return;}
        Set(out,cap,T(u"Odpowiedzi różnych od domyślnych: ",u"Answers that differ from the defaults: "));CatN(out,cap,changed);Cat(out,cap,u". ");
        if(BaselineOn()) {size_t same=0;for(size_t i=0;i<n;++i)if(ask::Applies(rows[i]) && Changed(rows[i]) && Inherited(rows,i))++same;
            if(same) {Cat(out,cap,T(u"Z tego wspólnych dla wszystkich systemów, już wymienionych: ",u"Of these, shared by every system and already named: "));CatN(out,cap,same);Cat(out,cap,u". ");}}
    }
    for(const auto& g:groups) {
        if(onlyGroup && !ask::Same(onlyGroup,g.id))continue;
        bool named=false;
        for(size_t i=0;i<n;++i) {
            const Question& q=rows[i];
            if(!q.group || !ask::Same(q.group,g.id) || !ask::Applies(q) || !Changed(q))continue;
            if(!onlyGroup && Inherited(rows,i))continue;   // a plan: said once, with the shared answers
            if(!named) {Cat(out,cap,Pl()?g.pl:g.en);Cat(out,cap,u": ");named=true;}
            Cat(out,cap,Pl()?q.pl:q.en);Cat(out,cap,u" ");
            static char16_t value[400];ValueText(q,value,400);Cat(out,cap,value);Cat(out,cap,u". ");
        }
    }
}
// ---- the carriers ---------------------------------------------------------------------------
struct Out {char* data;size_t capacity,size;bool overflow;
    void Add(const char* s) {while(*s) {if(size+1>=capacity) {overflow=true;return;}data[size++]=*s++;}data[size]=0;}
    void Char(char c) {char s[2]={c,0};Add(s);}};
// Linux: /var/lib/charon/answers.env, KEY='value' lines for sh (a quote inside a value is closed, escaped, opened).
inline size_t Env(char* buffer,size_t capacity) {
    Out o{buffer,capacity,0,false};const Question* rows;size_t n;Table(rows,n);
    o.Add("# Charon: answers of the detailed mode that differ from the defaults. Read by personal.sh.\n");
    o.Add("charon_path='");o.Char(C().path);o.Add("'\ncharon_user='");o.Add(ask::Get("user"));o.Add("'\ncharon_ssh='");o.Add(ask::Get("ssh"));o.Add("'\n");
    for(size_t i=0;i<n;++i) {
        const Question& q=rows[i];if(!ask::Applies(q) || !Changed(q))continue;
        o.Add(q.key);o.Add("='");
        for(const char* v=ask::Get(q.key);*v;++v) {if(*v=='\'')o.Add("'\\''");else if(*v=='\n' || *v=='\r')o.Char(' ');else o.Char(*v);}
        o.Add("'\n");
    }
    return o.overflow?0:o.size;
}
// Windows: \charon\answers.ini, key=value lines, UTF-8, CR LF.
inline size_t Ini(char* buffer,size_t capacity) {
    Out o{buffer,capacity,0,false};const Question* rows;size_t n;Table(rows,n);
    o.Add("; Charon: answers of the detailed mode that differ from the defaults. Read by Charon-Personal.ps1.\r\n");
    for(size_t i=0;i<n;++i) {
        const Question& q=rows[i];if(!ask::Applies(q) || !Changed(q))continue;
        o.Add(q.key);o.Char('=');
        for(const char* v=ask::Get(q.key);*v;++v)o.Char(*v=='\n' || *v=='\r'?' ':*v);
        o.Add("\r\n");
    }
    return o.overflow?0:o.size;
}
// ---- saved answer sets: the text of \EFI\HAK\answers\<name>.ini (CharonWsAnswerSets.h reads and writes the file) ----
// `main` are the system's own tables (their keys are saved too, so that a set repeats a whole dialogue).
struct MainTables {const Question* table[4];size_t count[4];size_t tables;};
inline const char* KindName() {
    switch(C().path) {case 'd':return "debian";case 'u':return "ubuntu";case 'D':return "image-debian";case 'U':return "image-ubuntu";
        case 'A':return "image-arch";case 'F':return "image-fedora";case 'w':return "windows";default:return "charondos";}
}
inline size_t SetText(const MainTables& main,const char* name,bool withSecrets,char* buffer,size_t capacity) {
    Out o{buffer,capacity,0,false};
    o.Add("[charon-answers]\r\nformat=1\r\nmade=charon-r410\r\n\r\n[plan]\r\nversion=1\r\nname=");o.Add(name);
    o.Add("\r\nsystems=");o.Add(KindName());o.Add("\r\nmode=detailed\r\n\r\n[system.");o.Add(KindName());o.Add("]\r\nkind=");o.Add(KindName());o.Add("\r\n");
    static char secrets[2048];Out s{secrets,sizeof(secrets),0,false};secrets[0]=0;
    auto one=[&](const Question& q,bool always) {
        if(ask::Find(q.key)==SIZE_MAX)return;
        if(q.kind==Kind::secret || (q.flags&ask::flagSecret)) {
            s.Add(q.key);s.Char('=');if(withSecrets)s.Add(ask::Get(q.key));else s.Add("ask");s.Add("\r\n");return;}
        if(!always && !Changed(q))return;
        o.Add(q.key);o.Char('=');for(const char* v=ask::Get(q.key);*v;++v)o.Char(*v=='\n' || *v=='\r'?' ':*v);o.Add("\r\n");
    };
    for(size_t t=0;t<main.tables;++t)for(size_t i=0;i<main.count[t];++i)one(main.table[t][i],true);
    const Question* rows;size_t n;Table(rows,n);
    for(size_t i=0;i<n;++i)if(ask::Applies(rows[i]))one(rows[i],false);
    o.Add("\r\n[secrets]\r\n");
    if(withSecrets)o.Add("; This file holds passwords in clear text. Keep it away from other people.\r\n");
    o.Add(secrets);
    for(auto& c:secrets)c=0;
    return o.overflow || s.overflow?0:o.size;
}
// Loads a set's text into the answer store. Keys this system does not know are counted, never applied;
// a value that is not one of a choice's values is counted as bad and the default stays. Secrets that say
// "ask" (or are missing) are left for the dialogue. False: not an answer set this Charon can read.
struct Loaded {size_t applied,unknown,bad;bool otherSystem;bool secretsInside;};
inline const Question* FindKey(const MainTables& main,const char* key) {
    for(size_t t=0;t<main.tables;++t)for(size_t i=0;i<main.count[t];++i)if(ask::Same(main.table[t][i].key,key))return &main.table[t][i];
    const Question* rows;size_t n;Table(rows,n);
    for(size_t i=0;i<n;++i)if(ask::Same(rows[i].key,key))return &rows[i];
    return nullptr;
}
inline bool ValueFits(const Question& q,const char* value) {
    if(q.kind==Kind::yesNo)return ask::Same(value,"yes") || ask::Same(value,"no");
    if(q.kind==Kind::choice) {for(size_t k=0;k<q.count;++k)if(ask::Same(q.choices[k].value,value))return true;return false;}
    if(q.kind==Kind::multi) {
        const char* p=value;
        while(*p) {const char* e=p;while(*e && *e!=',')++e;bool known=false;
            for(size_t k=0;k<q.count && !known;++k) {const char* c=q.choices[k].value;size_t len=0;while(c[len])++len;
                if(size_t(e-p)==len) {known=true;for(size_t i=0;i<len;++i)if(p[i]!=c[i])known=false;}}
            if(!known)return false;p=*e?e+1:e;}
        return true;}
    if(q.kind==Kind::number) {
        int64_t v=0;size_t i=0;const bool minus=value[0]=='-';if(minus)i=1;if(!value[i])return false;
        for(;value[i];++i) {if(value[i]<'0' || value[i]>'9' || v>100000000)return false;v=v*10+(value[i]-'0');}
        if(minus)v=-v;return v>=q.low && v<=q.high;}
    return true;
}
inline bool LoadSet(const MainTables& main,char* text,size_t size,Loaded& result) {
    result=Loaded{0,0,0,false,false};
    bool header=false,formatOk=false;char section[48]={};
    for(size_t at=0;at<size;) {
        size_t end=at;while(end<size && text[end]!='\n')++end;
        size_t stop=end;while(stop>at && (text[stop-1]=='\r' || text[stop-1]==' ' || text[stop-1]=='\t'))--stop;
        char* line=text+at;const char saved=text[stop<size?stop:size-1];if(stop<size)text[stop]=0;
        (void)saved;
        while(*line==' ' || *line=='\t')++line;
        if(*line=='[') {size_t n=0;for(const char* p=line+1;*p && *p!=']' && n+1<sizeof(section);++p)section[n++]=*p;section[n]=0;
            if(ask::Same(section,"charon-answers"))header=true;}
        else if(*line && *line!='#' && *line!=';') {
            char* eq=line;while(*eq && *eq!='=')++eq;
            if(*eq=='=') {
                *eq=0;const char* key=line;const char* value=eq+1;
                const bool inSystem=section[0]=='s' && section[1]=='y' && section[2]=='s';
                if(ask::Same(section,"charon-answers") && ask::Same(key,"format"))formatOk=ask::Same(value,"1");
                else if(inSystem && ask::Same(key,"kind")) {if(!ask::Same(value,KindName()))result.otherSystem=true;}
                else if(inSystem || ask::Same(section,"shared") || ask::Same(section,"secrets")) {
                    const Question* q=FindKey(main,key);
                    if(!q)++result.unknown;
                    else if(ask::Same(section,"secrets")) {if(!ask::Same(value,"ask") && value[0]) {if(ask::Put(q->key,value)) {++result.applied;result.secretsInside=true;}}}
                    else if(q->kind==Kind::secret || (q->flags&ask::flagSecret))++result.unknown;   // a secret outside [secrets] is not taken
                    else if(!ValueFits(*q,value))++result.bad;
                    else if(ask::Put(q->key,value))++result.applied;
                }
                *eq='=';
            }
        }
        at=end+1;
    }
    return header && formatOk;
}
// ---- the dialogue ---------------------------------------------------------------------------
// The system's own question tables of this dialogue (saved with a set, filled from a set).
inline MainTables& Main() {static MainTables main{};return main;}
inline void SetMain(const Question* a,size_t na,const Question* b=nullptr,size_t nb=0) {
    auto& m=Main();m.tables=0;
    if(a) {m.table[m.tables]=a;m.count[m.tables++]=na;}
    if(b) {m.table[m.tables]=b;m.count[m.tables++]=nb;}
}
// "Save these answers as a set": set by CharonWsAnswerSets.h (files on Charon's own volume); null: not offered.
using Hook=void (*)();
// "<set>: which programs?" is asked by the catalogue reader (CharonWsCatalogue.h); null: single programs cannot be
// chosen here, the set's default programs are installed.
using RowHook=int (*)(const Question&);
inline RowHook& PickHook() {static RowHook hook=nullptr;return hook;}
inline bool IsSetRow(const Question& q) {const char* k=q.key;if(k[0]=='w' && k[1]=='_')k+=2;return k[0]=='s' && k[1]=='e' && k[2]=='t' && k[3]=='_';}
inline int AskRow(const Question& q) {if(IsSetRow(q))return PickHook()?PickHook()(q):1;return ask::AskOne(q,true);}
inline Hook& SaveHook() {static Hook hook=nullptr;return hook;}
// Lets a long text be heard: ends when speech is idle, at a key press (the key stays for the next
// question) or when the voice rests after a stall; never longer than `tenths`.
inline void Hear(unsigned tenths) {
#ifndef CHARON_INSTALL_HOST
    auto* input=U().host->system?U().host->system->ConIn:nullptr;
    // r410 (known issue 5, seen in VMware: the summary was cut after 3 s by "What next?"): a long text is spoken
    // in pieces and the voice is idle for a moment between them, so only 1.5 s of unbroken silence ends the wait.
    unsigned idle=0;
    for(unsigned i=0;i<tenths;++i) {
        U().host->boot->Stall(100000);Pump();auto* fx=U().host->effects;
        idle=fx && fx->SpeechIdle && fx->SpeechIdle(fx->context)?idle+1:0;
        if(i>5 && idle>=15)break;
        if(input && input->WaitForKey && U().host->boot->CheckEvent(input->WaitForKey)==EFI_SUCCESS)break;
        if(i>5 && hakefi::VoiceResting())break;
    }
#else
    (void)tenths;
#endif
}
inline size_t InGroup(const Group& g,size_t* changed=nullptr) {
    const Question* rows;size_t n;Table(rows,n);size_t count=0;if(changed)*changed=0;
    for(size_t i=0;i<n;++i)if(rows[i].group && ask::Same(rows[i].group,g.id) && ask::Applies(rows[i])) {++count;if(changed && Changed(rows[i]))++*changed;}
    return count;
}
inline bool HasAdminRows() {
    const Question* rows;size_t n;Table(rows,n);
    for(size_t i=0;i<n;++i)if(rows[i].level && (!rows[i].when || rows[i].when()))return true;
    return false;
}
// Asks the questions of one group. Escape at its first question returns to the list.
inline void OpenGroup(const Group& g) {
    const Question* rows;size_t n;Table(rows,n);
    static size_t order[ask::maxAnswers];
    for(size_t at=0;;) {
        // the list is made again after every answer: an answer can add or remove a later question
        size_t count=0;for(size_t i=0;i<n && count<ask::maxAnswers;++i)if(rows[i].group && ask::Same(rows[i].group,g.id) && ask::Applies(rows[i]))order[count++]=i;
        if(at>=count)return;
        const int r=AskRow(rows[order[at]]);
        if(r==2)continue;
        if(r==1) {++at;continue;}
        if(!at)return;
        --at;Say(T(u"Wracam do poprzedniego pytania.",u"Back to the previous question."));
    }
}
inline bool Contains(const char16_t* text,const char16_t* word) {
    if(!text)return false;
    for(size_t i=0;text[i];++i) {size_t k=0;while(word[k] && text[i+k] && Fold(text[i+k])==Fold(word[k]))++k;if(!word[k])return true;}
    return false;
}
// F3: part of a word; the matching questions as a list; Enter asks the chosen one.
inline void Search() {
    static char16_t word[40];word[0]=0;
    if(!Prompt(T(u"Szukaj pytania, wpisz część słowa:",u"Search for a question, type part of a word:"),word,39) || !word[0])return;
    const Question* rows;size_t n;Table(rows,n);
    static Choice found[ask::maxBoxes];static size_t which[ask::maxBoxes];size_t count=0;
    static char16_t keyWide[ask::maxBoxes][48];
    for(size_t i=0;i<n && count<ask::maxBoxes;++i) {
        const Question& q=rows[i];if(!q.group || !ask::Applies(q))continue;
        ask::Wide(q.key,keyWide[count],48);
        if(Contains(q.pl,word) || Contains(q.en,word) || Contains(keyWide[count],word)) {found[count]=Choice{q.pl,q.en,q.key};which[count]=i;++count;}
    }
    if(!count) {Say(T(u"Żadne pytanie nie pasuje.",u"No question matches."));return;}
    size_t index=0;static char16_t label[120];Set(label,120,T(u"Znalezione pytania: ",u"Questions found: "));CatN(label,120,count);Cat(label,120,u".");
    if(!ask::Choose(label,found,count,index))return;
    while(AskRow(rows[which[index]])==2) {}
}
// The group list. False: Escape (the caller leaves the dialogue as after Escape at a first question).
inline bool Run() {
    C().active=true;
    const Question* rows;size_t n;Table(rows,n);
    for(size_t i=0;i<n;++i)if(ask::Find(rows[i].key)==SIZE_MAX && rows[i].fallback)ask::Put(rows[i].key,rows[i].fallback);
    if(!ask::Complete())return false;
    size_t index=0;bool intro=true;
    for(;;) {
        static const Group* shown[64];size_t count=0;
        for(const auto& g:groups)if((!g.level || ask::AdminOpen()) && InGroup(g) && count<63)shown[count++]=&g;
        const size_t total=count+1;if(index>=total)index=total-1;
        static char16_t line[500];
        auto say=[&](bool withIntro) {
            line[0]=0;
            if(withIntro)Cat(line,500,T(u"Grupy pytań szczegółowych. Enter otwiera grupę, D czyta zmienione odpowiedzi grupy, F3 szuka pytania, F10 kończy. ",
                                         u"Groups of detailed questions. Enter opens a group, D reads the group's changed answers, F3 searches for a question, F10 finishes. "));
            PositionVoice(line+Len(line),500-Len(line),index);
            if(index==count)Cat(line,500,T(u"Gotowe: przejdź dalej",u"Done: go on"));
            else {const Group& g=*shown[index];size_t changed=0;const size_t all=InGroup(g,&changed);
                Cat(line,500,Pl()?g.pl:g.en);
                if(changed) {Cat(line,500,T(u": zmienione ",u": changed "));CatN(line,500,changed);Cat(line,500,T(u" z ",u" of "));CatN(line,500,all);}
                else {Cat(line,500,T(u": domyślne, pytań: ",u": defaults, questions: "));CatN(line,500,all);}}
            Cat(line,500,u", ");CatN(line,500,index+1);Cat(line,500,T(u" z ",u" of "));CatN(line,500,total);
            static char16_t echo[300];Set(echo,300,index==count?u"  ":u"► ");
            Cat(echo,300,index==count?T(u"Gotowe: przejdź dalej",u"Done: go on"):(Pl()?shown[index]->pl:shown[index]->en));Echo(echo);
            Say(line);
        };
        say(intro);intro=false;
        bool done=false;
        for(;;) {
            const Key key=WaitKey();
            if(IsCancel(key)) {Echo(u"");return false;}
            if(key.scan==scDown) {index=(index+1)%total;Cue(MenuCue::focus);say(false);}
            else if(key.scan==scUp) {index=(index+total-1)%total;Cue(MenuCue::focus);say(false);}
            else if(key.scan==scHome) {index=0;say(false);}
            else if(key.scan==scEnd) {index=total-1;say(false);}
            else if(key.scan==scF1)say(true);
            else if(key.scan==scF3) {Search();break;}
            else if(key.scan==scF10) {done=true;break;}
            else if(!key.scan && key.ch==13) {if(index==count)done=true;else OpenGroup(*shown[index]);break;}
            else if(Printable(key) && Fold(key.ch)==u'd' && index<count) {
                static char16_t text[1600];Summary(text,1600,shown[index]->id);
                Say(text[0]?text:T(u"W tej grupie wszystko ma wartości domyślne.",u"Everything in this group has its default."));}
            else if(Printable(key)) {
                for(size_t step=1;step<count;++step) {const size_t i=(index+step)%count;const char16_t* name=Pl()?shown[i]->pl:shown[i]->en;
                    if(Fold(name[0])==Fold(key.ch)) {index=i;say(false);break;}}
            }
        }
        if(!done)continue;
        Echo(u"");
        // The administration gate: once, after the personal groups; default no.
        if(!ask::AdminOpen() && HasAdminRows()) {
            static const Choice gate[]={{u"nie",u"no","no"},{u"tak",u"yes","yes"}};size_t answer=0;
            if(!ask::Choose(T(u"Pokazać grupy administracyjne: konta, sieć, zasady, wdrażanie?",u"Show the administration groups: accounts, network, policies, deployment?"),gate,2,answer))continue;
            if(answer==1) {ask::AdminOpen()=true;index=0;intro=true;continue;}
        }
        // The summary: only what differs from the defaults; then go on, change something, or save the set.
        static char16_t text[2400];Summary(text,2400);Say(text);Hear(900);
        static const Choice next[]={{u"dalej",u"go on","go"},{u"zmień odpowiedź",u"change an answer","change"},
            {u"zapisz te odpowiedzi jako zestaw",u"save these answers as a set","save"}};
        size_t pick=0;
        if(!ask::Choose(T(u"Co dalej?",u"What next?"),next,SaveHook()?3:2,pick) || pick==1) {intro=true;continue;}
        if(pick==2) {SaveHook()();intro=true;continue;}
        return ask::Complete();
    }
}
}  // namespace detailed
}  // namespace ws
}  // namespace hakefi
