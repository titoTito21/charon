// Charon text formatting in speech (r393, r397). GPL-2.0-or-later.
//
// The user's request: read formatting professionally, after Emacspeak's
// voice-lock and auditory icons, as a named optional setting (Screen reader
// settings -> "Formatowanie tekstu").
//   - Voice formatting: every style gets an Emacspeak ACSS "personality"
//     (r397): average pitch, pitch range (monotone .. animated), stress,
//     richness (tense .. breathy), smoothness, rate and gain, changed in
//     mid-sentence, without extra words. Named after Emacspeak's
//     voice-setup: voice-bolden, voice-animate, voice-monotone,
//     voice-lighten, voice-smoothen, voice-brighten. Text in the screen's
//     own colours stays in the plain voice.
//   - Voice families (r397, own check box): another "voice" for another
//     kind of style, as Emacspeak does with DECtalk's Paul / Harry / Betty:
//     TVox scales its formants (head size) and voice source; a synthesizer
//     add-on with ABI 3 chooses one of its own voices or variants.
//   - Sound icons: short oscillator sounds that play AT THE SAME TIME as the
//     speech of the text they mark: a tone per foreground colour (16
//     pitches) and style sounds (bold/bright, highlight/inverse, font
//     change, back to plain).
//   - Event icons (r397): Emacspeak's auditory icons for what happens on the
//     screen (open-object, close-object, select-object, large-movement,
//     scroll, warn-user, ask-question, delete-object, button, help, ...).
//   - Stereo (r397): icons, or speech and icons, placed left .. right by the
//     screen column of the text they belong to (ACSS left/right volume).
// All of it is Charon's own work around the synthesizer (frames of TVox,
// parameters or ABI 3 persona of an add-on, Charon's mixer), so it works
// with every synthesizer.
// Attributes (each a check box): colours, bright/bold, font and size (from
// the OCR readers: face and pixel height), inverse/highlight.
//
// How it is done without touching the reader's text logic: the finished
// utterance (screen text plus the reader's own words between ownOpen /
// ownClose) is aligned word by word with the screen's rows (the same
// CellsText rendering, with a cell index per character); each matched word
// takes the style of its first cell. A change becomes a marker formatMark +
// `payload` characters before the word:
//   [0] rate step     'a'..'y', 'm' = none (4% per step)
//   [1] pitch step    'a'..'y', 'm' = none (3% per step)
//   [2] volume step   'a'..'y', 'm' = none (8% per step)
//   [3] timbre        '0' + bits: 1 echo, 2 smoothed (low pass), 4 bright (high shelf)
//   [4] colour tone   'a' + 0..15, '-' = none
//   [5] style icons   'a' + bits: 1 bold/bright, 2 highlight, 4 font, 8 plain again
//   r397:
//   [6] pitch range   'a'..'y', 'm' = the voice's own (12% per step; 'a' monotone)
//   [7] stress        'a'..'y', 'm' = none (10% per step: loudness contrast of stressed syllables)
//   [8] richness      'a'..'y', 'm' = none (+ tense and rich, - breathy)
//   [9] family        '0' = the voice itself, '1'..'7' (familyBold ...)
//   [10] event icon   'a' + Event, '-' = none
//   [11] stereo       'a'..'u' left .. right, 'k' centre (speech and icons);
//                     'A'..'U' the same for icons only; '-' = not placed
// CharonSpeechText Compose writes it as ESC 'f' + the payload; LiveVoice
// starts a chunk there, applies the voice and queues the icons with it.
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace hakefi::formatting {
constexpr char16_t formatMark=0xE003;
//   r410 (the user's answers 10-12):
//   [12] place tone   'a' (top of the list, 1760 Hz) .. 'y' (bottom, 440 Hz), '-' = none (NVDA's progress beeps, for the position)
//   [13] lead         '0'..'9': silence of 20 ms each before the words that follow (the pause between a name and the rest)
constexpr size_t payload=14;
// Settings bits (settings byte 40; ReaderControl reading bits 14..19, 25..26).
constexpr uint8_t flagVoice=1,flagIcons=2,flagColours=4,flagBright=8,flagFont=16,flagInverse=32,flagEvents=64,flagFamilies=128;
constexpr uint8_t flagsDefault=flagColours|flagBright|flagFont|flagInverse|flagEvents|flagFamilies;   // r410 (user): every box ticked but the two outputs, voice and sound icons: formatting is off until it is turned on (r399 had all on)
// Stereo modes (settings byte 41 bits 5-6).
constexpr uint8_t stereoOff=0,stereoIcons=1,stereoSpeech=2;
// Voice families (payload [9]).
enum Family : uint8_t {familyNone=0,familyBold,familyHighlight,familyWarm,familyTitle,familySmall,familyFace,familyDim};
// Emacspeak-style auditory icons for events (payload [10]).
enum class Event : uint8_t {
    none=0,
    openObject,     // a window, dialog or list opened; a new screen
    closeObject,    // it closed (back to the page under it)
    selectObject,   // the selection moved
    largeMovement,  // Home/End/Page keys, a jump
    wrap,           // the selection went round the end of a list
    scroll,         // console output scrolled
    warnUser,       // an error window or an error line
    askQuestion,    // a question (Y/N), "press any key"
    deleteObject,   // a character deleted in an editor
    button,         // a button got the focus
    tab,            // another tab
    openSubmenu,    // a submenu entered
    closeSubmenu,   // back from a submenu
    toggle,         // a check box or switch changed
    valueChanged,   // a value of the selected option changed
    disabled,       // the selected item is greyed out
    editor,         // an entry editor or command line opened
    help,           // a help screen or key list
    password,       // a password prompt
    newProgram,     // another program took the screen
    taskDone,       // progress finished, a command's output ended
    countdownEnd,   // a countdown ran out
    // r410: sounds of the screen model (CharonScreenModel.h): the KIND of the element the speech belongs to and
    // its PLACE in a list, in one sound (the place first, the kind 45 ms later), and states.
    listFirst,          // the first item of a list
    listLast,           // the last item of a list
    itemDefault,        // the entry that starts by itself
    itemDefaultFirst,itemDefaultLast,
    itemSubmenu,        // an entry that opens a submenu
    itemSubmenuFirst,itemSubmenuLast,
    editField,          // an edit field or command line has the caret
    helpLine,           // a help line is read
    errorAlert,         // an error (warnUser stays the warning)
    countdown,          // a countdown to an automatic start is running
    transient,          // a message that will disappear by itself
    indent1,indent2,indent3,   // nesting depth of the selected item (submenu levels, tree levels)
    count
};
// 'a'..'z' for the first 26 events, 'A'..'Z' for the following ones (r410).
inline char16_t EventChar(Event e) {return e==Event::none || e>=Event::count?u'-':uint8_t(e)<26?char16_t(u'a'+uint8_t(e)):char16_t(u'A'+uint8_t(e)-26);}
// r410 groups of model-driven formatting (settings V13; each a check box under "Formatowanie tekstu").
constexpr uint8_t groupKind=1,groupState=2,groupPosition=4,groupsDefault=groupKind|groupState|groupPosition;
// What the screen model says about the text of one utterance.
enum class Kind : uint8_t {none=0,selected,defaultItem,submenu,edit,help,title,error,warning,countdown,transient};
struct Element {
    Kind kind=Kind::none;
    bool first=false,last=false;      // the item is the first / last of its list
    uint8_t index=0,count=0,indent=0; // position in the list (count 0: not in a list), nesting depth
};
// One cell's look. fg/bg: console colour indices; face: font id (0 console);
// height: font pixel height (0 unknown); bold: OCR said a bold face.
struct Style {uint8_t fg=7,bg=0,face=0,height=0;bool bold=false,highlight=false;};
inline bool SameStyle(const Style& a,const Style& b) {return a.fg==b.fg && a.bg==b.bg && a.face==b.face && a.height==b.height && a.bold==b.bold && a.highlight==b.highlight;}
// The screen's plain look (majority colours, majority font).
struct Base {uint8_t fg=7,bg=0,face=0,height=0;};
// An ACSS personality in steps (0 = the voice's own value).
struct Voice {int rate=0,pitch=0,volume=0,range=0,stress=0,richness=0;unsigned timbre=0,family=0;};
inline bool Plain(const Voice& v) {return !v.rate && !v.pitch && !v.volume && !v.range && !v.stress && !v.richness && !v.timbre && !v.family;}
inline bool SameVoice(const Voice& a,const Voice& b) {return a.rate==b.rate && a.pitch==b.pitch && a.volume==b.volume && a.range==b.range && a.stress==b.stress && a.richness==b.richness && a.timbre==b.timbre && a.family==b.family;}
// Emacspeak voice-setup personalities (steps, combined and clamped):
struct Personality {int pitch,range,stress,richness,volume,rate;unsigned timbre;};
constexpr Personality voiceBolden   {-2, 1, 3, 3, 1, 0,0};   // bold, bright: lower, firmer, richer
constexpr Personality voiceAnimate  { 3, 4, 2, 1, 1, 0,1};   // selection: higher, lively, the echo of highlighted text
constexpr Personality voiceMonotone { 0,-6,-3, 0, 0, 0,0};   // cool colours: flat
constexpr Personality voiceLighten  { 1,-2,-2,-4,-2, 0,0};   // dim, greyed out: lighter, breathy, softer
constexpr Personality voiceSmoothen { 0,-1,-1,-1, 0, 0,2};   // another background: smooth
constexpr Personality voiceBrighten { 1, 1, 1, 2, 0, 0,4};   // another face: bright
constexpr Personality voiceWarn     { 4, 3, 3, 2, 1, 0,0};   // red, yellow: urgent
constexpr Personality voiceTitle    {-2, 2, 2, 2, 1,-3,0};   // large print: slower, lower, firm
constexpr Personality voiceSmall    { 1,-2,-1,-1,-1, 3,0};   // small print: faster, softer
// r410: one voice per KIND of element, the same in every program (Emacspeak's principle: voices differ inside an
// utterance; nobody has to name one in isolation). voice-defs.el maps bold/link to voice-bolden, highlight to
// voice-animate, error and warning to voice-brighten, comments to voice-monotone, annotations to voice-annotate.
// r410 (user, answers 10 and 11): ONE table for the kind of an element: its voice (pitch, range, stress, richness,
// volume, rate, timbre), its voice family (another "speaker" for auxiliary text), and its tone, played at the same
// time as the first word. Auxiliary text (help, messages that go away) is faster and lighter, important text
// (default entry, title) slower and firmer; an error is LOWER and SLOWER than everything else (answer 11), a
// warning a little lower. Pitch steps are 3 %, rate steps 4 %, volume steps 8 %.
struct Note {uint16_t start,ms,hz0,hz1,gain;bool square;};   // start and length in ms; a glide from hz0 to hz1
struct KindRow {Kind kind;Personality voice;uint8_t family;Note notes[3];};
constexpr KindRow kindTable[]={
    //                       pitch range stress rich vol rate timbre  family           tone
    {Kind::selected,        { 0, 0, 0, 0, 0, 0,0}, familyNone,  {}},                                                             // the selection: the plain voice, it is most of what is heard
    {Kind::defaultItem,     {-2, 1, 3, 3, 1,-1,0}, familyBold,  {{0,30,523,523,6500,false},{32,45,523,523,7500,false}}},         // default entry: lower, firmer, a little slower; the same note twice ("this one")
    {Kind::submenu,         { 2, 4, 1, 1, 0, 0,4}, familyNone,  {{0,25,659,659,6500,false},{27,25,988,988,6500,false}}},         // opens a submenu: lively, bright; a step up ("goes further")
    {Kind::edit,            { 0,-6,-2, 0, 0,-1,0}, familyNone,  {{0,18,900,900,5000,true},{40,18,900,900,5000,true},{80,18,900,900,5000,true}}},   // edit field: flat; three soft ticks (a caret)
    {Kind::help,            { 1,-2,-2,-3,-2, 2,2}, familyDim,   {{0,70,1319,1175,4500,false}}},                                  // help line: soft, smooth, faster, breathy speaker; a soft falling tone
    {Kind::title,           {-3, 2, 2, 2, 1,-2,0}, familyTitle, {{0,60,392,392,6000,false}}},                                    // title: low and slow, the deep speaker; one low note
    {Kind::error,           {-4, 1, 4, 3, 2,-3,0}, familyTitle, {{0,24,311,311,8000,true},{24,24,294,294,8000,true},{48,72,277,277,8000,true}}},   // error: lower, slower, loud; a low square warble
    {Kind::warning,         {-2, 2, 2, 2, 1,-1,0}, familyBold,  {{0,40,440,415,5000,false},{40,40,440,415,5000,false}}},         // warning: a little lower; a softer sine warble
    {Kind::countdown,       { 2,-3, 2, 0, 0, 3,0}, familyNone,  {{0,12,1000,1000,6000,false},{60,12,1000,1000,6000,false}}},     // countdown: flat and fast; two clock ticks
    {Kind::transient,       { 1,-1,-3,-4,-3, 1,2}, familyDim,   {{0,110,1047,784,3500,false}}},                                  // a message that goes away: light; a quiet glide down
};
inline const KindRow& KindSpec(Kind k) {for(const auto& row:kindTable)if(row.kind==k)return row;return kindTable[0];}
inline const Personality& KindVoice(Kind k) {return KindSpec(k).voice;}
// The reader's own words inside an utterance about an element (", default, 2 of 5"): auxiliary, so a little faster
// and in the muffled "other speaker" (answer 10: timbre for the reader's own words).
constexpr Personality ownWords { 0,-1,-1, 0,-1, 2,0};
constexpr uint8_t ownWordsFamily=familyFace;
// Place tone of a list position (payload [12]): 0 = top = 1760 Hz, 24 = bottom = 440 Hz, two octaves in 24 steps.
inline char16_t PlaceChar(const Element& e) {
    if(e.count<2 || e.index>=e.count)return u'-';
    return char16_t(u'a'+(unsigned(e.index)*24+(e.count-1)/2)/(e.count-1));
}
inline unsigned PlaceHertz(unsigned step) {   // 1760 * 2^(-step/12), in integers (a table of one octave)
    static const uint16_t octave[12]={1760,1661,1568,1480,1397,1319,1245,1175,1109,1047,988,932};
    if(step>24)step=24;
    return step==24?440:octave[step%12]>>(step/12);
}
// r410 verbosity profiles as sets of items (the user's answer 14: the fourth, custom profile has a check box per
// item). Profile 0 normal, 1 beginner, 2 expert, 3 custom.
constexpr uint16_t itemPosition=1,      // "2 of 5" after an entry
    itemKind=2,           // "submenu", "default" after an entry
    itemHints=4,          // key hints (what Enter does, which keys choose)
    itemIntro=8,          // "GRUB: menu, 5 items" when a menu is met; the reader's own lists explain their keys
    itemHelp=16,          // the screen's help line read with a new screen
    itemScreen=32,        // "Entry editor", "Command line" when one opens
    itemSounds=64,        // sounds of the screen model always on (they replace the words that are off)
    itemStructure=128,    // "start of list", "end of list", "level 2" in words
    itemWaitLong=256,     // hidden menu: 5 s and the full hint
    itemWaitShort=512,    // hidden menu: 1 s and a tone
    itemCountdown=1024,   // one sentence about what starts and when
    itemLineNumbers=2048; // "line 3" before an editor's line
constexpr size_t profileItemCount=12;
constexpr uint16_t itemsNormal=itemPosition|itemKind|itemIntro|itemHelp|itemScreen|itemCountdown,
    itemsBeginner=itemsNormal|itemHints|itemStructure|itemWaitLong|itemLineNumbers,itemsExpert=itemSounds|itemWaitShort;
inline uint16_t ProfileItems(uint8_t profile,uint16_t custom) {return profile==1?itemsBeginner:profile==2?itemsExpert:profile==3?uint16_t(custom&4095):itemsNormal;}
// Pitch by position in the list: the first item highest, the last lowest (+4 .. -4 steps of 3%).
inline int PositionPitch(const Element& e) {
    if(e.count<2 || e.index>=e.count)return 0;
    return 4-int((unsigned(e.index)*8+(e.count-1)/2)/(e.count-1));
}
// The sound of an element (payload [10]); Event::none when its groups are off or nothing is to be marked.
inline Event ElementEvent(const Element& e,uint8_t groups) {
    const bool kind=(groups&groupKind)!=0,state=(groups&groupState)!=0,place=(groups&groupPosition)!=0;
    if(state && e.kind==Kind::error)return Event::errorAlert;
    if(state && e.kind==Kind::warning)return Event::warnUser;
    if(state && e.kind==Kind::countdown)return Event::countdown;
    if(state && e.kind==Kind::transient)return Event::transient;
    const bool first=place && e.first && e.count>=2,last=place && e.last && e.count>=2;
    if(kind && e.kind==Kind::defaultItem)return first?Event::itemDefaultFirst:last?Event::itemDefaultLast:Event::itemDefault;
    if(kind && e.kind==Kind::submenu)return first?Event::itemSubmenuFirst:last?Event::itemSubmenuLast:Event::itemSubmenu;
    if(kind && e.kind==Kind::edit)return Event::editField;
    if(kind && e.kind==Kind::help)return Event::helpLine;
    if(first)return Event::listFirst;
    if(last)return Event::listLast;
    if(place && e.indent)return e.indent==1?Event::indent1:e.indent==2?Event::indent2:Event::indent3;
    return Event::none;
}
inline void Add(Voice& v,const Personality& p) {v.pitch+=p.pitch;v.range+=p.range;v.stress+=p.stress;v.richness+=p.richness;v.volume+=p.volume;v.rate+=p.rate;v.timbre|=p.timbre;}
inline bool Dim(const Style& s,const Base& base) {return (s.fg==8 && base.fg!=8) || (!(s.fg&8) && (base.fg&8) && s.fg!=base.fg);}
// r399: ABSOLUTE mapping so a uniform screen is not monotone and every
// attribute is audible: colour -> pitch (wysokosc), font face/height -> rate
// (predkosc) + timbre (barwa), bold/dim -> volume (glosnosc), highlight ->
// range (modulacja) + echo, row position -> range (modulacja). The reader's
// own words use the neutral voice (see Annotate) as the reference.
// r410 (user, answer 13): the DEFAULT is now relative (absolute=false): the voice changes only where a colour or
// attribute sets text apart from the rest of the screen (selection, a red error, a title in another colour or
// size); text in the screen's own colours sounds plain, and the row on the screen changes nothing. The r399
// ladder below stays as the option "absolute colours" (settings V14).
inline Voice VoiceFor(const Style& s,const Base& base,uint8_t flags,size_t row=SIZE_MAX,size_t rows=0,bool absolute=false) {
    Voice v;unsigned family=0;
    if(!absolute) {
        row=SIZE_MAX;
        if((flags&flagColours) && (s.fg!=base.fg || s.bg!=base.bg)) {
            const unsigned c=s.fg&7;
            if(s.fg!=base.fg) {
                if(c==4) {Add(v,voiceWarn);family=familyWarm;}                       // red: urgent
                else if(c==6) {v.pitch+=2;v.range+=2;v.stress+=1;family=familyWarm;}   // yellow, brown: raised
                else if(c==1 || c==3) Add(v,voiceMonotone);                           // blue, cyan: flat
                else if(c==5 || c==2) Add(v,voiceBrighten);                           // magenta, green: bright
            }
            if(s.bg!=base.bg && !s.highlight)Add(v,voiceSmoothen);                    // another background: smooth
        }
    } else
    if(flags&flagColours) {
        // kolor -> wysokosc: a pitch per foreground colour. The common body
        // colours (lightgray 7, white 15) sit at 0 so prose reads naturally,
        // while every other colour has a clearly distinct pitch.
        static const int pitchLadder[16]={-7,-5,-1,2,6,4,5,0,-4,-3,1,3,8,7,9,0};
        v.pitch+=pitchLadder[s.fg&15];
        const unsigned c=s.fg&7;
        // hue -> modulacja / barwa: warm colours animated, cool ones flat, magenta bright
        if(c==4 || c==6) {v.range+=3;v.stress+=2;family=familyWarm;}
        else if(c==1 || c==3) {v.range-=4;v.stress-=2;}
        else if(c==5)v.timbre|=4;
        if(s.bg!=base.bg && !s.highlight) {v.timbre|=2;v.richness-=1;}   // inne tlo -> gladsza barwa
    }
    if(flags&flagBright) {
        const bool bright=(s.fg&8) && s.fg!=8 && !(base.fg&8);   // r397: dark grey (8) is dim, not bright
        if(bright || s.bold) {v.volume+=2;v.stress+=3;v.richness+=2;v.pitch-=1;if(!family)family=familyBold;}   // styl pogrubiony -> glosnosc
        else if(Dim(s,base)) {v.volume-=2;v.richness-=3;v.range-=2;if(!family)family=familyDim;}                // przygaszony -> ciszej, oddechowo
    }
    if(flags&flagInverse && s.highlight) {v.range+=5;v.pitch+=4;v.volume+=1;v.stress+=2;family=familyHighlight;}   // zaznaczenie -> mocna modulacja + wyzszy glos (bez echa)
    if(flags&flagFont) {
        if(s.face!=base.face && s.face) {v.rate+=(s.face&1)?2:-2;v.timbre|=4;if(!family)family=familyFace;}   // czcionka (krój) -> predkosc + barwa
        if(s.height && base.height) {
            if(s.height*4>=base.height*5) {v.rate-=3;v.pitch-=2;v.range+=2;if(family!=familyHighlight)family=familyTitle;}   // duza czcionka -> wolniej, nizej
            else if(s.height*5<=base.height*4) {v.rate+=3;v.pitch+=1;if(family!=familyHighlight)family=familySmall;}         // mala -> szybciej
        }
    }
    // pozycja (wiersz na ekranie) -> modulacja: gora ekranu zywsza, dol plaski.
    if(row!=SIZE_MAX && rows>1) {const int band=int(row)*6/int(rows);v.range+=2-band;}
    if(flags&flagFamilies)v.family=family;
    auto clamp=[](int& x) {if(x<-12)x=-12;if(x>12)x=12;};
    clamp(v.rate);clamp(v.pitch);clamp(v.volume);clamp(v.range);clamp(v.stress);clamp(v.richness);
    return v;
}
// Icon of a change from `before` to `now` (only for the enabled attributes).
inline void IconFor(const Style& before,const Style& now,const Base& base,uint8_t flags,char16_t& colour,unsigned& style) {
    colour=u'-';style=0;
    if((flags&flagColours) && (now.fg!=before.fg || now.bg!=before.bg))colour=char16_t(u'a'+(now.fg&15));
    if((flags&flagBright) && ((now.bold && !before.bold) || ((now.fg&8) && now.fg!=8 && !(before.fg&8) && !(base.fg&8))))style|=1;
    if((flags&flagInverse) && now.highlight && !before.highlight)style|=2;
    if((flags&flagFont) && (now.face!=before.face || now.height!=before.height) && now.height && before.height)style|=4;
}
inline char16_t Step(int x) {return char16_t(u'm'+x);}
inline void WriteMark(char16_t* out,const Voice& v,bool voice,char16_t colour,unsigned style,bool icons,char16_t event=u'-',char16_t pan=u'-',char16_t place=u'-',unsigned lead=0) {
    out[0]=formatMark;
    out[1]=Step(voice?v.rate:0);out[2]=Step(voice?v.pitch:0);out[3]=Step(voice?v.volume:0);
    out[4]=char16_t(u'0'+(voice?v.timbre:0));out[5]=icons?colour:u'-';out[6]=char16_t(u'a'+(icons?style:0));
    out[7]=Step(voice?v.range:0);out[8]=Step(voice?v.stress:0);out[9]=Step(voice?v.richness:0);
    out[10]=char16_t(u'0'+(voice && v.family<8?v.family:0));out[11]=event;out[12]=pan;out[13]=place;out[14]=char16_t(u'0'+(lead<=9?lead:9));
}
inline bool MarkSilent(const char16_t* m) {return m[5]==u'-' && m[6]==u'a' && m[11]==u'-' && m[13]==u'-';}
// Stereo place of a screen column: 'a' (left) .. 'u' (right).
inline char16_t PanFor(size_t column,size_t columns) {
    if(columns<2)return u'k';
    size_t p=(column*20+(columns-1)/2)/(columns-1);if(p>20)p=20;
    return char16_t(u'a'+p);
}
// ---- alignment of an utterance with the screen --------------------------
// Grid: rows of rendered text (as the reader reads them) with the cell
// column of each character and the style of each cell.
struct Grid {
    static constexpr size_t maxRows=64,maxText=200,maxColumns=160;
    size_t rows=0,columns=80;
    char16_t text[maxRows][maxText+1]={};uint8_t cell[maxRows][maxText+1]={};size_t length[maxRows]={};
    Style style[maxRows][maxColumns]={};
};
inline bool Space(char16_t c) {return c==u' ' || c==u'\t';}
// What else goes into the utterance (r397).
struct Extras {Event event=Event::none;uint8_t stereo=stereoOff;
    Element element{};uint8_t groups=0;
    bool absolute=false;      // r410: the r399 absolute colour ladder instead of the relative default
    bool sounds=false;};      // r410: the sounds of the screen model are on (place tone)   // r410: what the screen model says about this utterance, and the enabled groups
// Annotates `in` (with ownOpen/ownClose words) into out. hintRow: row to
// try first (SIZE_MAX: none). Returns false (out untouched) when there is
// nothing to mark.
template<char16_t ownOpen,char16_t ownClose>
bool Annotate(const char16_t* in,const Grid& g,const Base& base,uint8_t flags,size_t hintRow,char16_t* out,size_t cap,Extras extras={}) {
    const bool voice=(flags&flagVoice)!=0,icons=(flags&flagIcons)!=0;
    const bool events=(flags&flagEvents) && extras.event!=Event::none;
    const bool placed=extras.stereo!=stereoOff && (icons || events || extras.stereo==stereoSpeech);
    if((!voice && !icons && !events && !placed) || !cap)return false;
    size_t at=0;bool any=false;
    auto put=[&](char16_t c) {if(at+1<cap)out[at++]=c;};
    Style current;current.fg=base.fg;current.bg=base.bg;current.face=base.face;current.height=base.height;
    Voice currentVoice;bool styled=false;
    char16_t currentPan=u'k';
    size_t lastRow=hintRow,lastEnd=0,panRow=SIZE_MAX;
    auto emit=[&](const Voice& v,char16_t colour,unsigned style,char16_t event,char16_t pan,char16_t place=u'-',unsigned lead=0) {
        // lower case: speech and icons placed; upper case: icons only
        const char16_t p=!placed?u'-':extras.stereo==stereoSpeech?pan:char16_t(pan-32);
        char16_t m[payload+1];WriteMark(m,v,voice,colour,style,icons,event,p,place,lead);
        if(at && out[at-1]!=u' ')put(u' ');
        for(char16_t c:m)put(c);put(u' ');any=true;
    };
    // The event icon comes first, with the utterance's first words.
    // r410 (answer 12): the element's place tone, and the sounds placed where the element is on the screen: its
    // row's middle column, shifted right by two steps of twenty per nesting level.
    const char16_t place=(extras.sounds && (extras.groups&groupPosition))?PlaceChar(extras.element):u'-';
    char16_t elementPan=u'k';
    if(placed && extras.element.kind!=Kind::none && hintRow<g.rows && g.length[hintRow]) {
        const size_t len=g.length[hintRow];size_t first=0;while(first<len && Space(g.text[hintRow][first]))++first;
        size_t lastCh=len;while(lastCh>first && Space(g.text[hintRow][lastCh-1]))--lastCh;
        if(lastCh>first) {unsigned p=unsigned(PanFor((size_t(g.cell[hintRow][first])+size_t(g.cell[hintRow][lastCh-1]))/2,g.columns)-u'a')+2u*extras.element.indent;elementPan=char16_t(u'a'+(p>20?20:p));}
    }
    if(events || place!=u'-') {emit(currentVoice,u'-',0,events?EventChar(extras.event):u'-',elementPan,place);}
    // r410: the element's kind and place change the voice of its screen text (not the reader's own words).
    const bool kindVoice=voice && extras.element.kind!=Kind::none
        && (((extras.groups&groupKind) && uint8_t(extras.element.kind)<=uint8_t(Kind::title)) || ((extras.groups&groupState) && uint8_t(extras.element.kind)>=uint8_t(Kind::error)));
    const int placePitch=voice && (extras.groups&groupPosition)?PositionPitch(extras.element):0;
    auto mark=[&](const Style& s,char16_t pan,size_t row) {
        Voice v=VoiceFor(s,base,flags,row,g.rows,extras.absolute);
        if(kindVoice) {Add(v,KindVoice(extras.element.kind));if((flags&flagFamilies) && KindSpec(extras.element.kind).family)v.family=KindSpec(extras.element.kind).family;}
        v.pitch+=placePitch;
        {auto clamp=[](int& x) {if(x<-12)x=-12;if(x>12)x=12;};clamp(v.rate);clamp(v.pitch);clamp(v.volume);clamp(v.range);clamp(v.stress);clamp(v.richness);}
        char16_t colour;unsigned style;IconFor(current,s,base,flags,colour,style);
        const bool voiceChange=voice && !SameVoice(v,currentVoice);
        const bool panChange=placed && (pan>currentPan?pan-currentPan:currentPan-pan)>=3;
        if(!voiceChange && !(icons && (colour!=u'-' || style)) && !panChange)  {current=s;return;}
        if(styled && Plain(v) && icons)style|=8;
        emit(v,colour,style,u'-',pan);
        current=s;currentVoice=v;styled=!Plain(v);if(placed)currentPan=pan;
    };
    for(size_t i=0;in[i];) {
        if(in[i]==ownOpen) {
            // The reader's own words: plain voice.
            if(styled || (kindVoice && at>0) || !SameStyle(current,Style{base.fg,base.bg,base.face,base.height,false,false})) {
                Style plain;plain.fg=base.fg;plain.bg=base.bg;plain.face=base.face;plain.height=base.height;
                Voice v{};   // reader's own words: the neutral reference voice (screen content carries the variation)
                // r410 (answer 10): about an element they are auxiliary: faster, another speaker, after a short pause
                const bool aux=kindVoice && at>0;
                if(aux) {Add(v,ownWords);if(flags&flagFamilies)v.family=ownWordsFamily;}
                if(voice && !SameVoice(v,currentVoice)) {
                    char16_t m[payload+1];WriteMark(m,v,true,u'-',0,icons,u'-',u'-',u'-',aux?4:0);if(at && out[at-1]!=u' ')put(u' ');for(char16_t c:m)put(c);put(u' ');currentVoice=v;any=true;}
                current=plain;styled=false;
            }
            while(in[i] && in[i]!=ownClose)put(in[i++]);
            if(in[i])put(in[i++]);
            continue;
        }
        if(Space(in[i])) {put(in[i++]);continue;}
        // one word of screen text
        size_t e=i;while(in[e] && !Space(in[e]) && in[e]!=ownOpen)++e;
        const size_t n=e-i;
        // find it on the screen: the hint row and the row after the last match first
        size_t bestRow=SIZE_MAX,bestAt=0;
        auto tryRow=[&](size_t r,size_t from) {
            if(r>=g.rows || bestRow!=SIZE_MAX)return;
            const size_t len=g.length[r];
            for(size_t o=from;o+n<=len;++o) {
                if(o && !Space(g.text[r][o-1]))continue;   // word starts only
                size_t k=0;while(k<n && g.text[r][o+k]==in[i+k])++k;
                if(k==n && (o+n==len || !((g.text[r][o+n]>=u'a' && g.text[r][o+n]<=u'z') || (g.text[r][o+n]>=u'A' && g.text[r][o+n]<=u'Z')))) {bestRow=r;bestAt=o;return;}
            }
        };
        if(lastRow<g.rows) {tryRow(lastRow,lastEnd);tryRow(lastRow+1,0);tryRow(lastRow,0);}
        for(size_t r=0;r<g.rows && bestRow==SIZE_MAX;++r)tryRow(r,0);
        if(bestRow!=SIZE_MAX) {
            const size_t column=g.cell[bestRow][bestAt];
            const Style& s=g.style[bestRow][column];
            // r397: one place per row (the middle of its text), so a row is not cut into pieces
            const size_t len=g.length[bestRow];size_t first=0;while(first<len && Space(g.text[bestRow][first]))++first;
            size_t lastCh=len;while(lastCh>first && Space(g.text[bestRow][lastCh-1]))--lastCh;
            const size_t middle=lastCh>first?(size_t(g.cell[bestRow][first])+size_t(g.cell[bestRow][lastCh-1]))/2:column;
            char16_t pan=bestRow==panRow?currentPan:PanFor(middle,g.columns);
            if(bestRow!=panRow && bestRow==hintRow && extras.element.kind!=Kind::none)pan=elementPan;   // r410: nesting depth shifts right
            panRow=bestRow;
            if(!SameStyle(s,current) || voice || (placed && (pan>currentPan?pan-currentPan:currentPan-pan)>=3))mark(s,pan,bestRow);
            lastRow=bestRow;lastEnd=bestAt+n;
        }
        for(size_t k=i;k<e;++k)put(in[k]);
        i=e;
    }
    if(!any)return false;
    out[at]=0;return true;
}
}  // namespace hakefi::formatting
