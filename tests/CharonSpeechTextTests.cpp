// Host tests for src/efi/CharonSpeechText.h and settings V9 (r390).
// clang++ -std=c++17 -I src/efi tests/CharonSpeechTextTests.cpp -o t.exe
#include <cstdio>
#include <cstring>
#include <string>
#include "CharonSpeechText.h"
#include "CharSettings.h"
#include "CharonBraille.h"
using namespace hakefi;
static int failures=0;
static std::string U8(const char16_t* s) {
    std::string o;
    for(;*s;++s) {uint32_t c=*s;
        if(c>=0xd800 && c<=0xdbff && s[1]>=0xdc00 && s[1]<=0xdfff) {c=0x10000+((c-0xd800)<<10)+(s[1]-0xdc00);++s;}
        if(c==0x1b) {o+="<ESC>";continue;}
        if(c<0x80)o+=char(c);else if(c<0x800) {o+=char(0xc0|(c>>6));o+=char(0x80|(c&63));}
        else if(c<0x10000) {o+=char(0xe0|(c>>12));o+=char(0x80|((c>>6)&63));o+=char(0x80|(c&63));}
        else {o+=char(0xf0|(c>>18));o+=char(0x80|((c>>12)&63));o+=char(0x80|((c>>6)&63));o+=char(0x80|(c&63));}}
    return o;
}
static void Expect(const char* name,const std::string& got,const char* want) {
    if(got!=want) {std::printf("FAIL %s\n  got:  [%s]\n  want: [%s]\n",name,got.c_str(),want);++failures;}
    else std::printf("ok   %s: %s\n",name,got.c_str());
}
static std::string Symbols(const char16_t* s,uint8_t level,bool pl) {
    char16_t out[800];speechtext::Out o{out,800};out[0]=0;
    size_t n=0;while(s[n])++n;speechtext::ProcessSymbols(s,n,level,pl,o);
    // trim
    std::string r=U8(out);while(!r.empty() && r.back()==' ')r.pop_back();while(!r.empty() && r.front()==' ')r.erase(0,1);
    return r;
}
static std::string Compose(const char16_t* s,bool own,uint8_t level,uint8_t lang,bool pl,const char** first=nullptr) {
    char16_t out[1600];speechtext::Reading r;r.level=level;r.screenLanguage=lang;r.interfacePolish=pl;
    speechtext::Lang sticky=speechtext::Lang::unknown;
    const auto l=speechtext::Compose(s,own,r,sticky,out,1600);
    if(first)*first=l==speechtext::Lang::pl?"pl":"en";
    return U8(out);
}
int main() {
    using speechtext::levelNone;using speechtext::levelSome;using speechtext::levelMost;using speechtext::levelAll;
    // NVDA symbol levels (Polish and English symbols.dic)
    Expect("some keeps sentence dot",Symbols(u"Press F10 to save.",levelSome,false),"Press F10 to save.");
    Expect("all names sentence dot",Symbols(u"Press F10 to save.",levelAll,false),"Press F10 to save dot.");
    Expect("pl some url",Symbols(u"www.example.com",levelSome,true),"www kropka example kropka com");
    Expect("pl none url",Symbols(u"www.example.com",levelNone,true),"www example com");
    Expect("repeated",Symbols(u"-----",levelMost,true),"5 minus");
    Expect("repeated below level",Symbols(u"=====",levelNone,true),"");
    Expect("decimal point",Symbols(u"Linux 6.1.0",levelSome,false),"Linux 6.1.0");
    Expect("negative number",Symbols(u"-5 C",levelSome,true),"minus 5 C");
    Expect("in-word apostrophe",Symbols(u"don't",levelMost,false),"don't");
    Expect("brackets most",Symbols(u"[Enter]",levelMost,true),"lewy kwadratowy Enter prawy kwadratowy");
    Expect("brackets some",Symbols(u"[Enter]",levelSome,true),"Enter");
    Expect("phrase colon all",Symbols(u"Boot: Ubuntu",levelAll,true),"Boot dwukropek: Ubuntu");
    Expect("key hints",Symbols(u"↑↓=Move Highlight  <Enter>=Select Entry  F10=Save  Esc=Exit",levelSome,false),"up arrow down arrow: Move Highlight Enter: Select Entry F10: Save Esc: Exit");
    Expect("key hint in a sentence untouched",Symbols(u"if a=b then",levelSome,false),"if a equals b then");
    Expect("prompt",Symbols(u"FS0:\\> ",levelSome,false),"FS0:");
    Expect("shell prompt",Symbols(u"Shell>",levelSome,false),"Shell");
    Expect("comparison kept",Symbols(u"a > b",levelSome,false),"a greater b");
    // language detection
    Expect("detect en",speechtext::Detect(u"Press Enter to boot the selected OS",35)==speechtext::Lang::en?"en":"x","en");
    Expect("detect pl",speechtext::Detect(u"Naciśnij Enter, aby uruchomić",29)==speechtext::Lang::pl?"pl":"x","pl");
    Expect("detect unknown",speechtext::Detect(u"Ubuntu",6)==speechtext::Lang::unknown?"unknown":"x","unknown");
    // composing: own words keep the menu language, screen text its own
    const char* first=nullptr;
    Expect("mixed",Compose(u"Windows Boot Manager\xE001, menu, 3 pozycje\xE002",false,levelSome,speechtext::screenAuto,true,&first),
        "Windows Boot Manager <ESC>p , menu, 3 pozycje");
    Expect("mixed first",first,"en");
    Expect("separator stays",Compose(u"Ubuntu\xE001. \xE002" u"Advanced options for Ubuntu",false,levelSome,speechtext::screenAuto,true,&first),"Ubuntu. Advanced options for Ubuntu");
    Expect("interface mode",Compose(u"Press Enter to boot",false,levelSome,speechtext::screenInterface,true,&first),"Press Enter to boot");
    Expect("interface first",first,"pl");
    Expect("own not processed",Compose(u"Brak tekstu: [x].",true,levelAll,speechtext::screenAuto,true,&first),"Brak tekstu: [x].");
    Expect("polish screen",Compose(u"Wybierz system i naciśnij Enter\xE001, 1 z 2\xE002",false,levelSome,speechtext::screenAuto,true,&first),"Wybierz system i naciśnij Enter, 1 z 2");
    // characters
    {char16_t t[64];speechtext::Out o{t,64};speechtext::CharName(u'A',true,true,o);Expect("cap A",U8(t),"duże A");}
    {char16_t t[64];speechtext::Out o{t,64};speechtext::CharName(u'/',true,true,o);Expect("slash pl",U8(t),"slesz");}
    {char16_t t[64];speechtext::Out o{t,64};speechtext::Phonetic(u'b',true,false,o);Expect("phonetic pl",U8(t),"Barbara");}
    {char16_t t[64];speechtext::Out o{t,64};speechtext::Phonetic(u'B',false,true,o);Expect("phonetic en",U8(t),"cap Bravo");}
    {char16_t t[64];speechtext::Out o{t,64};speechtext::CharCode(u'A',true,o);Expect("code",U8(t),"znak 65, hex 4 1");}
    {char16_t t[200];speechtext::Out o{t,200};speechtext::Spell(u"Ab.",3,true,true,o);Expect("spell",U8(t),"duże A, b, kropka");}
    // settings V9 round trip, V8 still read
    {CharSettings s;s.sequence=5;s.punctuation=3;s.capitals=6;s.screenLanguage=2;s.readerFlags=readerFlagsDefault|readerFlagWords;
     uint8_t b[charSettingsBytes];bool ok=EncodeSettings(s,b,sizeof(b));CharSettings r;ok=ok && DecodeSettings(b,sizeof(b),r);
     Expect("v9 round trip",ok && r.punctuation==3 && r.capitals==6 && r.screenLanguage==2 && r.readerFlags==63?"ok":"bad","ok");
     uint8_t v8[40]={};const char magic[]="CHARS008";for(int i=0;i<8;++i)v8[i]=uint8_t(magic[i]);v8[8]=1;v8[22]=10;v8[23]=10;v8[24]=10;v8[25]=1;v8[27]=10;
     v8[28]=10;v8[29]=10;v8[30]=10;v8[31]=10;v8[32]=7;v8[33]=2;v8[34]=14;v8[35]=10;v8[18]=1;v8[20]=1;
     const auto crc=SettingsCRC(v8,36);for(int i=0;i<4;++i)v8[36+i]=uint8_t(crc>>(i*8));
     CharSettings r8;Expect("v8 decodes to defaults",DecodeSettings(v8,40,r8) && r8.punctuation==1 && r8.readerFlags==readerFlagsDefault?"ok":"bad","ok");}
    // braille cells (8-dot computer braille, Polish letters with dot 8)
    {using namespace hakefi::braille;
     const bool ok=Cell(u'a')==d1 && Cell(u'A')==(d1|d7) && Cell(u'1')==d2 && Cell(u' ')==0 && Cell(u'<')==(d1|d2|d6)
        && Cell(u'\u0142')==(d1|d2|d6|d8) && Cell(u'\u0141')==(d1|d2|d6|d7|d8) && Cell(u'@')==(d4|d7) && Cell(u'{')==(d2|d4|d6);
     Expect("braille cells",ok?"ok":"bad","ok");}
    std::printf(failures?"FAILED %d\n":"ALL PASSED\n",failures);
    return failures?1:0;
}
