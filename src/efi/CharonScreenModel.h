// Charon screen model (r410). GPL-2.0-or-later.
//
// ONE structure for what is on a screen, whatever program drew it: windows and
// frames, a list with its items (selected, default, submenu, position and
// count), a countdown, the help lines, an edit field with its caret, progress,
// a message or alert, the title. It is built from the reader's screen grid
// (console text and the text the OCR readers wrote into the same grid) by a
// generic recogniser (highlight / inverse cells, selection marks, box-drawing
// frames, countdown and percent patterns) that a per-program recogniser then
// corrects with what that program is documented to draw.
//
// Rule of the design: nothing is guessed silently. Every element carries its
// own `sure` flag; the reader speaks from an element only when it is sure and
// reads the raw screen text, as before r410, when it is not.
//
// No firmware dependency: the same code runs in tests/CharonScreenModelTests.cpp
// on recorded screens (tests/charon-screens/*.grid).
//
// Sources for the per-program rules are named at each rule; the full list with
// URLs is in tests/charon-r410-reader-results.txt.
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace hakefi::screenmodel {
constexpr size_t maxItems=64,maxWindows=4,maxHelp=8;
constexpr uint8_t none8=0xff;
enum class Manager : uint8_t {unknown=0,grub,systemdBoot,refind,limine,openCore,clover,syslinux,ipxe,windows,memoryDiagnostic,shell,memtest,veraCrypt,firmware,count};
enum class Screen : uint8_t {unknown=0,menu,editor,commandLine,help,error,message,progress,options,pager,count};
// Item flags.
constexpr uint8_t itemSelected=1,itemDefault=2,itemSubmenu=4,itemDisabled=8,itemTool=16,itemChecked=32,itemUnchecked=64;
struct Item {uint8_t row=0,left=0,right=0,indent=0,flags=0;};
struct List {
    bool found=false,sure=false;
    uint8_t top=0,bottom=0,left=0,right=0;
    uint8_t count=0,selected=none8,defaultIndex=none8,depth=0;
    bool moreAbove=false,moreBelow=false;   // the program shows that the list scrolls
    Item items[maxItems]={};
};
struct Countdown {bool found=false,sure=false;uint8_t row=0;uint16_t seconds=0;};
struct Edit {bool found=false,sure=false,multiline=false;uint8_t row=0,left=0,right=0,caretRow=0,caretCol=0,top=0,bottom=0;};
struct Progress {bool found=false,sure=false;uint8_t row=0,percent=0;};
enum class Alert : uint8_t {none=0,information,question,warning,error};
struct Message {bool found=false,sure=false;Alert kind=Alert::none;uint8_t top=0,bottom=0;};
struct Window {uint8_t top=0,bottom=0,left=0,right=0;uint8_t titleLeft=0,titleRight=0;};
struct Model {
    Manager manager=Manager::unknown;Screen screen=Screen::unknown;
    uint8_t titleRow=none8;bool titleSure=false;
    uint8_t help[maxHelp]={};uint8_t helpCount=0;bool helpSure=false;
    List list;Countdown countdown;Edit edit;Progress progress;Message message;
    Window windows[maxWindows]={};uint8_t windowCount=0;
};
// The reader's grid. attr: bit 7 = the cell was drawn with a known attribute,
// bits 4-6 background, bits 0-3 foreground (the UEFI console attribute).
struct View {
    const char16_t* text=nullptr;const uint8_t* attr=nullptr;size_t stride=0;
    size_t rows=0,columns=0,cursorRow=0,cursorCol=0;bool cursorShown=false;uint8_t majorityBg=0;
    // Icon pickers (rEFInd, Clover, OpenCanopy): what the picture analysis found; 0 = none.
    uint8_t iconCount=0,iconSelected=none8;
    // The program counted down without a key since it started (the reader watched the number fall).
    bool countingDown=false;
    char16_t Ch(size_t r,size_t c) const {const char16_t ch=text[r*stride+c];return ch?ch:u' ';}
    uint8_t At(size_t r,size_t c) const {return attr[r*stride+c];}
    bool Lit(size_t r,size_t c) const {const uint8_t a=At(r,c);return (a&0x80) && ((a>>4)&7)!=majorityBg;}
};
inline bool Frame(char16_t ch) {return ch>=0x2500 && ch<=0x257f;}
inline bool Blank(char16_t ch) {return ch==u' ' || !ch || Frame(ch);}
inline bool Digit(char16_t ch) {return ch>=u'0' && ch<=u'9';}
inline bool Letter(char16_t ch) {return (ch>=u'A' && ch<=u'Z') || (ch>=u'a' && ch<=u'z') || (ch>=0xc0 && ch<0x2000);}
inline char16_t Lower(char16_t ch) {return ch>=u'A' && ch<=u'Z'?char16_t(ch+32):ch;}
// First and one-past-last column of a row's text (frames and blanks apart); false when the row is empty.
inline bool Bounds(const View& v,size_t r,size_t& start,size_t& end,size_t from=0,size_t to=SIZE_MAX) {
    if(to>v.columns)to=v.columns;
    start=from;while(start<to && Blank(v.Ch(r,start)))++start;
    if(start>=to)return false;
    end=to;while(end>start && Blank(v.Ch(r,end-1)))--end;
    return true;
}
// Case-insensitive search of ASCII `word` in a row; returns the column or SIZE_MAX.
inline size_t FindIn(const View& v,size_t r,const char16_t* word,size_t from=0) {
    size_t n=0;while(word[n])++n;
    for(size_t c=from;c+n<=v.columns;++c) {size_t k=0;while(k<n && Lower(v.Ch(r,c+k))==Lower(word[k]))++k;if(k==n)return c;}
    return SIZE_MAX;
}
inline bool Find(const View& v,const char16_t* word,size_t* row=nullptr,size_t* column=nullptr) {
    for(size_t r=0;r<v.rows;++r) {const size_t c=FindIn(v,r,word);if(c!=SIZE_MAX) {if(row)*row=r;if(column)*column=c;return true;}}
    return false;
}
// The number that starts at or after column c of row r (SIZE_MAX: none); `after` gets the column behind it.
inline size_t NumberAt(const View& v,size_t r,size_t c,size_t* after=nullptr) {
    while(c<v.columns && !Digit(v.Ch(r,c)))++c;
    if(c>=v.columns)return SIZE_MAX;
    size_t n=0,digits=0;while(c<v.columns && Digit(v.Ch(r,c)) && digits<6) {n=n*10+size_t(v.Ch(r,c)-u'0');++c;++digits;}
    if(after)*after=c;
    return n;
}
// ---- generic recognisers ---------------------------------------------------
inline bool TopLeft(char16_t ch) {return ch==0x250c || ch==0x2554 || ch==0x256d || ch==0x2552 || ch==0x2553;}
inline bool TopRight(char16_t ch) {return ch==0x2510 || ch==0x2557 || ch==0x256e || ch==0x2555 || ch==0x2556;}
inline bool BottomLeft(char16_t ch) {return ch==0x2514 || ch==0x255a || ch==0x2570 || ch==0x2558 || ch==0x2559;}
inline void FindWindows(const View& v,Model& m) {
    m.windowCount=0;
    for(size_t r=0;r+2<v.rows && m.windowCount<maxWindows;++r)for(size_t c=0;c+3<v.columns && m.windowCount<maxWindows;++c) {
        if(!TopLeft(v.Ch(r,c)))continue;
        size_t right=c+1;while(right<v.columns && !TopRight(v.Ch(r,right)) && (Frame(v.Ch(r,right)) || right<c+v.columns))++right;
        // the top border: frame characters with an optional title between them
        size_t e=c+1;while(e<v.columns && !TopRight(v.Ch(r,e)))++e;
        size_t bottom=r+1;while(bottom<v.rows && !BottomLeft(v.Ch(bottom,c)) && (Frame(v.Ch(bottom,c)) || v.Ch(bottom,c)==u'|'))++bottom;
        if(bottom>=v.rows || !BottomLeft(v.Ch(bottom,c)) || bottom<r+2)continue;
        // GRUB's console menu draws no right corner into the last column of some consoles: the frame may end at the row's end.
        if(e>=v.columns) {e=v.columns;while(e>c+2 && !Frame(v.Ch(r,e-1)))--e;if(e<=c+3)continue;--e;}
        Window w;w.top=uint8_t(r);w.bottom=uint8_t(bottom);w.left=uint8_t(c);w.right=uint8_t(e);
        size_t ts=0,te=0;if(Bounds(v,r,ts,te,c+1,e)) {w.titleLeft=uint8_t(ts);w.titleRight=uint8_t(te);}
        m.windows[m.windowCount++]=w;
        (void)right;
    }
}
inline const Window* WindowAt(const Model& m,size_t r,size_t c) {
    const Window* best=nullptr;
    for(size_t i=0;i<m.windowCount;++i) {const Window& w=m.windows[i];
        if(r>w.top && r<w.bottom && c>w.left && c<=w.right && (!best || w.top>=best->top))best=&w;}
    return best;
}
// A selection mark in front of a row's text: '*', '>', "=>", "->", "»", "►" followed by text.
inline size_t LeadMark(const View& v,size_t r,size_t c) {
    const char16_t a=v.Ch(r,c),b=c+1<v.columns?v.Ch(r,c+1):u' ';
    if((a==u'=' || a==u'-') && b==u'>')return 2;
    if(a==u'*' || a==u'>' || a==0xbb || a==0x25ba || a==0x25b6 || a==0x25b8 || a==0x2192)return 1;
    return 0;
}
struct RowLook {bool text=false,lit=false,wholeLit=false;size_t start=0,end=0,litCells=0,mark=0;};
inline RowLook Look(const View& v,size_t r,size_t from=0,size_t to=SIZE_MAX) {
    RowLook k;if(to>v.columns)to=v.columns;
    if(!Bounds(v,r,k.start,k.end,from,to))return k;
    k.text=true;size_t textCells=0,litText=0;
    for(size_t c=k.start;c<k.end;++c) {if(v.Ch(r,c)!=u' ')++textCells;if(v.Lit(r,c)) {++k.litCells;if(v.Ch(r,c)!=u' ')++litText;}}
    k.lit=litText>0;k.wholeLit=textCells && litText==textCells;
    k.mark=LeadMark(v,r,k.start);
    if(k.mark && (k.start+k.mark>=k.end))k.mark=0;
    return k;
}
// The list around one selected row: rows above and below that start in the same column (or, for centred
// menus, have the same centre), at most one blank row apart, inside the window that holds them.
inline void GrowList(const View& v,size_t row,size_t from,size_t to,size_t top,size_t bottom,List& list) {
    list=List{};
    const RowLook sel=Look(v,row,from,to);
    if(!sel.text)return;
    auto textStart=[&](size_t r,const RowLook& k) {size_t s=k.start+k.mark;while(s<k.end && v.Ch(r,s)==u' ')++s;return s;};
    const size_t textColumn=textStart(row,sel),centre=textColumn+sel.end;
    // 1 aligned, 0 blank, -1 other text
    auto kind=[&](size_t r,RowLook& k,size_t& s)->int {
        k=Look(v,r,from,to);
        if(!k.text) {   // a drawn rule (systemd-boot's line under its entries) ends a list; a blank row is a gap
            size_t rule=0;for(size_t c=from;c<to && c<v.columns;++c)if(Frame(v.Ch(r,c)))++rule;
            return rule>=3?-1:0;}
        s=textStart(r,k);
        if((s>textColumn?s-textColumn:textColumn-s)<=2)return 1;
        const size_t c2=s+k.end;
        return (c2>centre?c2-centre:centre-c2)<=2 && sel.start>from+2?1:-1;
    };
    size_t first=row;
    for(;;) {bool moved=false;
        for(size_t k=1;k<=2 && first>=top+k;++k) {RowLook look;size_t s=0;const int what=kind(first-k,look,s);
            if(what==1) {first-=k;moved=true;break;}
            if(what<0)break;}
        if(!moved)break;}
    size_t count=0;
    for(size_t r=first,gap=0;r<=bottom && r<v.rows && count<maxItems;++r) {
        RowLook k;size_t s=0;const int what=r==row?1:kind(r,k,s);
        if(r==row) {k=sel;s=textColumn;}
        if(what==1) {
            Item& it=list.items[count];it.row=uint8_t(r);it.left=uint8_t(s);it.right=uint8_t(k.end);it.flags=0;
            if(r==row) {it.flags|=itemSelected;list.selected=uint8_t(count);}
            ++count;gap=0;list.bottom=uint8_t(r);
        } else if(what==0) {if(++gap>1)break;} else break;
    }
    list.count=uint8_t(count);list.top=uint8_t(first);list.left=uint8_t(textColumn);
    size_t right=0;for(size_t i=0;i<count;++i)if(list.items[i].right>right)right=list.items[i].right;
    list.right=uint8_t(right);list.found=count>=1;
}
// Generic list: a block of aligned rows of which exactly one is highlighted or carries the only selection mark.
inline void FindList(const View& v,Model& m,bool marksFirst=true) {
    // inside the innermost (last drawn) window first, then the whole screen
    for(int pass=int(m.windowCount);pass>=0 && !m.list.found;--pass) {
        size_t top=0,bottom=v.rows?v.rows-1:0,from=0,to=v.columns;
        if(pass>0) {const Window& w=m.windows[pass-1];top=w.top+1;bottom=w.bottom-1;from=w.left+1;to=w.right;}
        bool lit[64]={},marked[64]={};
        for(size_t r=top;r<=bottom && r<v.rows && r<64;++r) {
            const RowLook k=Look(v,r,from,to);if(!k.text)continue;
            bool word=false;for(size_t c=k.start;c<k.end && !word;++c)word=Letter(v.Ch(r,c)) || Digit(v.Ch(r,c));
            if(!word)continue;
            lit[r]=k.lit;marked[r]=k.mark!=0;
        }
        List best;size_t accepted=0;
        for(int byMark=marksFirst?1:0;byMark>=0 && !accepted;--byMark)for(size_t r=top;r<=bottom && r<v.rows && r<64;++r) {
            if(!(byMark?marked[r]:lit[r]))continue;
            List list;GrowList(v,r,from,to,top,bottom,list);
            if(!list.found || list.count<2)continue;
            // the only one of its kind in its block
            size_t same=0;for(size_t i=0;i<list.count;++i) {const size_t q=list.items[i].row;if(q<64 && (byMark?marked[q]:lit[q]))++same;}
            if(same!=1)continue;
            ++accepted;if(list.count>best.count)best=list;
        }
        if(!accepted)continue;
        best.sure=accepted==1;   // two blocks that each look like a menu: the reader does not choose between them
        m.list=best;
    }
}
inline bool HasAny(const View& v,size_t r,const char16_t* const* words,size_t count) {
    for(size_t i=0;i<count;++i)if(FindIn(v,r,words[i])!=SIZE_MAX)return true;
    return false;
}
// Countdown: a row that names an automatic start and holds a number of seconds.
inline void FindCountdown(const View& v,Model& m) {
    static const char16_t* const cue[]={u"automatically in",u"boot in ",u"booting in",u"automatic boot in",u"seconds until",u"will boot in",u"will be started automatically",u"boots automatically",
        u"timeout",u"automatycznie za",u"uruchomiony automatycznie",u"sekund do",u"remaining",u"to continue in"};
    for(size_t r=0;r<v.rows;++r) {
        if(!HasAny(v,r,cue,sizeof(cue)/sizeof(cue[0])))continue;
        // the number of seconds: the last number on the row (Windows: "...automatically: 28"; GRUB "in 5s."; "Boot in 9 s.")
        size_t value=SIZE_MAX,c=0,after=0;
        for(;;) {const size_t n=NumberAt(v,r,c,&after);if(n==SIZE_MAX)break;
            // not a part of a word or a version ("Limine 11.4.1")
            size_t s=after;while(s>c && Digit(v.Ch(r,s-1)))--s;
            const char16_t before=s?v.Ch(r,s-1):u' ',next=after<v.columns?v.Ch(r,after):u' ';
            if(before!=u'.' && next!=u'.' ? true : (next==u'.' && (after+1>=v.columns || !Digit(v.Ch(r,after+1))) && before!=u'.'))value=n;
            c=after;}
        if(value==SIZE_MAX || value>3600)continue;
        m.countdown.found=true;m.countdown.row=uint8_t(r);m.countdown.seconds=uint16_t(value);
        // sure only with a time unit or a cue that cannot be anything else
        static const char16_t* const unit[]={u"second",u"sekund",u" s.",u"s.",u"automatically",u"automatycznie",u"seconds until"};
        m.countdown.sure=HasAny(v,r,unit,sizeof(unit)/sizeof(unit[0])) && FindIn(v,r,u"timeout set")==SIZE_MAX;
        return;
    }
}
inline void FindProgress(const View& v,Model& m) {
    for(size_t r=0;r<v.rows;++r)for(size_t c=1;c<v.columns;++c) {
        if(v.Ch(r,c)!=u'%' || !Digit(v.Ch(r,c-1)))continue;
        size_t s=c;while(s && Digit(v.Ch(r,s-1)))--s;
        const size_t n=NumberAt(v,r,s);
        if(n>100)continue;
        if(!m.progress.found || FindIn(v,r,u"overall")!=SIZE_MAX || FindIn(v,r,u"pass")!=SIZE_MAX) {m.progress.found=true;m.progress.row=uint8_t(r);m.progress.percent=uint8_t(n);m.progress.sure=true;}
    }
}
inline Alert AlertOf(const View& v,size_t r) {
    static const char16_t* const errors[]={u"error",u"failed",u"failure",u"fatal",u"cannot",u"can't",u"unable",u"not found",u"invalid",u"denied",u"corrupt",u"needs to be repaired",u"panic",
        u"b\x0142\x0105" u"d",u"nie mo\x017c" u"na",u"nie uda\x0142o",u"nie znaleziono"};
    static const char16_t* const warnings[]={u"warning",u"caution",(u"ostrze\x017c" u"enie"),u"uwaga"};
    static const char16_t* const questions[]={u"(y/n)",u"[y/n]",u"y/n",u"press any key",u"press enter to continue",u"'q' break",u"naci\x015bnij dowolny klawisz"};
    if(HasAny(v,r,errors,sizeof(errors)/sizeof(errors[0])))return Alert::error;
    if(HasAny(v,r,warnings,sizeof(warnings)/sizeof(warnings[0])))return Alert::warning;
    if(HasAny(v,r,questions,sizeof(questions)/sizeof(questions[0])))return Alert::question;
    return Alert::none;
}
// Help lines: rows outside the list that name keys.
inline bool KeyHintRow(const View& v,size_t r) {
    static const char16_t* const strong[]={u"press ",u"use the ",u"keys to",u"enter=",u"esc=",u"tab=",u"<esc>",u"<f1>",u"f1=",u"f1:",u"enter:",u"to edit",u"to boot",u"\x2191",u"\x2193",u"\x25b2",u"\x25bc",
        u"naci\x015bnij",u"u\x017cyj"};
    return HasAny(v,r,strong,sizeof(strong)/sizeof(strong[0]));
}
inline void FindHelp(const View& v,Model& m) {
    m.helpCount=0;
    for(size_t r=0;r<v.rows && m.helpCount<maxHelp;++r) {
        if(m.list.found && r>=m.list.top && r<=m.list.bottom)continue;
        if(m.countdown.found && r==m.countdown.row)continue;
        size_t s,e;if(!Bounds(v,r,s,e))continue;
        if(KeyHintRow(v,r)) {
            m.help[m.helpCount++]=uint8_t(r);
            // a wrapped hint: following rows that continue the sentence (start in the same column, no blank row between)
            while(r+1<v.rows && m.helpCount<maxHelp) {size_t s2,e2;
                if(!Bounds(v,r+1,s2,e2) || s2!=s || (m.countdown.found && r+1==m.countdown.row) || KeyHintRow(v,r+1))break;
                const char16_t last=v.Ch(r,e-1);if(last==u'.' || last==u':')break;
                m.help[m.helpCount++]=uint8_t(++r);e=e2;}
        }
    }
    m.helpSure=m.helpCount>0;
}
// Edit field at the caret: a visible console cursor outside any list.
inline void FindEdit(const View& v,Model& m) {
    if(!v.cursorShown || v.cursorRow>=v.rows || v.cursorCol>v.columns)return;
    const size_t r=v.cursorRow;
    if(m.list.found && m.list.sure && r>=m.list.top && r<=m.list.bottom)return;
    Edit e;e.found=true;e.row=e.caretRow=uint8_t(r);e.caretCol=uint8_t(v.cursorCol<v.columns?v.cursorCol:v.columns-1);
    // the prompt ends at the last "> ", "$ ", "# " or ": " before the caret
    size_t s=0,end=0;const bool any=Bounds(v,r,s,end);
    // Many programs leave the console cursor "visible" somewhere while they show a menu: a caret counts only
    // inside a window or with text in front of it on its row, not further than two cells behind that text.
    if(!WindowAt(m,r,v.cursorCol) && (!any || s>=v.cursorCol || v.cursorCol>end+2))return;
    size_t left=any?s:v.cursorCol;
    for(size_t c=any?s:0;c+1<=v.cursorCol && c+1<v.columns;++c) {const char16_t a=v.Ch(r,c),b=v.Ch(r,c+1);
        if((a==u'>' || a==u'$' || a==u'#' || a==u':') && (b==u' ' || c+1==v.cursorCol))left=c+1+(b==u' '?1:0);}
    if(left>v.cursorCol)left=v.cursorCol;
    // the field's text ends where three blank cells follow (a hint may stand further right on the same row)
    if(any) {size_t stop=left;size_t gap=0;for(size_t c=left;c<end;++c) {if(v.Ch(r,c)==u' ') {if(++gap>=3 && c>=v.cursorCol)break;} else {gap=0;stop=c+1;}}if(stop>=v.cursorCol || stop>left)end=stop>left?stop:left;}
    e.left=uint8_t(left);e.right=uint8_t(any && end>left?end:left);
    if(e.right<e.caretCol)e.right=e.caretCol;
    e.top=e.bottom=uint8_t(r);
    if(const Window* w=WindowAt(m,r,v.cursorCol)) {e.multiline=w->bottom>w->top+2;e.top=uint8_t(w->top+1);e.bottom=uint8_t(w->bottom-1);if(e.multiline) {e.left=uint8_t(w->left+1);e.right=uint8_t(w->right);}}
    e.sure=true;m.edit=e;
}
inline void FindMessage(const View& v,Model& m) {
    Alert worst=Alert::none;size_t top=0,bottom=0;
    for(size_t r=0;r<v.rows;++r) {
        if(m.list.found && r>=m.list.top && r<=m.list.bottom)continue;
        bool help=false;for(size_t i=0;i<m.helpCount;++i)if(m.help[i]==r)help=true;
        const Alert a=AlertOf(v,r);
        if(a==Alert::none || (help && a==Alert::question))continue;
        if(uint8_t(a)>uint8_t(worst)) {worst=a;top=bottom=r;} else if(a==worst)bottom=r;
    }
    if(worst==Alert::none)return;
    m.message.found=true;m.message.kind=worst;m.message.top=uint8_t(top);m.message.bottom=uint8_t(bottom);
    m.message.sure=worst==Alert::error || worst==Alert::warning;   // a question is a guess from words: raw reading decides
}
inline void FindTitle(const View& v,Model& m) {
    for(size_t r=0;r<v.rows;++r) {
        size_t s,e;if(!Bounds(v,r,s,e))continue;
        if(m.list.found && r>=m.list.top)return;
        m.titleRow=uint8_t(r);
        // a title stands alone: centred, or a full-width bar in another colour, with a blank row or a frame after it
        size_t s2,e2;const bool alone=r+1>=v.rows || !Bounds(v,r+1,s2,e2);
        const bool centred=s>4 && (s+e>v.columns-6) && (s+e<v.columns+6);
        size_t bar=0;for(size_t c=0;c<v.columns;++c)if(v.Lit(r,c))++bar;
        m.titleSure=(alone && centred) || bar*10>=v.columns*8;
        return;
    }
}
inline void Generic(const View& v,Model& m,bool marksFirst=true) {
    FindWindows(v,m);FindList(v,m,marksFirst);FindCountdown(v,m);FindProgress(v,m);FindHelp(v,m);FindEdit(v,m);FindMessage(v,m);FindTitle(v,m);
    if(m.list.found && m.list.sure)m.screen=Screen::menu;
    else if(m.edit.found)m.screen=m.edit.multiline?Screen::editor:Screen::commandLine;
    else if(m.message.found && m.message.kind==Alert::error)m.screen=Screen::error;
    else if(m.progress.found)m.screen=Screen::progress;
}
// ---- per-program recognisers ----------------------------------------------
// Each one only runs for its program (named by the image the reader saw start,
// or by text only that program shows) and may: confirm elements (sure), mark
// submenus and the default entry, name the screen, or withdraw an element
// the generic pass got wrong for this program.
inline void MarkSubmenuByTrail(const View& v,List& list,char16_t mark) {
    for(size_t i=0;i<list.count;++i) {Item& it=list.items[i];
        if(it.right>it.left+2 && v.Ch(it.row,it.right-1)==mark && v.Ch(it.row,it.right-2)==u' ') {it.flags|=itemSubmenu;--it.right;while(it.right>it.left && v.Ch(it.row,it.right-1)==u' ')--it.right;}}
}
inline void DefaultFromCountdown(const View& v,Model& m) {
    // While a program counts down untouched, the highlighted entry is the one that starts by itself: its default.
    if(v.countingDown && m.countdown.found && m.countdown.sure && m.list.found && m.list.sure && m.list.selected!=none8 && m.list.defaultIndex==none8) {
        m.list.defaultIndex=m.list.selected;m.list.items[m.list.selected].flags|=itemDefault;}
}
// GNU GRUB, text console and gfxterm (grub-core/normal/menu_text.c, menu_entry.c, normal/main.c).
inline void Grub(const View& v,Model& m) {
    size_t row=0;
    const bool banner=Find(v,u"GNU GRUB",&row) || Find(v,u"GRUB  version",&row);
    if(banner) {m.titleRow=uint8_t(row);m.titleSure=true;}
    // the entry editor: "Minimum Emacs-like screen editing is supported."
    if(Find(v,u"Emacs-like screen editing")) {
        m.screen=Screen::editor;m.list=List{};
        if(m.windowCount) {const Window& w=m.windows[0];Edit e;e.found=true;e.multiline=true;e.top=uint8_t(w.top+1);e.bottom=uint8_t(w.bottom-1);e.left=uint8_t(w.left+1);e.right=w.right;
            e.caretRow=uint8_t(v.cursorRow);e.caretCol=uint8_t(v.cursorCol);e.row=e.caretRow;e.sure=v.cursorShown && v.cursorRow>w.top && v.cursorRow<w.bottom;m.edit=e;}
        m.countdown=Countdown{};return;
    }
    // the command line: "Minimal BASH-like line editing is supported." and the prompt "grub> " / "grub rescue> "
    {size_t pr=0,pc=0;
     if(v.cursorShown && v.cursorRow<v.rows && (pc=FindIn(v,v.cursorRow,u"grub>"))!=SIZE_MAX) {pr=v.cursorRow;
        m.screen=Screen::commandLine;m.list=List{};m.countdown=Countdown{};m.helpCount=0;m.helpSure=false;
        Edit e;e.found=e.sure=true;e.row=e.caretRow=e.top=e.bottom=uint8_t(pr);e.left=uint8_t(pc+6<v.columns?pc+6:pc);e.caretCol=uint8_t(v.cursorCol);
        size_t s,en;e.right=uint8_t(Bounds(v,pr,s,en) && en>e.left?en:e.left);if(e.right<e.caretCol)e.right=e.caretCol;m.edit=e;return;}
     if(v.cursorShown && v.cursorRow<v.rows && FindIn(v,v.cursorRow,u"rescue>")!=SIZE_MAX) {m.screen=Screen::commandLine;m.list=List{};return;}}
    if(!m.list.found)return;
    // the menu: entries inside the frame; one highlighted (inverse) or marked with '*'
    if(m.windowCount && m.list.top>m.windows[0].top && m.list.bottom<m.windows[0].bottom) {
        const Window& w=m.windows[0];
        // GRUB shows scroll arrows in the frame's right column when entries are above or below.
        for(size_t r=w.top+1;r<w.bottom;++r) {const char16_t ch=v.Ch(r,w.right>0?w.right-1:0),ch2=v.Ch(r,w.right);
            if(ch==0x2191 || ch==0x25b2 || ch2==0x2191 || ch2==0x25b2)m.list.moreAbove=true;
            if(ch==0x2193 || ch==0x25bc || ch2==0x2193 || ch2==0x25bc)m.list.moreBelow=true;}
        m.list.sure=m.list.selected!=none8;
    }
    // A submenu entry is drawn like any other entry by GRUB's text menu (menu_text.c prints the title only), so a
    // submenu is known only from its title when the configuration was read; the reader adds that, not this model.
    // menu_text.c adds "ESC to return previous menu." to the help text only in a nested menu (a submenu or a menu
    // loaded with configfile).
    if(Find(v,u"ESC to return previous menu"))m.list.depth=1;
    m.screen=Screen::menu;DefaultFromCountdown(v,m);
}
// systemd-boot (src/boot/boot.c): centred entries, the selected one in inverse; "Boot in %u s." under the list.
inline void SystemdBoot(const View& v,Model& m) {
    // boot.c: the selection is shown by colour only; "=>" (or a triangle) in front of a title marks the default
    // entry set through the EFI variable, not the selection. Build() therefore looks for highlighted rows only.
    if(m.list.found && m.list.selected!=none8) {m.list.sure=true;m.screen=Screen::menu;
        for(size_t i=0;i<m.list.count;++i) {const Item& it=m.list.items[i];size_t s,e;
            if(Bounds(v,it.row,s,e) && s<it.left && LeadMark(v,it.row,s)) {m.list.items[i].flags|=itemDefault;m.list.defaultIndex=uint8_t(i);}}}
    if(m.countdown.found && FindIn(v,m.countdown.row,u"boot in")!=SIZE_MAX)m.countdown.sure=true;
    DefaultFromCountdown(v,m);
}
// Limine: "[+]" / "[-]" directories, sub-entries indented.
inline void Limine(const View& v,Model& m) {
    // the entry editor (menu.c config_entry_editor): help line "ESC Discard and Exit    F10/CTRL+X Boot", text in a box
    if(Find(v,u"Discard and Exit")) {m.list=List{};m.countdown=Countdown{};m.screen=Screen::editor;
        if(m.windowCount) {const Window& w=m.windows[0];Edit e;e.found=true;e.multiline=true;e.top=uint8_t(w.top+1);e.bottom=uint8_t(w.bottom-1);e.left=uint8_t(w.left+1);e.right=w.right;m.edit=e;}
        return;}
    if(!m.list.found)return;
    size_t minLeft=255;for(size_t i=0;i<m.list.count;++i)if(m.list.items[i].left<minLeft)minLeft=m.list.items[i].left;
    for(size_t i=0;i<m.list.count;++i) {Item& it=m.list.items[i];
        if(it.left+2<v.columns && v.Ch(it.row,it.left)==u'[' && (v.Ch(it.row,it.left+1)==u'+' || v.Ch(it.row,it.left+1)==u'-') && v.Ch(it.row,it.left+2)==u']')it.flags|=itemSubmenu;
        it.indent=uint8_t((it.left-minLeft)/2);}
    m.screen=Screen::menu;DefaultFromCountdown(v,m);
}
// OpenCore's built-in picker: "OpenCore Boot Menu", "Choose the Operating System:", entries "N. name" with a
// leading mark on the chosen one.
inline void OpenCore(const View& v,Model& m) {
    size_t row=0;if(Find(v,u"OpenCore Boot Menu",&row)) {m.titleRow=uint8_t(row);m.titleSure=true;}
    // BuiltinPicker.c: column 0 of an entry row is '*' (the default entry while the timeout runs), '>' (selected),
    // '+' (selected with the set-default key held) or a space; then "<index>. <name>". No number counts down:
    // the status after "Choose the Operating System: " reads "Timeout" when it ran out.
    {size_t rows[maxItems],n=0,selected=SIZE_MAX,def=SIZE_MAX;
     for(size_t r=0;r<v.rows && n<maxItems;++r) {
        size_t s,e;if(!Bounds(v,r,s,e))continue;
        size_t c=s;const char16_t mark=v.Ch(r,c);
        if(mark==u'*' || mark==u'>' || mark==u'+') {++c;while(c<e && v.Ch(r,c)==u' ')++c;}
        // the index: one digit or capital letter, then ". "
        if(c+2>=e || !(Digit(v.Ch(r,c)) || (v.Ch(r,c)>=u'A' && v.Ch(r,c)<=u'Z')) || v.Ch(r,c+1)!=u'.' || v.Ch(r,c+2)!=u' ')continue;
        if(n && r>rows[n-1]+2) {if(n>=2)break;n=0;selected=def=SIZE_MAX;}   // drawn in graphics the rows may stand a blank row apart
        if(mark==u'>' || mark==u'+')selected=n;
        if(mark==u'*')def=n;
        rows[n++]=r;
     }
     if(n>=1 && (selected!=SIZE_MAX || def!=SIZE_MAX)) {
        List list;list.found=true;list.count=uint8_t(n);list.top=uint8_t(rows[0]);list.bottom=uint8_t(rows[n-1]);
        for(size_t i=0;i<n;++i) {size_t s,e;Bounds(v,rows[i],s,e);size_t c=s;if(LeadMark(v,rows[i],c) || v.Ch(rows[i],c)==u'+') {++c;while(c<e && v.Ch(rows[i],c)==u' ')++c;}
            list.items[i].row=uint8_t(rows[i]);list.items[i].left=uint8_t(c);list.items[i].right=uint8_t(e);}
        if(selected==SIZE_MAX)selected=def;   // untouched: the default entry is the one Enter or the timeout starts
        list.selected=uint8_t(selected);list.items[selected].flags|=itemSelected;
        if(def!=SIZE_MAX) {list.defaultIndex=uint8_t(def);list.items[def].flags|=itemDefault;}
        list.left=list.items[0].left;list.right=uint8_t(v.columns);list.sure=true;m.list=list;m.screen=Screen::menu;
        m.countdown=Countdown{};return;
     }}
    if(m.list.found && m.list.selected!=none8) {
        // every entry starts with its number
        size_t numbered=0;for(size_t i=0;i<m.list.count;++i) {const Item& it=m.list.items[i];if(Digit(v.Ch(it.row,it.left)))++numbered;}
        m.list.sure=numbered==m.list.count;m.screen=Screen::menu;
    }
    DefaultFromCountdown(v,m);
}
// Windows Boot Manager's own screens (drawn by bootmgfw.efi before ExitBootServices).
inline void Windows(const View& v,Model& m) {
    size_t row=0;
    if(Find(v,u"Windows Boot Manager",&row) || Find(v,u"Mened\x017c" u"er rozruchu",&row)) {m.titleRow=uint8_t(row);m.titleSure=true;}
    if(Find(v,u"needs to be repaired") || Find(v,u"Recovery",&row)) {
        m.screen=Screen::error;m.message.found=m.message.sure=true;m.message.kind=Alert::error;
        size_t first=SIZE_MAX,last=0;for(size_t r=0;r<v.rows;++r) {size_t s,e;if(Bounds(v,r,s,e)) {if(first==SIZE_MAX)first=r;last=r;}}
        m.message.top=uint8_t(first==SIZE_MAX?0:first);m.message.bottom=uint8_t(last);m.list=List{};return;
    }
    // Seen on the recorded screens of bootmgfw.efi (10.0.26100): "Choose an operating system to start, or press TAB to
    // select a tool:", the entries two rows apart, "Tools:" with its own list (TAB moves the highlight there),
    // "Seconds until the highlighted choice will be started automatically: N", the bar "ENTER=Choose TAB=Menu ESC=Cancel".
    size_t tools=SIZE_MAX;Find(v,u"Tools:",&tools);if(tools==SIZE_MAX)Find(v,u"Narz\x0119" u"dzia:",&tools);
    if(!m.list.found && tools!=SIZE_MAX) {
        // the highlight is in the tools list, which may hold one tool only
        size_t lit=SIZE_MAX,count=0;
        for(size_t r=tools+1;r<v.rows;++r) {const RowLook k=Look(v,r);if(k.text && k.lit && k.litCells*2>=k.end-k.start) {lit=r;++count;}}
        if(count==1) {List list;GrowList(v,lit,0,v.columns,tools+1,v.rows-1,list);
            // the key bar at the bottom is no tool: only rows that start where the highlighted one starts
            size_t n=0;for(size_t i=0;i<list.count;++i)if(list.items[i].left==list.items[list.selected].left) {if(i==list.selected)list.selected=uint8_t(n);list.items[n++]=list.items[i];}
            list.count=uint8_t(n);
            if(n) {for(size_t i=0;i<n;++i)list.items[i].flags|=itemTool;list.bottom=list.items[n-1].row;list.found=list.sure=true;m.list=list;m.screen=Screen::menu;}}
    }
    if(m.list.found && m.list.selected!=none8) {
        // "Tools:" starts a second group in the same column: entries below it are tools
        if(tools!=SIZE_MAX)for(size_t i=0;i<m.list.count;++i)if(m.list.items[i].row>tools)m.list.items[i].flags|=itemTool;
        MarkSubmenuByTrail(v,m.list,u'>');
        m.list.sure=true;m.screen=Screen::menu;
    }
    if(m.countdown.found && (FindIn(v,m.countdown.row,u"seconds until")!=SIZE_MAX || FindIn(v,m.countdown.row,u"sekund")!=SIZE_MAX))m.countdown.sure=true;
    DefaultFromCountdown(v,m);
}
// Windows Memory Diagnostic: "Running test pass  1 of  2: 04% complete", "Overall test status: 02% complete".
inline void MemoryDiagnostic(const View& v,Model& m) {
    size_t row=0;
    if(Find(v,u"Overall test status",&row) || Find(v,u"test pass",&row)) {size_t c=0;while(c<v.columns && v.Ch(row,c)!=u'%')++c;
        if(c<v.columns && c && Digit(v.Ch(row,c-1))) {size_t s=c;while(s && Digit(v.Ch(row,s-1)))--s;m.progress.found=m.progress.sure=true;m.progress.row=uint8_t(row);m.progress.percent=uint8_t(NumberAt(v,row,s));}
        if(!m.list.found)m.screen=Screen::progress;}
}
// UEFI Shell (Shell Specification 2.2; edk2 ShellPkg): prompt "Shell> " or "FSn:\path> " at the caret;
// "Press ENTER to continue or 'Q' break:" when output is paged with -b.
inline void Shell(const View& v,Model& m) {
    m.list=List{};m.countdown=Countdown{};
    m.helpCount=0;m.helpSure=false;   // a terminal: "press" in scrolled output is no help line of the screen
    size_t row=0;
    if(v.cursorShown && v.cursorRow<v.rows && FindIn(v,v.cursorRow,u"'Q' break")!=SIZE_MAX) {m.screen=Screen::pager;m.message.found=m.message.sure=true;m.message.kind=Alert::question;m.message.top=m.message.bottom=uint8_t(v.cursorRow);m.edit=Edit{};return;}
    if(Find(v,u"Press ESC in",&row) && FindIn(v,row,u"seconds to skip")!=SIZE_MAX) {m.countdown.found=m.countdown.sure=true;m.countdown.row=uint8_t(row);m.countdown.seconds=uint16_t(NumberAt(v,row,FindIn(v,row,u"ESC in")));}
    if(m.edit.found && !m.edit.multiline)m.screen=Screen::commandLine;
}
// Memtest86+ (app/display.c): "Pass  N%" and "Test  N%" bars; errors counted on the right.
inline void Memtest(const View& v,Model& m) {
    m.list=List{};m.edit=Edit{};
    size_t row=0,col=0;
    if(Find(v,u"Pass ",&row,&col)) {size_t c=col;while(c<v.columns && v.Ch(row,c)!=u'%')++c;
        if(c<v.columns && Digit(v.Ch(row,c-1))) {size_t s=c;while(s && Digit(v.Ch(row,s-1)))--s;m.progress.found=m.progress.sure=true;m.progress.row=uint8_t(row);m.progress.percent=uint8_t(NumberAt(v,row,s));m.screen=Screen::progress;}}
}
// Syslinux menu.c32 / vesamenu.c32 and iPXE menus: a framed or plain list, one row in another colour;
// "Automatic boot in N seconds..." (Syslinux MENU AUTOBOOT default text).
inline void Syslinux(const View& v,Model& m) {
    if(m.list.found && m.list.selected!=none8) {m.list.sure=true;m.screen=Screen::menu;MarkSubmenuByTrail(v,m.list,u'>');}
    DefaultFromCountdown(v,m);
}
inline void Ipxe(const View& v,Model& m) {
    if(v.cursorShown && v.cursorRow<v.rows && FindIn(v,v.cursorRow,u"iPXE>")!=SIZE_MAX) {m.list=List{};m.screen=Screen::commandLine;m.helpCount=0;m.helpSure=false;return;}
    // hci/tui/settings_ui.c ("config"): title "iPXE configuration settings", the rows are scopes ("name/", bright)
    // and settings ("name..... value"), "..." where the list goes on beyond the screen. No gaps here: every row
    // can be selected. The count of a list that scrolls is not known, so the model says so (moreAbove/moreBelow).
    if(Find(v,u"Ctrl-X - exit configuration") || Find(v,u"configuration settings")) {
        if(m.list.found && m.list.selected!=none8) {
            List kept=m.list;size_t n=0;kept.selected=none8;
            for(size_t i=0;i<m.list.count;++i) {const Item& it=m.list.items[i];
                const bool dots=it.right==it.left+3 && v.Ch(it.row,it.left)==u'.' && v.Ch(it.row,it.left+1)==u'.' && v.Ch(it.row,it.left+2)==u'.';
                if(dots) {if(n)kept.moreBelow=true;else kept.moreAbove=true;continue;}
                if(it.flags&itemSelected)kept.selected=uint8_t(n);
                kept.items[n++]=it;}
            if(n && kept.selected!=none8) {kept.count=uint8_t(n);kept.top=kept.items[0].row;kept.bottom=kept.items[n-1].row;
                for(size_t i=0;i<n;++i) {Item& it=kept.items[i];if(it.right>it.left+1 && v.Ch(it.row,it.right-1)==u'/')it.flags|=itemSubmenu;}
                kept.sure=true;m.list=kept;m.screen=Screen::options;}
        }
        for(size_t r=0;r<v.rows;++r) {size_t s,e;if(Bounds(v,r,s,e)) {m.titleRow=uint8_t(r);m.titleSure=FindIn(v,r,u"configuration settings")!=SIZE_MAX;break;}}
        return;
    }
    if(m.list.found && m.list.selected!=none8) {
        // hci/tui/menu_ui.c: the title on the first row, items from the third; a gap (item --gap, netboot.xyz's
        // "Distributions:") is drawn in the separator colour and cannot be selected: it is no entry. The normal
        // colour is the one the menu's empty rows are painted in. While "choose --timeout" runs, the seconds left
        // stand as "(N)" at the right end of the selected row.
        size_t counts[16]={};
        for(size_t r=m.list.top;r<v.rows;++r) {size_t n=0;uint8_t fg=0;for(size_t c=0;c<v.columns;++c) {const uint8_t a=v.At(r,c);if((a&0x80) && ((a>>4)&7)==v.majorityBg) {++n;fg=uint8_t(a&15);}}
            if(n>=v.columns/2)++counts[fg];}
        uint8_t normal=0;for(uint8_t k=1;k<16;++k)if(counts[k]>counts[normal])normal=k;
        List kept=m.list;size_t n=0;kept.selected=none8;
        for(size_t i=0;i<m.list.count;++i) {const Item& it=m.list.items[i];const uint8_t a=v.At(it.row,it.left);
            const bool selected=(it.flags&itemSelected)!=0;
            if(!selected && counts[normal] && (a&0x80) && (a&15)!=normal)continue;
            if(selected)kept.selected=uint8_t(n);
            kept.items[n++]=it;}
        if(n>=1 && kept.selected!=none8) {kept.count=uint8_t(n);kept.top=kept.items[0].row;kept.bottom=kept.items[n-1].row;m.list=kept;}
        Item& sel=m.list.items[m.list.selected];
        if(sel.right>sel.left+4 && v.Ch(sel.row,sel.right-1)==u')') {
            size_t c=sel.right-1;while(c>sel.left && Digit(v.Ch(sel.row,c-1)))--c;
            if(c<sel.right-1 && c>sel.left+2 && v.Ch(sel.row,c-1)==u'(' && v.Ch(sel.row,c-2)==u' ' && v.Ch(sel.row,c-3)==u' ') {
                m.countdown.found=m.countdown.sure=true;m.countdown.row=sel.row;m.countdown.seconds=uint16_t(NumberAt(v,sel.row,c));
                size_t e=c-1;while(e>sel.left && v.Ch(sel.row,e-1)==u' ')--e;sel.right=uint8_t(e);}
        }
        if(m.list.top>=2) {size_t s,e;if(Bounds(v,m.list.top-1,s,e)) {} for(size_t r=0;r<m.list.top;++r)if(Bounds(v,r,s,e)) {m.titleRow=uint8_t(r);m.titleSure=true;break;}}
        m.list.sure=true;m.screen=Screen::menu;
    }
    DefaultFromCountdown(v,m);
}
// Icon pickers: the list is the row of icons the picture analysis counted; the text under it names the selected one.
inline void Icons(const View& v,Model& m) {
    if(!v.iconCount || v.iconSelected==none8 || v.iconSelected>=v.iconCount)return;
    if(m.list.found && m.list.sure)return;   // a text menu of the same program (rEFInd textonly, Clover's options)
    List list;list.found=true;list.sure=true;list.count=v.iconCount<maxItems?v.iconCount:uint8_t(maxItems);list.selected=v.iconSelected;
    for(size_t i=0;i<list.count;++i)list.items[i].row=none8;
    list.items[list.selected].flags|=itemSelected;
    m.list=list;m.screen=Screen::menu;DefaultFromCountdown(v,m);
}
inline Manager ManagerByName(const char16_t* name) {
    if(!name)return Manager::unknown;
    auto starts=[&](const char16_t* p) {size_t i=0;for(;p[i];++i)if(Lower(name[i])!=Lower(p[i]))return false;return true;};
    if(starts(u"GRUB") || starts(u"Ventoy"))return Manager::grub;
    if(starts(u"systemd-boot"))return Manager::systemdBoot;
    if(starts(u"rEFIn") || starts(u"rEFIt"))return Manager::refind;
    if(starts(u"Limine"))return Manager::limine;
    if(starts(u"OpenCore"))return Manager::openCore;
    if(starts(u"Clover"))return Manager::clover;
    if(starts(u"Syslinux"))return Manager::syslinux;
    if(starts(u"iPXE") || starts(u"netboot"))return Manager::ipxe;
    if(starts(u"Windows Boot") || starts(u"Mened"))return Manager::windows;
    if(starts(u"Memtest") || starts(u"MemTest"))return Manager::memtest;
    if(starts(u"VeraCrypt"))return Manager::veraCrypt;
    return Manager::unknown;
}
// What only one program shows: used when the reader did not see the image start (chain loaders, firmware menus).
inline Manager ManagerByScreen(const View& v) {
    if(Find(v,u"GNU GRUB") || Find(v,u"grub>") || Find(v,u"grub rescue>"))return Manager::grub;
    // the tool's own title is "Windows Memory Diagnostics Tool"; "Windows Memory Diagnostic" is its entry in the boot menu
    if(Find(v,u"Windows Memory Diagnostics Tool") || Find(v,u"Overall test status"))return Manager::memoryDiagnostic;
    if(Find(v,u"Windows Boot Manager") || Find(v,u"Your PC/Device needs to be repaired"))return Manager::windows;
    if(Find(v,u"OpenCore Boot Menu"))return Manager::openCore;
    if(Find(v,u"UEFI Interactive Shell") || Find(v,u"Shell> ") || Find(v,u"'Q' break"))return Manager::shell;
    if(Find(v,u"Memtest86+"))return Manager::memtest;
    if(Find(v,u"iPXE>") || Find(v,u"iPXE "))return Manager::ipxe;
    return Manager::unknown;
}
inline bool ShellPrompt(const View& v) {
    if(!v.cursorShown || v.cursorRow>=v.rows)return false;
    const size_t r=v.cursorRow;
    if(FindIn(v,r,u"Shell>")!=SIZE_MAX)return true;
    // "FS0:\> ", "fs1:\EFI\BOOT\> ", "BLK2:\> "
    size_t s,e;if(!Bounds(v,r,s,e))return false;
    size_t c=s;while(c<e && Letter(v.Ch(r,c)))++c;
    if(c==s || c>=e || !Digit(v.Ch(r,c)))return false;
    while(c<e && Digit(v.Ch(r,c)))++c;
    return c+1<e && v.Ch(r,c)==u':' && v.Ch(r,c+1)==u'\\' && FindIn(v,r,u">",c)!=SIZE_MAX;
}
// Builds the model. `name`: the program the reader saw start (may be null).
inline void Build(const View& v,const char16_t* name,Model& m) {
    m=Model{};
    if(!v.text || !v.attr || !v.rows || !v.columns)return;
    Manager who=ManagerByName(name);
    Manager shown=ManagerByScreen(v);
    // A started program is known by its image. Text on its screen changes that only when it is another program's
    // own banner or prompt (a chain-loaded GRUB, the Shell, Memory Diagnostic), never a name that may be a menu
    // entry ("Windows Boot Manager" in Limine's or GRUB's list).
    if(who!=Manager::unknown && shown!=Manager::grub && shown!=Manager::memoryDiagnostic && shown!=Manager::shell)shown=Manager::unknown;
    if(who!=Manager::unknown && shown==Manager::grub && !Find(v,u"GNU GRUB  version") && !Find(v,u"grub>"))shown=Manager::unknown;
    Generic(v,m,who!=Manager::systemdBoot);
    // text only one program shows wins over the started image (GRUB chain loaded from OpenCore, a tool started from a menu)
    if(shown!=Manager::unknown)who=shown;
    if(who==Manager::unknown && ShellPrompt(v))who=Manager::shell;
    if(who==Manager::windows && shown==Manager::memoryDiagnostic)who=Manager::memoryDiagnostic;
    m.manager=who;
    switch(who) {
        case Manager::grub:Grub(v,m);break;
        case Manager::systemdBoot:SystemdBoot(v,m);break;
        case Manager::limine:Limine(v,m);break;
        case Manager::openCore:OpenCore(v,m);Icons(v,m);break;
        case Manager::windows:Windows(v,m);break;
        case Manager::memoryDiagnostic:MemoryDiagnostic(v,m);break;
        case Manager::shell:Shell(v,m);break;
        case Manager::memtest:Memtest(v,m);break;
        case Manager::syslinux:Syslinux(v,m);break;
        case Manager::ipxe:Ipxe(v,m);break;
        case Manager::refind:case Manager::clover:Icons(v,m);DefaultFromCountdown(v,m);break;
        default:DefaultFromCountdown(v,m);break;
    }
    // Position information is only as good as the selection: a list without one selected item is not sure.
    if(m.list.found && m.list.selected==none8)m.list.sure=false;
    if(m.list.found && m.list.sure)for(size_t i=0;i<m.list.count;++i) {Item& it=m.list.items[i];
        if(it.row==none8)continue;
        // a leading ► or a trailing ► / ▶ marks a submenu in firmware forms and Charon's own screens
        const char16_t lead=it.left>=2?v.Ch(it.row,it.left-2):u' ',trail=it.right?v.Ch(it.row,it.right-1):u' ';
        if(lead==0x25ba || lead==0x25b6 || trail==0x25ba || trail==0x25b6)it.flags|=itemSubmenu;
        const uint8_t a=v.At(it.row,it.left);if((a&0x80) && (a&15)==8 && !(it.flags&itemSelected))it.flags|=itemDisabled;
    }
}
}  // namespace hakefi::screenmodel
