// r410 host tests of the reader side: settings V13 (verbosity profile, formatting groups, formatting off by
// default), the reading word, the formatting map from element kind / state / position to voice and sound,
// and the new reader keys in both layouts.
//   clang++ -std=c++17 -fuse-ld=lld -I src/efi -I <dir holding src/efi/haiku as "efi"> tests/CharonReaderR410Tests.cpp -o t.exe
#include <cstdio>
#include <cstring>
#include <string>
#include "CharonSpeechText.h"
#include "CharonFormatting.h"
#include "CharSettings.h"
#include "CharonReaderControl.h"
#include "CharonKeyNotify.h"
#include "BootMenuModel.h"
#include "CharonManagerKeys.h"
#include "CharonLoaderWords.h"
using namespace hakefi;
using namespace hakefi::formatting;
static int checks=0,failed=0;
static void Check(bool ok,const char* what) {++checks;if(!ok) {++failed;std::printf("FAIL %s\n",what);}}
static void Crc(uint8_t* b) {const uint32_t crc=SettingsCRC(b,44);for(int i=0;i<4;++i)b[44+i]=uint8_t(crc>>(8*i));}
static KeyData Key(uint16_t scan,char16_t ch,uint32_t shift) {KeyData d{};d.key.ScanCode=scan;d.key.UnicodeChar=ch;d.state.shift=shift?shiftValid|shift:0;return d;}
int main() {
    // ---- settings V13 ---------------------------------------------------------
    {
        CharSettings d;
        Check(d.formatFlags==252 && !(d.formatFlags&(flagVoice|flagIcons)),"new settings: text formatting is off (outputs not ticked)");
        Check(d.profile==0 && d.formatGroups==7,"new settings: normal profile, the three groups ticked");
        CharSettings v;v.sequence=9;v.profile=2;v.formatGroups=5;v.punctuation=3;v.screenLanguage=2;uint8_t b[48];
        Check(EncodeSettings(v,b,48) && b[7]=='E',"V14 magic CHARS00E");
        Check(b[36]==(3|(5<<2)) && b[38]==(2|(2<<2)),"V13 bytes 36 and 38 carry groups and profile");
        CharSettings w;Check(DecodeSettings(b,48,w) && w.profile==2 && w.formatGroups==5 && w.punctuation==3 && w.screenLanguage==2 && SameSettings(v,w),"V13 round trip");
        v.profile=4;Check(!EncodeSettings(v,b,48),"profile 4 is refused");v.profile=1;v.profileItems=4096;Check(!EncodeSettings(v,b,48),"items above 12 bits are refused");v.profileItems=1083;
        // V14: the custom profile, its items, the reader bar and the absolute colour ladder, in bits that were zero
        {CharSettings c;c.sequence=3;c.profile=3;c.profileItems=0xA55;c.readerBar=true;c.absoluteColours=true;c.detail=5;c.timers=1;c.capitals=6;uint8_t e[48];
         Check(EncodeSettings(c,e,48) && (e[32]&7)==5 && (e[33]&3)==1 && (e[37]&7)==6,"V14: detail, timers and capitals keep their low bits");
         CharSettings r;Check(DecodeSettings(e,48,r) && r.profile==3 && r.profileItems==0xA55 && r.readerBar && r.absoluteColours && r.detail==5 && r.timers==1 && r.capitals==6 && SameSettings(c,r),"V14 round trip: custom profile, items, bar, absolute colours");
         CharSettings d;d.sequence=3;uint8_t f[48];EncodeSettings(d,f,48);f[7]='D';f[32]&=7;f[33]&=3;f[37]&=7;Crc(f);
         Check(DecodeSettings(f,48,r) && r.profile==0 && r.profileItems==formatting::itemsNormal && !r.readerBar && !r.absoluteColours,"V13 record: normal items, no bar, relative colours");
         f[32]|=8;Crc(f);Check(!DecodeSettings(f,48,r),"V13 record with V14 bits is refused");
         Check(profileItemsNormal==formatting::itemsNormal,"the settings default equals the normal profile's items");
         struct O {uint8_t profile=0;uint16_t profileItems=0;bool readerBar=false,absoluteColours=false;} o;ApplyMore(o,PackMore(3,0xA55,true,false));
         Check(o.profile==3 && o.profileItems==0xA55 && o.readerBar && !o.absoluteColours,"ReaderControl more word carries profile, items, bar");
         O untouched;untouched.profile=2;ApplyMore(untouched,0);Check(untouched.profile==2,"a zero more word changes nothing");}
        v.profile=1;v.formatGroups=8;Check(!EncodeSettings(v,b,48),"groups above 7 are refused");
        // a V12 record (r397-r409) keeps what it holds
        CharSettings o;o.sequence=4;o.formatFlags=255;o.readerExtra=153;EncodeSettings(o,b,48);b[7]='C';b[36]=1;b[38]=0;b[32]&=7;b[33]&=3;b[37]&=7;Crc(b);
        CharSettings x;Check(DecodeSettings(b,48,x) && x.formatFlags==255 && x.profile==0 && x.formatGroups==7,"V12 record: formatting as stored, normal profile, groups on");
        b[36]=1|(3<<2);Crc(b);Check(!DecodeSettings(b,48,x),"V12 record with V13 bits is refused");
        b[7]='D';b[38]=3<<2;Crc(b);Check(!DecodeSettings(b,48,x),"V13 record with profile 3 is refused");
        // V9: no formatting byte
        b[7]='9';b[36]=1;b[38]=0;b[40]=b[41]=b[42]=b[43]=0;Crc(b);
        Check(DecodeSettings(b,48,x) && x.formatFlags==60 && x.profile==0,"V9 record: r393 meaning (attributes on, outputs off)");
        // a different profile is a different setting (two slots with one sequence must not both win)
        CharSettings p=o,q=o;q.profile=1;Check(!SameSettings(p,q),"profile takes part in the comparison");
    }
    // ---- reading word and Resume bytes ---------------------------------------
    {
        const uint32_t r=PackReading(1,1,0,47,252,153,2);
        Check(ReadingProfile(r)==2 && ReadingFormat(r)==252 && ReadingStereo(r)==0 && ((r>>29)&1)==1,"reading word: profile in bits 30-31, older fields unchanged");
        Check(ReadingProfile(PackReading(1,1,0,47,60,25))==0,"reading word: profile defaults to normal");
        const uint8_t p=PackProgress(3,5);Check((p&3)==3 && ((p>>2)&7)==5,"progress byte: reporting and groups");
        Check(charonRelease==411,"reader control release 411 (an r409 driver is not mixed with an r410 menu)");
    }
    // ---- the formatting map --------------------------------------------------
    {
        Element e;e.kind=Kind::selected;e.index=0;e.count=5;e.first=true;
        Check(ElementEvent(e,groupsDefault)==Event::listFirst,"first item: list-start sound");
        e.index=4;e.first=false;e.last=true;Check(ElementEvent(e,groupsDefault)==Event::listLast,"last item: list-end sound");
        Check(ElementEvent(e,groupKind|groupState)==Event::none,"position group off: no boundary sound");
        e.kind=Kind::defaultItem;Check(ElementEvent(e,groupsDefault)==Event::itemDefaultLast,"default entry at the end: one sound for both");
        e.last=false;e.index=2;Check(ElementEvent(e,groupsDefault)==Event::itemDefault,"default entry");
        Check(ElementEvent(e,groupState|groupPosition)==Event::none,"kind group off: no default sound");
        e.kind=Kind::submenu;e.first=true;e.index=0;Check(ElementEvent(e,groupsDefault)==Event::itemSubmenuFirst,"submenu entry at the start");
        e=Element{};e.kind=Kind::edit;Check(ElementEvent(e,groupsDefault)==Event::editField,"edit field");
        e.kind=Kind::help;Check(ElementEvent(e,groupsDefault)==Event::helpLine,"help line");
        e.kind=Kind::error;Check(ElementEvent(e,groupsDefault)==Event::errorAlert,"error");
        e.kind=Kind::warning;Check(ElementEvent(e,groupsDefault)==Event::warnUser,"warning: another sound than the error");
        e.kind=Kind::countdown;Check(ElementEvent(e,groupsDefault)==Event::countdown,"countdown");
        e.kind=Kind::transient;Check(ElementEvent(e,groupsDefault)==Event::transient,"message that disappears");
        Check(ElementEvent(e,groupKind|groupPosition)==Event::none,"state group off: no state sound");
        e=Element{};e.kind=Kind::selected;e.index=2;e.count=6;e.indent=2;Check(ElementEvent(e,groupsDefault)==Event::indent2,"nesting depth 2");
        // every event has its own character; the old ones keep theirs
        bool distinct=true;for(unsigned a=1;a<unsigned(Event::count);++a)for(unsigned b=a+1;b<unsigned(Event::count);++b)if(EventChar(Event(a))==EventChar(Event(b)))distinct=false;
        Check(distinct && EventChar(Event::openObject)==u'b' && EventChar(Event::countdownEnd)==u'w' && EventChar(Event::listFirst)==u'x' && EventChar(Event::itemDefaultFirst)==u'A',"event characters: a-z, then A-Z");
        Check(unsigned(Event::count)<=52,"all events fit the alphabet");
        // pitch by position: falls from the first to the last item, symmetric
        Element p;p.count=9;int last=99;bool falling=true;
        for(uint8_t i=0;i<9;++i) {p.index=i;const int s=PositionPitch(p);if(s>last)falling=false;last=s;}
        p.index=0;const int top=PositionPitch(p);p.index=8;const int bottom=PositionPitch(p);p.index=4;
        Check(falling && top==4 && bottom==-4 && PositionPitch(p)==0,"pitch by position: +4 at the top, 0 in the middle, -4 at the bottom");
        p.count=1;p.index=0;Check(PositionPitch(p)==0,"a list of one: no position pitch");
        // the kinds really differ from each other in at least one voice dimension
        const Kind kinds[]={Kind::selected,Kind::defaultItem,Kind::submenu,Kind::edit,Kind::help,Kind::error,Kind::warning,Kind::countdown,Kind::transient};
        bool differ=true;
        for(size_t a=0;a<9;++a)for(size_t b=a+1;b<9;++b) {const Personality& x=KindVoice(kinds[a]);const Personality& y=KindVoice(kinds[b]);
            if(x.pitch==y.pitch && x.range==y.range && x.stress==y.stress && x.richness==y.richness && x.volume==y.volume && x.rate==y.rate && x.timbre==y.timbre)differ=false;}
        Check(differ,"nine kinds, nine different voices");
    }
    // ---- Annotate: the element's voice reaches the marker ----------------------
    {
        static Grid g;g.rows=3;g.columns=40;
        const char16_t* rows[]={u"GNU GRUB",u"Ubuntu",u"Windows"};
        for(size_t r=0;r<3;++r) {size_t n=0;for(;rows[r][n];++n) {g.text[r][n]=rows[r][n];g.cell[r][n]=uint8_t(n);}g.length[r]=n;for(size_t c=0;c<40;++c)g.style[r][c]=Style{};}
        Base base;char16_t out[400];
        auto pitchOf=[&](const Element& e,uint8_t flags,uint8_t groups)->int {
            Extras x;x.element=e;x.groups=groups;
            if(!Annotate<speechtext::ownOpen,speechtext::ownClose>(u"Ubuntu",g,base,flags,1,out,400,x))return 0;   // no mark: the plain voice
            std::u16string a(out);const size_t m=a.find(formatMark);return m==std::u16string::npos?0:int(a[m+2])-int(u'm');};
        Element first;first.kind=Kind::selected;first.index=0;first.count=5;Element last=first;last.index=4;
        const int hi=pitchOf(first,flagVoice|flagsDefault,groupsDefault),lo=pitchOf(last,flagVoice|flagsDefault,groupsDefault);
        Check(hi-lo==8,"voice on: the first item is spoken 8 steps higher than the last");
        Check(pitchOf(first,flagVoice|flagsDefault,groupKind|groupState)==pitchOf(last,flagVoice|flagsDefault,groupKind|groupState),"position group off: same pitch for every place");
        Element def=first;def.index=2;def.kind=Kind::defaultItem;Element plain=def;plain.kind=Kind::selected;
        Check(pitchOf(def,flagVoice|flagsDefault,groupsDefault)<pitchOf(plain,flagVoice|flagsDefault,groupsDefault),"default entry: lower voice than a plain entry");
        Check(pitchOf(def,flagVoice|flagsDefault,groupState|groupPosition)==pitchOf(plain,flagVoice|flagsDefault,groupState|groupPosition),"kind group off: the default entry sounds like any other");
        Extras x;x.element=def;x.groups=groupsDefault;
        Check(!Annotate<speechtext::ownOpen,speechtext::ownClose>(u"Ubuntu",g,base,flagsDefault,1,out,400,x),"formatting off (the default): the utterance is untouched");
        // a model sound alone (sound icons on, voice off): one mark with the event, voice fields neutral
        x.event=ElementEvent(def,groupsDefault);
        const bool ok=Annotate<speechtext::ownOpen,speechtext::ownClose>(u"Ubuntu",g,base,uint8_t(flagIcons|flagsDefault),1,out,400,x);
        Check(ok && out[0]==formatMark && out[11]==EventChar(Event::itemDefault) && out[2]==u'm',"sound icons only: the default-entry sound, the voice unchanged");
        // the mark survives Compose with all 12 payload characters
        speechtext::Reading r;speechtext::Lang sticky=speechtext::Lang::unknown;char16_t composed[800];speechtext::Compose(out,false,r,sticky,composed,800);
        std::u16string c(composed);const size_t esc=c.find(u"\x1b" u"f");
        Check(esc!=std::u16string::npos && c[esc+12]==EventChar(Event::itemDefault),"compose keeps the new event character");
    }
    // ---- keys ------------------------------------------------------------------
    {
        const uint32_t ca=leftControl|leftAlt;
        struct {char16_t ch;char16_t want;} ctrlAlt[]={{u'f',u'F'},{u'/',u'N'},{u'\'',u'P'},{u';',u'L'},{u'[',u'['},{u']',u']'},{u'g',u'G'},{u'0',u'B'},{u'9',u'J'},{u'8',u'U'},{u'v',u'v'},{u'w',u'w'},{u'y',u'y'}};
        bool ok=true;for(auto& k:ctrlAlt) {KeyData d=Key(0,k.ch,ca);if(ReaderKeys::Command(&d)!=k.want) {ok=false;std::printf("  Ctrl+Alt+%c\n",char(k.ch));}}
        Check(ok,"Ctrl+Alt set: F find, / next, ' previous, ; elements, [ ] elements, G formatting, 0 9 8 bookmarks");
        {KeyData d=Key(0,6,ca);Check(ReaderKeys::Command(&d)==u'F',"Ctrl+Alt+F as the control code 6");}
        struct {uint16_t scan;char16_t ch;char16_t want;} insert[]={{0x11,0,u'L'},{0x0d,0,u'N'},{0x0c,0,u'P'},{0x0e,0,u'F'},{0x0f,0,u'['},{0x10,0,u']'},{0x12,0,u'B'},{0,u']',u'J'},{0,u'[',u'U'},
            {0,u'v',u'v'},{0,u'g',u'G'},{0,u'd',u'D'},{0,u't',u'w'},{0x0b,0,u'?'},{0x13,0,u'C'},{0x14,0,u'K'},{0x15,0,u'V'},{0x16,0,u'c'},{0,u'7',u'u'},{0,u'5',u'k'},{6,0,u'y'}};
        ok=true;for(auto& k:insert) {efi_input_key key{};key.ScanCode=k.scan;key.UnicodeChar=k.ch;if(InsertCommand(key)!=k.want) {ok=false;std::printf("  Insert scan %x char %x\n",k.scan,k.ch);}}
        Check(ok,"Insert layout: F7 elements, F3 / F2 find next / previous, F4 find, F5 / F6 elements, F8 bookmark, ] [ bookmarks; older keys unchanged");
    }
    // ---- settings menu rows ------------------------------------------------------
    {
        Check(readerProfileRow+1==readerCount && formatItemLastRow+1==formattingCount && formatKindRow==formatDictionaryRow+1 && formatAbsoluteRow==formatPositionRow+1 && formatItemRow+formatting::profileItemCount==formattingCount,"menu rows: profile last on the reader page; groups, absolute colours, reader bar and the 12 custom items on the formatting page");
        {MenuModel c;c.page=MenuPage::formatting;c.index=formatBarRow;c.Key(MenuEvent::space);Check(c.readerBar,"reader bar check box");
         c.index=formatAbsoluteRow;c.Key(MenuEvent::enter);Check(c.absoluteColours,"absolute colours check box");
         c.index=formatItemRow+2;c.Key(MenuEvent::space);Check(c.profileItems==(1083|4),"custom profile item check box (key hints)");}
        MenuModel m;m.page=MenuPage::reader;m.index=readerProfileRow;
        m.Key(MenuEvent::right);const uint8_t a=m.profile;m.Key(MenuEvent::right);const uint8_t b=m.profile;m.Key(MenuEvent::right);const uint8_t c3=m.profile;m.Key(MenuEvent::right);
        Check(a==1 && b==2 && c3==3 && m.profile==0,"profile row cycles normal, beginner, expert, custom with Right");
        // ---- the formatting design of the user's answers 10-13 ----
        {using namespace formatting;
         Check(KindVoice(Kind::error).pitch<0 && KindVoice(Kind::error).rate<0 && KindVoice(Kind::error).pitch<KindVoice(Kind::warning).pitch,"kind table: the error voice is lower and slower than everything, the warning a little lower");
         Check(KindVoice(Kind::help).rate>0 && KindVoice(Kind::defaultItem).rate<0 && KindSpec(Kind::help).family!=0 && KindSpec(Kind::error).notes[0].hz0<KindSpec(Kind::warning).notes[0].hz0 && KindSpec(Kind::error).notes[0].square && !KindSpec(Kind::warning).notes[0].square,"kind table: auxiliary faster, important slower, another speaker for help; the error tone is lower and harder than the warning's");
         Element e;e.count=5;e.index=0;const char16_t top=PlaceChar(e);e.index=4;const char16_t bottom=PlaceChar(e);e.index=2;const char16_t mid=PlaceChar(e);
         Check(top==u'a' && bottom==u'y' && mid==u'm' && PlaceHertz(0)==1760 && PlaceHertz(12)==880 && PlaceHertz(24)==440 && PlaceHertz(6)==1245,"place tone: top 1760 Hz, middle 880 Hz, bottom 440 Hz");
         e.count=1;e.index=0;Check(PlaceChar(e)==u'-',"place tone: none for a list of one");
         Base base;base.fg=7;base.bg=0;Style plain;plain.fg=7;plain.bg=0;Style red;red.fg=12;red.bg=0;Style sel;sel.fg=0;sel.bg=7;sel.highlight=true;
         Check(Plain(VoiceFor(plain,base,flagsDefault,3,25)) && Plain(VoiceFor(plain,base,flagsDefault,20,25)),"relative colours (default): text in the screen's own colours is plain on every row");
         Check(VoiceFor(red,base,flagsDefault).pitch>0 && VoiceFor(sel,base,flagsDefault).range>0,"relative colours: red text and the selection stand out");
         Base white;white.fg=12;white.bg=0;Check(Plain(VoiceFor(red,white,flagsDefault)),"relative colours: a uniform red screen sounds normal");
         Check(!Plain(VoiceFor(plain,base,flagsDefault,0,25,true)) && VoiceFor(red,white,flagsDefault,SIZE_MAX,0,true).pitch>0,"absolute colours (option): the r399 ladder still answers");
         Check(ProfileItems(0,0)==itemsNormal && (ProfileItems(1,0)&itemHints) && (ProfileItems(1,0)&itemStructure) && !(ProfileItems(0,0)&itemStructure) && ProfileItems(2,0)==(itemSounds|itemWaitShort) && ProfileItems(3,0x123)==0x123,"profiles as item sets: structure words only for beginners, the custom set as stored");
         // an element utterance: place tone on the first mark, a pause and the auxiliary voice before the reader's own words
         static Grid g{};g.rows=2;g.columns=80;const char16_t* t=u"    Ubuntu";size_t n=0;for(;t[n];++n) {g.text[0][n]=t[n];g.cell[0][n]=uint8_t(n);g.style[0][n]=sel;}g.length[0]=n;
         Extras x;x.element.kind=Kind::defaultItem;x.element.index=1;x.element.count=5;x.element.indent=2;x.groups=groupsDefault;x.sounds=true;x.stereo=stereoSpeech;
         char16_t out[300];const bool ok=Annotate<speechtext::ownOpen,speechtext::ownClose>(u"Ubuntu\xE001, default, 2 of 5\xE002",g,base,uint8_t(flagVoice|flagIcons|flagsDefault),0,out,300,x);
         std::u16string a(out);const size_t m1=a.find(formatMark),m2=a.find(formatMark,m1+1),m3=a.find(formatMark,m2+1);
         Check(ok && m1!=std::u16string::npos && a[m1+13]==u'g' && m3!=std::u16string::npos && a[m3+14]==u'4' && a[m3+10]==char16_t(u'0'+ownWordsFamily),"annotate: place tone of 2 of 5 on the first mark; 80 ms pause and the other speaker before the reader's own words");
         Check(a[m1+12]>a.npos-a.npos+u'a' && a[m1+12]==char16_t(PanFor(6,80)+4),"annotate: the element's sounds are placed at its column, shifted right by its depth");}
        m.Key(MenuEvent::left);Check(m.profile==3,"and back with Left (custom is the fourth state)");
        m.page=MenuPage::formatting;m.index=formatKindRow;m.Key(MenuEvent::space);Check(m.formatGroups==6,"kind check box");
        m.index=formatPositionRow;m.Key(MenuEvent::enter);Check(m.formatGroups==2,"position check box");
        m.index=formatStateRow;m.Key(MenuEvent::space);Check(m.formatGroups==0,"state check box");
        const uint8_t flags=m.formatFlags,extra=m.readerExtra;m.index=formatKindRow;m.Key(MenuEvent::space);
        Check(m.formatFlags==flags && m.readerExtra==extra && m.formatGroups==1,"the new boxes leave the older settings alone");
    }
    // ---- r410 (user's answers 4 and 6): keys of the programs, words of entries -------------------------
    {
        using namespace hakefi::managerkeys;using hakefi::screenmodel::Manager;using hakefi::screenmodel::Screen;
        size_t grubMenu=0,grubEditor=0,sendable=0,enterSent=0,noText=0,refindAny=0;
        for(size_t i=0;i<keyCount;++i) {
            if(Fits(i,Manager::grub,Screen::menu))++grubMenu;
            if(Fits(i,Manager::grub,Screen::editor))++grubEditor;
            if(Fits(i,Manager::refind,Screen::menu))++refindAny;
            if(Sendable(i))++sendable;
            // Enter in a menu starts a system: the elements list never sends it (the Shell's pager may)
            if(keys[i].ch==13 && keys[i].screen!=Screen::pager)++enterSent;
            if(!keys[i].name[0] || !keys[i].en[0] || !keys[i].pl[0])++noText;
        }
        Check(grubMenu==8 && grubEditor==6,"GRUB: 8 keys in the menu, 6 in the entry editor");
        Check(refindAny==7,"rEFInd: its keys fit every screen of it");
        Check(!Fits(0,Manager::limine,Screen::menu) && !Fits(keyCount,Manager::grub,Screen::menu),"a row belongs to one program; out of range fits nothing");
        Check(enterSent==0 && noText==0,"no menu's Enter is sendable; every row has a name and both languages");
        Check(sendable>=30 && sendable<keyCount,"some keys are only named, not sent");
        Check(Fits(0,Manager::unknown,Screen::menu)==false,"an unknown program has no keys: nothing is guessed");
    }
    {
        using namespace hakefi::loaderwords;
        auto find=[](const char16_t* t) {size_t n=0;while(t[n])++n;return Find(t,n);};
        Check(find(u"quiet")<wordCount && find(u"root=/dev/sda2")<wordCount && find(u"nomodeset")<wordCount && find(u"linux")<wordCount && find(u"initrd")<wordCount,
            "the user's examples are known: linux, initrd, quiet, root=, nomodeset");
        Check(find(u"root")==wordCount && find(u"quietly")==wordCount && find(u"Quiet")==wordCount && find(u"linux16x")==wordCount,"a word is known whole and in its own case only");
        Check(words[find(u"initrd=/boot/initrd.img")].word[6]==u'=' && words[find(u"initrd")].word[6]==0,"initrd the GRUB command and initrd= the kernel parameter are two rows");
        // Decision F5: words of distribution boot entries come from distribution documentation, source named per row.
        auto src=[&](const char16_t* t,const char16_t* want) {const size_t i=find(t);return i<wordCount && std::u16string(words[i].source)==want;};
        Check(src(u"splash",u"Arch Wiki: Plymouth") && src(u"single",u"systemd(1)") && src(u"rd.break=pre-mount",u"dracut.cmdline(7)") && src(u"rd.break",u"dracut.cmdline(7)")
            && src(u"boot=live",u"Debian live-boot(7)") && src(u"rhgb",u"Fedora Wiki: Features/BetterStartup") && src(u"recordfail",u"Ubuntu grub2: quick-boot.patch"),
            "distribution words are known and name their document: splash, single, rd.break, boot=live, rhgb, recordfail");
        Check(find(u"linuxefi")<wordCount && find(u"initrdefi")<wordCount && find(u"load_video")<wordCount && find(u"gfxpayload=keep")<wordCount && find(u"$vt_handoff")<wordCount
            && find(u"systemd.unit=rescue.target")<wordCount && find(u"cryptdevice=UUID=1:root")<wordCount && find(u"---")<wordCount,
            "words of generated GRUB entries are known: linuxefi, initrdefi, load_video, gfxpayload=, $vt_handoff");
        Check(find(u"gfxpayload")==wordCount && find(u"gfxmode=auto")!=find(u"gfxmode") && find(u"gfxmode")<wordCount && find(u"gfxmode=auto")<wordCount
            && find(u"boot=casper")==wordCount && find(u"persistence-label=x")==wordCount && find(u"Splash")==wordCount && find(u"singles")==wordCount,
            "the added words too are known whole (or as a marked prefix) and in their own case only");
        Check(find(u"BOOT_IMAGE=/vmlinuz")==wordCount && find(u"nvidia-drm.modeset=1")==wordCount && find(u"inst.stage2=hd:LABEL=x")==wordCount,
            "words that no fetched document confirmed are still not explained");
        size_t bad=0,unsourced=0,twice=0;
        for(size_t i=0;i<wordCount;++i) {
            if(!words[i].en[0] || !words[i].pl[0])++bad;
            if(!words[i].source || !words[i].source[0])++unsourced;
            for(size_t k=0;k<i;++k)if(std::u16string(words[k].word)==words[i].word)++twice;
        }
        Check(bad==0 && wordCount==95,"95 rows (52 of the three documents and 43 of distribution documentation), every one with both languages");
        Check(unsourced==0 && twice==0,"every row names its source document and no word is listed twice");
    }
    std::printf("%d checks, %d failed\n",checks,failed);
    return failed?1:0;
}
