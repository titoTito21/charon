// r382: text controls of boot managers and firmware forms (host test).
// CharonSystemTools.h (r409 clock.txt) uses efi_file_protocol before CharonResident.h includes its header.
#include <efi/protocol/file.h>
#define private public
#include "../src/efi/CharonResident.h"
#undef private
#include <cstdio>
#include <cstdlib>
#include <string>
namespace hakefi {inline void Utf8(const char16_t*,char* out,size_t capacity) {if(capacity)out[0]=0;}}
using namespace hakefi;
static int failures=0;
static std::u16string D(const char16_t* text,bool pl=true) {
    char16_t out[400]={};if(!ResidentReader::DescribeControls(text,out,400,pl))return u"-";
    // r390: the reader's own words are marked (speechtext::ownOpen/ownClose); the spoken text has no marks.
    std::u16string s;for(size_t i=0;out[i];++i)if(out[i]!=0xE001 && out[i]!=0xE002)s+=out[i];
    return s;
}
#define EXPECT(text,want) do {if(D(text)!=std::u16string(want)) {++failures;std::string a;for(auto c:D(text))a+=c<128?char(c):'?';fprintf(stderr,"FAIL line %d: got '%s'\n",__LINE__,a.c_str());}}while(false)
int main() {
    EXPECT(u"Enable Network Stack [X]",u"Enable Network Stack, pole wyboru, zaznaczone");
    EXPECT(u"[ ] Fast Boot",u"Fast Boot, pole wyboru, niezaznaczone");
    EXPECT(u"[X]",u"pole wyboru, zaznaczone");
    EXPECT(u"(*) UEFI only",u"UEFI only, przycisk opcji, zaznaczony");
    EXPECT(u"( ) Legacy",u"Legacy, przycisk opcji, niezaznaczony");
    EXPECT(u"[ OK ]",u"OK, przycisk");
    EXPECT(u"[ Yes ] [ No ]",u"Yes, przycisk, No, przycisk");
    EXPECT(u"< Cancel >",u"Cancel, przycisk");
    EXPECT(u"<1280x800>",u"lista rozwijana, 1280x800");
    EXPECT(u"<NONE>",u"lista rozwijana, NONE");
    EXPECT(u"Enter New Password [            ]",u"Enter New Password, pole edycji, puste");   // r397 vendor password box
    EXPECT(u"<panic+0x143>",u"-");          // r397: Limine stack trace, not a combo box
    EXPECT(u"<0x1bfaf4c3>",u"-");
    EXPECT(u"[0]",u"pokrętło, 0");
    EXPECT(u"_",u"pole edycji, puste");
    EXPECT(u"_ _ _",u"pole edycji, puste");
    EXPECT(u"<Standard English>",u"lista rozwijana, Standard English");
    EXPECT(u"Auto Boot Time-out [5]",u"Auto Boot Time-out, pokrętło, 5");
    EXPECT(u"System Time [12:30:05]",u"System Time, pole czasu, 12:30:05");   // r388: time field
    EXPECT(u"Boot Mode <UEFI>",u"Boot Mode, lista rozwijana, UEFI");
    EXPECT(u"SATA Mode [AHCI]",u"SATA Mode, lista rozwijana, AHCI");
    EXPECT(u"[+] Linux",u"Linux, folder, zwinięty");
    EXPECT(u"[-] Linux",u"Linux, folder, rozwinięty");
    EXPECT(u"☑ Secure Boot",u"Secure Boot, pole wyboru, zaznaczone");
    EXPECT(u"◉ English",u"English, przycisk opcji, zaznaczony");
    // r388: AMI Aptio / Insyde setup fields.
    EXPECT(u"► CPU Configuration",u"CPU Configuration, podmenu");
    EXPECT(u"> USB Configuration",u"USB Configuration, podmenu");
    EXPECT(u"Boot Options ▶",u"Boot Options, podmenu");
    EXPECT(u"System Date [Wed 11/18/2026]",u"System Date, pole daty, Wed 11/18/2026");
    EXPECT(u"Administrator Password [********]",u"Administrator Password, pole hasła, hasło ustawione");
    EXPECT(u"Erasing [#####.....]",u"Erasing, pasek postępu, 50%");
    EXPECT(u"> 5",u"-");   // a comparison, not a submenu mark
    // Not controls: key hints, GRUB entries, help text.
    EXPECT(u"[F9] Load Defaults",u"-");
    EXPECT(u"<Enter>=Select Entry",u"-");
    EXPECT(u"Esc=Exit [F10]",u"-");
    EXPECT(u"Toggle Checkbox Esc=Exit <Spacebar>",u"-");
    EXPECT(u"Advanced options for Debian GNU/Linux (with Linux 6.1)",u"-");
    EXPECT(u"Memory test (memtest86+)",u"-");
    EXPECT(u"Windows Boot Manager",u"-");
    EXPECT(u"Press enter to boot the selected OS, `e' to edit the commands",u"-");
    if(D(u"Enable [X]",false)!=u"Enable, check box, checked") {++failures;fprintf(stderr,"FAIL english\n");}
    EXPECT(u"|Shutdown| |Restart|",u"Shutdown, przycisk, Restart, przycisk");
    EXPECT(u"|Shutdown|",u"Shutdown, przycisk");
    if(ResidentReader::ChangedNumber(u"Boot in 10 s.",u"Boot in 9 s.")!=9) {++failures;fputs("FAIL countdown\n",stderr);}
    if(ResidentReader::ChangedNumber(u"Booting automatically in 5, press any key",u"Booting automatically in 4, press any key")!=4) {++failures;fputs("FAIL limine\n",stderr);}
    if(ResidentReader::ChangedNumber(u"Loading 45%",u"Loading 46%")!=-1) {++failures;fputs("FAIL progress\n",stderr);}
    if(ResidentReader::ChangedNumber(u"automatically in 1s.",u"automatically in 0s.")!=0) {++failures;fputs("FAIL zero\n",stderr);}
    // r397: Clover's submenu arrow; a list of values in brackets is text
    EXPECT(u"GUI tuning->",u"GUI tuning, podmenu");
    EXPECT(u"mem[640K 7M 20K 2021M 9M]",u"-");
    EXPECT(u"Quirks mask [0xA041]",u"Quirks mask, lista rozwijana, 0xA041");
    {char16_t out[16];const char16_t* text=u"word word word word word";   // r397: full output must still end
     hakefi::dictionary::Apply(text,24,true,out,8);}
    if(failures)return 1;
    puts("PASS r382 text controls: check boxes, radio buttons, buttons, spin buttons, edit fields, combo boxes, folders; key hints untouched");
}
