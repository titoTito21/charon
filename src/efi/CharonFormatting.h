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
constexpr size_t payload=12;
// Settings bits (settings byte 40; ReaderControl reading bits 14..19, 25..26).
constexpr uint8_t flagVoice=1,flagIcons=2,flagColours=4,flagBright=8,flagFont=16,flagInverse=32,flagEvents=64,flagFamilies=128;
constexpr uint8_t flagsDefault=flagVoice|flagIcons|flagColours|flagBright|flagFont|flagInverse|flagEvents|flagFamilies;   // r399: all on - voice varies by colour/font/style/position (non-monotone) + synthetic per-event icons
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
    count
};
inline char16_t EventChar(Event e) {return e==Event::none || e>=Event::count?u'-':char16_t(u'a'+uint8_t(e));}
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
inline void Add(Voice& v,const Personality& p) {v.pitch+=p.pitch;v.range+=p.range;v.stress+=p.stress;v.richness+=p.richness;v.volume+=p.volume;v.rate+=p.rate;v.timbre|=p.timbre;}
inline bool Dim(const Style& s,const Base& base) {return (s.fg==8 && base.fg!=8) || (!(s.fg&8) && (base.fg&8) && s.fg!=base.fg);}
// r399: ABSOLUTE mapping so a uniform screen is not monotone and every
// attribute is audible: colour -> pitch (wysokosc), font face/height -> rate
// (predkosc) + timbre (barwa), bold/dim -> volume (glosnosc), highlight ->
// range (modulacja) + echo, row position -> range (modulacja). The reader's
// own words use the neutral voice (see Annotate) as the reference.
inline Voice VoiceFor(const Style& s,const Base& base,uint8_t flags,size_t row=SIZE_MAX,size_t rows=0) {
    Voice v;unsigned family=0;
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
inline void WriteMark(char16_t* out,const Voice& v,bool voice,char16_t colour,unsigned style,bool icons,char16_t event=u'-',char16_t pan=u'-') {
    out[0]=formatMark;
    out[1]=Step(voice?v.rate:0);out[2]=Step(voice?v.pitch:0);out[3]=Step(voice?v.volume:0);
    out[4]=char16_t(u'0'+(voice?v.timbre:0));out[5]=icons?colour:u'-';out[6]=char16_t(u'a'+(icons?style:0));
    out[7]=Step(voice?v.range:0);out[8]=Step(voice?v.stress:0);out[9]=Step(voice?v.richness:0);
    out[10]=char16_t(u'0'+(v.family<8?v.family:0));out[11]=event;out[12]=pan;
}
inline bool MarkSilent(const char16_t* m) {return m[5]==u'-' && m[6]==u'a' && m[11]==u'-';}
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
struct Extras {Event event=Event::none;uint8_t stereo=stereoOff;};
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
    auto emit=[&](const Voice& v,char16_t colour,unsigned style,char16_t event,char16_t pan) {
        // lower case: speech and icons placed; upper case: icons only
        const char16_t p=!placed?u'-':extras.stereo==stereoSpeech?pan:char16_t(pan-32);
        char16_t m[payload+1];WriteMark(m,v,voice,colour,style,icons,event,p);
        if(at && out[at-1]!=u' ')put(u' ');
        for(char16_t c:m)put(c);put(u' ');any=true;
    };
    // The event icon comes first, with the utterance's first words.
    if(events) {emit(currentVoice,u'-',0,EventChar(extras.event),u'k');}
    auto mark=[&](const Style& s,char16_t pan,size_t row) {
        const Voice v=VoiceFor(s,base,flags,row,g.rows);
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
            if(styled || !SameStyle(current,Style{base.fg,base.bg,base.face,base.height,false,false})) {
                Style plain;plain.fg=base.fg;plain.bg=base.bg;plain.face=base.face;plain.height=base.height;
                const Voice v{};   // reader's own words: the neutral reference voice (screen content carries the variation)
                if(voice && !SameVoice(v,currentVoice)) {
                    char16_t m[payload+1];WriteMark(m,v,true,u'-',0,icons,u'-',u'-');if(at && out[at-1]!=u' ')put(u' ');for(char16_t c:m)put(c);put(u' ');currentVoice=v;any=true;}
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
            const char16_t pan=bestRow==panRow?currentPan:PanFor(middle,g.columns);panRow=bestRow;
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
