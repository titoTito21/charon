// r393 host tests: formatting markers, dictionary, boot-manager configurations, settings V10.
// clang++ -std=c++17 -fuse-ld=lld -I src/efi -I <dir holding src/efi/haiku as "efi"> tests/CharonFormattingTests.cpp -o t.exe
#include <cstdio>
#include <cstring>
#include <string>
#include "CharonSpeechText.h"
#include "CharonFormatting.h"
#include "CharonManagerProfiles.h"
#include "CharSettings.h"
#include "CharonSynthHost.h"
#include "CharonReaderControl.h"
using namespace hakefi;
static int failures=0;
static void Check(bool ok,const char* what) {std::printf("%s %s\n",ok?"PASS":"FAIL",what);if(!ok)++failures;}
static std::u16string S(const char16_t* s) {return std::u16string(s);}
static std::string Narrow(const std::u16string& s) {std::string o;for(char16_t c:s)o+=c<128?char(c):(c==0x1b?'^':'?');return o;}
int main() {
    using namespace formatting;
    // ---- alignment ------------------------------------------------------
    static Grid g{};g.rows=3;
    auto row=[&](size_t r,const char16_t* text,uint8_t fg,uint8_t bg,bool hl) {
        size_t n=0;for(;text[n];++n) {g.text[r][n]=text[n];g.cell[r][n]=uint8_t(n);Style s;s.fg=fg;s.bg=bg;s.highlight=hl;g.style[r][n]=s;}
        g.length[r]=n;g.text[r][n]=0;
    };
    row(0,u"GNU GRUB version 2.12",7,0,false);
    row(1,u"Ubuntu",15,1,true);
    row(2,u"Advanced options for Ubuntu",7,0,false);
    Base base;base.fg=7;base.bg=0;
    char16_t out[400];
    const char16_t in[]=u"GNU GRUB version 2.12 \xE001: menu, 2 items, \xE002Ubuntu\xE001, 1 of 2\xE002";
    bool ok=Annotate<speechtext::ownOpen,speechtext::ownClose>(in,g,base,flagVoice|flagIcons|flagsDefault,1,out,400);
    Check(ok,"annotate finds the styled word");
    std::u16string a=S(out);
    const size_t mark=a.find(formatMark);
    Check(mark!=std::u16string::npos && a.find(u"Ubuntu")>mark,"marker before the highlighted word");
    // after the styled word, the reader's own words return to the plain voice
    Check(a.find(formatMark,mark+1)!=std::u16string::npos,"plain voice again for own words");
    Check(a[mark+7]>u'm' && a[mark+2]>u'm',"highlight: livelier range and higher pitch (r399: no echo)");
    Check(a[mark+5]==u'a'+15,"icon colour = white (15)");
    // voice only: no icon payload
    ok=Annotate<speechtext::ownOpen,speechtext::ownClose>(in,g,base,flagVoice|flagsDefault,1,out,400);
    a=S(out);Check(ok && a[a.find(formatMark)+5]==u'-',"voice only: no colour icon");
    // nothing enabled: false
    Check(!Annotate<speechtext::ownOpen,speechtext::ownClose>(in,g,base,flagsDefault,1,out,400),"outputs off: untouched");
    // plain text only: no marker
    Check(!Annotate<speechtext::ownOpen,speechtext::ownClose>(u"Advanced options for Ubuntu",g,base,flagVoice|flagsDefault,1,out,400),"plain row: no marker");
    // ---- compose --------------------------------------------------------
    {
        ok=Annotate<speechtext::ownOpen,speechtext::ownClose>(in,g,base,flagVoice|flagIcons|flagsDefault,1,out,400);
        speechtext::Reading r;speechtext::Lang sticky=speechtext::Lang::unknown;char16_t composed[800];
        speechtext::Compose(out,false,r,sticky,composed,800);
        std::u16string c=S(composed);
        Check(c.find(formatMark)==std::u16string::npos,"compose: no private marker left");
        const size_t esc=c.find(u"\x1b" u"f");
        Check(esc!=std::u16string::npos && c.size()>esc+16 && c[esc+16]==u' ',"compose: ESC f + 14 bytes + space");
        Check(c.find(u"Ubuntu")>esc,"compose: switch before Ubuntu");
        std::printf("  %s\n",Narrow(c).c_str());
        // a trailing marker is dropped
        char16_t tail[64]=u"Menu \xE003mmm0-ammm0---0";speechtext::Compose(tail,false,r,sticky,composed,800);
        Check(S(composed)==u"Menu","compose: trailing marker dropped");
    }
    // ---- dictionary -----------------------------------------------------
    {
        char16_t o[200];dictionary::Apply(u"TPM Configuration, XHCI Hand-off",32,true,o,200);
        Check(S(o)==u"te pe em Configuration, iks ha ce i Hand-off","dictionary pl");
        dictionary::Apply(u"VT-d and NVMe",13,false,o,200);
        Check(S(o)==u"V T D and N V M E","dictionary en (with hyphen)");
        dictionary::Apply(u"TPMS UEFIX",10,false,o,200);
        Check(S(o)==u"TPMS UEFIX","dictionary: whole words only");
        speechtext::Reading r;speechtext::Lang sticky=speechtext::Lang::unknown;char16_t composed[200];
        r.dictionary=false;speechtext::Compose(u"TPM",false,r,sticky,composed,200);Check(S(composed)==u"TPM","dictionary off");
    }
    // ---- configurations -------------------------------------------------
    {
        const char grub[]="set default=1\nset timeout_style=hidden\nset timeout=0\nmenuentry 'Debian GNU/Linux' --class debian {\n}\nsubmenu \"Advanced options\" {\n menuentry 'x' {}\n}\nmenuentry \"UEFI Firmware Settings\" {\n}\n";
        BcdSummary s;managers::config::Grub(grub,sizeof(grub)-1,s);
        Check(s.count==3 && s.defaultIndex==1 && s.timeout==0 && s.hidden,"grub.cfg: entries, default, hidden");
        Check(S(s.name[0])==u"Debian GNU/Linux" && S(s.name[1])==u"Advanced options","grub.cfg names");
        const char stub[]="search.fs_uuid 1234 root\nset prefix=($root)'/boot/grub'\nconfigfile $prefix/grub.cfg\n";
        BcdSummary t;managers::config::Grub(stub,sizeof(stub)-1,t);Check(t.elsewhere && !t.count,"grub stub: configuration elsewhere");
        char16_t d[300];DescribeBcd(s,true,d,300);std::string n;for(size_t i=0;d[i];++i)n+=d[i]<128?char(d[i]):'?';std::printf("  %s\n",n.c_str());
        Check(S(d).find(u"menu ukryte")!=std::u16string::npos,"describe: hidden");
        const char loader[]="default arch.conf\ntimeout menu-hidden\n";BcdSummary l;managers::config::SystemdLoader(loader,sizeof(loader)-1,l);Check(l.hidden,"loader.conf menu-hidden");
        const char loader2[]="timeout 5\n";BcdSummary l2;managers::config::SystemdLoader(loader2,sizeof(loader2)-1,l2);Check(!l2.hidden && l2.timeout==5,"loader.conf timeout 5");
        const char entry[]="title   Arch Linux\nlinux /vmlinuz-linux\n";managers::config::SystemdEntry(entry,sizeof(entry)-1,l2);Check(l2.count==1 && S(l2.name[0])==u"Arch Linux","systemd entry title");
        const char refind[]="timeout -1\nmenuentry \"Arch Linux\" {\n loader /vmlinuz\n}\n";BcdSummary rf;managers::config::Refind(refind,sizeof(refind)-1,rf);Check(rf.hidden && rf.count==1 && rf.automatic,"refind.conf");
        const char limine[]="timeout: 0\ndefault_entry: 2\n/Arch\n    protocol: linux\n//Sub\n/Windows\n";BcdSummary lm;managers::config::Limine(limine,sizeof(limine)-1,lm);
        Check(lm.hidden && lm.count==2 && lm.defaultIndex==1,"limine.conf");
        const char sys[]="UI menu.c32\nTIMEOUT 50\nLABEL linux\n MENU LABEL Install Debian\nLABEL rescue\n";BcdSummary sy;managers::config::Syslinux(sys,sizeof(sys)-1,sy);
        Check(sy.count==2 && sy.timeout==5 && !sy.hidden && S(sy.name[0])==u"Install Debian","syslinux.cfg");
        const char plist[]="<dict><key>Misc</key><dict><key>Boot</key><dict><key>ShowPicker</key><false/><key>Timeout</key><integer>5</integer></dict>"
            "<key>Entries</key><array><dict><key>Name</key><string>Fedora</string></dict></array><key>Tools</key><array><dict><key>Name</key><string>Shell</string></dict></array></dict></dict>";
        BcdSummary oc;managers::config::Plist(plist,sizeof(plist)-1,oc,true);Check(oc.hidden && oc.timeout==5 && oc.count==1 && S(oc.name[0])==u"Fedora","OpenCore config.plist");
        Check(managers::Find(u"GRUB") && managers::Find(u"GRUB")->revealScan==0x17 && managers::Find(u"systemd-boot")->revealChar==u' ',"profiles: reveal keys");
    }
    // ---- settings V10 ---------------------------------------------------
    {
        CharSettings v;v.sequence=5;v.formatFlags=63;v.readerExtra=2|4|16;uint8_t b[48];
        Check(EncodeSettings(v,b,48) && b[7]=='E' && b[40]==63 && b[41]==22,"settings V14 encode");
        CharSettings w;Check(DecodeSettings(b,48,w) && w.formatFlags==63 && w.readerExtra==22,"settings V10 decode");
        b[7]='9';b[40]=0;b[41]=0;b[36]&=3;b[32]&=7;b[33]&=3;b[37]&=7;const uint32_t crc=SettingsCRC(b,44);for(int i=0;i<4;++i)b[44+i]=uint8_t(crc>>(8*i));
        CharSettings x;Check(DecodeSettings(b,48,x) && x.formatFlags==60 && x.readerExtra==153,"settings V9 decodes to the defaults (r393 formatting, r400 extras)");
        v.readerExtra=3;Check(!EncodeSettings(v,b,48),"hidden menu mode 3 rejected");
        CharSettings y;y.sequence=9;y.synthKey=0xa7;y.synthVoice=2;Check(EncodeSettings(y,b,48) && b[42]==0xa7 && b[43]==2,"settings V11 synth bytes");
        CharSettings z;Check(DecodeSettings(b,48,z) && z.synthKey==0xa7 && z.synthVoice==2,"settings V11 decode synth");
        uint8_t sha[32];Sha256 h;h.Update((const uint8_t*)"abc",3);h.Final(sha);Check(sha[0]==0xba && sha[1]==0x78 && sha[31]==0xad,"sha256(abc)");
        const char man[]="abi = 2\nname = eSpeak NG\nmodule = espeak.tts\ndata = espeak\nvoice = espeak-pl | eSpeak NG polski | pl\nvoice = espeak-en | eSpeak NG English | en\nsha256 = 00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\n";
        synths::Info info;Check(synths::ParseManifest(man,sizeof(man)-1,info) && info.abi==2 && info.voices==2 && info.hasSha && S(info.module)==u"\\EFI\\HAK\\synths\\espeak.tts" && S(info.voiceName[1])==u"eSpeak NG English","manifest parse");
    }
    // ---- r397 ACSS, families, events, stereo, settings V12 ----------------
    {
        // highlight (inverse): voice-animate with a wider range; families on: the selection family
        bool ok=Annotate<speechtext::ownOpen,speechtext::ownClose>(in,g,base,flagVoice|flagFamilies|flagsDefault,1,out,400);
        std::u16string a=S(out);const size_t m=a.find(formatMark);
        Check(ok && m!=std::u16string::npos && a[m+7]>u'm',"ACSS: highlighted text has a wider pitch range");
        Check(a[m+8]>u'm' && a[m+10]==u'0'+familyHighlight,"ACSS: stress up, selection family");
        // dim text: lighter (breathy)
        Grid& d=*new Grid{};d.rows=1;d.columns=80;const char16_t dim[]=u"Disabled";for(size_t i=0;dim[i];++i) {d.text[0][i]=dim[i];d.cell[0][i]=uint8_t(i);Style s;s.fg=8;d.style[0][i]=s;}d.length[0]=8;
        ok=Annotate<speechtext::ownOpen,speechtext::ownClose>(u"Disabled",d,base,flagVoice|flagFamilies|flagsDefault,0,out,400);a=S(out);
        std::printf("  dim mark: %s\n",Narrow(a).c_str());Check(ok && a[a.find(formatMark)+9]<u'm' && a[a.find(formatMark)+10]==u'0'+familyDim,"ACSS: greyed out = breathy, dim family");
        // an event alone (no voice / icons): one mark with the event first
        Extras x;x.event=Event::openObject;
        ok=Annotate<speechtext::ownOpen,speechtext::ownClose>(u"Advanced options for Ubuntu",g,base,flagEvents|flagsDefault,1,out,400,x);a=S(out);
        Check(ok && a[0]==formatMark && a[11]==EventChar(Event::openObject),"event icon mark at the start");
        x.event=Event::none;Check(!Annotate<speechtext::ownOpen,speechtext::ownClose>(u"Advanced options for Ubuntu",g,base,flagEvents|flagsDefault,1,out,400,x),"events on, no event: untouched");
        // stereo speech: a word at the right side is placed right
        // r397: one place per row (the middle of its text): "Left" on the left, "Right" on the next row at the right
        Grid& p=*new Grid{};p.rows=2;p.columns=80;
        {const char16_t l[]=u"Left";for(size_t i=0;l[i];++i) {p.text[0][i]=l[i];p.cell[0][i]=uint8_t(i);}p.length[0]=4;
         const char16_t rr[]=u"Right";for(size_t i=0;rr[i];++i) {p.text[1][i]=rr[i];p.cell[1][i]=uint8_t(75+i);}p.length[1]=5;}
        x.stereo=stereoSpeech;ok=Annotate<speechtext::ownOpen,speechtext::ownClose>(u"Left Right",p,base,flagsDefault,0,out,400,x);a=S(out);
        const size_t last=a.rfind(formatMark);Check(ok && last!=std::u16string::npos && a[last+12]>=u'r' && a[last+12]<=u'u',"stereo: the right word placed right");
        x.stereo=stereoIcons;ok=Annotate<speechtext::ownOpen,speechtext::ownClose>(u"Left Right",p,base,flagIcons|flagsDefault,0,out,400,x);a=S(out);
        Check(!ok || a.rfind(formatMark)==std::u16string::npos || (a[a.rfind(formatMark)+12]>=u'A' && a[a.rfind(formatMark)+12]<=u'U'),"stereo icons only: upper-case place");
        // compose keeps all 12 bytes
        x.event=Event::warnUser;x.stereo=stereoOff;Annotate<speechtext::ownOpen,speechtext::ownClose>(u"GNU GRUB",g,base,flagEvents|flagsDefault,0,out,400,x);
        speechtext::Reading r;speechtext::Lang sticky=speechtext::Lang::unknown;char16_t composed[800];speechtext::Compose(out,false,r,sticky,composed,800);
        std::u16string c=S(composed);const size_t esc=c.find(u"\x1b" u"f");Check(esc!=std::u16string::npos && c[esc+12]==EventChar(Event::warnUser),"compose: event byte kept");
        // settings V12 and the reading word
        CharSettings v;v.sequence=7;v.formatFlags=uint8_t(flagEvents|flagFamilies|flagVoice);v.readerExtra=uint8_t(25|(2<<5));uint8_t b[48];
        CharSettings w;Check(EncodeSettings(v,b,48) && b[7]=='E' && DecodeSettings(b,48,w) && w.formatFlags==v.formatFlags && w.readerExtra==v.readerExtra,"settings V12 events/families/stereo");
        v.readerExtra=uint8_t(25|(3<<5));Check(!EncodeSettings(v,b,48),"stereo mode 3 rejected");
        const uint32_t reading=PackReading(1,1,0,47,uint8_t(flagEvents|flagFamilies|flagVoice),uint8_t(25|(2<<5)));
        Check(ReadingFormat(reading)==uint8_t(flagEvents|flagFamilies|flagVoice) && ReadingStereo(reading)==stereoSpeech && ((reading>>20)&31)==25,"reading word carries r397 bits");
    }
    std::printf(failures?"FAILED %d\n":"ALL PASSED\n",failures);
    return failures?1:0;
}
