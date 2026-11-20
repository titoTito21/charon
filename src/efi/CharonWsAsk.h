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
enum class Kind : uint8_t {text,secret,choice,yesNo,name,multi};   // name: lower-case login/host name; multi: check boxes, the value lists the checked values with commas
struct Choice {const char16_t* pl;const char16_t* en;const char* value;};
struct Question {
    const char* key;bool quick;Kind kind;
    const char16_t* pl;const char16_t* en;
    const char* fallback;                 // default value (for a choice: one of the values)
    const Choice* choices;size_t count;
    const char16_t* helpPl;const char16_t* helpEn;   // said after the question in the detailed mode (may be null)
};
constexpr size_t maxAnswers=48,valueUnits=160;
struct Answers {const char* keys[maxAnswers];char values[maxAnswers][valueUnits];size_t count;};
inline Answers& A() {static Answers answers;return answers;}
inline bool Same(const char* a,const char* b) {for(size_t i=0;;++i) {if(a[i]!=b[i])return false;if(!a[i])return true;}}
inline char* Slot(const char* key) {
    auto& a=A();
    for(size_t i=0;i<a.count;++i)if(Same(a.keys[i],key))return a.values[i];
    if(a.count>=maxAnswers)return a.values[maxAnswers-1];
    a.keys[a.count]=key;a.values[a.count][0]=0;return a.values[a.count++];
}
inline const char* Get(const char* key) {
    auto& a=A();
    for(size_t i=0;i<a.count;++i)if(Same(a.keys[i],key))return a.values[i];
    return "";
}
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
inline void Put(const char* key,const char* value) {char* slot=Slot(key);size_t i=0;for(;value[i] && i+1<valueUnits;++i)slot[i]=value[i];slot[i]=0;}
inline void Clear() {auto& a=A();for(auto& v:a.values)for(auto& c:v)c=0;a.count=0;}
inline void Wide(const char* in,char16_t* out,size_t cap) {Utf8ToUtf16(reinterpret_cast<const uint8_t*>(in),crypt::Length(in),out,cap);}
// A hidden text: nothing is shown or spoken but "star"; typed twice.
inline bool Secret(const char16_t* label,char16_t* out,size_t cap) {
    static char16_t first[80],second[80];
    for(int pass=0;pass<2;++pass) {
        char16_t* text=pass?second:first;size_t n=0;text[0]=0;
        static char16_t line[300];Set(line,300,pass?T(u"Powtórz:",u"Again:"):label);Say(line);
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
    if(!same) {for(auto& c:first)c=0;for(auto& c:second)c=0;Fail(T(u"Wpisy się różnią. Jeszcze raz.",u"The two entries differ. Once more."));return Secret(label,out,cap);}
    Set(out,cap,first);for(auto& c:first)c=0;for(auto& c:second)c=0;
    return true;
}
// One of several values. Up/Down (or Left/Right) move, a letter jumps, Enter takes.
inline bool Choose(const char16_t* label,const Choice* choices,size_t count,size_t& index) {
    static char16_t line[400];
    auto say=[&](bool withLabel) {
        line[0]=0;if(withLabel) {Cat(line,400,label);Cat(line,400,u" ");}
        PositionVoice(line+Len(line),400-Len(line),index);   // r401: as the boot menu's lists
        Cat(line,400,Pl()?choices[index].pl:choices[index].en);
        Cat(line,400,u", ");CatN(line,400,index+1);Cat(line,400,T(u" z ",u" of "));CatN(line,400,count);
        static char16_t shown[400];Set(shown,400,label);Cat(shown,400,u"  ◄ ");Cat(shown,400,Pl()?choices[index].pl:choices[index].en);Cat(shown,400,u" ►");Echo(shown);
        Say(line);
    };
    if(index>=count)index=0;
    say(true);
    for(;;) {
        const Key key=WaitKey();
        if(IsCancel(key)) {Echo(u"");return false;}
        if(!key.scan && key.ch==13) {Echo(u"");Play(Icon::mark);return true;}
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
    }
}
// r403: check boxes - several values at once (desktops, screen readers).
// Up/Down move, Space checks or clears, a letter jumps, Enter takes the
// checked ones (none checked is an answer too: "none").
constexpr size_t maxBoxes=24;
inline bool Pick(const char16_t* label,const Choice* choices,size_t count,bool* on) {
    static char16_t line[500];size_t index=0;if(count>maxBoxes)count=maxBoxes;
    auto say=[&](bool withLabel) {
        line[0]=0;if(withLabel) {Cat(line,500,label);Cat(line,500,u" ");}
        PositionVoice(line+Len(line),500-Len(line),index);
        Cat(line,500,Pl()?choices[index].pl:choices[index].en);
        Cat(line,500,on[index]?T(u", zaznaczone, ",u", checked, "):T(u", niezaznaczone, ",u", not checked, "));
        CatN(line,500,index+1);Cat(line,500,T(u" z ",u" of "));CatN(line,500,count);
        if(withLabel)Cat(line,500,T(u". Spacja zaznacza, Enter zatwierdza.",u". Space checks, Enter accepts."));
        static char16_t shown[500];Set(shown,500,label);Cat(shown,500,on[index]?u"  [x] ":u"  [ ] ");Cat(shown,500,Pl()?choices[index].pl:choices[index].en);Echo(shown);
        Say(line);
    };
    say(true);
    for(;;) {
        const Key key=WaitKey();
        if(IsCancel(key)) {Echo(u"");return false;}
        if(!key.scan && key.ch==13) {
            Echo(u"");Play(Icon::mark);
            static char16_t summary[600];Set(summary,600,T(u"Wybrano: ",u"Chosen: "));bool any=false;
            for(size_t i=0;i<count;++i)if(on[i]) {if(any)Cat(summary,600,u", ");Cat(summary,600,Pl()?choices[i].pl:choices[i].en);any=true;}
            if(!any)Set(summary,600,T(u"Nic nie zaznaczono.",u"Nothing checked."));
            Say(summary);return true;
        }
        if(!key.scan && key.ch==u' ') {on[index]=!on[index];Play(on[index]?Icon::mark:Icon::deleted);
            Say(on[index]?T(u"zaznaczone",u"checked"):T(u"niezaznaczone",u"not checked"));
            static char16_t shown[500];Set(shown,500,label);Cat(shown,500,on[index]?u"  [x] ":u"  [ ] ");Cat(shown,500,Pl()?choices[index].pl:choices[index].en);Echo(shown);}
        else if(key.scan==scDown || key.scan==scRight) {index=(index+1)%count;Cue(MenuCue::focus);say(false);}
        else if(key.scan==scUp || key.scan==scLeft) {index=(index+count-1)%count;Cue(MenuCue::focus);say(false);}
        else if(key.scan==scHome) {index=0;say(false);}
        else if(key.scan==scEnd) {index=count-1;say(false);}
        else if(key.scan==scF1)say(true);
        else if(Printable(key)) {
            for(size_t step=1;step<=count;++step) {
                const size_t i=(index+step)%count;const char16_t* name=Pl()?choices[i].pl:choices[i].en;
                if(Fold(name[0])==Fold(key.ch)) {index=i;say(false);break;}
            }
        }
    }
}
// Asks the table. False: the user left at the first question.
inline bool Run(const Question* questions,size_t count,bool quick) {
    // Defaults first, so skipped questions have values.
    for(size_t i=0;i<count;++i)if(!Get(questions[i].key)[0] && questions[i].fallback)Put(questions[i].key,questions[i].fallback);
    size_t asked[maxAnswers];size_t askedCount=0;
    for(size_t i=0;i<count && askedCount<maxAnswers;++i)if(!quick || questions[i].quick)asked[askedCount++]=i;
    for(size_t at=0;at<askedCount;) {
        const Question& q=questions[asked[at]];static char16_t label[300];
        Set(label,300,Pl()?q.pl:q.en);
        if(!quick && (Pl()?q.helpPl:q.helpEn)) {static char16_t help[400];Set(help,400,Pl()?q.helpPl:q.helpEn);Say(help);
            // let the help be heard before the question itself
            for(unsigned i=0;i<25;++i) {U().host->boot->Stall(100000);Pump();auto* fx=U().host->effects;if(i>3 && fx && fx->SpeechIdle && fx->SpeechIdle(fx->context))break;}}
        bool ok=false;
        if(q.kind==Kind::multi) {
            static bool on[maxBoxes];const size_t n=q.count<maxBoxes?q.count:maxBoxes;
            for(size_t k=0;k<n;++k)on[k]=Has(q.key,q.choices[k].value);
            ok=Pick(label,q.choices,n,on);
            if(ok) {static char joined[valueUnits];size_t m=0;joined[0]=0;
                for(size_t k=0;k<n;++k)if(on[k]) {
                    const char* v=q.choices[k].value;size_t len=0;while(v[len])++len;
                    if(m+len+2>=valueUnits)break;
                    if(m)joined[m++]=',';
                    for(size_t c=0;c<len;++c)joined[m++]=v[c];joined[m]=0;
                }
                Put(q.key,joined);}
        } else if(q.kind==Kind::choice || q.kind==Kind::yesNo) {
            static const Choice yesNo[]={{u"tak",u"yes","yes"},{u"nie",u"no","no"}};
            const Choice* list=q.kind==Kind::yesNo?yesNo:q.choices;const size_t n=q.kind==Kind::yesNo?2:q.count;
            size_t index=0;for(size_t k=0;k<n;++k)if(Same(list[k].value,Get(q.key)))index=k;
            ok=Choose(label,list,n,index);
            if(ok)Put(q.key,list[index].value);
        } else if(q.kind==Kind::secret) {
            static char16_t wide[80];ok=Secret(label,wide,80);
            if(ok) {static uint8_t utf8[valueUnits];const size_t n=Utf16ToUtf8(wide,Len(wide),utf8,valueUnits-1);utf8[n]=0;Put(q.key,reinterpret_cast<const char*>(utf8));
                for(auto& c:wide)c=0;for(auto& c:utf8)c=0;
                if(!Get(q.key)[0]) {Fail(T(u"Hasło nie może być puste.",u"The password cannot be empty."));continue;}}
        } else {
            static char16_t wide[valueUnits];const char* current=Get(q.key);
            Utf8ToUtf16(reinterpret_cast<const uint8_t*>(current),crypt::Length(current),wide,valueUnits);
            ok=Prompt(label,wide,valueUnits/2);
            if(ok && q.kind==Kind::name) {
                bool good=wide[0]>=u'a' && wide[0]<=u'z';
                for(size_t i=0;wide[i];++i) {wide[i]=speechtext::LowerCase(wide[i]);if(!((wide[i]>=u'a' && wide[i]<=u'z') || (wide[i]>=u'0' && wide[i]<=u'9') || wide[i]==u'-'))good=false;}
                if(!good || Len(wide)>31) {Fail(T(u"Dozwolone są małe litery bez ogonków, cyfry i myślnik; pierwsza musi być litera.",u"Only lower-case plain letters, digits and a hyphen; it must start with a letter."));continue;}
            }
            if(ok) {static uint8_t utf8[valueUnits];const size_t n=Utf16ToUtf8(wide,Len(wide),utf8,valueUnits-1);utf8[n]=0;Put(q.key,reinterpret_cast<const char*>(utf8));}
        }
        if(ok) {++at;continue;}
        if(!at)return false;
        --at;Say(T(u"Wracam do poprzedniego pytania.",u"Back to the previous question."));
    }
    return true;
}
}  // namespace ask
}  // namespace ws
}  // namespace hakefi
