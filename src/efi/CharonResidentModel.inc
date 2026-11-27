// Charon resident reader: screen model, navigation and verbosity profiles (r410). GPL-2.0-or-later.
// Included INSIDE class ResidentReader (CharonResident.h); kept apart so that one implementation
// serves every screen: Charon's own screens (the review reader beside Charon), started programs and
// boot managers all reach these members through the same Command() path.
//
//   - BuildModel(): CharonScreenModel.h on the reader's grid; the default entry is remembered from the
//     moment a program counted down untouched.
//   - Speaking from the model: position "N of M", "submenu", "default, starts in N seconds", the
//     screen's kind when an editor or command line opens. Only from elements the model is sure of;
//     everything else is read as before r410 (raw screen text).
//   - Navigation: elements list (filter by typing), previous / next element, find on the screen,
//     bookmarks per screen, "what changed since this screen appeared", where am I, help line.
//   - Verbosity profiles: 0 normal, 1 beginner, 2 expert (the table is in the results file and in
//     README: what each speaks, which hints, which sounds replace words).
//
// Conventions taken from screen readers users know (URLs in tests/charon-r410-reader-results.txt):
// NVDA's elements list (NVDA+F7: type to filter, Enter moves), find next / previous (NVDA+F3,
// NVDA+Shift+F3), "N of M" positions and the word "submenu"; Orca's double press for a detailed
// "where am I"; Fenrir / BRLTTY bookmarks tied to a screen; Emacspeak's auditory icons played with speech.
screenmodel::Model model_{};
char16_t defaultText_[81]={};const void* defaultImage_=nullptr;
formatting::Element element_{};            // what the model says about the utterance being put together
uint8_t lastScreenKind_=0;const void* lastScreenImage_=nullptr;
uint8_t introKind_=0;const void* introImage_=nullptr;   // answer 2: what was last introduced (1 menu, 2 editor, 3 command line): the program's name is said only when the kind changes
uint32_t countdownSaidFor_=0;bool modelScrolls_=false;
const void* modelTickImage_=nullptr;uint16_t modelTickLast_=0xffff;   // the countdown row already spoken for this program
// navigation state
static constexpr char16_t navKeyCommand=0x14;
struct NavKey {uint16_t scan;char16_t ch;};
NavKey navKeys_[16]={};volatile uint8_t navHead_=0,navTail_=0;
volatile uint8_t navMode_=0;               // 0 none, 1 elements list, 2 find prompt
struct NavElement {uint8_t kind,row,col,index;};
enum : uint8_t {navTitle=0,navWindow,navItem,navCountdown,navHelp,navEdit,navMessage,navProgress,navLine,navKey};
static constexpr uint8_t navKeyRow=0xfe;   // keys of the program stand after everything on the screen
static constexpr size_t maxNav=96;
NavElement nav_[maxNav]={};size_t navCount_=0,navIndex_=0;
uint8_t navShown_[maxNav]={};size_t navShownCount_=0;
char16_t navFilter_[32]={};size_t navFilterLength_=0;
char16_t findText_[32]={};size_t findLength_=0;
struct Bookmark {uint32_t screen,rowHash;uint8_t row,col;bool used;};
Bookmark marks_[8]={};size_t markNext_=0;
char16_t enter_[maxRows][maxColumns]={};size_t enterRows_=0,enterColumns_=0;bool haveEnter_=false,enterDrawing_=true;
uint16_t sendScan_[48]={};size_t sendAt_=0,sendCount_=0;
size_t braillePrefix_=0;

uint8_t Profile() const {return options_.profile<=3?options_.profile:0;}
// r410 (answer 14): what a profile speaks is a set of items; the custom profile (3) has a check box per item.
uint16_t Items() const {return formatting::ProfileItems(Profile(),options_.profileItems);}
bool Item(uint16_t item) const {return (Items()&item)!=0;}
bool Terse() const {return !Item(formatting::itemIntro);}
uint8_t Groups() const {return Item(formatting::itemSounds)?formatting::groupsDefault:options_.formatGroups;}
// State and warning formatting is in use (answer 11: alarm before the text, an error read in full).
bool StateFormatting() const {return ((options_.formatFlags&(formatting::flagVoice|formatting::flagIcons))!=0 || Item(formatting::itemSounds)) && (Groups()&formatting::groupState)!=0;}
// Sounds of the model: with sound icons on, and always in the expert profile (there they replace words).
bool ModelSounds() const {return (options_.formatFlags&formatting::flagIcons)!=0 || Item(formatting::itemSounds);}

struct NavOut {
    char16_t* p;size_t cap,at;
    void C(char16_t c) {if(at+1<cap) {p[at++]=c;p[at]=0;}}
    void S(const char16_t* s) {while(*s)C(*s++);}
    void N(size_t v) {char16_t d[20];size_t n=0;do {d[n++]=char16_t(u'0'+v%10);v/=10;} while(v && n<20);while(n)C(d[--n]);}
    void Own(const char16_t* s) {C(speechtext::ownOpen);S(s);C(speechtext::ownClose);}
};
static const char16_t* SecondsWord(size_t n,bool pl) {
    if(!pl)return n==1?u" second":u" seconds";
    if(n==1)return u" sekund\x0119";
    return (n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20))?u" sekundy":u" sekund";
}
static uint32_t Hash16(const char16_t* s,size_t n) {uint32_t h=2166136261u;for(size_t i=0;i<n;++i) {h^=uint32_t(s[i]);h*=16777619u;}return h;}
bool CountingNow() const {return countdownSeen_ && tick_-countdownSeen_<250 && !everKey_;}
screenmodel::View ModelView() const {
    screenmodel::View v;v.text=&text_[0][0];v.attr=&attr_[0][0];v.stride=maxColumns;v.rows=rows_;v.columns=columns_;
    v.cursorRow=row_;v.cursorCol=column_;v.cursorShown=CursorShown();v.majorityBg=MajorityBackground();v.countingDown=CountingNow();
    // icon pickers (rEFInd, Clover): the row of icons the picture analysis holds, and the lit one
    if(iconSelected_<24 && icons_[iconSelected_].used) {
        size_t count=0,index=0;
        for(const auto& tile:icons_)if(tile.used && tile.h==icons_[iconSelected_].h && tile.y==icons_[iconSelected_].y) {++count;if(tile.x<icons_[iconSelected_].x)++index;}
        if(count>=2 && count<=24) {v.iconCount=uint8_t(count);v.iconSelected=uint8_t(index);}
    }
    return v;
}
// Text of an item or of cells [left,right) of a row, as it is read.
size_t CellRange(size_t r,size_t left,size_t right,char16_t* out,size_t capacity) const {
    if(r>=rows_) {out[0]=0;return 0;}
    return CellsText(text_[r],attr_[r],left,right<columns_?right:columns_,false,out,capacity);
}
void BuildModel() {
    const screenmodel::View v=ModelView();
    screenmodel::Build(v,bootManager_,model_);
    if(defaultImage_!=currentImage_) {defaultImage_=currentImage_;defaultText_[0]=0;}
    auto& list=model_.list;
    if(!list.found || !list.sure)return;
    if(list.defaultIndex!=screenmodel::none8 && list.items[list.defaultIndex].row<rows_) {
        const auto& it=list.items[list.defaultIndex];CellRange(it.row,it.left,it.right,defaultText_,81);
    } else if(defaultText_[0] && list.defaultIndex==screenmodel::none8) {
        for(size_t i=0;i<list.count;++i) {const auto& it=list.items[i];if(it.row>=rows_)continue;
            char16_t t[81];CellRange(it.row,it.left,it.right,t,81);
            if(Same16(t,defaultText_)) {list.defaultIndex=uint8_t(i);list.items[i].flags|=screenmodel::itemDefault;break;}}
    }
}
// Flush(): the highlighted row is `row`. With a sure list whose selected item is that row the position
// comes from the model; otherwise index and count stay as the reader worked them out before r410.
bool ModelPosition(size_t row,size_t& index,size_t& count) {
    element_=formatting::Element{};modelScrolls_=false;
    BuildModel();
    const auto& list=model_.list;
    if(!list.found || !list.sure || list.selected==screenmodel::none8 || list.items[list.selected].row!=row) {
#ifdef HAK_CHARON_TRACE
        if(count>=2)charontrace::Value("CHARON model raw",(uint64_t(list.found)<<32)|(uint64_t(list.sure)<<24)|(uint64_t(index)<<8)|count);
#endif
        return false;
    }
#ifdef HAK_CHARON_TRACE
    if(index!=list.selected || count!=list.count)charontrace::Value("CHARON model position differs",(uint64_t(index)<<48)|(uint64_t(count)<<32)|(uint64_t(list.selected)<<16)|list.count);
#endif
    // A model list SHORTER than the block of rows the reader itself found is not trusted (Ventoy's themed menu:
    // the model lost the first entry and said "1 of 2" for the second of three; r408 said "2 of 3"). iPXE apart:
    // there the model leaves the gaps out on purpose.
    if(list.count<count && model_.manager!=screenmodel::Manager::ipxe) {
#ifdef HAK_CHARON_TRACE
        charontrace::Value("CHARON model list shorter, not used",(uint64_t(count)<<16)|list.count);
#endif
        return false;
    }
    index=list.selected;count=list.count;modelScrolls_=list.moreAbove || list.moreBelow;
    const auto& it=list.items[list.selected];
    element_.kind=(it.flags&screenmodel::itemDefault)?formatting::Kind::defaultItem:(it.flags&screenmodel::itemSubmenu)?formatting::Kind::submenu:formatting::Kind::selected;
    element_.index=uint8_t(index);element_.count=uint8_t(count);element_.first=index==0 && !list.moreAbove;element_.last=index+1==count && !list.moreBelow;
    element_.indent=uint8_t(it.indent+list.depth);
    return true;
}
// Words after the selected item's text. Normal and beginner: "submenu", "default" and the time left;
// expert: nothing (the element's sound says it).
void ModelItemWords(char16_t* speech,size_t capacity) {
    const auto& list=model_.list;
    if(element_.kind==formatting::Kind::none || !list.sure || list.selected==screenmodel::none8)return;
    const bool pl=options_.polish;const auto& it=list.items[list.selected];
    char16_t words[160];NavOut o{words,160,0};words[0]=0;
    if(Item(formatting::itemKind)) {
        if((it.flags&screenmodel::itemSubmenu) && !HasWord(speech,u"submenu") && !HasWord(speech,u"podmenu"))o.S(pl?u", podmenu":u", submenu");
        if(it.flags&screenmodel::itemDefault)o.S(pl?u", domy\x015blna":u", default");
    }
    // r410 (user, follow-up F2): an entry whose NAME says recovery mode, firmware setup, memory test or erasing
    // gets a short warning SOUND every time it is selected, in every profile and with formatting off; the words
    // only where the profile gives hints (beginner) and on demand (where am I).
    if(it.row<rows_) {char16_t name[maxColumns+1];
        uint8_t risk=CellRange(it.row,it.left,it.right,name,maxColumns+1)?RiskOfName(name):0;
        // what the program's CONFIGURATION says about this entry (CharonEntryInfo.h), when exactly one entry has
        // this title: a risk the name does not show, and "submenu" (GRUB draws a submenu like any entry)
        if(name[0] && entryInfoCount_) {
            const size_t m=entryinfo::Match(entryInfo_,entryInfoCount_,name);
            if(m!=entryinfo::noMatch) {
                const auto& e=entryInfo_[m];
                if(!risk)risk=e.risk==entryinfo::riskRecovery?1:e.risk==entryinfo::riskFirmwareSetup?2:e.risk==entryinfo::riskMemoryTest?3:e.risk==entryinfo::riskInstaller?5:0;
                if(e.submenu && !(it.flags&screenmodel::itemSubmenu)) {
                    element_.kind=formatting::Kind::submenu;
                    if(Item(formatting::itemKind) && !HasWord(speech,u"submenu") && !HasWord(speech,u"podmenu"))o.S(pl?u", podmenu":u", submenu");}
            }
        }
        if(risk) {
            auto* fx=options_.effects;if(fx && fx->Beep)fx->Beep(fx->context,330,90);
#ifdef HAK_CHARON_TRACE
            charontrace::Value("CHARON entry risk sound",risk);
#endif
            if(Item(formatting::itemHints)) {
                static const char16_t* const en[]={u"",u", warning: recovery mode",u", warning: firmware setup",u", warning: memory test",u", warning: erases data",u", warning: installer"};
                static const char16_t* const plw[]={u"",u", uwaga: tryb awaryjny",u", uwaga: ustawienia firmware",(u", uwaga: test pami\x0119" u"ci"),u", uwaga: kasuje dane",u", uwaga: instalator"};
                o.S((pl?plw:en)[risk<6?risk:0]);}
        }
    }
    // answer 12: structure in WORDS only where the profile asks for them (beginner); sounds otherwise
    if(Item(formatting::itemStructure) && element_.count>=2) {
        if(element_.first)o.S(pl?u", pocz\x0105tek listy":u", start of list");
        else if(element_.last)o.S(pl?u", koniec listy":u", end of list");
    }
    if(!words[0])return;
    size_t at=0;while(at+1<capacity && speech[at])++at;
    NavOut s{speech,capacity,at};s.Own(words);
}
// The beginner's hint after a menu was introduced.
void ModelMenuHint(char16_t* speech,size_t capacity) {
    const bool pl=options_.polish;
    size_t at=0;while(at+1<capacity && speech[at])++at;
    NavOut s{speech,capacity,at};
    if(Item(formatting::itemStructure) && model_.list.sure && model_.list.depth) {
        char16_t level[40];NavOut l{level,40,0};l.S(pl?u", poziom ":u", level ");l.N(size_t(model_.list.depth)+1);s.Own(level);}
    if(!Item(formatting::itemHints))return;
    const bool icons=bootManager_ && (bootManager_[0]==u'r' || bootManager_[0]==u'C');
    s.Own(icons?(pl?u". Strza\x0142ki w lewo i w prawo wybieraj\x0105, Enter uruchamia. Insert F1: lista klawiszy, Insert F7: lista element\x00f3w."
                   :u". Left and Right choose, Enter starts. Insert F1: key list, Insert F7: elements list.")
               :(pl?u". Strza\x0142ki w g\x00f3r\x0119 i w d\x00f3\x0142 wybieraj\x0105, Enter uruchamia. Insert F1: lista klawiszy, Insert F7: lista element\x00f3w."
                   :u". Up and Down choose, Enter starts. Insert F1: key list, Insert F7: elements list."));
}
// iPXE shows the seconds left of "choose --timeout" as "(N)" at the end of the selected row: that is a countdown,
// not a new entry every second. Cuts it from the selected text; returns the seconds, or -1.
long TrimRowCountdown(char16_t* selected) {
    if(!bootManager_ || !(bootManager_[0]==u'i' || bootManager_[0]==u'n'))return -1;
    size_t n=0;while(selected[n])++n;
    if(n<5 || selected[n-1]!=u')')return -1;
    size_t c=n-1;long value=0;size_t digits=0;
    while(c && selected[c-1]>=u'0' && selected[c-1]<=u'9') {--c;++digits;}
    if(!digits || digits>4 || c<2 || selected[c-1]!=u'(' || selected[c-2]!=u' ')return -1;
    for(size_t k=c;k<n-1;++k)value=value*10+long(selected[k]-u'0');
    size_t e=c-1;while(e && selected[e-1]==u' ')--e;selected[e]=0;
    return value;
}
// r410 (user, answer 1): the beginner profile ends every entry with what Enter does there.
void ModelEntryHint(char16_t* speech,size_t capacity) {
    if(!Item(formatting::itemHints) || element_.kind==formatting::Kind::none)return;
    const bool pl=options_.polish;
    size_t at=0;while(at+1<capacity && speech[at])++at;
    NavOut s{speech,capacity,at};
    s.Own(element_.kind==formatting::Kind::submenu?(pl?u". Enter otwiera":u". Enter opens it"):(pl?u". Enter uruchamia":u". Enter starts it"));
}
// Is the position ("2 of 6") said for this move? Expert: only when the menu is first met.
bool ModelSayPosition(bool introduced) const {return Item(formatting::itemPosition) || introduced;}
// A screen of another kind after a key: an entry editor or a command line is named (expert: its sound only).
void ModelScreenIntro(char16_t* speech,size_t capacity) {
    BuildModel();
    const uint8_t kind=uint8_t(model_.screen);
    const bool changed=kind!=lastScreenKind_ || lastScreenImage_!=currentImage_;
    const bool wasMenu=lastScreenKind_==uint8_t(screenmodel::Screen::menu) && lastScreenImage_==currentImage_;
    lastScreenKind_=kind;lastScreenImage_=currentImage_;
    if(!changed || !KeyRecent())return;
    const bool pl=options_.polish;
    // r410: GRUB's menu went away after "e" or "c" and what came instead cannot be read (a theme that draws
    // its terminal in a spaced-out proportional font: Debian's starfield). Said, instead of silence.
    if(wasMenu && model_.manager==screenmodel::Manager::grub && model_.screen==screenmodel::Screen::unknown && !model_.list.found
        && (keyChar_==u'e' || keyChar_==u'c')
        && [&]() {size_t n=0;for(size_t r=0;r<rows_;++r)if(TextStart(r)!=SIZE_MAX)++n;return n<3;}()) {   // session 4: only when nothing was read (the theme's terminal is read now)
        char16_t words[300];NavOut o{words,300,0};words[0]=0;
        o.S(keyChar_==u'e'?(pl?u"GRUB otworzy\x0142 edytor wpisu":u"GRUB opened its entry editor"):(pl?u"GRUB otworzy\x0142 wiersz polece\x0144":u"GRUB opened its command line"));
        o.S(pl?u", ale ten motyw rysuje go czcionk\x0105, kt\x00f3rej Charon nie umie odczyta\x0107. Escape wraca do menu.":u", but this theme draws it in a font Charon cannot read. Escape returns to the menu.");
        size_t at=0;while(at+1<capacity && speech[at])++at;
        if(at)Add(speech,capacity,u"");
        at=0;while(at+1<capacity && speech[at])++at;
        NavOut s{speech,capacity,at};s.Own(words);
        element_=formatting::Element{};element_.kind=formatting::Kind::warning;
        return;
    }
    // r410 (user, answer 6): long output stopped at a page: "there is more" (a sound alone where the profile is terse)
    if(model_.screen==screenmodel::Screen::pager) {
        Raise(formatting::Event::askQuestion);
        if(!Item(formatting::itemScreen))return;
        char16_t words[200];NavOut o{words,200,0};words[0]=0;
        if(speech[0])o.S(u". ");   // session 4: no stray full stop when nothing precedes it
        o.S(pl?u"Jest wi\x0119" u"cej":u"There is more");
        if(Item(formatting::itemHints))o.S(pl?u": program czeka na klawisz, zanim poka\x017c" u"e nast\x0119pn\x0105 stron\x0119":u": the program waits for a key before it shows the next page");
        size_t at=0;while(at+1<capacity && speech[at])++at;
        NavOut s{speech,capacity,at};s.Own(words);
#ifdef HAK_CHARON_TRACE
        charontrace::Value("CHARON paged output announced",1);
#endif
        return;
    }
    const bool editor=model_.screen==screenmodel::Screen::editor,command=model_.screen==screenmodel::Screen::commandLine && model_.edit.found && model_.edit.sure;
    if(!editor && !command)return;
    element_.kind=formatting::Kind::edit;introKind_=editor?2:3;introImage_=currentImage_;
    if(!Item(formatting::itemScreen))return;
    char16_t words[200];NavOut o{words,200,0};words[0]=0;
    o.S(editor?(pl?u"Edytor wpisu":u"Entry editor"):(pl?u"Wiersz polece\x0144":u"Command line"));
    if(Item(formatting::itemHints))o.S(pl?u". Insert F1 podaje klawisze tego ekranu, Escape zwykle wraca":u". Insert F1 lists the keys of this screen, Escape usually goes back");
    size_t at=0;while(at+1<capacity && speech[at])++at;
    if(at)Add(speech,capacity,u"");
    at=0;while(at+1<capacity && speech[at])++at;
    NavOut s{speech,capacity,at};s.Own(words);
}
// A menu that counts down untouched is not read out (r400); with a sure model one short sentence says what
// will start and when. Returns true when speech was written.
bool ModelCountdownIntro(char16_t* speech,size_t capacity) {
    BuildModel();
    const auto& list=model_.list;
    if(!list.found || !list.sure || list.selected==screenmodel::none8 || !model_.countdown.found || !model_.countdown.sure || !CountingNow())return false;
    const auto& it=list.items[list.selected];if(it.row>=rows_)return false;
    char16_t name[maxColumns+1];if(!CellRange(it.row,it.left,it.right,name,maxColumns+1))return false;
    size_t n=0;while(name[n])++n;
    const uint32_t key=Hash16(name,n)^uint32_t(uintptr_t(currentImage_));
    if(key==countdownSaidFor_)return false;
    countdownSaidFor_=key;
    const bool pl=options_.polish;
    speech[0]=0;NavOut s{speech,capacity,0};
    if(Item(formatting::itemCountdown)) {s.S(ProgramName());s.Own(u": ");}
    s.S(name);
    char16_t words[220];NavOut o{words,220,0};words[0]=0;
    if(Item(formatting::itemCountdown)) {o.S(pl?u", domy\x015blna, start za ":u", default, starts in ");o.N(model_.countdown.seconds);o.S(SecondsWord(model_.countdown.seconds,pl));}
    if(Item(formatting::itemHints))o.S(pl?u". Dowolny klawisz zatrzymuje odliczanie i czyta menu":u". Any key stops the countdown and reads the menu");
    if(words[0])s.Own(words);
    element_=formatting::Element{};element_.kind=formatting::Kind::countdown;
    return true;
}
// ---- where am I, help line ----------------------------------------------
// r410 (user, answer 7, the part that needs no file of the program): what the NAME of an entry says about a
// risk. 0 none, 1 recovery or rescue mode, 2 firmware setup, 3 memory test, 4 erasing. Only from words that
// stand in the entry's own text; said on demand (where am I) with the warning signal, never as a fact about
// what the entry really starts.
static uint8_t RiskOfName(const char16_t* name) {
    static const char16_t* const erase[]={u"erase",u"wipe",u"secure erase",u"dban",u"kasowanie",u"wymaz"};
    static const char16_t* const memory[]={u"memtest",u"memory test",u"memory diagnostic",(u"test pami\x0119" u"ci")};
    static const char16_t* const firmware[]={u"firmware",u"uefi setup",u"bios setup",(u"oprogramowania uk\x0142" u"adowego")};
    static const char16_t* const recovery[]={u"recovery",u"rescue",u"single user",u"tryb awaryjny",u"odzyskiwani",u"ratunkow"};
    auto any=[&](const char16_t* const* list,size_t count) {for(size_t i=0;i<count;++i) {size_t n=0;while(list[i][n])++n;if(Contains(name,list[i],n))return true;}return false;};
    if(any(erase,6))return 4;
    if(any(memory,4))return 3;
    if(any(firmware,4))return 2;
    if(any(recovery,6))return 1;
    return 0;
}
// Writes what the model knows; returns true when the selected entry was said.
bool ModelWhere(NavOut& o,bool detailed) {
    BuildModel();
    const bool pl=options_.polish;bool said=false;
    static const char16_t* kindsPl[]={u"",u"Menu",u"Edytor wpisu",u"Wiersz polece\x0144",u"Pomoc",(u"Ekran b\x0142\x0119" u"du"),u"Komunikat",u"Post\x0119p",u"Opcje",u"Stronicowanie"};
    static const char16_t* kindsEn[]={u"",u"Menu",u"Entry editor",u"Command line",u"Help",u"Error screen",u"Message",u"Progress",u"Options",u"Paged output"};
    const size_t k=size_t(model_.screen);
    if(k && k<10 && !screenName_) {o.S((pl?kindsPl:kindsEn)[k]);o.S(u". ");}
    const auto& list=model_.list;
    if(list.found && list.sure && list.selected!=screenmodel::none8) {
        const auto& it=list.items[list.selected];
        o.S(pl?u"Pozycja ":u"Entry ");o.N(size_t(list.selected)+1);o.S(pl?u" z ":u" of ");o.N(list.count);
        if(list.moreAbove || list.moreBelow)o.S(pl?u" widocznych":u" shown");
        uint8_t risk=0;
        if(it.row<rows_) {char16_t name[maxColumns+1];if(CellRange(it.row,it.left,it.right,name,maxColumns+1)) {o.S(u": ");o.S(name);risk=RiskOfName(name);}}
        if(risk) {
            static const char16_t* const en[]={u"",u". Warning, by its name: recovery or rescue mode, not the normal system",u". Warning, by its name: it restarts the computer into the firmware setup",
                u". Warning, by its name: a memory test; it takes the machine over and runs for a long time",u". Warning, by its name: it erases data"};
            static const char16_t* const plw[]={u"",u". Uwaga, wed\x0142ug nazwy: tryb awaryjny albo ratunkowy, nie zwyk\x0142y system",u". Uwaga, wed\x0142ug nazwy: restart komputera do ustawie\x0144 firmware",
                (u". Uwaga, wed\x0142ug nazwy: test pami\x0119" u"ci; przejmuje komputer i trwa d\x0142ugo"),u". Uwaga, wed\x0142ug nazwy: kasuje dane"};
            o.S((pl?plw:en)[risk]);
            Raise(formatting::Event::warnUser);
#ifdef HAK_CHARON_TRACE
            charontrace::Value("CHARON entry risk by name",risk);
#endif
        }
        if(it.flags&screenmodel::itemSubmenu)o.S(pl?u", podmenu":u", submenu");
        if(it.flags&screenmodel::itemDefault)o.S(pl?u", domy\x015blna":u", default");
        if(it.flags&screenmodel::itemDisabled)o.S(pl?u", niedost\x0119pne":u", unavailable");
        if(list.depth)o.S(pl?u", w podmenu":u", inside a submenu");
        o.S(u". ");said=true;
    }
    if(model_.countdown.found && model_.countdown.sure && CountingNow()) {o.S(pl?u"Start za ":u"Starts in ");o.N(model_.countdown.seconds);o.S(SecondsWord(model_.countdown.seconds,pl));o.S(u". ");}
    if(model_.edit.found && model_.edit.sure && !model_.edit.multiline) {
        char16_t content[maxColumns+1];CellRange(model_.edit.row,model_.edit.left,model_.edit.right,content,maxColumns+1);
        o.S(pl?u"Pole edycji":u"Edit field");
        if(content[0] && !SecretScreen()) {o.S(u": ");o.S(content);} else if(!content[0])o.S(pl?u", puste":u", empty");
        o.S(pl?u", kursor w kolumnie ":u", caret at column ");o.N(size_t(model_.edit.caretCol)-model_.edit.left+1);o.S(u". ");
    } else if(model_.edit.found && model_.edit.sure) {
        o.S(pl?u"Pole edycji, wiersz ":u"Edit field, line ");o.N(size_t(model_.edit.caretRow)-model_.edit.top+1);o.S(pl?u", kolumna ":u", column ");o.N(size_t(model_.edit.caretCol)-model_.edit.left+1);o.S(u". ");
    }
    if(model_.progress.found && model_.progress.sure) {o.S(pl?u"Post\x0119p ":u"Progress ");o.N(model_.progress.percent);o.S(pl?u" procent. ":u" percent. ");}
    if(model_.message.found && model_.message.sure)o.S(model_.message.kind==screenmodel::Alert::error?(pl?u"Na ekranie jest b\x0142\x0105" u"d. ":u"The screen shows an error. "):(pl?u"Na ekranie jest ostrze\x017c" u"enie. ":u"The screen shows a warning. "));
    if(detailed) {
        static const char16_t* profPl[]={u"zwyk\x0142y",(u"dla pocz\x0105tkuj\x0105" u"cych"),u"dla zaawansowanych"};static const char16_t* profEn[]={u"normal",u"beginner",u"expert"};
        if(model_.helpCount && model_.helpSure) {o.S(pl?u"Pomoc ekranu: ":u"Help on the screen: ");
            for(size_t i=0;i<model_.helpCount;++i) {char16_t line[maxColumns+1];if(RowText(model_.help[i],line,maxColumns+1)) {o.S(line);o.C(u' ');}}
            o.S(u". ");}
        o.S(pl?u"Profil m\x00f3wienia: ":u"Verbosity profile: ");o.S((pl?profPl:profEn)[Profile()]);o.S(u". ");
    }
    return said;
}
// The help lines of the screen, when the model is sure of them. False: the caller reads the bottom rows as before.
bool ModelHelp() {
    BuildModel();
    if(!model_.helpCount || !model_.helpSure)return false;
    char16_t text[maxColumns*6]={};size_t at=0;
    for(size_t i=0;i<model_.helpCount;++i) {
        char16_t line[maxColumns+1];if(!RowText(model_.help[i],line,maxColumns+1))continue;
        if(at && at+2<maxColumns*6)text[at++]=u' ';
        for(size_t k=0;line[k] && at+1<maxColumns*6;++k)text[at++]=line[k];
    }
    text[at]=0;if(!text[0])return false;
    element_=formatting::Element{};element_.kind=formatting::Kind::help;
    Speak(text);return true;
}
// ---- verbosity profile (Ctrl+Alt+V / Insert V) --------------------------
void CycleProfile() {
    const bool pl=options_.polish;
    static const uint8_t next[4]={2,3,1,0};   // normal -> expert -> beginner -> custom -> normal
    options_.profile=next[Profile()];
    static const char16_t* namesPl[]={u"Profil m\x00f3wienia: zwyk\x0142y.",(u"Profil m\x00f3wienia: dla pocz\x0105tkuj\x0105" u"cych. Pe\x0142ne opisy i podpowiedzi klawiszy."),
        u"Profil m\x00f3wienia: dla zaawansowanych. Kr\x00f3tko, d\x017awi\x0119ki zamiast s\x0142\x00f3w.",
        u"Profil m\x00f3wienia: w\x0142\x0061sny. Pozycje wybierasz w ustawieniach czytnika."};
    static const char16_t* namesEn[]={u"Verbosity profile: normal.",u"Verbosity profile: beginner. Full descriptions and key hints.",u"Verbosity profile: expert. Short, sounds instead of words.",u"Verbosity profile: custom. Its items are chosen in the reader settings."};
    SayOwn((pl?namesPl:namesEn)[Profile()]);
}
// ---- elements -------------------------------------------------------------
void BuildElements(bool withKeys=false) {
    BuildModel();navCount_=0;
    auto add=[&](uint8_t kind,size_t row,size_t col,size_t index) {if(navCount_<maxNav)nav_[navCount_++]={kind,uint8_t(row),uint8_t(col),uint8_t(index)};};
    const auto& m=model_;
    if(m.titleRow!=screenmodel::none8 && m.titleSure)add(navTitle,m.titleRow,TextStart(m.titleRow)!=SIZE_MAX?TextStart(m.titleRow):0,0);
    for(size_t i=0;i<m.windowCount;++i)add(navWindow,m.windows[i].top,m.windows[i].left,i);
    if(m.list.found && m.list.sure)for(size_t i=0;i<m.list.count;++i)if(m.list.items[i].row<rows_)add(navItem,m.list.items[i].row,m.list.items[i].left,i);
    if(m.countdown.found && m.countdown.sure)add(navCountdown,m.countdown.row,TextStart(m.countdown.row)!=SIZE_MAX?TextStart(m.countdown.row):0,0);
    if(m.helpSure)for(size_t i=0;i<m.helpCount;++i) {
        if(i && m.help[i]==m.help[i-1]+1)continue;   // a wrapped hint is one element
        add(navHelp,m.help[i],TextStart(m.help[i])!=SIZE_MAX?TextStart(m.help[i]):0,i);}
    if(m.edit.found && m.edit.sure)add(navEdit,m.edit.caretRow,m.edit.left,0);
    if(m.message.found && m.message.sure)add(navMessage,m.message.top,TextStart(m.message.top)!=SIZE_MAX?TextStart(m.message.top):0,0);
    if(m.progress.found && m.progress.sure)add(navProgress,m.progress.row,TextStart(m.progress.row)!=SIZE_MAX?TextStart(m.progress.row):0,0);
    // Rows no element covers are plain lines: nothing on the screen is left out of the list.
    for(size_t r=0;r<rows_;++r) {
        if(!RowHasText(r) || ArtRow(r))continue;
        bool covered=false;
        for(size_t i=0;i<navCount_ && !covered;++i) {const auto& e=nav_[i];
            covered=e.row==r && e.kind!=navWindow;
            if(e.kind==navHelp)for(size_t h=e.index;h<m.helpCount && (h==e.index || m.help[h]==m.help[h-1]+1);++h)if(m.help[h]==r)covered=true;
            if(e.kind==navMessage && r>=m.message.top && r<=m.message.bottom)covered=true;}
        if(m.windowCount)for(size_t i=0;i<m.windowCount;++i)if(m.windows[i].top==r && m.windows[i].titleRight<=m.windows[i].titleLeft)covered=true;
        if(!covered)add(navLine,r,TextStart(r),0);
    }
    // reading order
    for(size_t i=1;i<navCount_;++i) {const NavElement e=nav_[i];size_t k=i;
        while(k && (nav_[k-1].row>e.row || (nav_[k-1].row==e.row && nav_[k-1].col>e.col))) {nav_[k]=nav_[k-1];--k;}
        nav_[k]=e;}
    // r410 (user, answer 4): the program's own keys for this kind of screen, as a group at the end of the list
    // (CharonManagerKeys.h: only keys its documentation names). Enter on one sends it to the program.
    if(withKeys)for(size_t i=0;i<managerkeys::keyCount;++i)if(managerkeys::Fits(i,m.manager,m.screen))add(navKey,navKeyRow,0,i);
}
void KeyLabel(size_t i,NavOut& o) {
    const bool pl=options_.polish;const auto& k=managerkeys::keys[i];
    char16_t w[200];NavOut t{w,200,0};w[0]=0;
    t.S(pl?u"Klawisz ":u"Key ");t.S(k.name);t.S(u": ");t.S(pl?k.pl:k.en);
    o.Own(w);
}
// The spoken key list of the program on the screen (Insert+apostrophe, Ctrl+Alt+7).
void SayProgramKeys() {
    const bool pl=options_.polish;
    BuildModel();
    char16_t text[1600];NavOut o{text,1600,0};text[0]=0;size_t n=0;
    char16_t head[160];NavOut h{head,160,0};head[0]=0;
    h.S(pl?u"Klawisze programu ":u"Keys of ");h.S(ProgramName());h.S(u": ");
    for(size_t i=0;i<managerkeys::keyCount;++i)if(managerkeys::Fits(i,model_.manager,model_.screen)) {
        const auto& k=managerkeys::keys[i];
        if(!n)h.S(u"");
        char16_t w[220];NavOut t{w,220,0};w[0]=0;
        t.S(k.name);t.S(u": ");t.S(pl?k.pl:k.en);t.S(u". ");
        if(!n)o.Own(head);
        o.Own(w);++n;
    }
    if(!n) {SayOwn(pl?u"Charon nie ma listy klawiszy tego programu. Insert End czyta wiersz pomocy z ekranu.":u"Charon has no key list for this program. Insert End reads the help line of the screen.");return;}
    element_=formatting::Element{};element_.kind=formatting::Kind::help;
    Speak(text);
}
// r410 (user, answer 6): explains the known words of the line with the caret (an editor, a command line) or,
// without one, of the line under the review cursor. Only words of CharonLoaderWords.h; the others are counted.
void DescribeLine() {
    const bool pl=options_.polish;
    BuildModel();ReviewStart();
    size_t row=reviewRow_;
    if(model_.edit.found && model_.edit.sure && model_.edit.caretRow<rows_)row=model_.edit.caretRow;
    char16_t line[maxColumns+1]={};
    if(row>=rows_ || !RowText(row,line,maxColumns+1) || SecretScreen()) {SayOwn(pl?u"W tym wierszu nie ma tekstu.":u"There is no text on this line.");return;}
    char16_t text[1600];NavOut o{text,1600,0};text[0]=0;size_t known=0,unknown=0;
    for(size_t i=0;line[i];) {
        while(line[i]==u' ')++i;
        size_t s=i;while(line[i] && line[i]!=u' ')++i;
        size_t n=i-s;
        // quotes and a prompt's bracket around a word are not a part of it
        while(n && (line[s]==u'"' || line[s]==u'\'' || line[s]==u'`'))++s,--n;
        while(n && (line[s+n-1]==u'"' || line[s+n-1]==u'\'' || line[s+n-1]==u';'))--n;
        if(!n)continue;
        size_t w=loaderwords::Find(line+s,n);
        // session 4: a GRUB command or variable is described only in GRUB, a Limine key only in Limine ("probe" in
        // systemd-boot's entry title "Charon probe loader" was explained as GRUB's command)
        if(w!=loaderwords::wordCount) {
            const char16_t* en=loaderwords::words[w].en;
            const bool grubWord=en[0]==u'G' && en[1]==u'R' && en[2]==u'U' && en[3]==u'B',limineWord=en[0]==u'L' && en[1]==u'i' && en[2]==u'm' && en[3]==u'i';
            if((grubWord && model_.manager!=screenmodel::Manager::grub && !GrubFamily(ProgramName()))
                || (limineWord && model_.manager!=screenmodel::Manager::limine && !managers::SameName(ProgramName(),u"Limine")))w=loaderwords::wordCount;
        }
        if(w==loaderwords::wordCount) {bool letter=false;for(size_t k=0;k<n && !letter;++k)letter=(line[s+k]>=u'a' && line[s+k]<=u'z') || (line[s+k]>=u'A' && line[s+k]<=u'Z');if(letter)++unknown;continue;}
        bool again=false;   // a word explained once on this line is not explained twice
        for(size_t q=0;q+n<=s && !again;++q) {size_t k=0;while(k<n && line[q+k]==line[s+k])++k;again=k==n && (q==0 || line[q-1]==u' ') && (line[q+n]==u' ') && q+n<=s;}
        if(again)continue;
        for(size_t k=0;k<n;++k)o.C(line[s+k]);
        char16_t e[260];NavOut x{e,260,0};e[0]=0;x.S(u": ");x.S(pl?loaderwords::words[w].pl:loaderwords::words[w].en);x.S(u". ");
        o.Own(e);++known;
    }
    if(!known) {SayOwn(pl?u"Charon nie ma opisu \x017c" u"adnego s\x0142owa z tego wiersza.":u"Charon has no description of any word on this line.");return;}
    if(unknown) {char16_t e[120];NavOut x{e,120,0};e[0]=0;x.S(pl?u"Innych s\x0142\x00f3w bez opisu: ":u"Other words without a description: ");x.N(unknown);x.S(u".");o.Own(e);}
    element_=formatting::Element{};element_.kind=formatting::Kind::help;
    Speak(text);
}
void ElementLabel(const NavElement& e,NavOut& o,bool position) {
    const bool pl=options_.polish;const auto& m=model_;
    char16_t line[maxColumns+1]={};
    switch(e.kind) {
        case navTitle:o.Own(pl?u"Tytu\x0142: ":u"Title: ");RowText(e.row,line,maxColumns+1);o.S(line);break;
        case navWindow: {const auto& w=m.windows[e.index<m.windowCount?e.index:0];o.Own(pl?u"Okno":u"Window");
            if(w.titleRight>w.titleLeft && CellRange(w.top,w.titleLeft,w.titleRight,line,maxColumns+1)) {o.Own(u": ");o.S(line);}break;}
        case navItem: {const auto& it=m.list.items[e.index<m.list.count?e.index:0];CellRange(it.row,it.left,it.right,line,maxColumns+1);o.S(line);
            char16_t w[80];NavOut k{w,80,0};w[0]=0;
            if(it.flags&screenmodel::itemSelected)k.S(pl?u", wybrana":u", selected");
            if(it.flags&screenmodel::itemSubmenu)k.S(pl?u", podmenu":u", submenu");
            if(it.flags&screenmodel::itemDefault)k.S(pl?u", domy\x015blna":u", default");
            if(it.flags&screenmodel::itemDisabled)k.S(pl?u", niedost\x0119pne":u", unavailable");
            if(position && options_.positions) {k.S(u", ");k.N(size_t(e.index)+1);k.S(pl?u" z ":u" of ");k.N(m.list.count);}
            if(w[0])o.Own(w);break;}
        case navCountdown:o.Own(pl?u"Odliczanie: ":u"Countdown: ");RowText(e.row,line,maxColumns+1);o.S(line);break;
        case navHelp:o.Own(pl?u"Pomoc: ":u"Help: ");
            for(size_t h=e.index;h<m.helpCount && (h==e.index || m.help[h]==m.help[h-1]+1);++h) {if(RowText(m.help[h],line,maxColumns+1)) {o.S(line);o.C(u' ');}}break;
        case navEdit:o.Own(pl?u"Pole edycji: ":u"Edit field: ");
            if(SecretScreen())o.Own(pl?u"has\x0142o":u"password");
            else if(m.edit.multiline)RowText(e.row,line,maxColumns+1);else CellRange(m.edit.row,m.edit.left,m.edit.right,line,maxColumns+1);
            o.S(line);break;
        case navMessage:o.Own(m.message.kind==screenmodel::Alert::error?(pl?u"B\x0142\x0105" u"d: ":u"Error: "):(pl?u"Ostrze\x017c" u"enie: ":u"Warning: "));RowText(e.row,line,maxColumns+1);o.S(line);break;
        case navProgress:o.Own(pl?u"Post\x0119p: ":u"Progress: ");RowText(e.row,line,maxColumns+1);o.S(line);break;
        case navKey:KeyLabel(e.index,o);break;
        default:RowText(e.row,line,maxColumns+1);o.S(line);break;
    }
}
void SetElementKind(const NavElement& e) {
    element_=formatting::Element{};
    switch(e.kind) {
        case navTitle:element_.kind=formatting::Kind::title;break;
        case navHelp:case navKey:element_.kind=formatting::Kind::help;break;
        case navEdit:element_.kind=formatting::Kind::edit;break;
        case navCountdown:element_.kind=formatting::Kind::countdown;break;
        case navMessage:element_.kind=model_.message.kind==screenmodel::Alert::error?formatting::Kind::error:formatting::Kind::warning;break;
        case navItem: {const auto& it=model_.list.items[e.index<model_.list.count?e.index:0];
            element_.kind=(it.flags&screenmodel::itemDefault)?formatting::Kind::defaultItem:(it.flags&screenmodel::itemSubmenu)?formatting::Kind::submenu:formatting::Kind::selected;
            element_.index=e.index;element_.count=model_.list.count;element_.first=e.index==0;element_.last=size_t(e.index)+1==model_.list.count;element_.indent=uint8_t(it.indent+model_.list.depth);break;}
        default:break;
    }
}
void SayElement(const NavElement& e,const char16_t* headOwn=nullptr,size_t number=0,size_t of=0) {
    char16_t text[maxColumns*3+200];NavOut o{text,sizeof(text)/2,0};text[0]=0;
    if(headOwn)o.Own(headOwn);
    ElementLabel(e,o,true);
    if(of && options_.positions) {char16_t w[48];NavOut k{w,48,0};w[0]=0;k.S(options_.polish?u"; element ":u"; element ");k.N(number);k.S(options_.polish?u" z ":u" of ");k.N(of);o.Own(w);}
    SetElementKind(e);
    if(e.kind!=navKey) {reviewRow_=e.row;reviewCol_=e.col;}
    Speak(text);
}
// Previous / next element from the review cursor (NVDA's flattened object navigation, NVDA+Shift+[ and ]).
void StepElement(bool forward) {
    const bool pl=options_.polish;
    BuildElements();
    if(!navCount_) {SayOwn(pl?u"Brak tekstu na ekranie, kt\x00f3ry Charon mo\x017c" u"e odczyta\x0107.":u"No text on the screen Charon can read.");return;}
    ReviewStart();
    const size_t r=reviewRow_<rows_?reviewRow_:0,c=reviewCol_;
    size_t at=SIZE_MAX;
    if(forward) {for(size_t i=0;i<navCount_;++i)if(nav_[i].row>r || (nav_[i].row==r && nav_[i].col>c)) {at=i;break;}}
    else for(size_t i=navCount_;i-->0;)if(nav_[i].row<r || (nav_[i].row==r && nav_[i].col<c)) {at=i;break;}
    if(at==SIZE_MAX) {SayOwn(forward?(pl?u"Ostatni element":u"Last element"):(pl?u"Pierwszy element":u"First element"));return;}
    SayElement(nav_[at]);
}
static char16_t Fold(char16_t ch) {
    if(ch>=u'A' && ch<=u'Z')return char16_t(ch+32);
    switch(ch) {case 0x0104:return 0x0105;case 0x0106:return 0x0107;case 0x0118:return 0x0119;case 0x0141:return 0x0142;case 0x0143:return 0x0144;case 0x00d3:return 0x00f3;case 0x015a:return 0x015b;case 0x0179:return 0x017a;case 0x017b:return 0x017c;default:return ch;}
}
static bool Contains(const char16_t* text,const char16_t* part,size_t n) {
    if(!n)return true;
    for(size_t i=0;text[i];++i) {size_t k=0;while(k<n && text[i+k] && Fold(text[i+k])==Fold(part[k]))++k;if(k==n)return true;}
    return false;
}
void FilterElements() {
    navShownCount_=0;
    for(size_t i=0;i<navCount_;++i) {
        char16_t text[maxColumns*3+200];NavOut o{text,sizeof(text)/2,0};text[0]=0;ElementLabel(nav_[i],o,false);
        // the label without the markers of the reader's own words
        size_t k=0;for(size_t j=0;text[j];++j)if(text[j]!=speechtext::ownOpen && text[j]!=speechtext::ownClose)text[k++]=text[j];text[k]=0;
        if(Contains(text,navFilter_,navFilterLength_))navShown_[navShownCount_++]=uint8_t(i);
    }
    if(navIndex_>=navShownCount_)navIndex_=navShownCount_?navShownCount_-1:0;
}
void SayListEntry(const char16_t* headOwn=nullptr) {
    if(!navShownCount_) {SayOwn(options_.polish?u"\x017b" u"aden element nie pasuje. Backspace usuwa litery filtru.":u"No element matches. Backspace removes filter letters.");return;}
    SayElement(nav_[navShown_[navIndex_]],headOwn,navIndex_+1,navShownCount_);
}
void OpenElements(bool open) {
    const bool pl=options_.polish;insertArmed_=false;
    if(!open) {navMode_=0;SayOwn(pl?u"Lista element\x00f3w zamkni\x0119ta.":u"Elements list closed.");return;}
    BuildElements(true);
    if(!navCount_) {SayOwn(pl?u"Brak tekstu na ekranie, kt\x00f3ry Charon mo\x017c" u"e odczyta\x0107.":u"No text on the screen Charon can read.");return;}
    navFilterLength_=0;navFilter_[0]=0;navIndex_=0;FilterElements();
    // start on the selected item, when there is one
    for(size_t i=0;i<navShownCount_;++i) {const auto& e=nav_[navShown_[i]];if(e.kind==navItem && e.index==model_.list.selected) {navIndex_=i;break;}}
    navMode_=1;navHead_=navTail_=0;
    char16_t head[260];NavOut o{head,260,0};head[0]=0;
    o.S(pl?u"Lista element\x00f3w, rozruch wstrzymany, ":u"Elements list, boot paused, ");o.N(navCount_);
    o.S(Terse()?u". ":(pl?u". Strza\x0142ki przechodz\x0105 po li\x015b" u"cie, pisanie filtruje, Enter przenosi tam, Tab tylko kursor przegl\x0105" u"du, Escape zamyka. "
                          :u". Arrows walk the list, typing filters, Enter goes there, Tab moves only the review cursor, Escape closes. "));
    SayListEntry(head);
}
// May the program's own selection be moved to list item `target` by arrow keys? Only in a sure vertical list of
// a program whose arrows move one entry per key and never start anything.
bool CanMoveSelection(size_t target) const {
    const auto& list=model_.list;
    if(!list.found || !list.sure || list.selected==screenmodel::none8 || target>=list.count || list.items[target].row>=rows_ || SecretScreen())return false;
    for(size_t i=0;i<list.count;++i)if(list.items[i].row>=rows_ || (list.items[i].flags&screenmodel::itemDisabled))return false;   // icons, or entries the arrows skip
    switch(model_.manager) {
        case screenmodel::Manager::grub:case screenmodel::Manager::systemdBoot:case screenmodel::Manager::limine:case screenmodel::Manager::openCore:
        case screenmodel::Manager::windows:case screenmodel::Manager::syslinux:case screenmodel::Manager::ipxe:return !options_.reviewOnly;
        default:return false;   // unknown programs: the review cursor only (an arrow may mean something else there)
    }
}
void ActivateElement(bool reviewOnly) {
    const bool pl=options_.polish;
    if(!navShownCount_)return;
    const NavElement e=nav_[navShown_[navIndex_]];
    if(e.kind==navKey) {
        const auto& k=managerkeys::keys[e.index];
        navMode_=0;
        if(reviewOnly || !managerkeys::Sendable(e.index) || options_.reviewOnly || SecretScreen()) {
            char16_t text[260];NavOut o{text,260,0};text[0]=0;
            o.S(pl?u"Lista zamkni\x0119ta. Tego klawisza Charon nie wysy\x0142" u"a z listy; naci\x015bnij sam: ":u"List closed. Charon does not send this key from the list; press it yourself: ");o.S(k.name);
            SayOwn(text);return;
        }
        char16_t text[120];NavOut o{text,120,0};text[0]=0;
        o.S(pl?u"Wysy\x0142" u"am klawisz ":u"Sending key ");o.S(k.name);
        if(!Terse())SayOwn(text);
#ifdef HAK_CHARON_TRACE
        charontrace::Value("CHARON nav send program key",(uint64_t(k.scan)<<16)|k.ch);
#endif
        sendCount_=sendAt_=0;Arm(k.scan,k.ch);return;
    }
    navMode_=0;reviewRow_=e.row;reviewCol_=e.col;
    if(!reviewOnly && e.kind==navItem && e.index!=model_.list.selected && CanMoveSelection(e.index)) {
        const size_t from=model_.list.selected,to=e.index;const bool down=to>from;const size_t n=down?to-from:from-to;
        sendCount_=0;sendAt_=0;for(size_t i=0;i<n && sendCount_<48;++i)sendScan_[sendCount_++]=down?2:1;
        char16_t text[120];NavOut o{text,120,0};text[0]=0;
        o.S(pl?u"Przesuwam wyb\x00f3r, ":u"Moving the selection, ");o.N(n);o.S(down?(pl?u" razy w d\x00f3\x0142.":u" times down."):(pl?u" razy w g\x00f3r\x0119.":u" times up."));
        if(!Terse())SayOwn(text);
#ifdef HAK_CHARON_TRACE
        charontrace::Value("CHARON nav send keys",(uint64_t(down)<<32)|n);
#endif
        Arm(sendScan_[0],0);return;
    }
    char16_t head[80];NavOut o{head,80,0};head[0]=0;o.S(pl?u"Kursor przegl\x0105" u"du: ":u"Review cursor: ");
    SayElement(e,Terse()?nullptr:head);
}
// Keys queued for the program by ActivateElement: one per key read.
bool NavInjected(uint16_t& scan,char16_t& ch) {
    if(!sendCount_ || sendAt_>=sendCount_ || stopped_ || suspended_ || navMode_) return false;
    scan=sendScan_[sendAt_++];ch=0;revealArmed_=false;
    if(sendAt_<sendCount_)Arm(sendScan_[sendAt_],0);else sendCount_=sendAt_=0;
    return true;
}
// ---- find on the screen ---------------------------------------------------
bool FindFrom(size_t row,size_t col,bool forward,size_t& fr,size_t& fc) const {
    if(!findLength_ || !rows_ || !columns_)return false;
    const size_t cells=rows_*columns_;size_t start=row*columns_+col;
    for(size_t step=1;step<=cells;++step) {
        const size_t p=forward?(start+step)%cells:(start+cells-step%cells)%cells;
        const size_t r=p/columns_,c=p%columns_;
        if(c+findLength_>columns_)continue;
        size_t k=0;while(k<findLength_ && Fold(text_[r][c+k]?text_[r][c+k]:u' ')==Fold(findText_[k]))++k;
        if(k==findLength_) {fr=r;fc=c;return true;}
    }
    return false;
}
void FindStep(bool forward) {
    const bool pl=options_.polish;
    if(!findLength_) {OpenFind();return;}
    ReviewStart();
    const size_t r=reviewRow_<rows_?reviewRow_:0,c=reviewCol_<columns_?reviewCol_:0;
    size_t fr=0,fc=0;
    char16_t text[maxColumns+160];NavOut o{text,sizeof(text)/2,0};text[0]=0;
    if(!FindFrom(r,c,forward,fr,fc)) {
        // the only place may be the one under the cursor
        size_t k=0;while(k<findLength_ && c+k<columns_ && Fold(text_[r][c+k])==Fold(findText_[k]))++k;
        o.S(k==findLength_?(pl?u"Nie ma innego miejsca z tekstem: ":u"No other place with: "):(pl?u"Nie znaleziono: ":u"Not found: "));o.S(findText_);
        Raise(formatting::Event::warnUser);SayOwn(text);return;
    }
    const bool wrapped=forward?(fr<r || (fr==r && fc<=c)):(fr>r || (fr==r && fc>=c));
    reviewRow_=fr;reviewCol_=fc;
    char16_t head[100];NavOut h{head,100,0};head[0]=0;
    if(wrapped)h.S(forward?(pl?u"Od g\x00f3ry ekranu. ":u"Wrapped to the top. "):(pl?u"Od do\x0142u ekranu. ":u"Wrapped to the bottom. "));
    if(!Terse()) {h.S(pl?u"Wiersz ":u"Line ");h.N(fr+1);h.S(pl?u", kolumna ":u", column ");h.N(fc+1);h.S(u": ");}
    if(head[0])o.Own(head);
    char16_t line[maxColumns+1];RowText(fr,line,maxColumns+1);o.S(line);
    Raise(formatting::Event::selectObject);
    Speak(text);
}
void OpenFind() {
    const bool pl=options_.polish;insertArmed_=false;
    navMode_=2;navHead_=navTail_=0;findLength_=0;findText_[0]=0;
    SayOwn(Terse()?(pl?u"Szukaj":u"Find")
        :(pl?u"Szukaj na ekranie, rozruch wstrzymany. Wpisz tekst i naci\x015bnij Enter; Escape anuluje.":u"Find on the screen, boot paused. Type the text and press Enter; Escape cancels."));
}
// ---- bookmarks per screen -------------------------------------------------
uint32_t ScreenSignature() const {
    uint32_t h=2166136261u;
    const char16_t* name=ProgramName();for(size_t i=0;name[i];++i) {h^=name[i];h*=16777619u;}
    // the screen's first text row (its title) tells the screens of one program apart; digits apart (clocks, counters)
    for(size_t r=0;r<rows_;++r)if(RowHasText(r)) {for(size_t c=0;c<columns_;++c) {const char16_t ch=text_[r][c];if(ch && ch!=u' ' && !(ch>=u'0' && ch<=u'9')) {h^=ch;h*=16777619u;}}break;}
    return h?h:1;
}
uint32_t RowHash(size_t r) const {char16_t line[maxColumns+1];const size_t n=RowText(r,line,maxColumns+1);return Hash16(line,n);}
void ToggleBookmark() {
    const bool pl=options_.polish;ReviewStart();
    if(reviewRow_>=rows_) {SayOwn(pl?u"Brak tekstu na ekranie, kt\x00f3ry Charon mo\x017c" u"e odczyta\x0107.":u"No text on the screen Charon can read.");return;}
    const uint32_t screen=ScreenSignature();
    for(auto& b:marks_)if(b.used && b.screen==screen && b.row==reviewRow_) {b.used=false;SayOwn(pl?u"Zak\x0142" u"adka usuni\x0119ta.":u"Bookmark removed.");return;}
    size_t slot=SIZE_MAX;for(size_t i=0;i<8;++i)if(!marks_[i].used) {slot=i;break;}
    if(slot==SIZE_MAX) {slot=markNext_;markNext_=(markNext_+1)%8;}
    marks_[slot]={screen,RowHash(reviewRow_),uint8_t(reviewRow_),uint8_t(reviewCol_<columns_?reviewCol_:0),true};
    char16_t text[maxColumns+80];NavOut o{text,sizeof(text)/2,0};text[0]=0;
    o.Own(pl?u"Zak\x0142" u"adka: ":u"Bookmark set: ");char16_t line[maxColumns+1];RowText(reviewRow_,line,maxColumns+1);o.S(line);
    Speak(text);
}
void StepBookmark(bool forward) {
    const bool pl=options_.polish;ReviewStart();
    const uint32_t screen=ScreenSignature();
    size_t list[8],n=0;
    for(size_t i=0;i<8;++i)if(marks_[i].used && marks_[i].screen==screen)list[n++]=i;
    if(!n) {SayOwn(pl?u"Na tym ekranie nie ma zak\x0142" u"adek. Insert F8 ustawia zak\x0142" u"adk\x0119 w miejscu kursora przegl\x0105" u"du.":u"No bookmarks on this screen. Insert F8 sets one at the review cursor.");return;}
    for(size_t i=1;i<n;++i) {const size_t e=list[i];size_t k=i;while(k && marks_[list[k-1]].row>marks_[e].row) {list[k]=list[k-1];--k;}list[k]=e;}
    const size_t r=reviewRow_<rows_?reviewRow_:0;size_t at=SIZE_MAX;
    if(forward) {for(size_t i=0;i<n;++i)if(marks_[list[i]].row>r) {at=i;break;}if(at==SIZE_MAX)at=0;}
    else {for(size_t i=n;i-->0;)if(marks_[list[i]].row<r) {at=i;break;}if(at==SIZE_MAX)at=n-1;}
    Bookmark& b=marks_[list[at]];
    // the marked text may have moved (a list that scrolled): look for the same row text
    size_t row=b.row;bool lost=false;
    if(row>=rows_ || RowHash(row)!=b.rowHash) {lost=true;for(size_t q=0;q<rows_;++q)if(RowHasText(q) && RowHash(q)==b.rowHash) {row=q;lost=false;b.row=uint8_t(q);break;}}
    char16_t text[maxColumns+160];NavOut o{text,sizeof(text)/2,0};text[0]=0;
    char16_t head[120];NavOut h{head,120,0};head[0]=0;
    h.S(pl?u"Zak\x0142" u"adka ":u"Bookmark ");h.N(at+1);h.S(pl?u" z ":u" of ");h.N(n);
    if(lost)h.S(pl?u", zaznaczonego tekstu nie ma ju\x017c na ekranie; wiersz ":u", the marked text is no longer on the screen; line ");
    else h.S(u": ");
    if(lost)h.N(size_t(b.row)+1);
    o.Own(head);
    if(!lost) {char16_t line[maxColumns+1];RowText(row,line,maxColumns+1);o.S(line);reviewRow_=row;reviewCol_=b.col<columns_?b.col:0;}
    Speak(text);
}
// ---- what changed since this screen appeared (Insert D twice) -------------
void SnapshotEntry() {
    for(size_t r=0;r<rows_ && r<maxRows;++r)for(size_t c=0;c<columns_ && c<maxColumns;++c)enter_[r][c]=text_[r][c]?text_[r][c]:u' ';
    enterRows_=rows_;enterColumns_=columns_;haveEnter_=true;
}
void SayChangesSinceEntry() {
    const bool pl=options_.polish;
    if(!haveEnter_) {SayOwn(pl?u"Nie ma jeszcze zapami\x0119tanego ekranu.":u"No screen has been remembered yet.");return;}
    char16_t lines[1400]={};size_t changed=0,emptied=0;
    for(size_t r=0;r<rows_ && r<maxRows;++r) {
        bool diff=false;
        for(size_t c=0;c<columns_ && !diff;++c) {const char16_t a=text_[r][c]?text_[r][c]:u' ',b=(r<enterRows_ && c<enterColumns_)?enter_[r][c]:u' ';diff=a!=b;}
        if(!diff)continue;
        char16_t line[maxColumns+1];
        if(!RowText(r,line,maxColumns+1) || ArtRow(r)) {++emptied;continue;}
        ++changed;if(changed<=12)Add(lines,1400,line);
    }
    if(!changed && !emptied) {SayOwn(pl?u"Bez zmian, odk\x0105" u"d pojawi\x0142 si\x0119 ten ekran.":u"Nothing changed since this screen appeared.");return;}
    char16_t text[1600]={};NavOut o{text,1600,0};
    char16_t head[160];NavOut h{head,160,0};head[0]=0;
    h.S(pl?u"Odk\x0105" u"d pojawi\x0142 si\x0119 ten ekran, zmieni\x0142o si\x0119 ":u"Since this screen appeared, changed: ");h.N(changed+emptied);h.S(RowsWord(changed+emptied,pl));
    if(emptied) {h.S(pl?u", w tym pustych ":u", empty now ");h.N(emptied);}
    h.S(changed?u": ":u".");o.Own(head);o.S(lines);
    Speak(text);
}
// ---- keys of the two modal states ----------------------------------------
void NavKeyIn(const efi_input_key& key) {
    const uint8_t next=uint8_t((navHead_+1)&15);
    if(next==navTail_)return;   // full: the key is dropped, never blocks
    navKeys_[navHead_]={key.ScanCode,key.UnicodeChar};navHead_=next;command_=navKeyCommand;
}
void NavDrain() {
    while(navTail_!=navHead_) {
        const NavKey k=navKeys_[navTail_];navTail_=uint8_t((navTail_+1)&15);
        if(navMode_==1)ElementsKey(k);else if(navMode_==2)FindKey(k);
    }
}
void ElementsKey(const NavKey& k) {
    const bool pl=options_.polish;
    switch(k.scan) {
        case 1:if(navIndex_) {--navIndex_;SayListEntry();} else {Raise(formatting::Event::listFirst);SayOwn(pl?u"Pocz\x0105tek listy":u"Top of the list");}return;
        case 2:if(navIndex_+1<navShownCount_) {++navIndex_;SayListEntry();} else {Raise(formatting::Event::listLast);SayOwn(pl?u"Koniec listy":u"End of the list");}return;
        case 5:case 9:navIndex_=0;SayListEntry();return;
        case 6:case 0x0a:navIndex_=navShownCount_?navShownCount_-1:0;SayListEntry();return;
        case 0x17:case 0x11:OpenElements(false);return;   // Escape, or F7 again
        default:break;
    }
    if(k.scan)return;
    if(k.ch==13) {ActivateElement(false);return;}
    if(k.ch==9) {ActivateElement(true);return;}
    if(k.ch==8) {
        if(!navFilterLength_) {SayOwn(pl?u"Filtr jest pusty":u"The filter is empty");return;}
        navFilter_[--navFilterLength_]=0;
    } else if(k.ch>=32 && navFilterLength_<31) {navFilter_[navFilterLength_++]=k.ch;navFilter_[navFilterLength_]=0;}
    else return;
    navIndex_=0;FilterElements();
    char16_t head[120];NavOut o{head,120,0};head[0]=0;
    if(navFilterLength_) {o.S(pl?u"Filtr ":u"Filter ");o.S(navFilter_);o.S(u", ");o.N(navShownCount_);o.S(u". ");}
    else {o.S(pl?u"Bez filtru, ":u"No filter, ");o.N(navShownCount_);o.S(u". ");}
    if(navShownCount_)SayListEntry(head);else {o.S(pl?u"\x017b" u"aden element nie pasuje.":u"No element matches.");SayOwn(head);}
}
void FindKey(const NavKey& k) {
    const bool pl=options_.polish;
    if(k.scan==0x17) {navMode_=0;SayOwn(pl?u"Szukanie anulowane.":u"Find cancelled.");return;}
    if(k.scan)return;
    if(k.ch==13) {
        navMode_=0;
        if(!findLength_) {SayOwn(pl?u"Nie wpisano tekstu.":u"No text was typed.");return;}
        // from the top of the screen, as a new search does in NVDA's find dialog from the cursor: the review cursor stays the start
        FindStep(true);return;
    }
    if(k.ch==8) {if(findLength_) {const char16_t gone=findText_[--findLength_];findText_[findLength_]=0;if(options_.readerFlags&readerFlagTyped) {Raise(formatting::Event::deleteObject);SayChar(gone);}}return;}
    if(k.ch>=32 && findLength_<31) {findText_[findLength_++]=k.ch;findText_[findLength_]=0;if(options_.readerFlags&readerFlagTyped)SayChar(k.ch);}
}
// Commands of r410 (true: handled).
bool NavShortcut(char16_t c) {
    if(inputHelp_)return false;
    switch(c) {
        case u'L':OpenElements(navMode_!=1);return true;
        case u'[':if(!StepControl(false))StepElement(false);return true;   // graphical screens: controls
        case u']':if(!StepControl(true))StepElement(true);return true;
        case u'F':OpenFind();return true;
        case u'N':FindStep(true);return true;
        case u'P':FindStep(false);return true;
        case u'B':ToggleBookmark();return true;
        case u'J':StepBookmark(true);return true;
        case u'U':StepBookmark(false);return true;
        case u'Y':SayProgramKeys();return true;
        case u'X':DescribeLine();return true;
        default:return false;
    }
}
// r410 (user, answer 6): in an editor of several lines the line the caret moved to is read with its number where
// the profile has line numbers (beginner, or the custom item). False: the caller reads the row as before.
bool SayEditorLine(size_t r) {
    if(!Item(formatting::itemLineNumbers))return false;
    BuildModel();
    const auto& e=model_.edit;
    if(model_.screen!=screenmodel::Screen::editor || !e.found || !e.sure || !e.multiline || r<e.top || r>e.bottom)return false;
    static char16_t text[maxColumns+60];NavOut o{text,maxColumns+60,0};text[0]=0;
    char16_t head[40];NavOut h{head,40,0};h.S(options_.polish?u"Wiersz ":u"Line ");h.N(r-e.top+1);h.S(u": ");o.Own(head);
    char16_t line[maxColumns+1];
    if(RowText(r,line,maxColumns+1))o.S(line);else o.Own(options_.polish?u"pusty":u"blank");
    element_=formatting::Element{};element_.kind=formatting::Kind::edit;
    Speak(text);return true;
}
// ---- reader bar (r410, the user's answer 15) -------------------------------
// One high-contrast line at the bottom of the screen that shows what the reader is saying, for low-vision users
// and sighted helpers. A setting (off by default; Insert+minus / Ctrl+Alt+1 switches it).
//   - Drawn through the firmware's ORIGINAL console output, never through the reader's proxy: the reader's shadow
//     of the screen, its screen model and its row counting never see it, so it is never read back. The console's
//     own Blt calls run with inConOut_ set, which makes the pixel reader take the new pixels as its baseline.
//   - The program's row is restored: the pixels under the bar are saved before it is drawn and put back when the
//     next text replaces it, after 5 s, when the program writes on that row or scrolls, and before a hand-off. A
//     pixel the program has changed in the meantime is left as the program drew it (compared with what the bar
//     itself drew), so a program that draws straight into the frame buffer is not painted over with old pixels.
//   - Without a graphics output protocol (a serial console alone) there is no bar.
static constexpr size_t barMaxWidth=2560,barMaxHeight=40;
uint32_t* barSaved_=nullptr;uint32_t* barDrawn_=nullptr;uint32_t* barNow_=nullptr;
size_t barY_=0,barH_=0,barW_=0,barRows_=0;bool barShown_=false;uint64_t barTick_=0;Gop* barGop_=nullptr;
// The firmware's console font has Latin-1 but no Polish letters: on the bar they lose their marks (a blank cell
// would be worse). The voice is not affected.
static char16_t BarLetter(char16_t c) {
    switch(c) {case 0x105:return u'a';case 0x107:return u'c';case 0x119:return u'e';case 0x142:return u'l';case 0x144:return u'n';case 0x15b:return u's';case 0x17a:case 0x17c:return u'z';
        case 0x104:return u'A';case 0x106:return u'C';case 0x118:return u'E';case 0x141:return u'L';case 0x143:return u'N';case 0x15a:return u'S';case 0x179:case 0x17b:return u'Z';
        case 0x2019:case 0x2018:return char16_t(39);case 0x2014:case 0x2013:return u'-';default:return c>0xff?u'?':c;}
}
bool BarBuffers() {
    if(barSaved_)return true;
    uint32_t* block=nullptr;
    if(boot_->AllocatePool(EfiBootServicesData,barMaxWidth*barMaxHeight*4*3,(void**)&block)!=EFI_SUCCESS || !block)return false;
    barSaved_=block;barDrawn_=block+barMaxWidth*barMaxHeight;barNow_=block+barMaxWidth*barMaxHeight*2;return true;
}
void BarShow(const char16_t* text) {
    if(!options_.readerBar || stopped_ || suspended_ || !text || !real_ || !real_->Mode)return;
    // The CONSOLE's own size: on a graphical screen the reader's grid (rows_, columns_) is that of the picture, not
    // of the firmware's text console the bar is written with (seen on rEFInd: 42 rows against the console's 31).
    size_t barColumns=0,barRows=0;
    if(!real_->QueryMode || real_->QueryMode(real_,size_t(real_->Mode->Mode),&barColumns,&barRows)!=EFI_SUCCESS || barRows<3 || barColumns<20)return;
    if(barColumns>maxColumns)barColumns=maxColumns;
    Gop* gop=DeviceGop();const GopBltFn blt=gop?OriginalBlt(gop):nullptr;
    if(!gop || !blt || !gop->Mode || !gop->Mode->info || !BarBuffers())return;
    const size_t width=gop->Mode->info->horizontal,height=gop->Mode->info->vertical;
    if(!width || width>barMaxWidth || height<40)return;
    if(barShown_)BarRestore();
    // the console's bottom text row: cells of 8 x 19 pixels, the text area centred (edk2 GraphicsConsole)
    if(height<barRows*19 || width<barColumns*8)return;   // not the 8 x 19 cell console this is written for
    size_t top=(height-barRows*19)/2+(barRows-1)*19;
    size_t h=height-top;if(h>barMaxHeight) {h=barMaxHeight;}
    if(blt(gop,barSaved_,bltVideoToBuffer,0,top,0,0,width,h,width*4)!=EFI_SUCCESS)return;
    char16_t line[maxColumns+1];size_t at=0;bool space=true;const size_t limit=barColumns-1;
    for(size_t i=0;text[i] && at<limit;++i) {
        const char16_t c=text[i];
        if(c==speechtext::ownOpen || c==speechtext::ownClose)continue;
        if(c==formatting::formatMark) {size_t k=1;while(k<=formatting::payload && text[i+k])++k;i+=k-1;continue;}
        if(c==speechtext::languageSwitch) {if(text[i+1])++i;continue;}
        if(c<u' ' || (c>=0x2500 && c<=0x259f) || c>=0xE000)continue;
        if(c==u' ') {if(space)continue;space=true;} else space=false;
        line[at++]=BarLetter(c);
    }
    if(!at)return;
    while(at<limit)line[at++]=u' ';
    line[at]=0;
    const int32_t column=real_->Mode->CursorColumn,row=real_->Mode->CursorRow,attribute=real_->Mode->Attribute;   // the console's own state, not the reader's idea of it
    inConOut_=true;
    saved_.SetAttribute(real_,0x70);   // black on light grey: the console's strongest contrast against a dark screen
    if(saved_.SetCursorPosition(real_,0,barRows-1)==EFI_SUCCESS)saved_.OutputString(real_,line);
    saved_.SetAttribute(real_,size_t(attribute)&0x7f);
    saved_.SetCursorPosition(real_,column>=0?size_t(column):0,row>=0?size_t(row):0);
    inConOut_=false;
    if(blt(gop,barDrawn_,bltVideoToBuffer,0,top,0,0,width,h,width*4)!=EFI_SUCCESS) {for(size_t i=0;i<width*h;++i)barDrawn_[i]=barSaved_[i];}
    RehashBands(top,h);
    barRows_=barRows;barShown_=true;barTick_=tick_;barY_=top;barH_=h;barW_=width;barGop_=gop;
#ifdef HAK_CHARON_TRACE
    charontrace::Value("CHARON reader bar shown",(uint64_t(top)<<32)|(uint64_t(h)<<16)|at);
#endif
}
void BarRestore() {
    if(!barShown_)return;
    barShown_=false;
    Gop* gop=DeviceGop();const GopBltFn blt=gop?OriginalBlt(gop):nullptr;
    if(!gop || gop!=barGop_ || !blt || !gop->Mode || !gop->Mode->info || gop->Mode->info->horizontal!=barW_ || gop->Mode->info->vertical<barY_+barH_)return;
    if(blt(gop,barNow_,bltVideoToBuffer,0,barY_,0,0,barW_,barH_,barW_*4)!=EFI_SUCCESS)return;
    size_t kept=0;(void)kept;
    for(size_t i=0;i<barW_*barH_;++i) {if((barNow_[i]&0xffffff)==(barDrawn_[i]&0xffffff))barNow_[i]=barSaved_[i];else ++kept;}
    blt(gop,barNow_,bltBufferToVideo,0,0,0,barY_,barW_,barH_,barW_*4);
    RehashBands(barY_,barH_);
#ifdef HAK_CHARON_TRACE
    charontrace::Value("CHARON reader bar restored, pixels the program changed",kept);
#endif
}
// ---- braille: the same information in three status cells ------------------
// "d2/6 " in front of the selected item's row: kind (d default, s submenu, e edit field, h help, - plain),
// position / count. Dots 7-8 keep marking highlighted cells. Returns the prefix length (0: none).
size_t BraillePrefix(size_t row,char16_t* out,size_t capacity) {
    out[0]=0;
    if(!options_.brailleAttributes || !(Groups()&(formatting::groupKind|formatting::groupPosition)))return 0;
    const auto& list=model_.list;NavOut o{out,capacity,0};
    if(list.found && list.sure && list.selected!=screenmodel::none8 && list.items[list.selected].row==row) {
        const auto& it=list.items[list.selected];
        o.C((it.flags&screenmodel::itemDefault)?u'd':(it.flags&screenmodel::itemSubmenu)?u's':u'-');o.C(u' ');
    } else if(model_.edit.found && model_.edit.sure && model_.edit.caretRow==row) {o.C(u'e');o.C(u' ');}
    else for(size_t i=0;i<model_.helpCount && model_.helpSure;++i)if(model_.help[i]==row) {o.C(u'h');o.C(u' ');break;}
    return o.at;
}
// " 2/6" behind the selected item's row (user, answer 15). Returns the length (0: none).
size_t BrailleSuffix(size_t row,char16_t* out,size_t capacity) {
    out[0]=0;
    if(!options_.brailleAttributes || !(Groups()&formatting::groupPosition))return 0;
    const auto& list=model_.list;
    if(!list.found || !list.sure || list.selected==screenmodel::none8 || list.items[list.selected].row!=row)return 0;
    NavOut o{out,capacity,0};
    o.C(u' ');o.N(size_t(list.selected)+1);o.C(u'/');o.N(list.count);
    return o.at;
}
// ---- r410: entry information, pointer and controls (answers 7 and 8) ------
// What the selected entry STARTS, from the program's configuration on its own volume (CharonEntryInfo.h): read
// once when the program starts (file access is not possible from every place a command runs), matched with the
// selected row by its title; no match or an ambiguous one is said as such, never guessed.
static constexpr size_t maxEntryInfo=48;
entryinfo::Entry entryInfo_[maxEntryInfo]={};size_t entryInfoCount_=0;const void* entryInfoImage_=nullptr;
uint8_t entryInfoFiles_=0;entryinfo::Info entryInfoMore_{};
template<class Parse> bool EntryFile(efi_file_protocol* root,const char16_t* path,Parse parse) {
    size_t size=0;char* data=managers::ReadFile(boot_,root,path,size);
    if(!data)return false;
    entryInfoCount_=parse(data,size);boot_->FreePool(data);++entryInfoFiles_;return true;
}
// Every file of `folder` whose name ends with `ending` (lower case, with the dot), at most 12.
template<class Each> void EntryFolder(efi_file_protocol* root,const char16_t* folder,const char16_t* ending,Each each) {
    efi_file_protocol* dir=nullptr;
    if(root->Open(root,&dir,(char16_t*)folder,EFI_FILE_MODE_READ,0)!=EFI_SUCCESS || !dir)return;
    alignas(8) static uint8_t buffer[512];size_t el=0;while(ending[el])++el;
    for(unsigned guard=0,taken=0;guard<96 && taken<12;++guard) {
        size_t bytes=sizeof(buffer);
        if(dir->Read(dir,&bytes,buffer)!=EFI_SUCCESS || !bytes)break;
        const char16_t* name=reinterpret_cast<const char16_t*>(buffer+80);   // EFI_FILE_INFO.FileName
        if(*reinterpret_cast<const uint64_t*>(buffer+72)&0x10)continue;       // a directory
        size_t n=0;while(name[n] && n<200)++n;
        if(n<=el)continue;
        bool same=true;for(size_t k=0;k<el;++k) {char16_t c=name[n-el+k];if(c>=u'A' && c<=u'Z')c=char16_t(c+32);if(c!=ending[k]) {same=false;break;}}
        if(!same)continue;
        static char16_t path[300];size_t at=0;
        for(const char16_t* p=folder;*p && at<90;++p)path[at++]=*p;
        if(!at || path[at-1]!=u'\\')path[at++]=u'\\';
        for(size_t i=0;i<n && at+1<300;++i)path[at++]=name[i];
        path[at]=0;++taken;each(path,name);
    }
    dir->Close(dir);
}
void LoadEntryInfo(const char16_t* name,efi_loaded_image_protocol* loaded) {
    entryInfoCount_=0;entryInfoFiles_=0;entryInfoImage_=currentImage_;entryInfoMore_=entryinfo::Info{};
    if(!name || !loaded || !loaded->DeviceHandle || !boot_->HandleProtocol)return;
    efi_guid fsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;efi_simple_file_system_protocol* fs=nullptr;
    if(boot_->HandleProtocol(loaded->DeviceHandle,&fsGuid,reinterpret_cast<void**>(&fs))!=EFI_SUCCESS || !fs)return;
    efi_file_protocol* root=nullptr;
    if(fs->OpenVolume(fs,&root)!=EFI_SUCCESS || !root)return;
    static char16_t dir[260];ImageDir(loaded->FilePath,dir,260);
    static char16_t own[300];
    auto inDir=[&](const char16_t* file) {size_t at=0;for(const char16_t* p=dir;*p && at<200;++p)own[at++]=*p;
        if(!at || own[at-1]!=u'\\')own[at++]=u'\\';for(const char16_t* p=file;*p && at<298;++p)own[at++]=*p;own[at]=0;return own;};
    using namespace entryinfo;
    Info* info=&entryInfoMore_;
    if(managers::SameName(name,u"GRUB") || managers::SameName(name,u"Ventoy")) {
        auto grub=[&](const char* d,size_t n) {return ParseGrub(d,n,entryInfo_,maxEntryInfo,info,entryInfoCount_);};
        const char16_t* tries[]={inDir(u"grub.cfg"),u"\\boot\\grub\\grub.cfg",u"\\grub\\grub.cfg",u"\\EFI\\BOOT\\grub.cfg",u"\\EFI\\debian\\grub.cfg",u"\\EFI\\ubuntu\\grub.cfg"};
        for(auto* t:tries)if(EntryFile(root,t,grub))break;
        // files the configuration loads (configfile, source), then the other .cfg files beside the program: a
        // menu loaded from GRUB's command line is found too
        static char16_t include[8][128];const size_t includes=info->includeCount<8?info->includeCount:8;
        for(size_t i=0;i<includes;++i) {size_t k=0;for(;info->include[i][k] && k<127;++k)include[i][k]=info->include[i][k]==u'/'?u'\\':info->include[i][k];include[i][k]=0;}
        Info other;
        auto more=[&](const char* d,size_t n) {return ParseGrub(d,n,entryInfo_,maxEntryInfo,&other,entryInfoCount_);};
        for(size_t i=0;i<includes;++i)if(include[i][0]==u'\\')EntryFile(root,include[i],more);
        EntryFolder(root,dir,u".cfg",[&](const char16_t* path,const char16_t* file) {
            const char16_t* g=u"grub.cfg";size_t k=0;while(g[k] && (file[k]|0x20)==g[k])++k;
            if(g[k] || file[k])EntryFile(root,path,more);});
    } else if(managers::SameName(name,u"systemd-boot")) {
        EntryFolder(root,u"\\loader\\entries",u".conf",[&](const char16_t* path,const char16_t* file) {
            EntryFile(root,path,[&](const char* d,size_t n) {return ParseSystemdBoot(d,n,file,entryInfo_,maxEntryInfo,info,entryInfoCount_);});});
        size_t size=0;char* data=managers::ReadFile(boot_,root,u"\\loader\\loader.conf",size);
        if(data) {ApplyLoaderConf(data,size,entryInfo_,entryInfoCount_,info);boot_->FreePool(data);++entryInfoFiles_;}
    } else if(managers::SameName(name,u"rEFInd")) {
        auto parse=[&](const char* d,size_t n) {return ParseRefind(d,n,entryInfo_,maxEntryInfo,info,entryInfoCount_);};
        const char16_t* tries[]={inDir(u"refind.conf"),u"\\EFI\\refind\\refind.conf",u"\\EFI\\BOOT\\refind.conf"};
        for(auto* t:tries)if(EntryFile(root,t,parse))break;
    } else if(managers::SameName(name,u"Limine")) {
        auto parse=[&](const char* d,size_t n) {return ParseLimine(d,n,entryInfo_,maxEntryInfo,info,entryInfoCount_);};
        const char16_t* tries[]={inDir(u"limine.conf"),u"\\limine.conf",u"\\boot\\limine\\limine.conf",u"\\limine\\limine.conf",u"\\boot\\limine.conf",u"\\EFI\\BOOT\\limine.conf"};
        for(auto* t:tries)if(EntryFile(root,t,parse))break;
    } else if(managers::SameName(name,u"Syslinux")) {
        auto parse=[&](const char* d,size_t n) {return ParseSyslinux(d,n,entryInfo_,maxEntryInfo,info,entryInfoCount_);};
        const char16_t* tries[]={inDir(u"syslinux.cfg"),u"\\syslinux.cfg",u"\\syslinux\\syslinux.cfg",u"\\EFI\\BOOT\\syslinux.cfg",u"\\boot\\syslinux\\syslinux.cfg"};
        for(auto* t:tries)if(EntryFile(root,t,parse))break;
    } else if(managers::SameName(name,u"OpenCore")) {
        auto parse=[&](const char* d,size_t n) {return ParseOpenCore(d,n,entryInfo_,maxEntryInfo,info,entryInfoCount_);};
        const char16_t* tries[]={u"\\EFI\\OC\\config.plist",inDir(u"config.plist")};
        for(auto* t:tries)if(EntryFile(root,t,parse))break;
    }
    root->Close(root);
#ifdef HAK_CHARON_TRACE
    charontrace::Value("CHARON entry info: files, entries",(uint64_t(entryInfoFiles_)<<16)|entryInfoCount_);
#endif
}
// The configuration's entry for the selected row; noMatch when there is none or more than one.
size_t SelectedEntryInfo(char16_t* name,size_t capacity) {
    name[0]=0;
    BuildModel();
    const auto& list=model_.list;
    // (the table is read anew for every program that starts: LoadEntryInfo)
    if(list.found && list.sure && list.selected!=screenmodel::none8 && list.items[list.selected].row<rows_) {
        const auto& it=list.items[list.selected];CellRange(it.row,it.left,it.right,name,capacity);
    } else if(lastHighlightRow_<rows_)RowText(lastHighlightRow_,name,capacity,true);
    // session 4: an icon picker (rEFInd, Clover, OpenCanopy) shows the selected icon's name as a line of text
    if(!name[0] && GraphicalNow() && iconName_[0] && iconNameImage_==currentImage_) {size_t k=0;for(;iconName_[k] && k+1<capacity;++k)name[k]=iconName_[k];name[k]=0;}
    // ... or, before any key, the label of the control the pixel layer sees as selected
    if(!name[0] && GraphicalNow() && ControlsAnalyse() && pcResult_->focused>=0 && pcResult_->focused<int(pcResult_->count)
        && pcResult_->controls[pcResult_->focused].confidence>=pixelcontrols::confident) {
        const char16_t* label=pcResult_->controls[pcResult_->focused].label;size_t k=0;for(;label[k] && k+1<capacity;++k)name[k]=label[k];name[k]=0;}
    if(!name[0] || !entryInfoCount_)return entryinfo::noMatch;
    size_t found=entryinfo::Match(entryInfo_,entryInfoCount_,name);
    if(found!=entryinfo::noMatch)return found;
    // rEFInd writes "Boot TITLE from VOLUME" under its icons: the configuration's title is the middle part
    if(managers::SameName(ProgramName(),u"rEFInd") && name[0]==u'B' && name[1]==u'o' && name[2]==u'o' && name[3]==u't' && name[4]==u' ') {
        static char16_t title[160];size_t n=0,cut=SIZE_MAX;
        for(size_t i=5;name[i] && n<159;++i)title[n++]=name[i];
        title[n]=0;
        for(size_t i=0;i+6<=n;++i)if(title[i]==u' ' && title[i+1]==u'f' && title[i+2]==u'r' && title[i+3]==u'o' && title[i+4]==u'm' && title[i+5]==u' ')cut=i;
        if(cut!=SIZE_MAX)title[cut]=0;
        found=entryinfo::Match(entryInfo_,entryInfoCount_,title);
        if(found!=entryinfo::noMatch)return found;
    }
    // The same file read twice (a copy of grub.cfg in two folders of the volume): several entries of this title
    // that say exactly the same are one entry. Anything else stays ambiguous.
    size_t first=entryinfo::noMatch;
    for(size_t i=0;i<entryInfoCount_;++i) {
        const auto& e=entryInfo_[i];
        if(e.disabled || !Same16(e.title,name))continue;
        if(first==entryinfo::noMatch) {first=i;continue;}
        const auto& f=entryInfo_[first];
        if(!Same16(e.program,f.program) || !Same16(e.options,f.options) || !Same16(e.initrd,f.initrd) || e.kind!=f.kind || e.submenu!=f.submenu || e.depth!=f.depth)return entryinfo::noMatch;
    }
    return first;
}
void SayEntryInfo() {
    const bool pl=options_.polish;
    static char16_t name[maxColumns+1];static char16_t text[900];
    const size_t at=SelectedEntryInfo(name,maxColumns+1);
    NavOut o{text,900,0};text[0]=0;
    if(!name[0]) {SayOwn(pl?u"Nie wiem, kt\x00f3ra pozycja jest wybrana na tym ekranie.":u"I cannot tell which entry is selected on this screen.");return;}
    if(at==entryinfo::noMatch) {
        entryinfo::Entry automatic;
        if(bootManager_ && bootManager_[0]==u's' && entryinfo::SystemdAutoEntry(name,automatic)) {
            o.S(name);o.S(u": ");o.S(pl?u"pozycja, kt\x00f3r\x0105 systemd-boot dodaje sam. ":u"an entry systemd-boot adds by itself. ");
            entryinfo::Describe(automatic,pl,text+o.at,900-o.at);SayOwn(text);return;}
        o.S(name);o.S(u": ");
        if(!entryInfoFiles_)o.S(pl?u"Charon nie znalaz\x0142 pliku konfiguracji tego programu na jego partycji, wi\x0119" u"c nie wie, co ta pozycja uruchamia."
                                  :u"Charon found no configuration file of this program on its partition, so it cannot tell what this entry starts.");
        else o.S(pl?u"w konfiguracji tego programu nie ma dok\x0142" u"adnie jednej pozycji o tej nazwie, wi\x0119" u"c nie zgaduj\x0119, co uruchamia."
                   :u"the program's configuration has no single entry of this name, so I do not guess what it starts.");
        SayOwn(text);return;
    }
    const auto& e=entryInfo_[at];
    o.S(name);o.S(u": ");
    entryinfo::Describe(e,pl,text+o.at,900-o.at);
#ifdef HAK_CHARON_TRACE
    charontrace::Value("CHARON entry info said: index, risk, kind",(uint64_t(at)<<16)|(uint64_t(e.risk)<<8)|e.kind);
#endif
    if(e.risk)Raise(formatting::Event::warnUser);
    SayOwn(text);
}
bool MoreCommand(char16_t c) {
    const bool pl=options_.polish;
    if(c==u'E') {SayEntryInfo();return true;}
    return ControlCommand(c);
    (void)pl;
}
