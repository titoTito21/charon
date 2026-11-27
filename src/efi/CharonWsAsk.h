#pragma once
// r400: Charon workspace - spoken questionnaires. A table of questions
// (free text, a hidden text typed twice, a choice from a list, yes/no) is
// asked one by one on the echo line; in the quick mode only the questions
// marked quick are asked and the others take their defaults. Escape goes back
// one question (and leaves at the first), so nothing is decided by accident.
// GPL-2.0-or-later.
#include "CharonWsCore.h"
#include "CharonCrypt.h"
namespace hakefi {
namespace ws {
namespace ask {
enum class Kind : uint8_t {text,secret,choice,yesNo,name,multi,number};   // name: lower-case login/host name; multi: check boxes, the value lists the checked values with commas
struct Choice {const char16_t* pl;const char16_t* en;const char* value;};
struct Question {
    // r410: every member has an initialiser, so that a static Question (or array of them) is constant-initialised:
    // the firmware build has no C++ runtime, and a guarded static does not link.
    const char* key=nullptr;bool quick=false;Kind kind=Kind::text;
    const char16_t* pl=nullptr;const char16_t* en=nullptr;
    const char* fallback=nullptr;         // default value (for a choice: one of the values)
    const Choice* choices=nullptr;size_t count=0;
    const char16_t* helpPl=nullptr;const char16_t* helpEn=nullptr;   // said after the question in the detailed mode (may be null)
    // r410, all optional (the tables written before r410 keep compiling and keep their meaning):
    const char* group=nullptr;            // group of the detailed mode; null: the system's own main questions
    uint8_t level=0;                      // 0 personal, 1 administration (asked only after the administration gate)
    bool (*when)()=nullptr;               // asked only when it returns true; the default is stored either way
    uint8_t flags=0;                      // flagSecret: never read back or written to a saved set without consent
    int32_t low=0,high=0;                 // Kind::number: the allowed range
    const char* risk=nullptr;             // the value that can leave the system inaccessible or unsafe: typed word needed
};
constexpr uint8_t flagSecret=1;
inline bool& AdminOpen() {static bool open=false;return open;}   // r410: the administration gate was answered yes
inline bool Applies(const Question& q) {return (!q.when || q.when()) && (!q.level || AdminOpen());}
// r409: room for the detailed dialogue (about 200 questions) and for plans of several systems.
// Until r408 the store held 48 keys of 159 bytes; a 49th key silently took over the last slot and
// a longer value was silently cut. Now: 384 keys; a value under 512 bytes lies in the key's own
// slot, a longer one (a list of programs, an SSH key, an address; under 4096 bytes) in one of 32
// long slots. What still does not fit is NOT stored and NOT cut: Put returns false, the previous
// value stays and the store remembers the key (Overflow, LostKey), so the dialogue can say so
// (Complete below) instead of installing with an answer missing. The store is static storage
// (about 330 KB), never on the stack.
constexpr size_t maxAnswers=384,valueUnits=512;
constexpr size_t maxLongAnswers=32,longUnits=4096;
struct Answers {
    const char* keys[maxAnswers];char values[maxAnswers][valueUnits];
    uint8_t longOf[maxAnswers];                       // 0, or 1 + the long slot that holds this key's value
    char longs[maxLongAnswers][longUnits];bool longUsed[maxLongAnswers];
    size_t count;
    bool overflow;const char* lostKey;                // an answer that could not be kept, and which
};
static_assert(maxLongAnswers<255,"longOf is one byte");
inline Answers& A() {static Answers answers;return answers;}
inline bool Same(const char* a,const char* b) {for(size_t i=0;;++i) {if(a[i]!=b[i])return false;if(!a[i])return true;}}
inline size_t Find(const char* key) {
    auto& a=A();
    for(size_t i=0;i<a.count;++i)if(Same(a.keys[i],key))return i;
    return SIZE_MAX;
}
inline const char* Get(const char* key) {
    auto& a=A();const size_t i=Find(key);
    if(i==SIZE_MAX)return "";
    return a.longOf[i]?a.longs[a.longOf[i]-1]:a.values[i];
}
inline bool Overflow() {return A().overflow;}
inline const char* LostKey() {return A().lostKey?A().lostKey:"";}
inline bool Is(const char* key,const char* value) {return Same(Get(key),value);}
// r403: one value of a check-box answer ("gnome-desktop,xfce-desktop").
inline bool Has(const char* key,const char* value) {
    const char* p=Get(key);size_t n=0;while(value[n])++n;
    while(*p) {
        const char* e=p;while(*e && *e!=',')++e;
        if(size_t(e-p)==n) {bool same=true;for(size_t i=0;i<n;++i)if(p[i]!=value[i])same=false;if(same)return true;}
        p=*e?e+1:e;
    }
    return false;
}
inline bool Any(const char* key) {return Get(key)[0]!=0;}
// False: the value was not stored (no room for another key, value of 4096 bytes or more, or no
// long slot left); the key's previous value is unchanged and Overflow() is set.
inline bool Put(const char* key,const char* value) {
    auto& a=A();size_t n=0;while(value[n])++n;
    auto lost=[&]() {a.overflow=true;a.lostKey=key;return false;};
    size_t i=Find(key);const bool fresh=i==SIZE_MAX;
    if(n>=longUnits || (fresh && a.count>=maxAnswers))return lost();
    if(n<valueUnits) {
        if(fresh) {i=a.count++;a.keys[i]=key;a.longOf[i]=0;}
        if(value!=a.values[i]) {for(size_t k=0;k<n;++k)a.values[i][k]=value[k];a.values[i][n]=0;}
        if(a.longOf[i]) {const size_t slot=a.longOf[i]-1;for(auto& c:a.longs[slot])c=0;a.longUsed[slot]=false;a.longOf[i]=0;}
        return true;
    }
    size_t slot=fresh || !a.longOf[i]?SIZE_MAX:size_t(a.longOf[i]-1);
    if(slot==SIZE_MAX) {
        for(size_t k=0;k<maxLongAnswers && slot==SIZE_MAX;++k)if(!a.longUsed[k])slot=k;
        if(slot==SIZE_MAX)return lost();
    }
    if(fresh) {i=a.count++;a.keys[i]=key;a.values[i][0]=0;}
    if(value!=a.longs[slot]) {for(size_t k=0;k<n;++k)a.longs[slot][k]=value[k];a.longs[slot][n]=0;}
    a.longUsed[slot]=true;a.longOf[i]=uint8_t(slot+1);
    for(auto& c:a.values[i])c=0;
    return true;
}
inline void Clear() {
    auto& a=A();
    for(auto& v:a.values)for(auto& c:v)c=0;
    for(auto& v:a.longs)for(auto& c:v)c=0;
    for(auto& u:a.longUsed)u=false;
    for(auto& l:a.longOf)l=0;
    a.count=0;a.overflow=false;a.lostKey=nullptr;
}
inline void Wide(const char* in,char16_t* out,size_t cap) {Utf8ToUtf16(reinterpret_cast<const uint8_t*>(in),crypt::Length(in),out,cap);}
// r409: before an answer file is written. False: an answer could not be kept, and it was said
// which one; the installation must not start with an answer missing.
inline bool Complete() {
    if(!Overflow())return true;
    static char16_t text[400];static char16_t key[80];Wide(LostKey(),key,80);
    Set(text,400,T(u"Nie udało się zapamiętać odpowiedzi „",u"The answer \""));Cat(text,400,key);
    Cat(text,400,T(u"”: jest za długa albo odpowiedzi jest za dużo dla tej wersji Charona. Instalacja nie zostanie uruchomiona, a na dysku nic się nie zmieniło.",
                   u"\" could not be kept: it is too long, or there are too many answers for this version of Charon. The installation will not be started, and nothing on a disk has changed."));
    Fail(text);return false;
}
// r409: stores a typed or chosen answer; when it does not fit, says so and returns false, so the
// question is asked again (an overflow that was already there stays recorded).
inline bool Keep(const char* key,const char* value) {
    auto& a=A();const bool before=a.overflow;const char* lostBefore=a.lostKey;
    if(Put(key,value))return true;
    a.overflow=before;a.lostKey=lostBefore;
    Fail(T(u"Ta odpowiedź się nie zmieściła: jest za długa albo nie ma już miejsca na kolejne odpowiedzi. Podaj krótszą albo naciśnij Escape, żeby wrócić.",
           u"This answer did not fit: it is too long, or there is no room for more answers. Give a shorter one, or press Escape to go back."));
    return false;
}
// A hidden text: nothing is shown or spoken but "star"; typed twice.
inline bool Secret(const char16_t* label,char16_t* out,size_t cap) {
    static char16_t first[80],second[80];
    for(int pass=0;pass<2;++pass) {
        char16_t* text=pass?second:first;size_t n=0;text[0]=0;
        static char16_t line[300];Set(line,300,pass?T(u"Powtórz hasło:",u"Type the password again:"):label);Say(line);
        for(;;) {
            static char16_t shown[300];Set(shown,300,line);Cat(shown,300,u" ");for(size_t i=0;i<n && i<60;++i)CatC(shown,300,u'*');
            Echo(shown);Cursor(true,Len(shown),U().rows-1);
            const Key key=WaitKey();
            if(IsCancel(key)) {Cursor(false);Echo(u"");for(auto& c:first)c=0;for(auto& c:second)c=0;return false;}
            if(!key.scan && key.ch==13)break;
            if(!key.scan && key.ch==8) {if(n) {text[--n]=0;Play(Icon::deleted);} else Play(Icon::edge);continue;}
            if(Printable(key) && n+1<79 && n+1<cap) {text[n++]=key.ch;text[n]=0;Tone(880,20);SayEcho(T(u"gwiazdka",u"star"));}
        }
        Cursor(false);
    }
    Echo(u"");
    bool same=true;for(size_t i=0;;++i) {if(first[i]!=second[i]) {same=false;break;}if(!first[i])break;}
    if(!same) {for(auto& c:first)c=0;for(auto& c:second)c=0;Fail(T(u"Hasła się różnią. Wpisz hasło jeszcze raz.",u"The passwords differ. Type the password again."));return Secret(label,out,cap);}
    Set(out,cap,first);for(auto& c:first)c=0;for(auto& c:second)c=0;
    return true;
}
// r410 (agent 2's run 23, limit a): the options of a list question stand on the screen as a list - one row each,
// indented, the current one marked "►" - at the bottom of the body above one blank row. Before, the screen held only
// the echo row and the inverse mode line, and Charon's reader took the help line for "entry 1 of 2". The indent keeps
// the help and echo rows (column 0) out of the list the reader finds. `on` (check boxes) adds [x] / [ ].
inline void DrawOptions(const Choice* choices,size_t count,size_t index,const bool* on=nullptr) {
#ifndef CHARON_INSTALL_HOST
    auto& u=U();if(!u.out || u.rows<8 || !count || !Script().on)return;   // only in the dialogues that keep a transcript
    const size_t body=BodyRows();if(body<4)return;
    const size_t room=body-2;const size_t shown=count<room?count:room;
    size_t top=index>=shown?index-shown+1:0;if(top+shown>count)top=count-shown;
    const size_t first=1+body-1-shown;   // the last body row (rows-3) stays blank
    Row(first-1,u"",7);   // one blank row parts the list from the transcript above it (which starts in column 0)
    static char16_t text[300];
    for(size_t k=0;k<shown;++k) {
        const size_t i=top+k;Set(text,300,i==index?u"    ► ":u"      ");
        if(on)Cat(text,300,on[i]?u"[x] ":u"[ ] ");
        Cat(text,300,Pl()?choices[i].pl:choices[i].en);Row(first+k,text,7);
    }
    Row(1+body-1,u"",7);
#else
    (void)choices;(void)count;(void)index;(void)on;
#endif
}
inline void RestoreBody() {
#ifndef CHARON_INSTALL_HOST
    if(Script().on)ScriptDraw();
#endif
}
// One of several values. Up/Down (or Left/Right) move, a letter jumps, Enter takes.
inline bool Choose(const char16_t* label,const Choice* choices,size_t count,size_t& index,size_t fallbackIndex=SIZE_MAX) {
    static char16_t line[400];
    auto say=[&](bool withLabel) {
        line[0]=0;if(withLabel) {Cat(line,400,label);Cat(line,400,u" ");}
        PositionVoice(line+Len(line),400-Len(line),index);   // r401: as the boot menu's lists
        Cat(line,400,Pl()?choices[index].pl:choices[index].en);
        Cat(line,400,u", ");CatN(line,400,index+1);Cat(line,400,T(u" z ",u" of "));CatN(line,400,count);
        static char16_t shown[400];Set(shown,400,label);Cat(shown,400,u"  ");Cat(shown,400,Pl()?choices[index].pl:choices[index].en);Echo(shown);
        Say(line);DrawOptions(choices,count,index);   // r410: the list on the screen, after the transcript line
    };
    if(index>=count)index=0;
    say(true);
    for(;;) {
        const Key key=WaitKey();
        if(IsCancel(key)) {Echo(u"");RestoreBody();return false;}
        if(!key.scan && key.ch==13) {Echo(u"");RestoreBody();Play(Icon::mark);return true;}
        if(key.scan==scDown || key.scan==scRight) {index=(index+1)%count;Cue(MenuCue::focus);say(false);}
        else if(key.scan==scUp || key.scan==scLeft) {index=(index+count-1)%count;Cue(MenuCue::focus);say(false);}
        else if(key.scan==scHome) {index=0;say(false);}
        else if(key.scan==scEnd) {index=count-1;say(false);}
        else if(Printable(key)) {
            for(size_t step=1;step<=count;++step) {
                const size_t i=(index+step)%count;const char16_t* name=Pl()?choices[i].pl:choices[i].en;
                if(Fold(name[0])==Fold(key.ch)) {index=i;say(false);break;}
            }
        }
        else if(key.scan==scF1)say(true);
        else if(key.scan==scF2 && fallbackIndex<count) {   // r410: the default again
            static char16_t text[300];Set(text,300,T(u"Domyślnie: ",u"Default: "));Cat(text,300,Pl()?choices[fallbackIndex].pl:choices[fallbackIndex].en);Say(text);}
        else if(key.scan==scDelete && fallbackIndex<count) {index=fallbackIndex;Play(Icon::deleted);say(false);}   // r410: back to the default
    }
}
// r403: check boxes - several values at once (desktops, screen readers).
// Up/Down move, Space checks or clears, a letter jumps, Enter takes the
// checked ones (none checked is an answer too: "none").
constexpr size_t maxBoxes=64;      // r409: was 24; a set of programs has more boxes than a list of desktops
constexpr size_t typedUnits=256;   // r409: a typed answer: up to 255 characters (was 79)
inline bool Pick(const char16_t* label,const Choice* choices,size_t count,bool* on,const bool* fallbackOn=nullptr) {
    static char16_t line[500];size_t index=0;if(count>maxBoxes)count=maxBoxes;
    auto say=[&](bool withLabel) {
        line[0]=0;if(withLabel) {Cat(line,500,label);Cat(line,500,u" ");}
        PositionVoice(line+Len(line),500-Len(line),index);
        Cat(line,500,Pl()?choices[index].pl:choices[index].en);
        Cat(line,500,on[index]?T(u", zaznaczone, ",u", checked, "):T(u", niezaznaczone, ",u", not checked, "));
        CatN(line,500,index+1);Cat(line,500,T(u" z ",u" of "));CatN(line,500,count);
        if(withLabel)Cat(line,500,T(u". Spacja zaznacza, Enter zatwierdza.",u". Space checks, Enter confirms."));
        static char16_t shown[500];Set(shown,500,label);Cat(shown,500,on[index]?u"  [x] ":u"  [ ] ");Cat(shown,500,Pl()?choices[index].pl:choices[index].en);Echo(shown);
        Say(line);DrawOptions(choices,count,index,on);   // r410
    };
    say(true);
    for(;;) {
        const Key key=WaitKey();
        if(IsCancel(key)) {Echo(u"");RestoreBody();return false;}
        if(!key.scan && key.ch==13) {
            Echo(u"");RestoreBody();Play(Icon::mark);
            static char16_t summary[600];Set(summary,600,T(u"Wybrano: ",u"Chosen: "));bool any=false;
            for(size_t i=0;i<count;++i)if(on[i]) {if(any)Cat(summary,600,u", ");Cat(summary,600,Pl()?choices[i].pl:choices[i].en);any=true;}
            if(!any)Set(summary,600,T(u"Nic nie zaznaczono.",u"Nothing checked."));
            Say(summary);return true;
        }
        if(!key.scan && key.ch==u' ') {on[index]=!on[index];Play(on[index]?Icon::mark:Icon::deleted);
            Say(on[index]?T(u"zaznaczone",u"checked"):T(u"niezaznaczone",u"not checked"));
            static char16_t shown[500];Set(shown,500,label);Cat(shown,500,on[index]?u"  [x] ":u"  [ ] ");Cat(shown,500,Pl()?choices[index].pl:choices[index].en);Echo(shown);DrawOptions(choices,count,index,on);}
        else if(key.scan==scDown || key.scan==scRight) {index=(index+1)%count;Cue(MenuCue::focus);say(false);}
        else if(key.scan==scUp || key.scan==scLeft) {index=(index+count-1)%count;Cue(MenuCue::focus);say(false);}
        else if(key.scan==scHome) {index=0;say(false);}
        else if(key.scan==scEnd) {index=count-1;say(false);}
        else if(key.scan==scF1)say(true);
        else if(key.scan==scF2 && fallbackOn) {   // r410: the default again
            static char16_t text[600];Set(text,600,T(u"Domyślnie: ",u"Default: "));bool any=false;
            for(size_t i=0;i<count;++i)if(fallbackOn[i]) {if(any)Cat(text,600,u", ");Cat(text,600,Pl()?choices[i].pl:choices[i].en);any=true;}
            if(!any)Cat(text,600,T(u"nic nie zaznaczone",u"nothing checked"));Say(text);}
        else if(key.scan==scDelete && fallbackOn) {for(size_t i=0;i<count;++i)on[i]=fallbackOn[i];Play(Icon::deleted);say(false);}   // r410: back to the default
        else if(Printable(key)) {
            for(size_t step=1;step<=count;++step) {
                const size_t i=(index+step)%count;const char16_t* name=Pl()?choices[i].pl:choices[i].en;
                if(Fold(name[0])==Fold(key.ch)) {index=i;say(false);break;}
            }
        }
    }
}
// r410: the typed word for an answer that can leave the system without speech or unsafe.
inline bool ConfirmRisk(const char16_t* what) {
    static char16_t text[700];Set(text,700,what);
    Cat(text,700,T(u" Jeśli na pewno tego chcesz, wpisz słowo ROZUMIEM i naciśnij Enter. Escape wraca do pytania.",u" If you are sure, type the words I UNDERSTAND and press Enter. Escape goes back to the question."));
    Play(Icon::error);Say(text);
    static char16_t word[24];word[0]=0;
    if(!Prompt(T(u"Wpisz ROZUMIEM:",u"Type I UNDERSTAND:"),word,23,nullptr,nullptr,false))return false;
    if(SameFold(word,T(u"rozumiem",u"i understand")))return true;
    Say(T(u"Nie wpisano potwierdzenia. Odpowiedź nie została zmieniona.",u"No confirmation was typed. The answer was not changed."));return false;
}
inline bool RiskTaken(const Question& q,const char* value) {
    if(!q.risk || !Same(q.risk,value) || Same(Get(q.key),value))return true;   // not risky, or already confirmed before
    static char16_t text[500];Set(text,500,T(u"Uwaga. Ta odpowiedź może sprawić, że system nie będzie mówił albo będzie mniej bezpieczny: ",u"Warning. This answer can leave the system without speech or make it less safe: "));
    Cat(text,500,Pl()?q.pl:q.en);
    return ConfirmRisk(text);
}
// One question. 1: answered; 0: Escape; 2: ask it again (the reason was said).
// The keyboard and time zone that suit the system language already answered; null: no suggestion.
inline const char* Suggested(const char* key) {
    const bool keyboard=Same(key,"keyboard"),zone=Same(key,"timezone");
    if(!keyboard && !zone)return nullptr;
    static const char* const table[][3]={{"pl_PL","pl","Europe/Warsaw"},{"en_US","us","UTC"},{"en_GB","gb","Europe/London"},
        {"de_DE","de","Europe/Berlin"},{"fr_FR","fr","Europe/Paris"},{"es_ES","es","Europe/Madrid"},{"it_IT","it","Europe/Rome"},
        {"uk_UA","ua","Europe/Kyiv"},{"cs_CZ","cz","Europe/Prague"}};
    const char* locale=Get("locale");
    for(const auto& row:table)if(Same(row[0],locale))return keyboard?row[1]:row[2];
    return nullptr;
}
inline int AskOne(const Question& q,bool withHelp) {
    static char16_t label[300];
    Set(label,300,Pl()?q.pl:q.en);
    if(withHelp && (Pl()?q.helpPl:q.helpEn)) {static char16_t help[400];Set(help,400,Pl()?q.helpPl:q.helpEn);Say(help);
        // let the help be heard before the question itself
        for(unsigned i=0;i<25;++i) {U().host->boot->Stall(100000);Pump();auto* fx=U().host->effects;if(i>3 && fx && fx->SpeechIdle && fx->SpeechIdle(fx->context))break;}}
    bool ok=false;
    if(q.kind==Kind::multi) {
        static bool on[maxBoxes];static bool standard[maxBoxes];const size_t n=q.count<maxBoxes?q.count:maxBoxes;
        for(size_t k=0;k<n;++k) {on[k]=Has(q.key,q.choices[k].value);standard[k]=false;
            if(q.fallback) {const char* p=q.fallback;const char* v=q.choices[k].value;size_t len=0;while(v[len])++len;
                while(*p) {const char* e=p;while(*e && *e!=',')++e;if(size_t(e-p)==len) {bool same=true;for(size_t i=0;i<len;++i)if(p[i]!=v[i])same=false;if(same)standard[k]=true;}p=*e?e+1:e;}}}
        ok=Pick(label,q.choices,n,on,q.fallback?standard:nullptr);   // r410: F2/Delete in the main questions too
        if(ok) {static char joined[longUnits];size_t m=0;joined[0]=0;bool fits=true;
            for(size_t k=0;k<n;++k)if(on[k]) {
                const char* v=q.choices[k].value;size_t len=0;while(v[len])++len;
                if(m+len+2>=longUnits) {fits=false;break;}   // r409: was a silent "break": checked boxes were dropped
                if(m)joined[m++]=',';
                for(size_t c=0;c<len;++c)joined[m++]=v[c];joined[m]=0;
            }
            if(!fits) {Fail(T(u"Zaznaczono więcej pozycji, niż mieści jedna odpowiedź. Odznacz część z nich.",u"More items are checked than one answer can hold. Clear some of them."));return 2;}
            if(!RiskTaken(q,joined))return 2;
            if(!Keep(q.key,joined))return 2;}
    } else if(q.kind==Kind::choice || q.kind==Kind::yesNo) {
        static const Choice yesNo[]={{u"tak",u"yes","yes"},{u"nie",u"no","no"}};
        const Choice* list=q.kind==Kind::yesNo?yesNo:q.choices;const size_t n=q.kind==Kind::yesNo?2:q.count;
        size_t index=0;for(size_t k=0;k<n;++k)if(Same(list[k].value,Get(q.key)))index=k;
        // r410 (helper ls): an unanswered keyboard or time zone starts at the one that suits the chosen language
        // (it started at Polish / Warsaw whatever the language was).
        if(!Get(q.key)[0])if(const char* s=Suggested(q.key))for(size_t k=0;k<n;++k)if(Same(list[k].value,s))index=k;
        // r410 (helper pv, R5a): the system language's default follows the interface, as the dialogue pre-sets it
        // (a Polish Charon offers "polski"); F2 / Delete said and restored "English" before.
        const char* fallback=Same(q.key,"locale") && Pl()?"pl_PL":q.fallback;
        size_t standard=SIZE_MAX;if(fallback)for(size_t k=0;k<n;++k)if(Same(list[k].value,fallback))standard=k;
        ok=Choose(label,list,n,index,standard);
        if(ok && !RiskTaken(q,list[index].value))return 2;
        if(ok && !Keep(q.key,list[index].value))return 2;
    } else if(q.kind==Kind::secret) {
        // r409: the buffers hold the longest text that can be typed (79 units here, typedUnits-1 below)
        // in UTF-8, so the conversion never cuts; whether the value fits the store is Keep's business.
        static char16_t wide[80];ok=Secret(label,wide,80);
        if(ok) {static uint8_t utf8[80*3+8];const size_t n=Utf16ToUtf8(wide,Len(wide),utf8,sizeof(utf8)-1);utf8[n]=0;const bool kept=Keep(q.key,reinterpret_cast<const char*>(utf8));
            for(auto& c:wide)c=0;for(auto& c:utf8)c=0;
            if(!kept)return 2;
            if(!Get(q.key)[0]) {Fail(T(u"Hasło nie może być puste.",u"The password cannot be empty."));return 2;}}
    } else {
        static char16_t wide[typedUnits];const char* current=Get(q.key);
        // r409: a stored value too long for the typing line (a list from an answer file) is not shown
        // cut for editing - Enter would then store the cut text. It stays unless something is typed.
        const bool longValue=crypt::Length(current)+2>=typedUnits;   // Utf8ToUtf16 fills at most cap-3 units
        if(longValue) {wide[0]=0;Say(T(u"Obecna wartość jest za długa, żeby ją tu poprawiać. Zostanie bez zmian, jeśli nic nie wpiszesz.",u"The current value is too long to edit here. It is kept unless you type something."));}
        else Utf8ToUtf16(reinterpret_cast<const uint8_t*>(current),crypt::Length(current),wide,typedUnits);
        // r410: F2 says the default of a typed field, Delete puts it back (detailed questions; a main question
        // without a default says "empty").
        static char16_t fallbackWide[typedUnits];fallbackWide[0]=0;
        if(q.fallback && crypt::Length(q.fallback)+2<typedUnits)Utf8ToUtf16(reinterpret_cast<const uint8_t*>(q.fallback),crypt::Length(q.fallback),fallbackWide,typedUnits);
        PromptDefault()=fallbackWide;
        ok=Prompt(label,wide,typedUnits);
        PromptDefault()=nullptr;
        if(ok && longValue && !wide[0])return 1;
        if(ok && q.kind==Kind::name) {
            bool good=wide[0]>=u'a' && wide[0]<=u'z';
            for(size_t i=0;wide[i];++i) {wide[i]=speechtext::LowerCase(wide[i]);if(!((wide[i]>=u'a' && wide[i]<=u'z') || (wide[i]>=u'0' && wide[i]<=u'9') || wide[i]==u'-'))good=false;}
            if(!good || Len(wide)>31) {Fail(T(u"Dozwolone są małe litery bez polskich znaków, cyfry i myślnik; pierwszym znakiem musi być litera.",u"Only lower-case letters without accents, digits and the hyphen are allowed; the first character must be a letter."));return 2;}
        }
        if(ok && q.kind==Kind::number) {
            // r410: digits only, inside the range; an empty line takes the default again
            if(!wide[0] && q.fallback) {if(!Keep(q.key,q.fallback))return 2;return 1;}
            int64_t v=0;bool good=wide[0]!=0;size_t i=0;const bool minus=wide[0]==u'-';if(minus)i=1;
            if(!wide[i])good=false;
            for(;wide[i];++i) {if(wide[i]<u'0' || wide[i]>u'9' || v>100000000) {good=false;break;}v=v*10+(wide[i]-u'0');}
            if(minus)v=-v;
            if(!good || v<q.low || v>q.high) {
                static char16_t text[300];Set(text,300,T(u"Podaj liczbę od ",u"Give a number from "));
                if(q.low<0) {Cat(text,300,T(u"minus ",u"minus "));CatN(text,300,size_t(-int64_t(q.low)));} else CatN(text,300,size_t(q.low));
                Cat(text,300,T(u" do ",u" to "));CatN(text,300,size_t(q.high));Cat(text,300,u".");Fail(text);return 2;}
        }
        if(ok) {static uint8_t utf8[typedUnits*3+8];const size_t n=Utf16ToUtf8(wide,Len(wide),utf8,sizeof(utf8)-1);utf8[n]=0;
            if(!RiskTaken(q,reinterpret_cast<const char*>(utf8)))return 2;
            if(!Keep(q.key,reinterpret_cast<const char*>(utf8)))return 2;}
    }
    return ok?1:0;
}
// Asks the table. False: the user left at the first question.
inline bool Run(const Question* questions,size_t count,bool quick) {
    // Defaults first, so skipped questions have values.
    for(size_t i=0;i<count;++i)if(!Get(questions[i].key)[0] && questions[i].fallback)Put(questions[i].key,questions[i].fallback);
    if(!Complete())return false;   // r409: a default that could not be stored is said, not dropped
    // r409: static (384 indices are not for a firmware stack); a table with more questions than
    // the store has keys is refused aloud instead of losing its tail.
    static size_t asked[maxAnswers];size_t askedCount=0;
    for(size_t i=0;i<count;++i)if((!quick || questions[i].quick) && Applies(questions[i])) {
        if(askedCount>=maxAnswers) {Fail(T(u"Ta lista pytań jest za długa dla tej wersji Charona.",u"This list of questions is too long for this version of Charon."));return false;}
        asked[askedCount++]=i;
    }
    for(size_t at=0;at<askedCount;) {
        const int r=AskOne(questions[asked[at]],!quick);
        if(r==2)continue;
        if(r==1) {++at;continue;}
        if(!at)return false;
        --at;Say(T(u"Wracam do poprzedniego pytania.",u"Back to the previous question."));
    }
    return true;
}
}  // namespace ask
}  // namespace ws
}  // namespace hakefi
