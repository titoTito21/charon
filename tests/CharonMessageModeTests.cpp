#include "../src/efi/CharonMessageMode.h"
#include "../src/efi/CharSettings.h"
#include "../src/efi/CharonMenuLabels.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d\n",__LINE__);exit(1);}}while(false)
using namespace hakefi;
void Recrc(uint8_t* bytes) {const auto crc=SettingsCRC(bytes,28);for(unsigned i=0;i<4;++i)bytes[28+i]=uint8_t(crc>>(8*i));}
void Recrc40(uint8_t* bytes) {const auto crc=SettingsCRC(bytes,44);for(unsigned i=0;i<4;++i)bytes[44+i]=uint8_t(crc>>(8*i));}
int main() {
    MessageModeSequence sequence;bool mode=false;
    for(auto ch:u"qRwA")if(ch && sequence.Feed(ch))mode=!mode;
    CHECK(mode);
    for(auto ch:u"QRwa")if(ch && sequence.Feed(ch))mode=!mode;
    CHECK(!mode);
    for(auto ch:u"qrqRWA")if(ch && sequence.Feed(ch))mode=!mode;
    CHECK(mode);
    CHECK(!sequence.Feed('q'));CHECK(!sequence.Feed('r'));CHECK(!sequence.Feed(0));
    CHECK(!sequence.Feed('w'));CHECK(!sequence.Feed('a'));
    CHECK(!sequence.Feed('q'));CHECK(!sequence.Feed('r',false));CHECK(!sequence.Feed('w'));CHECK(!sequence.Feed('a'));
    CharSettings settings,parsed;CHECK(!settings.explicitMessages);settings.sequence=1;settings.explicitMessages=true;
    uint8_t bytes[48],old[48];CHECK(EncodeSettings(settings,bytes,48));
    CHECK(DecodeSettings(bytes,48,parsed) && parsed.explicitMessages);
    for(char version:{'1','2'}) {for(unsigned i=0;i<32;++i) {old[i]=bytes[i];}old[7]=version;old[19]=0;for(unsigned i=20;i<28;++i)old[i]=0;if(version=='1')old[18]=0;
        Recrc(old);CHECK(DecodeSettings(old,32,parsed) && !parsed.explicitMessages);
        old[19]=1;Recrc(old);CHECK(!DecodeSettings(old,32,parsed));}
    for(unsigned i=0;i<48;++i) {old[i]=bytes[i];}old[19]=0;Recrc40(old);
    CHECK(!ChooseSettings(bytes,48,old,48,parsed));
    bytes[19]=2;Recrc40(bytes);CHECK(!DecodeSettings(bytes,48,parsed));
    char16_t output[100]={};
    // r397: records are 48 bytes since V9 (CRC at 44).
    // r373: tone-dependent interjections, no fixed prefix; short labels untouched.
    CHECK(FormatCharonMessage(u"UEFI: 0x800000000000001A",output,100,true,true));
    {const std::u16string s(output);CHECK(s.size()>24 && s.find(u"UEFI: 0x800000000000001A")!=std::u16string::npos && s.find(u"O kurwa")==std::u16string::npos);}
    CHECK(FormatCharonMessage(u"Missing file",output,100,true,false) && std::u16string(output).rfind(u"Missing file, ",0)==0);
    CHECK(FormatCharonMessage(u"Rok, 2027, suwak",output,100,true,true) && std::u16string(output).rfind(u"Rok, 2027, suwak, ",0)==0);
    CHECK(FormatCharonMessage(u"Operacja zakończona Enter lub Escape zamyka komunikat.",output,100,true,true)
        && std::u16string(output)==u"Ta pieprzona operacja zakończona. Enter lub Escape zamyka komunikat.");
    {std::u16string seen;unsigned different=0;
     for(auto* text:{u"Nie odczytano zmiennych.",u"Nie połączono sterowników.",u"Firmware nie przyjęło godziny.",u"Błąd syntezy.",u"Nie można narysować menu."}) {
        char16_t one[200]={};CHECK(FormatCharonMessage(text,one,200,true,true));
        const std::u16string s(one);const auto at=s.find(text);CHECK(at!=std::u16string::npos && s.size()>std::u16string(text).size());
        const auto head=s.substr(0,at);if(head!=seen)++different;seen=head;}
     CHECK(different>=3);}
    CHECK(FormatCharonMessage(u"Normal",output,100,false,true) && std::u16string(output)==u"Normal");
    char16_t tight[7]={};CHECK(FormatCharonMessage(u"Normal",tight,7,true,true) && std::u16string(tight)==u"Normal");
    CHECK(!FormatCharonMessage(u"Too long",tight,7,true,true) && tight[0]==0);
    CHECK(!FormatCharonMessage(nullptr,output,100,true,true));
    CHECK(FormatCharonMessage(u"",output,100,true,true) && output[0]==0);
    CHECK(FormatCharonMessage(u"Code",output,100,false,true));
    CHECK(FormatCharonMessage(output,output,100,true,false) && std::u16string(output).rfind(u"Code, ",0)==0);
    {char16_t place[120]=u"Operation completed UEFI: 0x1";CHECK(FormatCharonMessage(place,place,120,true,false));
     CHECK(std::u16string(place)==u"That fucking operation completed. UEFI: 0x1");}
    for(unsigned index=0;index<=unsigned(BootProblem::loaderReturned);++index) {
        for(bool pl:{false,true}) {
            const auto normal=BootMessage(BootProblem(index),pl);
            const auto vulgar=BootMessage(BootProblem(index),pl,true);
            CHECK(std::u16string(normal)==BootMessage(BootProblem(index),pl,false));
            CHECK(std::u16string(normal)!=vulgar && std::u16string(vulgar).size()<450);
            char16_t full[768]={};CHECK(FormatCharonMessage(vulgar,full,768,true,pl));
            CHECK(std::u16string(full)==vulgar);
            AppendBootStatus(full,768,uint64_t(1)<<63);
            const auto withCode=std::u16string(full);
            CHECK(FormatCharonMessage(full,full,768,true,pl) && std::u16string(full)==withCode);
        }
    }
    for(auto page:{MenuPage::main,MenuPage::settings,MenuPage::language,MenuPage::confirm,MenuPage::notice,MenuPage::startMelody,MenuPage::backgroundMelody}) {
        MenuModel model;model.page=page;
        for(size_t row=0;row<model.Count();++row)for(bool pl:{false,true}) {
            const auto* label=ExplicitMenuLabel(page,row,pl);if(!(label && *label))printf("missing explicit label page %d row %zu pl %d\n",int(page),row,int(pl));CHECK(label && *label);
            char16_t styled[512]={};CHECK(FormatCharonMessage(label,styled,512,true,pl,true));
            CHECK(std::u16string(styled)==label);
        }
        CHECK(ExplicitMenuLabel(page,model.Count(),true)==nullptr);
    }
    CHECK(ExplicitMenuLabel(MenuPage::devices,0,true)==nullptr);
    CHECK(std::u16string(ExplicitMenuLabel(MenuPage::main,0,false)).find(u"submenu")!=std::u16string::npos);
    CHECK(FormatCharonMessage(u"Settings saved",output,100,true,false));
    CHECK(std::u16string(output)==u"Fucking saved. Those settings will stick.");
    CHECK(FormatCharonMessage(u"Koniec listy",output,100,true,true));
    CHECK(std::u16string(output)==u"Dupa, dalej nic nie ma. Koniec listy.");
    CHECK(FormatCharonMessage(u"Settings saved",output,100,false,false));
    CHECK(std::u16string(output)==u"Settings saved");
    CHECK(FormatCharonMessage(u"Explicit messages enabled.",output,100,true,false));
    CHECK(std::u16string(output)==u"Well, fuck it! If that’s what you want, let’s fucking go!");
    CHECK(FormatCharonMessage(u"Tryb dosadnych komunikatów włączony.",output,100,true,true));
    CHECK(std::u16string(output).rfind(u"No i chuj",0)==0);
    CHECK(FormatCharonMessage(u"Profanity mode disabled.",output,100,false,false));
    CHECK(std::u16string(output)==u"Profanity mode disabled.");
    // r373 variety: the same event worded differently on the next key press.
    {std::u16string a,b;char16_t one[200]={};ExplicitVariety()=0;CHECK(FormatCharonMessage(u"Koniec listy",one,200,true,true));a=one;
     ++ExplicitVariety();CHECK(FormatCharonMessage(u"Koniec listy",one,200,true,true));b=one;CHECK(a!=b);ExplicitVariety()=0;}
    // Neighbouring rows of one page never share a wording index.
    {const std::u16string r0=ExplicitMenuLabel(MenuPage::tools,0,true),r1=ExplicitMenuLabel(MenuPage::tools,1,true);CHECK(r0!=r1);}
    // r374: advice inside a message is replaced wherever it is.
    {char16_t one[600]={};ExplicitVariety()=0;
     CHECK(FormatCharonMessage(u"Kolejność rozruchu. Charon zawsze pierwszy. Enter chwyta wpis, strzałki go przenoszą, Enter upuszcza. 1. Dysk",one,600,true,true));
     const std::u16string s(one);CHECK(s.find(u"Enter chwyta wpis")==std::u16string::npos && s.find(u"Charon zawsze pierwszy.")==std::u16string::npos && s.find(u"1. Dysk")!=std::u16string::npos);
     CHECK(FormatCharonMessage(u"Ustawić? Domyślnie wybrano Anuluj.",one,600,true,true) && std::u16string(one).find(u"Domyślnie wybrano Anuluj.")==std::u16string::npos);
     CHECK(FormatCharonMessage(u"Kolejność rozruchu. Charon zawsze pierwszy.",one,600,false,true) && std::u16string(one)==u"Kolejność rozruchu. Charon zawsze pierwszy.");}
    puts("PASS hidden sequence/reset/overlap/context, persistent default/migration/conflict, bounded localized message styling");
}
