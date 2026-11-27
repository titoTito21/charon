// Charon control reader for graphical screens (r410). GPL-2.0-or-later.
//
// Finds CONTROLS in a picture of a screen, whatever program drew it: rows and
// columns of icons or tiles with the selected one, buttons, edit fields,
// sliders, tabs, check boxes, highlighted list rows, and - from two frames -
// the mouse pointer and the control that gained the focus. It is the UEFI
// counterpart of src/pc/pc_winread.cpp (roles, states, labels, "N of M"),
// but with generic rules only: nothing here knows rEFInd, Clover or OpenCanopy.
//
// Input: one 32-bit frame (blue, green, red, reserved = 0x00RRGGBB as a
// little-endian uint32_t; the reserved byte is ignored), and optionally the
// text lines an OCR already read, each with its pixel rectangle. This header
// reads no text itself.
//
// Rules of the design:
//  * Nothing is guessed silently. Every control has `confidence` (0..100) for
//    its role and `stateConfidence` for its focused/selected state; the whole
//    result has `confidence`. Below `confident` (60) the caller should say
//    only the position ("icon 2 of 3") or fall back to the layout description.
//  * A label is NEVER invented. It is copied from a caller's text line chosen
//    by the geometric rules documented at AssignLabels(); no line near the
//    control -> label empty, labelSource none.
//  * No heap, no STL, no exceptions; the large buffers are in Workspace, which
//    the caller provides (static or pool; about 0.9 MB), and in Result (about
//    17 KB). No function here keeps more than about 1 KB on the stack.
//  * No firmware dependency: tests/CharonPixelControlsTests.cpp runs it on
//    synthetic frames and on recorded screens (tests/charon-screens/pixel).
//
// How it works (Analyse):
//  1. Background = the most frequent colour of the area. Its share of the
//     area is `backgroundCoverage`; a photograph as background gives a low
//     share and every confidence is then capped (see Cap()).
//  2. The area is divided into cells of 1..n pixels (at most 640 x 400
//     cells); a cell is "ink" when one of its pixels differs from the
//     background. Connected ink cells form blobs (8-neighbourhood).
//  3. Blobs lying on caller's text lines are text and are dropped; small
//     pieces beside each other are merged (parts of one icon, letters).
//  4. Each blob is classified by shape, on the real pixels: slider (a thin
//     long track with one taller knob), frame around another blob (focus
//     frame), panel (a large rectangle: tabs on its top edge are looked for,
//     then its inside is analysed again with the panel's colour as the
//     background), check box, rectangle with text (button when the ink is
//     centred, field when it is left-aligned or a caret, list row when the
//     caller's text lines continue above or below with the same left edge),
//     icon-sized blob.
//  5. Icon-sized blobs (and, apart, buttons and check boxes) on one line with
//     similar size form a group: index and count. The selected member is the
//     one that differs from its siblings: the only one inside a frame, the
//     only one standing on a plate (a ring of one colour, not the background,
//     just inside its bounding box), the only plate of another colour, or the
//     only one clearly larger than all the others (glow).
//
// Two frames: FindPointer() (a small blob that moved) and
// FocusFromDifference() (the control whose surroundings gained ink).
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace hakefi::pixelcontrols {
constexpr size_t maxControls=64,labelCap=64,maxLines=128;
constexpr int gridMaxWidth=640,gridMaxHeight=400;
constexpr size_t maxCells=size_t(gridMaxWidth)*gridMaxHeight,maxLabels=8192,maxBlobs=768,maxPanels=6;
constexpr uint8_t confident=60;          // from here on a claim may be spoken as a fact
constexpr uint8_t noPercent=255;
struct Point {int32_t x=0,y=0;};
struct Rect {int32_t x=0,y=0,w=0,h=0;};
struct Frame {const uint32_t* pixels=nullptr;uint32_t width=0,height=0,stride=0;};   // stride in pixels
// One line (or one word group) the caller's OCR read, with its place in pixels.
struct TextLine {int32_t x=0,y=0,w=0,h=0;const char16_t* text=nullptr;};
enum class Role : uint8_t {unknown=0,iconItem,tile,button,field,slider,tab,checkbox,listItem,pointer,image};
// Where the label came from. inside/below/beside/above: a caption by geometry.
// status: the line a picker draws under its rows for the selected item (rEFInd, Clover).
enum class LabelSource : uint8_t {none=0,inside,below,beside,above,status};
constexpr uint8_t stateFocused=1,stateSelected=2,statePressed=4,stateDisabled=8,stateChecked=16,stateCaret=32;
struct Control {
    Role role=Role::unknown;LabelSource labelSource=LabelSource::none;
    uint8_t state=0;
    uint8_t confidence=0;        // of the role
    uint8_t stateConfidence=0;   // of stateFocused / stateSelected (0 when neither is set)
    uint8_t group=0;             // 0 = alone; members of one row / column / tab bar / list share a number
    uint8_t index=0,count=0;     // 1-based position in the group; 0 = alone
    uint8_t percent=noPercent;   // slider value
    Rect rect;
    char16_t label[labelCap]={};
    char16_t value[labelCap]={}; // field: the text inside it
};
struct Result {
    uint8_t count=0;
    int8_t focused=-1;           // index of the one focused or selected control, -1 = none or not unique
    uint8_t confidence=0;        // of the picture as a whole (background share, table overflow)
    uint8_t backgroundCoverage=0;// percent of the screen that has the background colour
    uint8_t groups=0;
    bool truncated=false;        // more blobs or controls than the tables hold: the result is incomplete
    uint32_t background=0;       // 0x00RRGGBB
    Control controls[maxControls];
};
namespace detail {
struct Blob {int32_t x0=0,y0=0,x1=0,y1=0;uint32_t cells=0;uint8_t flags=0,pieces=1;};   // x1,y1 exclusive
constexpr uint8_t blobDead=1,blobFramed=2;
struct Panel {Rect rect;uint32_t background=0;uint8_t coverage=0,depth=0;};
constexpr uint8_t featPlate=1,featFramed=2,featRect=4,featInk=8;
}
// Caller-provided scratch memory (about 0.9 MB). Contents are meaningless between calls.
struct Workspace {
    uint16_t label[maxCells];
    uint16_t parent[maxLabels];
    uint16_t bx0[maxLabels],by0[maxLabels],bx1[maxLabels],by1[maxLabels];
    uint32_t cells[maxLabels];
    uint32_t histogram[4096];
    detail::Blob blobs[maxBlobs];
    detail::Panel panels[maxPanels];
    uint32_t ring[maxControls],fill[maxControls];     // per control: colour of the ring / border, of the inside
    uint8_t feature[maxControls],contrast[maxControls],level[maxControls];
    uint8_t lineUsed[maxLines];
    uint32_t blobCount=0,panelCount=0;
    int32_t cell=1,gridWidth=0,gridHeight=0;
    bool truncated=false;
};
namespace detail {
constexpr int backgroundTolerance=12,ringTolerance=14,inkTolerance=24;
inline int Abs(int v) {return v<0?-v:v;}
inline int Min(int a,int b) {return a<b?a:b;}
inline int Max(int a,int b) {return a>b?a:b;}
inline uint32_t Px(const Frame& f,int x,int y) {return f.pixels[size_t(y)*f.stride+size_t(x)]&0xffffffu;}
inline bool Near(uint32_t a,uint32_t b,int tolerance) {
    for(unsigned shift=0;shift<24;shift+=8) {const int d=int((a>>shift)&255)-int((b>>shift)&255);if(d>tolerance || d<-tolerance)return false;}
    return true;
}
inline int Luma(uint32_t c) {return int(((c>>16)&255)*77+((c>>8)&255)*150+(c&255)*29)>>8;}
inline int Right(const Rect& r) {return r.x+r.w;}
inline int Bottom(const Rect& r) {return r.y+r.h;}
inline bool Inside(const Rect& outer,const Rect& inner) {return inner.x>=outer.x && inner.y>=outer.y && Right(inner)<=Right(outer) && Bottom(inner)<=Bottom(outer);}
inline int64_t Overlap(const Rect& a,const Rect& b) {
    const int w=Min(Right(a),Right(b))-Max(a.x,b.x),h=Min(Bottom(a),Bottom(b))-Max(a.y,b.y);
    return w>0 && h>0?int64_t(w)*h:0;
}
inline Rect Clip(Rect r,const Frame& f) {
    if(r.x<0) {r.w+=r.x;r.x=0;}
    if(r.y<0) {r.h+=r.y;r.y=0;}
    if(Right(r)>int(f.width))r.w=int(f.width)-r.x;
    if(Bottom(r)>int(f.height))r.h=int(f.height)-r.y;
    if(r.w<0)r.w=0;
    if(r.h<0)r.h=0;
    return r;
}
// The most frequent colour of an area (sampled) and the percent of the samples that have it.
inline uint32_t Mode(const Frame& f,Rect r,Workspace& ws,int& coverage) {
    coverage=0;r=Clip(r,f);
    if(r.w<=0 || r.h<=0)return 0;
    int step=1;while(int64_t(r.w/step+1)*(r.h/step+1)>8192)++step;
    for(auto& bin:ws.histogram)bin=0;
    auto binOf=[](uint32_t p) {return ((p>>20)&15)<<8|((p>>12)&15)<<4|((p>>4)&15);};
    uint32_t samples=0;
    for(int y=r.y;y<Bottom(r);y+=step)for(int x=r.x;x<Right(r);x+=step) {++ws.histogram[binOf(Px(f,x,y))];++samples;}
    uint32_t best=0;for(uint32_t i=1;i<4096;++i)if(ws.histogram[i]>ws.histogram[best])best=i;
    uint64_t red=0,green=0,blue=0;const uint32_t n=ws.histogram[best];
    for(int y=r.y;y<Bottom(r);y+=step)for(int x=r.x;x<Right(r);x+=step) {
        const uint32_t p=Px(f,x,y);if(binOf(p)!=best)continue;
        red+=(p>>16)&255;green+=(p>>8)&255;blue+=p&255;
    }
    if(!n)return 0;
    const uint32_t colour=uint32_t((red+n/2)/n)<<16|uint32_t((green+n/2)/n)<<8|uint32_t((blue+n/2)/n);
    uint32_t same=0;
    for(int y=r.y;y<Bottom(r);y+=step)for(int x=r.x;x<Right(r);x+=step)if(Near(Px(f,x,y),colour,backgroundTolerance+2))++same;
    coverage=int(uint64_t(same)*100/samples);
    return colour;
}
// The most frequent colour of a short list; `share` = how many of them have it.
inline uint32_t ListMode(const uint32_t* colours,int count,int tolerance,int& share) {
    share=0;uint32_t best=0;
    for(int i=0;i<count;++i) {
        int n=0;for(int j=0;j<count;++j)if(Near(colours[i],colours[j],tolerance))++n;
        if(n>share) {share=n;best=colours[i];}
    }
    return best;
}
inline uint16_t Find(Workspace& ws,uint16_t l) {
    while(ws.parent[l]!=l) {ws.parent[l]=ws.parent[ws.parent[l]];l=ws.parent[l];}
    return l;
}
// Connected components of the cells of `area` in which `ink(x,y)` holds for a pixel.
template<class Ink> inline void LabelCells(Workspace& ws,const Rect& area,Ink ink) {
    ws.blobCount=0;
    int cell=1;while((area.w+cell-1)/cell>gridMaxWidth || (area.h+cell-1)/cell>gridMaxHeight)++cell;
    const int gw=(area.w+cell-1)/cell,gh=(area.h+cell-1)/cell;
    ws.cell=cell;ws.gridWidth=gw;ws.gridHeight=gh;
    uint32_t next=1;
    for(int gy=0;gy<gh;++gy)for(int gx=0;gx<gw;++gx) {
        const int x0=area.x+gx*cell,y0=area.y+gy*cell,x1=Min(x0+cell,Right(area)),y1=Min(y0+cell,Bottom(area));
        bool on=false;
        for(int y=y0;y<y1 && !on;++y)for(int x=x0;x<x1;++x)if(ink(x,y)) {on=true;break;}
        uint16_t& here=ws.label[size_t(gy)*gw+gx];
        if(!on) {here=0;continue;}
        uint16_t root=0;
        auto join=[&](int nx,int ny) {
            if(nx<0 || ny<0 || nx>=gw)return;
            const uint16_t l=ws.label[size_t(ny)*gw+nx];if(!l)return;
            const uint16_t r=Find(ws,l);
            if(!root)root=r;
            else if(r!=root) {if(r<root) {ws.parent[root]=r;root=r;} else ws.parent[r]=root;}
        };
        join(gx-1,gy);join(gx-1,gy-1);join(gx,gy-1);join(gx+1,gy-1);
        if(!root) {
            if(next>=maxLabels) {ws.truncated=true;here=0;continue;}
            root=uint16_t(next);ws.parent[next]=uint16_t(next);++next;
        }
        here=root;
    }
    for(uint32_t l=1;l<next;++l) {ws.bx0[l]=0xffff;ws.by0[l]=0xffff;ws.bx1[l]=0;ws.by1[l]=0;ws.cells[l]=0;}
    for(int gy=0;gy<gh;++gy)for(int gx=0;gx<gw;++gx) {
        const uint16_t l=ws.label[size_t(gy)*gw+gx];if(!l)continue;
        const uint16_t r=Find(ws,l);
        if(gx<ws.bx0[r])ws.bx0[r]=uint16_t(gx);
        if(gy<ws.by0[r])ws.by0[r]=uint16_t(gy);
        if(gx>ws.bx1[r])ws.bx1[r]=uint16_t(gx);
        if(gy>ws.by1[r])ws.by1[r]=uint16_t(gy);
        ++ws.cells[r];
    }
    for(uint32_t l=1;l<next;++l) {
        if(ws.parent[l]!=l)continue;
        Blob b;
        b.x0=area.x+ws.bx0[l]*cell;b.y0=area.y+ws.by0[l]*cell;
        b.x1=Min(area.x+(ws.bx1[l]+1)*cell,Right(area));b.y1=Min(area.y+(ws.by1[l]+1)*cell,Bottom(area));
        b.cells=ws.cells[l];
        if(b.x1-b.x0<4 && b.y1-b.y0<4)continue;   // specks
        if(ws.blobCount>=maxBlobs) {ws.truncated=true;break;}
        ws.blobs[ws.blobCount++]=b;
    }
}
// Shrinks a blob's box (known to cell precision) to the pixels that are ink.
template<class Ink> inline void Refine(Blob& b,int cell,Ink ink) {
    auto rowEmpty=[&](int y) {for(int x=b.x0;x<b.x1;++x)if(ink(x,y))return false;return true;};
    auto columnEmpty=[&](int x) {for(int y=b.y0;y<b.y1;++y)if(ink(x,y))return false;return true;};
    for(int i=0;i<cell && b.y1-b.y0>1 && rowEmpty(b.y0);++i)++b.y0;
    for(int i=0;i<cell && b.y1-b.y0>1 && rowEmpty(b.y1-1);++i)--b.y1;
    for(int i=0;i<cell && b.x1-b.x0>1 && columnEmpty(b.x0);++i)++b.x0;
    for(int i=0;i<cell && b.x1-b.x0>1 && columnEmpty(b.x1-1);++i)--b.x1;
}
inline Rect BoxOf(const Blob& b) {Rect r;r.x=b.x0;r.y=b.y0;r.w=b.x1-b.x0;r.h=b.y1-b.y0;return r;}
inline void CopyText(char16_t* out,const char16_t* text,bool append) {
    size_t at=0;
    if(append) {while(out[at])++at;if(at && at+1<labelCap)out[at++]=u' ';}
    if(text) {while(*text==u' ')++text;for(;*text && at+1<labelCap;++text)out[at++]=*text;}
    while(at && out[at-1]==u' ')--at;
    out[at]=0;
}
struct Context {
    const Frame& f;const TextLine* lines;size_t lineCount;Workspace& ws;Result& out;
    uint32_t bg=0;int coverage=0;uint8_t depth=0;Rect area;
    int minIcon=20,maxIcon=200,maxButton=64,gap=6,verticalGap=3;
    bool Ink(int x,int y) const {return !Near(Px(f,x,y),bg,backgroundTolerance);}
};
// Confidence after the state of the picture: a busy background makes every claim weak.
inline uint8_t Cap(const Context& c,int confidence) {
    if(c.coverage<35)confidence=Min(confidence,30);
    else if(c.coverage<55)confidence-=20;
    if(c.ws.truncated)confidence=Min(confidence,35);
    return uint8_t(confidence<0?0:confidence>100?100:confidence);
}
inline int Add(Context& c,Role role,const Rect& rect,int confidence,uint8_t feature=0,uint32_t ring=0,uint32_t fill=0) {
    if(c.out.count>=maxControls) {c.out.truncated=true;return -1;}
    const int i=c.out.count++;
    Control& k=c.out.controls[i];
    k.role=role;k.labelSource=LabelSource::none;k.state=0;k.confidence=Cap(c,confidence);k.stateConfidence=0;
    k.group=0;k.index=0;k.count=0;k.percent=noPercent;k.rect=rect;k.label[0]=0;k.value[0]=0;
    c.ws.feature[i]=feature;c.ws.ring[i]=ring;c.ws.fill[i]=fill;c.ws.contrast[i]=0;c.ws.level[i]=c.depth;
    return i;
}
// Share (percent) of ink on the four edges of a box; the lowest of the four. A
// rectangle, also with rounded corners, has all four edges nearly full.
inline int EdgeFill(const Context& c,const Rect& r) {
    if(r.w<4 || r.h<4)return 0;
    int lowest=100;
    for(int edge=0;edge<4;++edge) {
        int best=0;
        for(int inset=0;inset<2;++inset) {
            int on=0,total=0;
            if(edge<2) {const int y=edge?Bottom(r)-1-inset:r.y+inset;for(int x=r.x;x<Right(r);++x) {++total;if(c.Ink(x,y))++on;}}
            else {const int x=edge==2?r.x+inset:Right(r)-1-inset;for(int y=r.y;y<Bottom(r);++y) {++total;if(c.Ink(x,y))++on;}}
            best=Max(best,on*100/total);
        }
        lowest=Min(lowest,best);
    }
    return lowest;
}
// The ring just inside a box: is it one colour? (the plate under a selected icon, a tile)
inline bool Ring(const Context& c,const Rect& r,uint32_t& colour) {
    const int inset=Max(3,Min(r.w,r.h)*6/100);
    const int x0=r.x+inset,y0=r.y+inset,x1=Right(r)-1-inset,y1=Bottom(r)-1-inset;
    if(x1-x0<6 || y1-y0<6)return false;
    uint32_t samples[64];int n=0;
    for(int i=0;i<16;++i) {
        const int x=x0+(x1-x0)*i/15,y=y0+(y1-y0)*i/15;
        samples[n++]=Px(c.f,x,y0);samples[n++]=Px(c.f,x,y1);samples[n++]=Px(c.f,x0,y);samples[n++]=Px(c.f,x1,y);
    }
    int share=0;colour=ListMode(samples,n,ringTolerance,share);
    return share*100>=n*85;
}
// A thin long track with exactly one taller knob on it.
inline bool TrySlider(Context& c,const Blob& b) {
    const int w=b.x1-b.x0,h=b.y1-b.y0;
    if(h<6 || h>c.maxButton || w<60 || w<h*5)return false;
    int first=-1,last=-1,track=0;
    for(int y=b.y0;y<b.y1;++y) {
        int on=0;for(int x=b.x0;x<b.x1;++x)if(c.Ink(x,y))++on;
        if(on*10>=w*9) {if(first<0)first=y;last=y;++track;}
    }
    if(track<1 || track!=last-first+1 || track*10>h*7)return false;
    int knobStart=-1,knobEnd=-1,runs=0;bool in=false;
    for(int x=b.x0;x<b.x1;++x) {
        int on=0;for(int y=b.y0;y<b.y1;++y)if(c.Ink(x,y))++on;
        const bool tall=on>=track+2;
        if(tall && !in) {++runs;knobStart=x;}
        if(tall)knobEnd=x+1;
        in=tall;
    }
    const int knob=knobEnd-knobStart;
    if(runs!=1 || knob<3 || knob>Max(3*h,w/4) || knob*2>=w)return false;
    const int i=Add(c,Role::slider,BoxOf(b),75);
    if(i<0)return true;
    int percent=((knobStart-b.x0)*100+(w-knob)/2)/(w-knob);
    c.out.controls[i].percent=uint8_t(percent<0?0:percent>100?100:percent);
    return true;
}
// A small square box: empty or with a mark in it.
inline bool TryCheckbox(Context& c,const Rect& r) {
    if(r.w<10 || r.h<10 || r.w>32 || r.h>32 || Abs(r.w-r.h)>3)return false;
    Rect inner=r;inner.x+=3;inner.y+=3;inner.w-=6;inner.h-=6;
    if(inner.w<4)return false;
    uint32_t colours[64];int n=0;
    for(int i=0;i<8;++i)for(int j=0;j<8;++j)colours[n++]=Px(c.f,inner.x+inner.w*i/8,inner.y+inner.h*j/8);
    int share=0;const uint32_t fill=ListMode(colours,n,ringTolerance,share);
    int ink=0;for(int y=inner.y;y<Bottom(inner);++y)for(int x=inner.x;x<Right(inner);++x)if(!Near(Px(c.f,x,y),fill,inkTolerance))++ink;
    const int percent=ink*100/(inner.w*inner.h);
    if(percent>60)return false;   // a small picture, not a box
    const int i=Add(c,Role::checkbox,r,55,featRect,0,fill);
    if(i>=0 && percent>=10)c.out.controls[i].state|=stateChecked;
    return true;
}
// Caller's text lines whose centre is inside `r`: their common box; false when none.
inline bool LinesInside(const Context& c,const Rect& r,Rect& box,int& first) {
    bool any=false;int x0=0,y0=0,x1=0,y1=0;first=-1;
    for(size_t i=0;i<c.lineCount;++i) {
        const TextLine& l=c.lines[i];
        if(!l.text || !l.text[0] || l.w<=0 || l.h<=0)continue;
        const int cx=l.x+l.w/2,cy=l.y+l.h/2;
        if(cx<r.x || cx>=Right(r) || cy<r.y || cy>=Bottom(r))continue;
        if(!any) {x0=l.x;y0=l.y;x1=l.x+l.w;y1=l.y+l.h;first=int(i);any=true;}
        else {x0=Min(x0,l.x);y0=Min(y0,l.y);x1=Max(x1,l.x+l.w);y1=Max(y1,l.y+l.h);}
    }
    box.x=x0;box.y=y0;box.w=x1-x0;box.h=y1-y0;
    return any;
}
// A highlighted bar over one of several text lines with the same left edge: a list.
inline bool TryList(Context& c,const Rect& bar,int lineIndex) {
    const TextLine& t=c.lines[lineIndex];
    int chain[32];int count=0;chain[count++]=lineIndex;
    for(int direction=-1;direction<=1;direction+=2) {
        int at=lineIndex;
        for(;count<32;) {
            const TextLine& from=c.lines[at];int best=-1,bestGap=0;
            for(size_t i=0;i<c.lineCount;++i) {
                const TextLine& l=c.lines[i];
                if(int(i)==at || !l.text || !l.text[0] || Abs(l.x-t.x)>Max(6,t.h/2) || c.ws.lineUsed[i])continue;
                const int gap=direction<0?from.y-(l.y+l.h):l.y-(from.y+from.h);
                if(gap<-2 || gap>t.h*3/2)continue;
                if(best<0 || gap<bestGap) {best=int(i);bestGap=gap;}
            }
            if(best<0)break;
            bool seen=false;for(int k=0;k<count;++k)if(chain[k]==best)seen=true;
            if(seen)break;
            chain[count++]=best;at=best;
        }
    }
    if(count<2)return false;
    for(int i=1;i<count;++i)for(int j=i;j>0 && c.lines[chain[j]].y<c.lines[chain[j-1]].y;--j) {const int s=chain[j];chain[j]=chain[j-1];chain[j-1]=s;}
    if(c.out.groups==255)return false;
    const uint8_t group=++c.out.groups;
    for(int i=0;i<count;++i) {
        const TextLine& l=c.lines[chain[i]];
        Rect r=bar;
        if(chain[i]!=lineIndex) {r.y=l.y-(bar.h-l.h)/2;r.h=bar.h;}
        const int k=Add(c,Role::listItem,r,75);
        if(k<0)break;
        Control& item=c.out.controls[k];
        item.group=group;item.index=uint8_t(i+1);item.count=uint8_t(count);
        CopyText(item.label,l.text,false);item.labelSource=LabelSource::inside;
        if(chain[i]==lineIndex) {item.state|=stateSelected;item.stateConfidence=Cap(c,80);}
        if(size_t(chain[i])<maxLines)c.ws.lineUsed[chain[i]]=1;
    }
    return true;
}
// A rectangle of button height: button (centred ink), field (left-aligned ink
// or a caret), list row, or unknown. false = not a plain control (a picture).
inline bool TryRectControl(Context& c,const Rect& r) {
    const int border=Max(2,Min(4,r.h/6));
    Rect inner=r;inner.x+=border;inner.y+=border;inner.w-=2*border;inner.h-=2*border;
    if(inner.w<6 || inner.h<4)return false;
    int coverage=0;const uint32_t fill=Mode(c.f,inner,c.ws,coverage);
    if(coverage<45)return false;
    int x0=Right(inner),y0=Bottom(inner),x1=inner.x,y1=inner.y,ink=0,contrast=0;
    const int fillLuma=Luma(fill);
    for(int y=inner.y;y<Bottom(inner);++y)for(int x=inner.x;x<Right(inner);++x) {
        const uint32_t p=Px(c.f,x,y);if(Near(p,fill,inkTolerance))continue;
        ++ink;x0=Min(x0,x);y0=Min(y0,y);x1=Max(x1,x+1);y1=Max(y1,y+1);contrast=Max(contrast,Abs(Luma(p)-fillLuma));
    }
    // The border colour: the most frequent colour of the outer edge.
    uint32_t edge[64];int n=0;
    for(int i=0;i<16;++i) {
        const int x=r.x+2+(r.w-5)*i/15,y=r.y+2+(r.h-5)*i/15;
        edge[n++]=Px(c.f,x,r.y);edge[n++]=Px(c.f,x,Bottom(r)-1);edge[n++]=Px(c.f,r.x,y);edge[n++]=Px(c.f,Right(r)-1,y);
    }
    int share=0;const uint32_t borderColour=ListMode(edge,n,ringTolerance,share);
    Rect text;int line=-1;const bool read=LinesInside(c,r,text,line);
    if(read && r.w>=r.h*3 && TryList(c,r,line))return true;
    if(read) {x0=text.x;y0=text.y;x1=text.x+text.w;y1=text.y+text.h;}
    const bool any=read || ink>0;
    if(any && !read && (y1-y0)*10>inner.h*8 && (x1-x0)>3)return false;   // ink as tall as the box: a picture on a plate
    Role role=Role::unknown;int confidence=20;uint8_t state=0;
    const bool outlined=Near(fill,c.bg,backgroundTolerance);
    if(!any) {
        if(outlined && r.w>=r.h*4 && r.w<=r.h*40) {role=Role::field;confidence=40;}
    } else if(!read && x1-x0<=3 && (y1-y0)*2>=inner.h) {
        role=Role::field;confidence=70;state|=stateCaret;
    } else {
        const int left=x0-inner.x,right=Right(inner)-x1;
        if(Abs(left-right)<=Max(4,r.w*15/100) && r.w<=r.h*10) {role=Role::button;confidence=read?75:55;}
        else if(left<=Max(12,r.h*6/10) && right>=2*left+8 && r.w<=r.h*40) {role=Role::field;confidence=read?60:45;}
    }
    if(role==Role::button) {
        // A sunken bevel (dark top edge, light bottom edge) = held down.
        int top=0,bottom=0;
        for(int i=0;i<16;++i) {const int x=r.x+2+(r.w-5)*i/15;top+=Luma(Px(c.f,x,r.y));bottom+=Luma(Px(c.f,x,Bottom(r)-1));}
        if(top/16<fillLuma-30 && bottom/16>fillLuma+30)state|=statePressed;
    }
    const int i=Add(c,role,r,confidence,featRect|(any?featInk:0),borderColour,fill);
    if(i>=0) {c.out.controls[i].state=state;c.ws.contrast[i]=uint8_t(contrast>255?255:contrast);}
    return true;
}
// Tabs on the top edge of a panel: boxes side by side above the line where the
// panel reaches its full width; the open tab is the one with no line between
// it and the panel. Returns the y where the panel proper begins (r.y = none).
inline int TryTabs(Context& c,const Rect& r) {
    int panelTop=-1;
    for(int y=r.y;y<Bottom(r) && y-r.y<=c.maxButton+8;++y) {
        int first=-1,last=-1;
        for(int x=r.x;x<Right(r);++x)if(c.Ink(x,y)) {if(first<0)first=x;last=x;}
        if(first>=0 && first<=r.x+2 && last>=Right(r)-3) {panelTop=y;break;}
    }
    if(panelTop<0 || panelTop-r.y<12)return r.y;
    {Rect body=r;body.y=panelTop;body.h=Bottom(r)-panelTop;if(body.h<40 || EdgeFill(c,body)<80)return r.y;}   // what is below must be a rectangle
    int stripLeft=-1,stripRight=-1;
    {const int y=(r.y+panelTop)/2;for(int x=r.x;x<Right(r);++x)if(c.Ink(x,y)) {if(stripLeft<0)stripLeft=x;stripRight=x+1;}}
    if(stripLeft<0 || stripRight-stripLeft<28)return r.y;
    struct Tab {int x0,x1,top;uint32_t fill;};
    Tab tabs[12];int count=0;
    int runStart=-1,runTop=0;uint32_t runColour=0;
    auto top=[&](int x) {int y=r.y;while(y<panelTop && !c.Ink(x,y))++y;return y;};
    auto interior=[&](int x,int t,uint32_t& colour) {
        const int span=panelTop-t,margin=Max(3,span*15/100);
        if(span<2*margin+2)return false;
        const uint32_t a=Px(c.f,x,t+margin),b=Px(c.f,x,panelTop-margin),m=Px(c.f,x,t+span/2);
        if(Near(a,b,ringTolerance)) {colour=a;return true;}
        if(Near(a,m,ringTolerance)) {colour=a;return true;}
        if(Near(b,m,ringTolerance)) {colour=b;return true;}
        return false;
    };
    auto close=[&](int x) {
        if(runStart>=0 && x-runStart>=14 && count<12)tabs[count++]={runStart,x,runTop,runColour};
        runStart=-1;
    };
    for(int x=stripLeft;x<stripRight;++x) {
        const int t=top(x);
        if(t>=panelTop-3) {close(x);continue;}                 // a gap between tabs
        uint32_t colour=0;
        if(!interior(x,t,colour))continue;                     // a column through a letter
        if(runStart<0) {runStart=x;runTop=t;runColour=colour;continue;}
        if(Near(colour,runColour,ringTolerance)) {runTop=Min(runTop,t);continue;}
        // Another colour: a separator line when narrow, a tab of another colour when it lasts.
        int ahead=1;
        for(;x+ahead<stripRight && ahead<=4;++ahead) {
            uint32_t next=0;const int nt=top(x+ahead);
            if(nt>=panelTop-3 || !interior(x+ahead,nt,next) || !Near(next,colour,ringTolerance))break;
        }
        close(x);
        if(ahead>4) {runStart=x;runTop=t;runColour=colour;}
        else x+=ahead-1;
    }
    close(stripRight);
    // A run narrower than a tab that is only the outer border line is dropped by the width test above.
    if(count<2)return r.y;
    for(int i=0;i<count;++i)if(panelTop-tabs[i].top<12)return r.y;
    int joined=-1,joinedCount=0;
    for(int i=0;i<count;++i) {
        bool line=false;
        for(int y=panelTop-1;y<=panelTop+2 && y<Bottom(r) && !line;++y) {
            int other=0,total=0;
            for(int x=tabs[i].x0+3;x<tabs[i].x1-3;++x) {++total;if(!Near(Px(c.f,x,y),tabs[i].fill,inkTolerance-4))++other;}
            if(total && other*10>=total*8)line=true;
        }
        if(!line) {joined=i;++joinedCount;}
    }
    if(c.out.groups==255)return r.y;
    const uint8_t group=++c.out.groups;
    for(int i=0;i<count;++i) {
        Rect box;box.x=tabs[i].x0;box.y=tabs[i].top;box.w=tabs[i].x1-tabs[i].x0;box.h=panelTop-tabs[i].top;
        const int k=Add(c,Role::tab,box,joinedCount==1?75:45,featRect,0,tabs[i].fill);
        if(k<0)break;
        Control& tab=c.out.controls[k];
        tab.group=group;tab.index=uint8_t(i+1);tab.count=uint8_t(count);
        if(joinedCount==1 && i==joined) {tab.state|=stateSelected;tab.stateConfidence=Cap(c,75);}
    }
    return panelTop;
}
inline bool Iconish(Role role) {return role==Role::iconItem || role==Role::tile || role==Role::image;}
// Rows, then columns, of controls of one kind: group number, index and count.
inline void GroupKind(Context& c,int first,Role kind) {
    Control* k=c.out.controls;
    for(int axis=0;axis<2;++axis) {
        // axis 0: members share a line (centres level), ordered by x; axis 1: a column.
        auto along=[&](const Rect& r) {return axis?r.y:r.x;};
        auto extent=[&](const Rect& r) {return axis?r.h:r.w;};
        auto across=[&](const Rect& r) {return axis?r.x+r.w/2:r.y+r.h/2;};
        auto thickness=[&](const Rect& r) {return axis?r.w:r.h;};
        for(int seed=first;seed<c.out.count;++seed) {
            if(k[seed].role!=kind || k[seed].group)continue;
            uint8_t members[maxControls];int n=0;
            for(int j=first;j<c.out.count;++j) {
                if(k[j].role!=kind || k[j].group)continue;
                const Rect& a=k[seed].rect;const Rect& b=k[j].rect;
                const int tolerance=Max(4,Min(thickness(a),thickness(b))/5);
                const int big=Max(Max(a.w,a.h),Max(b.w,b.h)),small=Min(Max(a.w,a.h),Max(b.w,b.h));
                const bool level=Abs(across(a)-across(b))<=tolerance
                    || (!axis && Abs(Bottom(a)-Bottom(b))<=2 && Abs(a.h-b.h)*4<=Min(a.h,b.h));
                if(j!=seed && (!level || big*10>small*24))continue;
                if(kind!=Role::iconItem && j!=seed && Abs(thickness(a)-thickness(b))*4>Min(thickness(a),thickness(b)))continue;
                members[n++]=uint8_t(j);
            }
            if(n<2)continue;
            for(int i=1;i<n;++i)for(int j=i;j>0 && along(k[members[j]].rect)<along(k[members[j-1]].rect);--j) {const uint8_t s=members[j];members[j]=members[j-1];members[j-1]=s;}
            // The stretch of neighbours around the seed with no hole: centres no further apart than
            // four (in a column: two and a half) times the smaller member.
            int seedAt=0;for(int i=0;i<n;++i)if(members[i]==seed)seedAt=i;
            auto far=[&](int i) {const Rect& a=k[members[i]].rect;const Rect& b=k[members[i+1]].rect;return (along(b)+extent(b)/2)-(along(a)+extent(a)/2)>(axis?5:8)*Min(Max(a.w,a.h),Max(b.w,b.h))/2;};
            int from=seedAt,to=seedAt;
            while(from>0 && !far(from-1))--from;
            while(to+1<n && !far(to))++to;
            const int count=to-from+1;
            if(count<2 || c.out.groups==255)continue;
            const uint8_t group=++c.out.groups;
            // Regular spacing and enough members raise the confidence of the whole group.
            int minPitch=1<<30,maxPitch=0;
            for(int i=from;i<to;++i) {
                const Rect& a=k[members[i]].rect;const Rect& b=k[members[i+1]].rect;
                const int pitch=(along(b)+extent(b)/2)-(along(a)+extent(a)/2);minPitch=Min(minPitch,pitch);maxPitch=Max(maxPitch,pitch);
            }
            const bool regular=count>=3 && (maxPitch-minPitch)*100<=maxPitch*15;
            for(int i=from;i<=to;++i) {
                Control& m=k[members[i]];
                m.group=group;m.index=uint8_t(i-from+1);m.count=uint8_t(count);
                if(kind==Role::iconItem)m.confidence=Cap(c,(count>=3?60:40)+(regular?15:0)+(c.coverage>=70?15:0));   // two alike prove little
                else if(kind==Role::button)m.confidence=Cap(c,m.confidence+10);
            }
        }
    }
}
// The member that differs from its siblings (see the header comment, step 5).
inline void SelectAmong(Context& c,int first,Role kind) {
    Control* k=c.out.controls;Workspace& ws=c.ws;
    const uint8_t bit=kind==Role::iconItem?stateSelected:stateFocused;
    int total=0,framed=-1,framedCount=0,plate=-1,plateCount=0;
    for(int i=first;i<c.out.count;++i) {
        if(k[i].role!=kind)continue;
        ++total;
        if(ws.feature[i]&featFramed) {framed=i;++framedCount;}
        if(ws.feature[i]&featPlate) {plate=i;++plateCount;}
    }
    if(total<1)return;
    auto mark=[&](int i,int confidence) {k[i].state|=bit;k[i].stateConfidence=Cap(c,confidence);};
    if(framedCount==1 && total>=2) {mark(framed,85);return;}
    if(kind==Role::iconItem && plateCount==1 && total>=2) {mark(plate,total>=3?85:70);return;}
    for(int g=1;g<=c.out.groups;++g) {
        uint8_t members[maxControls];int n=0;
        for(int i=first;i<c.out.count;++i)if(k[i].role==kind && k[i].group==g)members[n++]=uint8_t(i);
        if(n<3)continue;
        // One member of another colour while all the others agree (plate / fill, then border).
        int found=-1;
        for(int pass=0;pass<2 && found<0;++pass) {
            const uint32_t* colour=pass==0 && kind!=Role::iconItem?ws.fill:ws.ring;
            if(kind==Role::iconItem) {if(pass)break;bool all=true;for(int i=0;i<n;++i)if(!(ws.feature[members[i]]&featPlate))all=false;if(!all)break;}
            int odd=-1,oddCount=0;
            for(int i=0;i<n;++i) {
                int same=0;for(int j=0;j<n;++j)if(Near(colour[members[i]],colour[members[j]],ringTolerance))++same;
                if(same==1) {odd=i;++oddCount;}
                else if(same!=n-1) {oddCount=99;break;}
            }
            if(oddCount==1)found=members[odd];
        }
        if(found>=0) {mark(found,75);continue;}
        // One member larger than every other in both directions, the others alike: a glow or a frame touching it.
        int big=-1;
        for(int i=0;i<n && big<0;++i) {
            const Rect& a=k[members[i]].rect;
            int maxW=0,maxH=0,minW=1<<30,minH=1<<30;
            for(int j=0;j<n;++j) {if(j==i)continue;const Rect& b=k[members[j]].rect;maxW=Max(maxW,b.w);maxH=Max(maxH,b.h);minW=Min(minW,b.w);minH=Min(minH,b.h);}
            const int needW=Max(4,maxW*8/100),needH=Max(4,maxH*8/100);
            if(a.w-maxW>=needW && a.h-maxH>=needH && (maxW-minW)*2<a.w-maxW && (maxH-minH)*2<a.h-maxH)big=members[i];
        }
        if(big>=0)mark(big,55);
    }
}
// One analysis pass over an area with a known background colour.
inline void Level(Context& c,const Panel& panel) {
    Workspace& ws=c.ws;
    c.bg=panel.background;c.coverage=panel.coverage;c.depth=panel.depth;c.area=Clip(panel.rect,c.f);
    if(c.area.w<16 || c.area.h<16)return;
    const int first=c.out.count;
    LabelCells(ws,c.area,[&](int x,int y) {return c.Ink(x,y);});
    Blob* b=ws.blobs;const int n=int(ws.blobCount);
    auto width=[&](int i) {return b[i].x1-b[i].x0;};
    auto height=[&](int i) {return b[i].y1-b[i].y0;};
    auto dead=[&](int i) {return (b[i].flags&blobDead)!=0;};
    // Text the caller already read is not a control.
    for(int i=0;i<n;++i) {
        const Rect box=BoxOf(b[i]);int64_t covered=0;int tallest=0;
        for(size_t l=0;l<c.lineCount;++l) {
            Rect line;line.x=c.lines[l].x-1;line.y=c.lines[l].y-1;line.w=c.lines[l].w+2;line.h=c.lines[l].h+2;
            const int64_t o=Overlap(box,line);if(o) {covered+=o;tallest=Max(tallest,line.h);}
        }
        if(covered*10>=int64_t(box.w)*box.h*6 && box.h<=tallest*3/2+4)b[i].flags|=blobDead;
    }
    // Pieces beside each other are one thing when at least one of them is small.
    for(int pass=0;pass<6;++pass) {
        bool merged=false;
        for(int i=0;i<n;++i) {
            if(dead(i))continue;
            for(int j=i+1;j<n;++j) {
                if(dead(j))continue;
                const bool smallI=Min(width(i),height(i))<c.minIcon,smallJ=Min(width(j),height(j))<c.minIcon;
                const Rect boxI=BoxOf(b[i]),boxJ=BoxOf(b[j]);
                // Two boxes that half cover each other (neither inside the other) are one picture.
                const bool entangled=!Inside(boxI,boxJ) && !Inside(boxJ,boxI)
                    && Overlap(boxI,boxJ)*2>=(int64_t(boxI.w)*boxI.h<int64_t(boxJ.w)*boxJ.h?int64_t(boxI.w)*boxI.h:int64_t(boxJ.w)*boxJ.h);
                const int gapX=Max(b[i].x0,b[j].x0)-Min(b[i].x1,b[j].x1),gapY=Max(b[i].y0,b[j].y0)-Min(b[i].y1,b[j].y1);
                if(!entangled && ((!smallI && !smallJ) || gapX>c.gap || gapY>c.verticalGap))continue;
                b[i].x0=Min(b[i].x0,b[j].x0);b[i].y0=Min(b[i].y0,b[j].y0);b[i].x1=Max(b[i].x1,b[j].x1);b[i].y1=Max(b[i].y1,b[j].y1);
                b[i].pieces=uint8_t(Min(255,int(b[i].pieces)+b[j].pieces));
                b[i].cells+=b[j].cells;b[j].flags|=blobDead;merged=true;
            }
        }
        if(!merged)break;
    }
    for(int i=0;i<n;++i)if(!dead(i) && Max(width(i),height(i))>=8)Refine(b[i],ws.cell,[&](int x,int y) {return c.Ink(x,y);});
    for(int i=0;i<n;++i)if(!dead(i) && TrySlider(c,b[i]))b[i].flags|=blobDead;
    for(int i=0;i<n;++i)if(!dead(i) && Min(width(i),height(i))<10)b[i].flags|=blobDead;
    auto panelSized=[&](int i) {return width(i)>=160 && height(i)>=100 && (width(i)>c.maxIcon || height(i)>c.maxIcon);};
    // An outline that holds other blobs: a frame around one thing (focus), or a group box.
    for(int i=0;i<n;++i) {
        if(dead(i))continue;
        const Rect outer=BoxOf(b[i]);
        if(int64_t(b[i].cells)*ws.cell*ws.cell*2>=int64_t(outer.w)*outer.h)continue;   // not hollow
        int inside=0,x0=0,y0=0,x1=0,y1=0,keep=-1;
        for(int j=0;j<n;++j) {
            if(j==i || dead(j) || !Inside(outer,BoxOf(b[j])))continue;
            if(!inside) {x0=b[j].x0;y0=b[j].y0;x1=b[j].x1;y1=b[j].y1;keep=j;}
            else {x0=Min(x0,b[j].x0);y0=Min(y0,b[j].y0);x1=Max(x1,b[j].x1);y1=Max(y1,b[j].y1);}
            ++inside;
        }
        if(!inside)continue;
        if(EdgeFill(c,outer)<80) {
            // Not a plain rectangle: perhaps an outlined panel with tabs standing on its top edge.
            if(panelSized(i) && TryTabs(c,outer)>outer.y)b[i].flags|=blobDead;
            continue;
        }
        const bool tight=(x1-x0)*10>=outer.w*6 && (y1-y0)*10>=outer.h*6;
        if(tight && !panelSized(i)) {
            // The content becomes one blob, marked as framed; the frame itself goes.
            for(int j=0;j<n;++j)if(j!=i && j!=keep && !dead(j) && Inside(outer,BoxOf(b[j])))b[j].flags|=blobDead;
            b[keep].x0=x0;b[keep].y0=y0;b[keep].x1=x1;b[keep].y1=y1;b[keep].flags|=blobFramed;
            b[i].flags|=blobDead;
        } else if(panelSized(i)) {
            TryTabs(c,outer);   // an outlined panel: what is inside it is analysed here, on this level
            b[i].flags|=blobDead;
        }
    }
    // Rectangles.
    const int rectFirst=c.out.count;
    for(int i=0;i<n;++i) {
        if(dead(i))continue;
        const Rect r=BoxOf(b[i]);
        if(panelSized(i)) {
            b[i].flags|=blobDead;
            const int top=TryTabs(c,r);
            Rect body=r;body.y=top;body.h=Bottom(r)-top;
            if(EdgeFill(c,body)<80) {if(int64_t(r.w)*r.h*2<int64_t(c.area.w)*c.area.h)Add(c,Role::image,r,30);continue;}
            Rect inner=r;inner.y=top+3;inner.h=Bottom(r)-3-inner.y;inner.x+=3;inner.w-=6;
            int coverage=0;const uint32_t fill=Mode(c.f,inner,ws,coverage);
            if(coverage<40) {if(int64_t(r.w)*r.h*2<int64_t(c.area.w)*c.area.h)Add(c,Role::image,r,30);continue;}
            if(Near(fill,c.bg,backgroundTolerance))continue;   // outlined: its content is already on this level
            if(ws.panelCount<maxPanels && panel.depth<3) {
                Panel& next=ws.panels[ws.panelCount++];
                next.rect=inner;next.background=fill;next.coverage=uint8_t(coverage);next.depth=uint8_t(panel.depth+1);
            } else c.out.truncated=true;
            continue;
        }
        if(EdgeFill(c,r)<80)continue;
        bool taken=false;
        if(!(b[i].flags&blobFramed) && TryCheckbox(c,r))taken=true;
        else if(r.h>=12 && r.h<=c.maxButton && r.w*10>=r.h*13 && TryRectControl(c,r))taken=true;
        if(taken) {
            b[i].flags|=blobDead;
            if(b[i].flags&blobFramed)for(int k=rectFirst;k<c.out.count;++k)if(c.out.controls[k].rect.x==r.x && c.out.controls[k].rect.y==r.y)ws.feature[k]|=featFramed;
        }
    }
    // What lies inside a control just found is its content, not another control.
    for(int i=0;i<n;++i) {
        if(dead(i))continue;
        for(int k=rectFirst;k<c.out.count;++k)if(Inside(c.out.controls[k].rect,BoxOf(b[i]))) {b[i].flags|=blobDead;break;}
    }
    // Icon-sized blobs.
    for(int i=0;i<n;++i) {
        if(dead(i))continue;
        const Rect r=BoxOf(b[i]);
        const int small=Min(r.w,r.h),big=Max(r.w,r.h);
        if(small<c.minIcon || (b[i].pieces>=5 && small<2*c.minIcon))continue;   // many small pieces side by side: text nobody read, not a picture
        bool part=false;
        for(int j=0;j<n && !part;++j)part=j!=i && !dead(j) && Inside(BoxOf(b[j]),r);
        if(part)continue;                                // lies within a larger picture: a part of it
        uint32_t ring=0;const bool uniform=Ring(c,r,ring);
        const bool plate=uniform && !Near(ring,c.bg,ringTolerance);
        if(big<=c.maxIcon && (big*10<=small*22 || (plate && big<=small*4))) {
            Add(c,Role::iconItem,r,45,uint8_t((plate?featPlate:0)|((b[i].flags&blobFramed)?featFramed:0)),ring,ring);
        } else if(big<=small*4 && int64_t(r.w)*r.h*2<int64_t(c.area.w)*c.area.h) Add(c,Role::image,r,25);
    }
    GroupKind(c,first,Role::iconItem);
    GroupKind(c,first,Role::button);
    GroupKind(c,first,Role::checkbox);
    // An icon alone on its line belongs to the picker when it sits right above or
    // below a real row and is not much smaller than its members; otherwise it is a picture.
    Control* k=c.out.controls;
    for(int i=first;i<c.out.count;++i) {
        if(k[i].role!=Role::iconItem || k[i].group)continue;
        bool attached=false;
        const Rect& a=k[i].rect;
        for(int g=1;g<=c.out.groups && !attached;++g) {
            int x0=1<<30,x1=0,y0=1<<30,y1=0,size=0,members=0;
            for(int j=first;j<c.out.count;++j) {
                if(k[j].role!=Role::iconItem || k[j].group!=g)continue;
                const Rect& m=k[j].rect;x0=Min(x0,m.x);x1=Max(x1,Right(m));y0=Min(y0,m.y);y1=Max(y1,Bottom(m));size+=Max(m.w,m.h);++members;
            }
            if(members<2)continue;
            const int centre=a.x+a.w/2,distance=a.y>=y1?a.y-y1:y0>=Bottom(a)?y0-Bottom(a):0;
            attached=centre>=x0 && centre<x1 && distance*2<=a.h*3 && Max(a.w,a.h)*2>=size/members;
        }
        if(attached && c.out.groups<255) {k[i].group=++c.out.groups;k[i].index=1;k[i].count=1;}
        else {k[i].role=Role::image;k[i].confidence=Cap(c,30);}
    }
    SelectAmong(c,first,Role::iconItem);
    SelectAmong(c,first,Role::button);
    // Groups made mostly of plates are tiles; a single plate was the selection background.
    for(int g=1;g<=c.out.groups;++g) {
        int members=0,plates=0;
        for(int i=first;i<c.out.count;++i)if(k[i].role==Role::iconItem && k[i].group==g) {++members;if(ws.feature[i]&featPlate)++plates;}
        if(members>=2 && plates>=Max(2,members/2+1))for(int i=first;i<c.out.count;++i)if(k[i].role==Role::iconItem && k[i].group==g)k[i].role=Role::tile;
    }
    // A button whose text is much fainter than its siblings' is unavailable.
    for(int i=first;i<c.out.count;++i) {
        if(k[i].role!=Role::button || !k[i].group || !(ws.feature[i]&featInk))continue;
        int strongest=0;
        for(int j=first;j<c.out.count;++j)if(j!=i && k[j].role==Role::button && k[j].group==k[i].group)strongest=Max(strongest,ws.contrast[j]);
        if(strongest>=60 && ws.contrast[i]*5<strongest*2)k[i].state|=stateDisabled;
    }
}
// Labels, by geometry only. A text line is used once.
//  1. inside: the line's centre is inside the control (the smallest such
//     control). For a field the text goes to `value`; for an icon only when
//     the line is in the lower 35 % of its box (a caption on the plate).
//  2. below (icons, tiles, pictures): the line starts no lower than half the
//     control's height (at least 24 pixels) under it, its centre is within the
//     control's width, it is not wider than 1.8 x the control and the nearest
//     such control wins.
//  3. beside / above: check box - the line starts right of the box within
//     three box widths and overlaps it vertically; field and slider - the line
//     ends left of it within max(40, 2 x height) pixels and overlaps it
//     vertically, else it ends at most 1.2 line heights above it with its left
//     edge within 12 pixels of the control's.
//  4. status: when exactly one icon or tile is selected and it has no label,
//     the first unused line under the lowest row (within 2.5 x the selected
//     control's height), centred within 6 % of the screen width on the centre
//     of all the icons. That is how rEFInd and Clover name the selection.
inline void AssignLabels(Context& c) {
    Control* k=c.out.controls;Workspace& ws=c.ws;
    auto usable=[&](size_t i) {const TextLine& l=c.lines[i];return !ws.lineUsed[i] && l.text && l.text[0] && l.w>0 && l.h>0;};
    for(size_t i=0;i<c.lineCount;++i) {
        if(!usable(i))continue;
        const TextLine& l=c.lines[i];const int cx=l.x+l.w/2,cy=l.y+l.h/2;
        int best=-1;int64_t bestArea=0;
        for(int j=0;j<c.out.count;++j) {
            const Rect& r=k[j].rect;
            if(cx<r.x || cx>=Right(r) || cy<r.y || cy>=Bottom(r))continue;
            if(k[j].role==Role::slider || k[j].role==Role::pointer || k[j].role==Role::image || k[j].role==Role::listItem)continue;
            if(k[j].role==Role::iconItem && cy<r.y+r.h*65/100)continue;
            const int64_t area=int64_t(r.w)*r.h;
            if(best<0 || area<bestArea) {best=j;bestArea=area;}
        }
        if(best<0)continue;
        if(k[best].role==Role::field)CopyText(k[best].value,l.text,k[best].value[0]!=0);
        else {CopyText(k[best].label,l.text,k[best].label[0]!=0);k[best].labelSource=LabelSource::inside;}
        ws.lineUsed[i]=1;
    }
    for(size_t i=0;i<c.lineCount;++i) {
        if(!usable(i))continue;
        const TextLine& l=c.lines[i];const int cx=l.x+l.w/2;
        int best=-1,bestDistance=0;
        for(int j=0;j<c.out.count;++j) {
            if(!Iconish(k[j].role) || k[j].label[0])continue;
            const Rect& r=k[j].rect;
            if(l.y<Bottom(r)-2 || l.y>Bottom(r)+Max(24,r.h/2) || cx<r.x || cx>=Right(r) || l.w*10>r.w*18)continue;
            const int distance=Abs(cx-(r.x+r.w/2))+(l.y-Bottom(r));
            if(best<0 || distance<bestDistance) {best=j;bestDistance=distance;}
        }
        if(best<0)continue;
        CopyText(k[best].label,l.text,false);k[best].labelSource=LabelSource::below;ws.lineUsed[i]=1;
    }
    for(int j=0;j<c.out.count;++j) {
        if(k[j].label[0])continue;
        const Rect& r=k[j].rect;
        if(k[j].role==Role::checkbox) {
            for(size_t i=0;i<c.lineCount;++i) {
                if(!usable(i))continue;
                const TextLine& l=c.lines[i];
                if(l.x<Right(r) || l.x>Right(r)+3*r.w || l.y>=Bottom(r) || l.y+l.h<=r.y)continue;
                CopyText(k[j].label,l.text,false);k[j].labelSource=LabelSource::beside;ws.lineUsed[i]=1;
                k[j].confidence=uint8_t(Min(100,k[j].confidence+15));break;
            }
        } else if(k[j].role==Role::field || k[j].role==Role::slider) {
            int best=-1;LabelSource source=LabelSource::none;
            for(size_t i=0;i<c.lineCount && best<0;++i) {
                if(!usable(i))continue;
                const TextLine& l=c.lines[i];
                if(l.x+l.w<=r.x && r.x-(l.x+l.w)<=Max(40,2*r.h) && l.y<Bottom(r) && l.y+l.h>r.y) {best=int(i);source=LabelSource::beside;}
            }
            for(size_t i=0;i<c.lineCount && best<0;++i) {
                if(!usable(i))continue;
                const TextLine& l=c.lines[i];
                if(l.y+l.h<=r.y+1 && r.y-(l.y+l.h)<=l.h*12/10 && Abs(l.x-r.x)<=12) {best=int(i);source=LabelSource::above;}
            }
            if(best>=0) {
                CopyText(k[j].label,c.lines[best].text,false);k[j].labelSource=source;ws.lineUsed[best]=1;
                if(k[j].role==Role::field)k[j].confidence=uint8_t(Min(100,k[j].confidence+15));
            }
        }
    }
    int selected=-1,selectedCount=0,x0=1<<30,x1=0,bottom=0;
    for(int j=0;j<c.out.count;++j) {
        if(k[j].role!=Role::iconItem && k[j].role!=Role::tile)continue;
        if(ws.level[j]!=0)continue;
        x0=Min(x0,k[j].rect.x);x1=Max(x1,Right(k[j].rect));bottom=Max(bottom,Bottom(k[j].rect));
        if(k[j].state&stateSelected) {selected=j;++selectedCount;}
    }
    if(selectedCount==1 && !k[selected].label[0]) {
        int best=-1;
        for(size_t i=0;i<c.lineCount;++i) {
            if(!usable(i))continue;
            const TextLine& l=c.lines[i];
            if(l.y<bottom || l.y>bottom+k[selected].rect.h*5/2 || Abs(l.x+l.w/2-(x0+x1)/2)>int(c.f.width)*6/100)continue;
            if(best<0 || l.y<c.lines[best].y)best=int(i);
        }
        if(best>=0) {CopyText(k[selected].label,c.lines[best].text,false);k[selected].labelSource=LabelSource::status;ws.lineUsed[best]=1;}
    }
}
inline void Scale(Context& c) {
    const int h=int(c.f.height);
    c.minIcon=Max(16,h/50);c.maxIcon=Max(160,h/3);c.maxButton=Max(64,h/10);c.gap=Max(4,h/130);c.verticalGap=Max(2,h/260);
}
inline void SetFocused(Result& out) {
    // The one focused or selected control; several -> the single most certain one, else none.
    int best=-1;bool tie=false;
    for(int i=0;i<out.count;++i) {
        const Control& k=out.controls[i];
        if(!(k.state&(stateFocused|stateSelected)))continue;
        if(best<0 || k.stateConfidence>out.controls[best].stateConfidence) {best=i;tie=false;}
        else if(k.stateConfidence==out.controls[best].stateConfidence)tie=true;
    }
    out.focused=int8_t(best>=0 && !tie?best:-1);
}
}  // namespace detail

// Finds the controls of one frame. `lines` may be null (then no control has a label).
inline void Analyse(const Frame& frame,const TextLine* lines,size_t lineCount,Workspace& ws,Result& out) {
    using namespace detail;
    out.count=0;out.focused=-1;out.confidence=0;out.backgroundCoverage=0;out.groups=0;out.truncated=false;out.background=0;
    ws.truncated=false;ws.panelCount=0;ws.blobCount=0;
    for(auto& used:ws.lineUsed)used=0;
    if(!frame.pixels || frame.width<32 || frame.height<32 || frame.stride<frame.width)return;
    if(!lines)lineCount=0;
    if(lineCount>maxLines)lineCount=maxLines;
    Context c{frame,lines,lineCount,ws,out,0,0,0,Rect{}};
    Scale(c);
    Rect all;all.w=int(frame.width);all.h=int(frame.height);
    int coverage=0;const uint32_t background=Mode(frame,all,ws,coverage);
    out.background=background;out.backgroundCoverage=uint8_t(coverage);
    ws.panels[0].rect=all;ws.panels[0].background=background;ws.panels[0].coverage=uint8_t(coverage);ws.panels[0].depth=0;ws.panelCount=1;
    for(uint32_t p=0;p<ws.panelCount;++p) {const Panel panel=ws.panels[p];Level(c,panel);}
    AssignLabels(c);
    if(ws.truncated)out.truncated=true;
    out.confidence=uint8_t(coverage<35?20:coverage<55?45:coverage<70?70:90);
    if(out.truncated && out.confidence>35)out.confidence=35;
    SetFocused(out);
}

// --- Two frames ---------------------------------------------------------------

struct PointerFind {
    bool found=false,hadPrevious=false;
    uint8_t confidence=0;
    int32_t x=0,y=0;       // top left corner of the pointer's picture: where an arrow points
    Rect rect,previous;    // the pointer now, and where it was
};
namespace detail {
// Changed areas between two frames of the same size -> ws.blobs (close ones merged).
inline bool Changes(const Frame& before,const Frame& after,Workspace& ws,int join) {
    ws.truncated=false;ws.blobCount=0;
    if(!before.pixels || !after.pixels || before.width!=after.width || before.height!=after.height || after.width<32 || after.height<32)return false;
    Rect all;all.w=int(after.width);all.h=int(after.height);
    LabelCells(ws,all,[&](int x,int y) {return !Near(Px(before,x,y),Px(after,x,y),8);});
    Blob* b=ws.blobs;const int n=int(ws.blobCount);
    for(int pass=0;pass<6;++pass) {
        bool merged=false;
        for(int i=0;i<n;++i) {
            if(b[i].flags&blobDead)continue;
            for(int j=i+1;j<n;++j) {
                if(b[j].flags&blobDead)continue;
                if(Max(b[i].x0,b[j].x0)-Min(b[i].x1,b[j].x1)>join || Max(b[i].y0,b[j].y0)-Min(b[i].y1,b[j].y1)>join)continue;
                b[i].x0=Min(b[i].x0,b[j].x0);b[i].y0=Min(b[i].y0,b[j].y0);b[i].x1=Max(b[i].x1,b[j].x1);b[i].y1=Max(b[i].y1,b[j].y1);
                b[i].cells+=b[j].cells;b[j].flags|=blobDead;merged=true;
            }
        }
        if(!merged)break;
    }
    return true;
}
// The colour around a box (its surroundings), from a ring three pixels outside it.
inline uint32_t Surround(const Frame& f,const Rect& r) {
    uint32_t samples[64];int n=0;
    const int x0=Max(0,r.x-3),y0=Max(0,r.y-3),x1=Min(int(f.width)-1,Right(r)+2),y1=Min(int(f.height)-1,Bottom(r)+2);
    for(int i=0;i<16;++i) {
        const int x=x0+(x1-x0)*i/15,y=y0+(y1-y0)*i/15;
        samples[n++]=Px(f,x,y0);samples[n++]=Px(f,x,y1);samples[n++]=Px(f,x0,y);samples[n++]=Px(f,x1,y);
    }
    int share=0;return ListMode(samples,n,ringTolerance,share);
}
// Pixels of a box that differ from `colour`, and their bounding box.
inline int InkIn(const Frame& f,const Rect& r,uint32_t colour,Rect& box) {
    int count=0,x0=Right(r),y0=Bottom(r),x1=r.x,y1=r.y;
    for(int y=r.y;y<Bottom(r);++y)for(int x=r.x;x<Right(r);++x)if(!Near(Px(f,x,y),colour,16)) {++count;x0=Min(x0,x);y0=Min(y0,y);x1=Max(x1,x+1);y1=Max(y1,y+1);}
    box.x=x0;box.y=y0;box.w=count?x1-x0:0;box.h=count?y1-y0:0;
    return count;
}
}
// The mouse pointer, from two frames between which it moved: a small area that
// gained ink (where it is now) and, usually, a small area that lost it (where
// it was). `expectedDirection` (optional): the direction the caller pushed the
// pointer, used only to decide which of two areas is the new one when the
// background under them does not tell (a picture).
// Not found: the pointer did not move, moved under one of its own pixels only,
// or more than a pointer-sized area changed in a pointer-like way.
inline bool FindPointer(const Frame& before,const Frame& after,Workspace& ws,PointerFind& out,const Point* expectedDirection=nullptr) {
    using namespace detail;
    out=PointerFind{};
    if(!Changes(before,after,ws,6))return false;
    const int limit=Max(48,int(after.height)/14);
    Blob* b=ws.blobs;const int n=int(ws.blobCount);
    int gain=-1,gains=0,loss=-1,losses=0,mixed=-1,mixedCount=0,others=0;
    Rect gainBox,lossBox,mixedNow,mixedWas;
    for(int i=0;i<n;++i) {
        if(b[i].flags&blobDead)continue;
        Rect r=BoxOf(b[i]);
        if(r.w>limit || r.h>limit) {++others;continue;}
        Rect now,was;
        const int inkNow=InkIn(after,r,Surround(after,r),now),inkWas=InkIn(before,r,Surround(before,r),was);
        if(inkNow>=2*inkWas+6) {gain=i;++gains;gainBox=now;}
        else if(inkWas>=2*inkNow+6) {loss=i;++losses;lossBox=was;}
        else {mixed=i;++mixedCount;mixedNow=now;mixedWas=was;}
    }
    if(gains==1 && losses<=1) {
        out.found=true;out.rect=gainBox;out.x=gainBox.x;out.y=gainBox.y;
        int confidence=others || mixedCount?65:85;
        if(losses==1) {
            out.hadPrevious=true;out.previous=lossBox;
            if(Abs(lossBox.w-gainBox.w)>3 || Abs(lossBox.h-gainBox.h)>3)confidence-=15;
        }
        out.confidence=uint8_t(confidence);
        return true;
    }
    if(!gains && !losses && mixedCount==1) {
        // The old and the new picture overlap: the same shape, shifted.
        if(Abs(mixedNow.w-mixedWas.w)<=2 && Abs(mixedNow.h-mixedWas.h)<=2 && mixedNow.w>=4 && (mixedNow.x!=mixedWas.x || mixedNow.y!=mixedWas.y)) {
            out.found=true;out.rect=mixedNow;out.x=mixedNow.x;out.y=mixedNow.y;out.hadPrevious=true;out.previous=mixedWas;
            out.confidence=uint8_t(others?55:70);
            return true;
        }
        return false;
    }
    if(expectedDirection && !gains && !losses && mixedCount==2 && (expectedDirection->x || expectedDirection->y)) {
        // Two pointer-sized areas over a picture: the one further along the push is the new one.
        int first=-1,second=-1;
        for(int i=0;i<n;++i) {const Rect r=BoxOf(b[i]);if((b[i].flags&blobDead) || r.w>limit || r.h>limit)continue;if(first<0)first=i;else second=i;}
        const Rect a=BoxOf(b[first]),z=BoxOf(b[second]);
        const int64_t along=int64_t(z.x-a.x)*expectedDirection->x+int64_t(z.y-a.y)*expectedDirection->y;
        if(!along)return false;
        out.found=true;out.rect=along>0?z:a;out.previous=along>0?a:z;out.hadPrevious=true;out.x=out.rect.x;out.y=out.rect.y;out.confidence=50;
        return true;
    }
    (void)gain;(void)loss;(void)mixed;
    return false;
}
// Adds the pointer to a result as a control of role `pointer` (for one list to walk).
inline bool AppendPointer(Result& out,const PointerFind& pointer) {
    if(!pointer.found || out.count>=maxControls)return false;
    Control& k=out.controls[out.count++];
    k=Control{};k.role=Role::pointer;k.rect=pointer.rect;k.confidence=pointer.confidence;
    return true;
}

struct FocusChange {
    bool found=false,hasLost=false;
    uint8_t confidence=0;
    int8_t control=-1;     // index in the result of `after`; -1 = the change is on no known control
    Rect gained,lost;      // where ink appeared (the new focus) and where it went away
};
// The focus from two frames (before and after a key): the changed area that
// gained the most non-background pixels - a frame, a glow, a plate or a
// highlight bar that appeared - and the control of `afterResult` under it.
// When a control is found it is marked (selected for icons, tiles, list rows
// and tabs; focused otherwise), the mark is taken from its siblings, and
// afterResult.focused is set. `afterResult` must come from Analyse(after).
inline bool FocusFromDifference(const Frame& before,const Frame& after,Workspace& ws,Result& afterResult,FocusChange& out) {
    using namespace detail;
    out=FocusChange{};
    const uint32_t bg=afterResult.background;
    if(!Changes(before,after,ws,4))return false;
    Blob* b=ws.blobs;const int n=int(ws.blobCount);
    int best=0,second=0,worst=0;
    for(int i=0;i<n;++i) {
        if(b[i].flags&blobDead)continue;
        const Rect r=BoxOf(b[i]);
        if(Max(r.w,r.h)<12)continue;
        int gain=0;
        for(int y=r.y;y<Bottom(r);++y)for(int x=r.x;x<Right(r);++x) {
            const uint32_t now=Px(after,x,y),was=Px(before,x,y);
            if(Near(now,was,8))continue;
            gain+=(Near(now,bg,backgroundTolerance)?0:1)-(Near(was,bg,backgroundTolerance)?0:1);
        }
        if(gain>best) {second=best;best=gain;out.gained=r;}
        else if(gain>second)second=gain;
        if(gain<worst) {worst=gain;out.lost=r;out.hasLost=true;}
    }
    if(best<24)return false;
    out.found=true;
    int control=-1;int64_t most=0;
    for(int i=0;i<afterResult.count;++i) {
        const Control& k=afterResult.controls[i];
        if(k.role==Role::pointer || k.role==Role::image || k.role==Role::unknown)continue;
        const int64_t overlap=Overlap(k.rect,out.gained);
        const int64_t smaller=Min(k.rect.w*k.rect.h,out.gained.w*out.gained.h);
        if(overlap*10<smaller*3)continue;
        if(overlap>most) {most=overlap;control=i;}
    }
    out.control=int8_t(control);
    if(control<0) {out.confidence=40;return true;}
    out.confidence=uint8_t(best>=2*second?80:60);
    Control& target=afterResult.controls[control];
    const bool selects=target.role==Role::iconItem || target.role==Role::tile || target.role==Role::listItem || target.role==Role::tab;
    const uint8_t bit=selects?stateSelected:stateFocused;
    for(int i=0;i<afterResult.count;++i) {
        Control& k=afterResult.controls[i];
        if(i==control || !(k.state&(stateSelected|stateFocused)))continue;
        if(selects && (k.role==Role::tab || k.role==Role::listItem) && k.group!=target.group)continue;   // another tab bar or list keeps its own selection
        k.state&=uint8_t(~(stateSelected|stateFocused));k.stateConfidence=0;
    }
    target.state|=bit;
    if(target.stateConfidence<out.confidence)target.stateConfidence=out.confidence;
    afterResult.focused=int8_t(control);
    return true;
}

// --- Walking ------------------------------------------------------------------

// Reading order for keyboard walking: rows from the top, each from the left.
// Two controls are on one row when the centre of one lies within the height
// of the other. Writes indices into `order`; returns how many.
inline size_t ReadingOrder(const Result& result,uint8_t* order,size_t capacity) {
    using namespace detail;
    size_t n=0;
    for(int i=0;i<result.count && n<capacity;++i)order[n++]=uint8_t(i);
    auto rect=[&](size_t i) -> const Rect& {return result.controls[order[i]].rect;};
    for(size_t i=1;i<n;++i)for(size_t j=i;j>0 && rect(j).y+rect(j).h/2<rect(j-1).y+rect(j-1).h/2;--j) {const uint8_t s=order[j];order[j]=order[j-1];order[j-1]=s;}
    size_t start=0;
    while(start<n) {
        size_t end=start+1;
        const Rect& head=rect(start);
        for(;end<n;++end) {
            const Rect& r=rect(end);const int centre=r.y+r.h/2,headCentre=head.y+head.h/2;
            if(!((centre>=head.y && centre<Bottom(head)) || (headCentre>=r.y && headCentre<Bottom(r))))break;
        }
        for(size_t i=start+1;i<end;++i)for(size_t j=i;j>start && rect(j).x<rect(j-1).x;--j) {const uint8_t s=order[j];order[j]=order[j-1];order[j-1]=s;}
        start=end;
    }
    return n;
}
// The next (direction > 0) or previous control in reading order; -1 at the end.
// from = -1 starts at the first (or last) control.
inline int Step(const Result& result,int from,int direction) {
    uint8_t order[maxControls];const size_t n=ReadingOrder(result,order,maxControls);
    if(!n)return -1;
    if(from<0)return direction>0?order[0]:order[n-1];
    for(size_t i=0;i<n;++i)if(order[i]==from) {
        if(direction>0)return i+1<n?order[i+1]:-1;
        return i?order[i-1]:-1;
    }
    return -1;
}
// Where to click a control: its centre.
inline Point ClickPoint(const Control& control) {
    Point p;p.x=control.rect.x+control.rect.w/2;p.y=control.rect.y+control.rect.h/2;return p;
}
// Where to click a slider's track to set it to `percent` (0..100).
inline Point SliderPoint(const Control& slider,unsigned percent) {
    if(percent>100)percent=100;
    Point p;p.x=slider.rect.x+int(int64_t(slider.rect.w-1)*percent/100);p.y=slider.rect.y+slider.rect.h/2;return p;
}
// Closed-loop pointer walking: the reader pushes the pointer by `counts`
// (EFI_SIMPLE_POINTER RelativeMovement units), finds it again with
// FindPointer(), and tells Learn() how far it went. Next() then gives the
// counts for the rest of the way. Programs scale differently (rEFInd:
// counts * mouse_speed / resolution; Clover and OpenCanopy their own), so the
// scale starts as "unknown" and the first push is a small probe.
struct PointerWalk {
    int32_t scaleX=0,scaleY=0;   // pixels per 1024 counts; 0 = not yet measured
    void Learn(int32_t countsX,int32_t countsY,int32_t movedX,int32_t movedY) {
        if(countsX && movedX && (countsX>0)==(movedX>0))scaleX=int32_t(int64_t(movedX)*1024/countsX);
        if(countsY && movedY && (countsY>0)==(movedY>0))scaleY=int32_t(int64_t(movedY)*1024/countsY);
    }
    // Counts to push now. `probe` = counts used while an axis is not yet measured;
    // `maxPixels` limits one step so that an error in the scale cannot overshoot far.
    void Next(Point from,Point to,int32_t probe,int32_t maxPixels,int32_t& countsX,int32_t& countsY) const {
        auto axis=[&](int32_t distance,int32_t scale) -> int32_t {
            if(!distance)return 0;
            if(scale<=0)return distance>0?probe:-probe;
            if(distance>maxPixels)distance=maxPixels;
            if(distance<-maxPixels)distance=-maxPixels;
            int32_t counts=int32_t(int64_t(distance)*1024/scale);
            if(!counts)counts=distance>0?1:-1;
            return counts;
        };
        countsX=axis(to.x-from.x,scaleX);countsY=axis(to.y-from.y,scaleY);
    }
};
// Is the point on the control? (the pointer has arrived)
inline bool On(const Control& control,Point p) {
    return p.x>=control.rect.x && p.y>=control.rect.y && p.x<control.rect.x+control.rect.w && p.y<control.rect.y+control.rect.h;
}
}  // namespace hakefi::pixelcontrols
