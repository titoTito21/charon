// Charon speech text (r390). GPL-2.0-or-later.
//
// What the screen reader hands to the voice, processed like NVDA does it:
//  - symbol and punctuation levels (none / some / most / all; NVDA's
//    SpeechSymbolProcessor algorithm with NVDA's own symbols.dic data,
//    CharonSymbols.h): repeated symbols ("----" -> "4 minus"), the complex
//    symbols coded by hand (sentence and phrase endings, decimal point,
//    in-word apostrophe, negative number), then simple symbols, longest first;
//  - character names at the "char" level, phonetic descriptions
//    (characterDescriptions.dic) and spelling;
//  - the language of screen text: automatic (Polish / English by letters and
//    words), the menu's language, or fixed. The reader's own words keep the
//    menu's language; a language switch inside one utterance is written as
//    ESC 'p' / ESC 'e' for LiveVoice, which ends a chunk there.
// The reader marks its own words inside an utterance with ownOpen/ownClose
// (private-use characters): symbols are processed only outside them.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "CharonSymbols.h"
#include "CharonDictionary.h"
namespace hakefi::speechtext {
constexpr char16_t ownOpen=0xE001,ownClose=0xE002,languageSwitch=0x001B;
constexpr char16_t formatMarkChar=0xE003;   // r393 CharonFormatting.h formatMark + formatPayloadChars payload characters
constexpr size_t formatPayloadChars=12;      // r397 (was 6): ACSS range/stress/richness, family, event, stereo
enum Level : uint8_t {levelNone=0,levelSome=1,levelMost=2,levelAll=3,levelChar=4};
enum class Lang : uint8_t {unknown,pl,en};
// Screen text language setting (settings V9 byte 38).
enum ScreenLanguage : uint8_t {screenAuto=0,screenInterface=1,screenEnglish=2,screenPolish=3};
struct Out {
    char16_t* p;size_t cap;size_t at=0;
    void Put(char16_t c) {if(at+1<cap) {p[at++]=c;p[at]=0;}}
    void Puts(const char16_t* s) {while(*s)Put(*s++);}
    void Number(size_t v) {char16_t d[20];size_t n=0;do {d[n++]=char16_t(u'0'+v%10);v/=10;}while(v && n<20);while(n)Put(d[--n]);}
    char16_t Last() const {return at?p[at-1]:0;}
    void Space() {if(at && p[at-1]!=u' ')Put(u' ');}
};
inline bool IsSpace(char16_t c) {return c==u' ' || (c>=9 && c<=13) || c==0xa0 || (c>=0x2000 && c<=0x200a) || c==0x202f || c==0x205f || c==0x3000;}
inline bool IsDigit(char16_t c) {return c>=u'0' && c<=u'9';}
// \w of Python's re for the texts boot screens show: letters, digits, '_'.
inline bool IsWord(char16_t c) {
    if((c>=u'a' && c<=u'z') || (c>=u'A' && c<=u'Z') || IsDigit(c) || c==u'_')return true;
    if(c<0xc0 || c==0xd7 || c==0xf7)return false;
    if((c>=0x2000 && c<=0x2bff) || (c>=0x3000 && c<=0x303f) || (c>=0xe000 && c<=0xf8ff) || (c>=0xfe30 && c<=0xfe4f) || (c>=0xff00 && c<=0xff20))return false;
    return true;
}
struct Table {const symbols::Symbol* simple;size_t simpleCount;const symbols::Complex* complex;const symbols::Phonetic* phonetic;size_t phoneticCount;};
inline Table TableFor(bool pl) {
    return pl?Table{symbols::simple_pl,symbols::simpleCount_pl,symbols::complex_pl,symbols::phonetic_pl,symbols::phoneticCount_pl}
             :Table{symbols::simple_en,symbols::simpleCount_en,symbols::complex_en,symbols::phonetic_en,symbols::phoneticCount_en};
}
// The simple symbol starting at s[i] (longest first; the table is sorted so).
inline const symbols::Symbol* FindSimple(const Table& t,const char16_t* s,size_t i,size_t n) {
    const char16_t c=s[i];
    if((c>=u'a' && c<=u'z') || (c>=u'A' && c<=u'Z') || IsDigit(c))return nullptr;
    for(size_t k=0;k<t.simpleCount;++k) {
        const auto& sym=t.simple[k];
        if(sym.id[0]!=c || i+sym.length>n)continue;
        size_t j=1;while(j<sym.length && sym.id[j]==s[i+j])++j;
        if(j==sym.length)return &sym;
    }
    return nullptr;
}
inline const symbols::Symbol* FindChar(const Table& t,char16_t c) {
    for(size_t k=0;k<t.simpleCount;++k)if(t.simple[k].length==1 && t.simple[k].id[0]==c)return &t.simple[k];
    return nullptr;
}
// Which complex symbol (NVDA's complexSymbols, in file order) matches at s[i].
inline int MatchComplex(const char16_t* s,size_t i,size_t n,size_t& length) {
    const char16_t c=s[i],prev=i?s[i-1]:0,next=i+1<n?s[i+1]:0;
    length=1;
    auto closing=[](char16_t x) {return x==u'"' || x==u'\'' || x==0x201d || x==0x2019 || x==u')';};
    // . ! ? sentence ending: (?<=[^\s.])\.(?=[\"'”’)\s]|$)
    if(c==u'.' || c==u'!' || c==u'?') {
        if(i && !IsSpace(prev) && prev!=c && (i+1==n || closing(next) || IsSpace(next)))
            return c==u'.'?symbols::sentenceDot:c==u'!'?symbols::sentenceBang:symbols::sentenceQuestion;
    }
    // ; : phrase ending: (?<=[^\s;]);(?=\s|$)
    if(c==u';' || c==u':') {
        if(i && !IsSpace(prev) && prev!=c && (i+1==n || IsSpace(next)))return c==u';'?symbols::phraseSemicolon:symbols::phraseColon;
    }
    if(c==u'.') {
        // multiple .: \.{4,}
        size_t run=0;while(i+run<n && s[i+run]==u'.')++run;
        if(run>=4) {length=run;return symbols::multipleDots;}
        // decimal point: (?<![^\d -])\.(?=\d)
        if(IsDigit(next) && (!i || IsDigit(prev) || prev==u' ' || prev==u'-'))return symbols::decimalPoint;
    }
    // in-word ': (?<=[^\W_])['’]
    if((c==u'\'' || c==0x2019) && i && IsWord(prev) && prev!=u'_')return symbols::inWordApostrophe;
    // negative number: (?<!\w)[-−]{1}(?=[$£€¥.]?\d)
    if((c==u'-' || c==0x2212) && !(i && IsWord(prev))) {
        const char16_t after=i+2<n?s[i+2]:0;
        if(IsDigit(next) || ((next==u'$' || next==0xa3 || next==0x20ac || next==0xa5 || next==u'.') && IsDigit(after)))return symbols::negativeNumber;
    }
    return -1;
}
// NVDA's _regexpRepl: replacement and suffix by level and preserve.
inline void Replace(Out& o,const char16_t* text,size_t length,const char16_t* name,uint8_t level,uint8_t preserve,uint8_t user) {
    const bool keep=preserve==1 || (preserve==2 && user<level);
    if(user>=level && name[0]) {o.Space();o.Puts(name);if(keep)for(size_t k=0;k<length;++k)o.Put(text[k]);else o.Put(u' ');}
    else if(keep)for(size_t k=0;k<length;++k)o.Put(text[k]);
    else o.Space();
}
// r390: key hints of firmware and boot managers ("<Enter>=Select Entry",
// "F10=Save", "\u2191\u2193=Move Highlight", "Esc: Exit") are read as "key: action":
// the brackets round a key name and the '=' after a key are not spoken.
inline bool KeyHint(const char16_t* s,size_t i,size_t n,size_t& skip,bool& colon) {
    skip=0;colon=false;
    auto alnum=[](char16_t c) {return (c>=u'a' && c<=u'z') || (c>=u'A' && c<=u'Z') || IsDigit(c);};
    const char16_t c=s[i];
    if(c==u'<') {   // <Key> with letters only, at most 12
        size_t e=i+1;while(e<n && e-i<=13 && (alnum(s[e]) || s[e]==u'+' || s[e]==u'-'))++e;
        if(e<n && s[e]==u'>' && e>i+1) {skip=1;return true;}
    }
    if(c==u'>' && i) {size_t b=i;while(b && (alnum(s[b-1]) || s[b-1]==u'+' || s[b-1]==u'-'))--b;if(b && s[b-1]==u'<' && b<i) {skip=1;colon=i+1<n && s[i+1]==u'=';if(colon)skip=2;return true;}}
    if(c==u'=' && i && i+1<n && alnum(s[i+1]) && !IsSpace(s[i-1])) {
        const char16_t p=s[i-1];
        if((p>=0x2190 && p<=0x2193) || alnum(p)) {
            // F10=Save, Esc=Exit, arrows=Move: the key before is short (no word of a sentence).
            size_t b=i;while(b && alnum(s[b-1]))--b;
            const bool keyName=s[b]>=u'A' && s[b]<=u'Z';   // Esc, F10, Enter (not "a=b")
            if((p>=0x2190 && p<=0x2193) || (keyName && i-b<=6)) {skip=1;colon=true;return true;}
        }
    }
    return false;
}
// Screen text through the symbol processor at the user's level.
inline void ProcessSymbolsRaw(const char16_t* s,size_t n,uint8_t level,bool pl,Out& o);
// r392: no space before kept punctuation after a named symbol ("up arrow: Select").
inline void ProcessSymbols(const char16_t* s,size_t n,uint8_t level,bool pl,Out& o) {
    const size_t from=o.at;ProcessSymbolsRaw(s,n,level,pl,o);
    size_t w=from;
    for(size_t i=from;i<o.at;++i) {
        const char16_t c=o.p[i];
        if(c==u' ' && i+1<o.at && (o.p[i+1]==u':' || o.p[i+1]==u',' || o.p[i+1]==u';') && w>from && IsWord(o.p[w-1]))continue;
        o.p[w++]=c;
    }
    o.at=w;if(o.cap)o.p[o.at]=0;
}
inline void ProcessSymbolsRaw(const char16_t* s,size_t n,uint8_t level,bool pl,Out& o) {
    const Table t=TableFor(pl);
    // rstripSpace: trailing blanks are dropped.
    while(n && IsSpace(s[n-1]) && s[n-1]!=u'\n')--n;
    for(size_t i=0;i<n;) {
        const char16_t c=s[i];
        if((c>=u'a' && c<=u'z') || (c>=u'A' && c<=u'Z') || IsDigit(c)) {o.Put(c);++i;continue;}
        // r390: a prompt sign at the end of a line ("Shell>", "FS0:\\>", "grub>", "$", "#") is not named.
        if(level<levelAll && (c==u'>' || c==u'$' || c==u'#') && i) {
            size_t k=i+1;while(k<n && IsSpace(s[k]))++k;
            const char16_t p=s[i-1];
            if(k==n && (IsWord(p) || p==u':' || p==u'\\' || p==u'/' || p==u']' || p==u')' || p==u'~')) {++i;continue;}
        }
        if(level<levelAll) {size_t skip=0;bool colon=false;
            if(KeyHint(s,i,n,skip,colon)) {if(colon) {while(o.at && o.p[o.at-1]==u' ')o.p[--o.at]=0;o.Put(u':');o.Put(u' ');} i+=skip;continue;}}
        // repeated: a non-word, non-space symbol four times or more.
        if(!IsWord(c) && !IsSpace(c)) {
            size_t run=1;while(i+run<n && s[i+run]==c)++run;
            if(run>=4) if(const auto* sym=FindChar(t,c)) {
                if(level>=sym->level) {o.Space();o.Number(run);o.Put(u' ');o.Puts(sym->name);o.Put(u' ');} else o.Space();
                i+=run;continue;
            }
        }
        size_t length=1;const int kind=MatchComplex(s,i,n,length);
        if(kind>=0) {const auto& cx=t.complex[kind];Replace(o,s+i,length,cx.name,cx.level,cx.preserve,level);i+=length;continue;}
        if(const auto* sym=FindSimple(t,s,i,n)) {
            if(IsSpace(c)) {o.Space();i+=sym->length;continue;}
            Replace(o,s+i,sym->length,sym->name,sym->level,sym->preserve,level);i+=sym->length;continue;
        }
        if(IsSpace(c))o.Space();else o.Put(c);
        ++i;
    }
}
// ---- language of screen text ------------------------------------------
inline bool PolishLetter(char16_t c) {
    switch(c) {case 0x105:case 0x107:case 0x119:case 0x142:case 0x144:case 0xf3:case 0x15b:case 0x17a:case 0x17c:
        case 0x104:case 0x106:case 0x118:case 0x141:case 0x143:case 0xd3:case 0x15a:case 0x179:case 0x17b:return true;default:return false;}
}
inline bool WordIn(const char16_t* w,size_t n,const char* const* list) {
    for(size_t k=0;list[k];++k) {
        size_t j=0;while(j<n && list[k][j] && char16_t(list[k][j])==w[j])++j;
        if(j==n && !list[k][j])return true;
    }
    return false;
}
// Scores: Polish letters weigh most; then words typical of each language on
// boot screens; then letter pairs. Unclear text is unknown.
inline Lang Detect(const char16_t* s,size_t n) {
    static const char* const en[]={"the","and","of","to","for","is","are","be","with","from","this","that","will","press","enter","select","boot","booting",
        "options","option","settings","setup","device","devices","default","exit","save","change","changes","key","keys","use","highlighted","entry",
        "automatically","seconds","in","or","not","yes","loading","kernel","version","mode","safe","advanced","recovery","memory","test","windows",
        "shell","firmware","manager","continue","reset","drive","network","language","time","date","error","failed","found","unable","cannot",
        "warning","please","wait","restart","shutdown","power","off","edit","command","commands","line","help","back","next","previous","up","down",
        "left","right","install","installer","start","run","tools","graphical","console","an","it","you","your","any","all","new","open","close",
        "file","files","volume","partition","disk","password","user","about","information","quit","cancel","apply","return","load","loaded","choose",
        "operating","computer","hard","removable","media","other","more","less","check","checking","starting","started","ready","done","ok","on",nullptr};
    static const char* const pl[]={"w","z","na","nie","jest","lub","oraz","dla","aby","przez","jako","czy","ze","sie","tak","juz","przy","tylko","wybierz",
        "nacisnij","wcisnij","uruchom","ustawienia","systemu","dysk","plik","pliki","wersja","zapisz","pomoc","brak","klawisz","klawisze","wyjscie",
        "dalej","wstecz","opcje","urzadzenie","urzadzenia","rozruch","rozruchu","jezyk","czas","haslo","program","programu","sekund","sekundy","za",
        "zostanie","automatycznie","prosze","czekaj","trwa","ladowanie","bezpieczny","tryb","zaawansowane","pamiec","sieci","siec","zamknij","otworz",nullptr};
    int plScore=0,enScore=0;
    for(size_t i=0;i<n;) {
        if(PolishLetter(s[i])) {plScore+=4;++i;continue;}
        const char16_t c=s[i];
        const bool letter=(c>=u'a' && c<=u'z') || (c>=u'A' && c<=u'Z');
        if(!letter) {++i;continue;}
        char16_t word[24];size_t k=0;size_t j=i;bool polishInside=false;
        while(j<n && (((s[j]|0x20)>=u'a' && (s[j]|0x20)<=u'z') || PolishLetter(s[j]))) {
            if(PolishLetter(s[j]))polishInside=true;
            if(k<23)word[k++]=char16_t(s[j]>=u'A' && s[j]<=u'Z'?s[j]+32:s[j]);
            ++j;
        }
        if(polishInside)plScore+=4;
        if(!polishInside) {
            if(WordIn(word,k,en))enScore+=2;
            else if(WordIn(word,k,pl))plScore+=2;
            for(size_t m=0;m+1<k;++m) {
                const char16_t a=word[m],b=word[m+1];
                if((a==u't' && b==u'h') || (a==u'w' && b==u'h') || (a==u'e' && b==u'e') || (a==u'o' && b==u'o') || (a==u'e' && b==u'a') || (a==u'o' && b==u'u'))++enScore;
                if((a==u's' && b==u'z') || (a==u'c' && b==u'z') || (a==u'r' && b==u'z') || (a==u'd' && b==u'z') || (a==u'y' && b==u'c') || (a==u'w' && b==u'y') || (a==u'j' && b==u'e'))++plScore;
                if(a==u'q' || a==u'x' || a==u'v')++enScore;
            }
            if(k>=3 && word[k-3]==u'i' && word[k-2]==u'n' && word[k-1]==u'g')enScore+=2;
            if(k>=4 && word[k-4]==u't' && word[k-3]==u'i' && word[k-2]==u'o' && word[k-1]==u'n')enScore+=2;
        }
        i=j;
    }
    if(plScore>enScore)return Lang::pl;
    if(enScore>plScore)return Lang::en;
    return Lang::unknown;
}
// ---- composing an utterance ---------------------------------------------
struct Reading {
    uint8_t level=levelSome;        // NVDA's default: some
    uint8_t screenLanguage=screenAuto;
    bool interfacePolish=false;
    bool dictionary=true;           // r393 built-in firmware pronunciation dictionary
};
// text: screen text with the reader's own words between ownOpen/ownClose
// (own=true: all of it is the reader's). Writes the voice's text with
// ESC p/e language switches into out and returns the first language (the
// utterance's). `sticky` keeps the last detected screen language between
// utterances (a menu of names alone stays in the language of its screen).
inline Lang Compose(const char16_t* text,bool own,const Reading& r,Lang& sticky,char16_t* out,size_t cap) {
    Out o{out,cap};if(cap)out[0]=0;
    const Lang ui=r.interfacePolish?Lang::pl:Lang::en;
    // The screen language of the whole utterance (unclear segments use it).
    Lang whole=Lang::unknown;
    if(!own && r.screenLanguage==screenAuto) {
        char16_t screen[1600];size_t m=0;bool inOwn=false;
        for(size_t i=0;text[i] && m+1<1600;++i) {if(text[i]==formatMarkChar) {size_t k=1;while(k<=formatPayloadChars && text[i+k])++k;i+=k-1;continue;}if(text[i]==ownOpen)inOwn=true;else if(text[i]==ownClose)inOwn=false;else if(!inOwn)screen[m++]=text[i];}
        whole=Detect(screen,m);
        if(whole!=Lang::unknown)sticky=whole;
    }
    auto screenLang=[&](const char16_t* s,size_t n) {
        switch(r.screenLanguage) {
            case screenEnglish:return Lang::en;
            case screenPolish:return Lang::pl;
            case screenInterface:return ui;
            default:break;
        }
        // Short pieces ("Ubuntu", "2") follow the whole utterance; longer ones decide for themselves.
        Lang l=n>=12?Detect(s,n):Lang::unknown;
        if(l==Lang::unknown)l=whole;
        if(l==Lang::unknown)l=sticky;
        if(l==Lang::unknown)l=ui;
        return l;
    };
    Lang first=Lang::unknown,current=Lang::unknown;
    auto switchTo=[&](Lang l) {
        if(first==Lang::unknown) {first=current=l;return;}
        if(l==current)return;
        // A switch costs a chunk boundary: skip it before pure punctuation/digits.
        o.Space();o.Put(languageSwitch);o.Put(l==Lang::pl?u'p':u'e');o.Put(u' ');current=l;
    };
    size_t i=0;
    if(own) {
        switchTo(ui);
        for(;text[i];++i)if(text[i]!=ownOpen && text[i]!=ownClose)o.Put(text[i]);
    } else while(text[i]) {
        if(text[i]==ownOpen) {
            size_t e=i+1;while(text[e] && text[e]!=ownClose)++e;
            bool words=false;for(size_t k=i+1;k<e;++k)if(IsWord(text[k]) && !IsDigit(text[k]))words=true;
            if(words)switchTo(ui);   // ", " or ". " alone keeps the current language
            else if(first==Lang::unknown)switchTo(ui);
            for(size_t k=i+1;k<e;++k)o.Put(text[k]);
            i=text[e]?e+1:e;continue;
        }
        // r393: a formatting marker (CharonFormatting.h) becomes ESC 'f' + 6 bytes.
        if(text[i]==formatMarkChar) {
            size_t k=1;while(k<=formatPayloadChars && text[i+k])++k;
            if(k==formatPayloadChars+1) {o.Space();o.Put(languageSwitch);o.Put(u'f');for(size_t j=1;j<=formatPayloadChars;++j)o.Put(text[i+j]);o.Put(u' ');i+=formatPayloadChars+1;}
            else i+=k;
            continue;
        }
        size_t e=i;while(text[e] && text[e]!=ownOpen && text[e]!=formatMarkChar)++e;
        const Lang l=screenLang(text+i,e-i);
        bool words=false;for(size_t k=i;k<e;++k)if(IsWord(text[k]) && !IsDigit(text[k]))words=true;
        if(words || first==Lang::unknown)switchTo(l);
        const bool segmentPl=(words?l:current)==Lang::pl;
        // r393: the built-in firmware pronunciation dictionary (screen text only).
        if(r.dictionary) {char16_t replaced[1600];const size_t m=dictionary::Apply(text+i,e-i,segmentPl,replaced,1600);ProcessSymbols(replaced,m,r.level,segmentPl,o);}
        else ProcessSymbols(text+i,e-i,r.level,segmentPl,o);
        i=e;
    }
    // Never end on a switch (LiveVoice would get an empty chunk).
    while(o.at && (o.p[o.at-1]==u' ' || o.p[o.at-1]==languageSwitch)) {o.p[--o.at]=0;}
    if(o.at>=2 && o.p[o.at-2]==languageSwitch) {o.at-=2;o.p[o.at]=0;}
    // r393: nor on a formatting switch (ESC 'f' + 6).
    for(;;) {
        while(o.at && o.p[o.at-1]==u' ') {o.p[--o.at]=0;}
        if(o.at>=formatPayloadChars+2 && o.p[o.at-formatPayloadChars-2]==languageSwitch && o.p[o.at-formatPayloadChars-1]==u'f') {o.at-=formatPayloadChars+2;o.p[o.at]=0;continue;}
        break;
    }
    return first==Lang::unknown?ui:first;
}
// ---- characters -----------------------------------------------------------
inline bool UpperCase(char16_t c) {return (c>=u'A' && c<=u'Z') || (c>=0xc0 && c<=0xde && c!=0xd7) || (c>=0x100 && c<0x180 && !(c&1)) || (c>=0x391 && c<=0x3a9) || (c>=0x410 && c<=0x42f);}
inline char16_t LowerCase(char16_t c) {
    if(c>=u'A' && c<=u'Z')return char16_t(c+32);
    if(c>=0xc0 && c<=0xde && c!=0xd7)return char16_t(c+32);
    if(c>=0x100 && c<0x180 && !(c&1))return char16_t(c+1);
    if((c>=0x391 && c<=0x3a9) || (c>=0x410 && c<=0x42f))return char16_t(c+32);
    return c;
}
// NVDA's character name: a symbol at the "char" level, otherwise the
// character itself (the voice says the letter).
inline void CharName(char16_t c,bool pl,bool sayCap,Out& o) {
    if(!c)c=u' ';
    if(UpperCase(c) && sayCap) {o.Puts(pl?u"duże ":u"cap ");}
    if(const auto* sym=FindChar(TableFor(pl),c)) if(sym->name[0]) {o.Puts(sym->name);return;}
    if(c>=0x2500 && c<=0x257f) {o.Puts(pl?u"ramka":u"frame");return;}
    o.Put(c);
}
// characterDescriptions.dic: "a" -> "Adam" / "alpha"; false when none.
inline bool Phonetic(char16_t c,bool pl,bool sayCap,Out& o) {
    const Table t=TableFor(pl);const char16_t low=LowerCase(c);
    for(size_t k=0;k<t.phoneticCount;++k)if(t.phonetic[k].ch==low) {
        if(UpperCase(c) && sayCap)o.Puts(pl?u"duże ":u"cap ");
        o.Puts(t.phonetic[k].name);return true;}
    return false;
}
// NVDA's third press: the character's Unicode value, decimal and hex.
inline void CharCode(char16_t c,bool pl,Out& o) {
    o.Puts(pl?u"znak ":u"character ");o.Number(c);o.Puts(u", hex ");
    static const char16_t hex[]=u"0123456789abcdef";bool any=false;
    for(int shift=12;shift>=0;shift-=4) {const unsigned d=(c>>shift)&15;if(d || any || !shift) {o.Put(hex[d]);o.Put(u' ');any=true;}}
    while(o.at && o.p[o.at-1]==u' ')o.p[--o.at]=0;
}
// Spelling (a word or line pressed twice): character names one by one.
inline void Spell(const char16_t* s,size_t n,bool pl,bool sayCap,Out& o) {
    bool first=true;
    for(size_t i=0;i<n;++i) {
        if(!first)o.Puts(u", ");
        CharName(s[i],pl,sayCap,o);first=false;
    }
}
}  // namespace hakefi::speechtext
