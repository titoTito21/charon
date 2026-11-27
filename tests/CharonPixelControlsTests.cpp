// Host tests of src/efi/CharonPixelControls.h (r410). GPL-2.0-or-later.
// Usage: CharonPixelControlsTests [DIR_WITH_PPM] [--dump]
// (default tests/charon-screens/pixel, run from the source root).
// Synthetic frames are drawn here; the expected values for the recorded
// screens were read off the pictures by eye (see pixel/README.txt), not taken
// from the recogniser's output.
#include "CharonPixelControls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
using namespace hakefi::pixelcontrols;
using hakefi::pixelcontrols::detail::Abs;
static int checks=0,failed=0;
static void Check(bool ok,const char* what) {++checks;if(!ok)++failed;printf("%s %s\n",ok?"PASS":"FAIL",what);}
static Workspace ws;static Result result,result2;
constexpr int SW=800,SH=600;
static uint32_t canvas[SW*SH],canvas2[SW*SH];
static void Fill(uint32_t* c,int x,int y,int w,int h,uint32_t colour) {for(int j=y;j<y+h;++j)for(int i=x;i<x+w;++i)if(i>=0 && j>=0 && i<SW && j<SH)c[j*SW+i]=colour;}
static void Outline(uint32_t* c,int x,int y,int w,int h,int t,uint32_t colour) {Fill(c,x,y,w,t,colour);Fill(c,x,y+h-t,w,t,colour);Fill(c,x,y,t,h,colour);Fill(c,x+w-t,y,t,h,colour);}
static void Disc(uint32_t* c,int cx,int cy,int r,uint32_t colour) {for(int j=-r;j<=r;++j)for(int i=-r;i<=r;++i)if(i*i+j*j<=r*r)Fill(c,cx+i,cy+j,1,1,colour);}
// "Text": a row of small strokes, as wide as asked.
static void Ink(uint32_t* c,int x,int y,int w,int h,uint32_t colour) {for(int i=0;i+3<=w;i+=6)Fill(c,x+i,y+(i/6%2),3,h-1,colour);}
static void Arrow(uint32_t* c,int x,int y,uint32_t colour) {for(int j=0;j<16;++j)Fill(c,x,y+j,j<11?j+1:(j<14?4:2),1,colour);}
static Frame Synthetic(const uint32_t* c) {Frame f;f.pixels=c;f.width=SW;f.height=SH;f.stride=SW;return f;}
static bool Same(const char16_t* a,const char* b) {size_t i=0;for(;a[i] && b[i];++i)if(a[i]!=char16_t(b[i]))return false;return !a[i] && !b[i];}
static const char* RoleName(Role r) {static const char* n[]={"unknown","iconItem","tile","button","field","slider","tab","checkbox","listItem","pointer","image"};return n[int(r)];}
static void Dump(const char* name,const Result& r) {
    printf("  [%s] controls %d focused %d confidence %d background %06x %d%% groups %d%s\n",name,r.count,r.focused,r.confidence,unsigned(r.background),r.backgroundCoverage,r.groups,r.truncated?" TRUNCATED":"");
    for(int i=0;i<r.count;++i) {
        const Control& k=r.controls[i];char label[labelCap];size_t n=0;for(;k.label[n];++n)label[n]=k.label[n]<127?char(k.label[n]):'?';label[n]=0;
        printf("   %2d %-9s %4d,%4d %4dx%-4d g%d %d/%d state %02x conf %d/%d pct %d '%s'\n",i,RoleName(k.role),k.rect.x,k.rect.y,k.rect.w,k.rect.h,k.group,k.index,k.count,k.state,k.confidence,k.stateConfidence,k.percent,label);
    }
}
static bool dump=false;
// Controls of one role, left to right then top to bottom, optionally within a y band.
static int Collect(const Result& r,Role role,int* out,int y0=0,int y1=1<<30) {
    int n=0;
    for(int i=0;i<r.count;++i)if(r.controls[i].role==role && r.controls[i].rect.y>=y0 && r.controls[i].rect.y<y1)out[n++]=i;
    for(int i=1;i<n;++i)for(int j=i;j>0 && r.controls[out[j]].rect.x<r.controls[out[j-1]].rect.x;--j) {int s=out[j];out[j]=out[j-1];out[j-1]=s;}
    return n;
}
static int SelectedAmong(const Result& r,const int* list,int n) {int at=-1,count=0;for(int i=0;i<n;++i)if(r.controls[list[i]].state&(stateSelected|stateFocused)) {at=i;++count;}return count==1?at+1:count?-count:0;}

// --- synthetic ------------------------------------------------------------
static void IconRow(uint32_t* c,int selected,bool glow) {
    Fill(c,0,0,SW,SH,0x00203060);
    for(int i=0;i<4;++i) {
        const int cx=200+i*110,cy=250;
        if(i==selected && glow)for(int r=40;r>24;--r)Disc(c,cx,cy,r,0x00203060+uint32_t(44-r)*0x00080808);
        Disc(c,cx,cy,24,0x00e0a040);Disc(c,cx-6,cy-6,8,0x00ffffff);
        if(i==selected && !glow)Outline(c,cx-34,cy-34,68,68,2,0x00ffffff);
    }
}
static void TestIconFrame() {
    IconRow(canvas,1,false);
    const TextLine lines[]={{176,290,48,12,u"Alpha"},{396,290,48,12,u"Gamma"}};
    for(const auto& l:lines)Ink(canvas,l.x,l.y,l.w,l.h,0x00ffffff);
    Analyse(Synthetic(canvas),lines,2,ws,result);if(dump)Dump("icon frame",result);
    int list[maxControls];const int n=Collect(result,Role::iconItem,list);
    Check(n==4,"synthetic frame row: four icons");
    Check(n==4 && result.controls[list[0]].count==4 && result.controls[list[3]].index==4 && result.controls[list[0]].index==1,"synthetic frame row: index 1..4 of 4");
    Check(SelectedAmong(result,list,n)==2,"synthetic frame row: the framed icon (2) is selected");
    Check(n==4 && result.focused==list[1] && result.controls[list[1]].stateConfidence>=confident,"synthetic frame row: focused = icon 2, confident");
    Check(n==4 && Same(result.controls[list[0]].label,"Alpha") && result.controls[list[0]].labelSource==LabelSource::below,"synthetic frame row: label under icon 1");
    Check(n==4 && Same(result.controls[list[2]].label,"Gamma"),"synthetic frame row: label under icon 3");
    Check(n==4 && !result.controls[list[1]].label[0] && result.controls[list[1]].labelSource==LabelSource::none && !result.controls[list[3]].label[0],"synthetic frame row: no line near icons 2 and 4 -> no label invented");
    Check(result.confidence>=confident && n==4 && result.controls[list[0]].confidence>=confident,"synthetic frame row: confident on a flat background");
    // The same picture with no text lines at all: the unread ink under the icons is not a control and nothing is labelled.
    Analyse(Synthetic(canvas),nullptr,0,ws,result);
    bool anyLabel=false;for(int i=0;i<result.count;++i)if(result.controls[i].label[0])anyLabel=true;
    Check(!anyLabel && Collect(result,Role::iconItem,list)==4,"synthetic frame row without OCR: four icons, no labels");
}
static void TestIconGlow() {
    IconRow(canvas,2,true);
    Analyse(Synthetic(canvas),nullptr,0,ws,result);if(dump)Dump("icon glow",result);
    int list[maxControls];const int n=Collect(result,Role::iconItem,list);
    Check(n==4,"synthetic glow row: four icons");
    Check(SelectedAmong(result,list,n)==3,"synthetic glow row: the glowing icon (3) is selected");
    Check(n==4 && result.controls[list[2]].stateConfidence>0 && result.controls[list[2]].stateConfidence<confident,"synthetic glow row: a glow alone is reported below the confident mark");
    // Two frames: the glow moves from icon 3 to icon 1.
    IconRow(canvas2,0,true);
    Analyse(Synthetic(canvas2),nullptr,0,ws,result2);
    FocusChange change;const bool found=FocusFromDifference(Synthetic(canvas),Synthetic(canvas2),ws,result2,change);
    const int m=Collect(result2,Role::iconItem,list);
    Check(found && m==4 && change.control==list[0] && result2.focused==list[0],"synthetic glow row, two frames: the focus went to icon 1");
    Check(found && change.hasLost && change.lost.x<=400 && change.lost.x+change.lost.w>=420,"synthetic glow row, two frames: it left icon 3");
    Check(found && m==4 && result2.controls[list[0]].stateConfidence>=confident,"synthetic glow row, two frames: confident");
}
static void Dialog(uint32_t* c,int focusFrame) {
    Fill(c,0,0,SW,SH,0x00c0c0c0);
    const char* unused=nullptr;(void)unused;
    for(int i=0;i<3;++i) {
        const int x=100+i*160;
        Fill(c,x,400,120,32,0x00e8e8e8);Outline(c,x,400,120,32,1,i==1 && focusFrame<0?0x000000ff:0x00404040);
        Ink(c,x+40,410,40,12,i==2?0x00d0d0d0:0x00000000);   // the third is greyed
        if(i==focusFrame)Outline(c,x-5,395,130,42,1,0x00000000);
    }
    Outline(c,220,100,300,26,1,0x00404040);Ink(c,226,107,60,12,0x00000000);          // field with left-aligned text
    Ink(c,100,107,90,12,0x00000000);                                                // its caption, to the left
    Outline(c,220,150,300,26,1,0x00404040);Fill(c,226,154,2,18,0x00000000);         // empty field with a caret
    Outline(c,100,200,16,16,1,0x00000000);Fill(c,104,204,8,8,0x00000000);Ink(c,124,202,80,12,0x00000000);   // checked
    Outline(c,100,230,16,16,1,0x00000000);Ink(c,124,232,80,12,0x00000000);                                  // unchecked
    Fill(c,220,300,300,4,0x00606060);Fill(c,220+118-6,292,12,20,0x00303030);        // slider: knob at 40 % of the travel
}
static const TextLine dialogLines[]={{140,410,40,12,u"One"},{300,410,40,12,u"Two"},{460,410,40,12,u"Exit"},{226,107,60,12,u"charon"},{100,107,90,12,u"Name:"},
    {124,202,80,12,u"Speak"},{124,232,80,12,u"Braille"}};
static void TestDialog() {
    Dialog(canvas,-1);
    Analyse(Synthetic(canvas),dialogLines,7,ws,result);if(dump)Dump("dialog",result);
    int list[maxControls];int n=Collect(result,Role::button,list);
    Check(n==3,"synthetic dialog: three buttons");
    Check(n==3 && Same(result.controls[list[0]].label,"One") && Same(result.controls[list[1]].label,"Two") && Same(result.controls[list[2]].label,"Exit"),"synthetic dialog: button labels from the lines inside");
    Check(n==3 && result.controls[list[0]].labelSource==LabelSource::inside && result.controls[list[0]].confidence>=confident,"synthetic dialog: caption inside, confident");
    Check(n==3 && result.controls[list[1]].index==2 && result.controls[list[1]].count==3,"synthetic dialog: button 2 of 3");
    Check(SelectedAmong(result,list,n)==2,"synthetic dialog: the button with the other border colour is focused");
    Check(n==3 && (result.controls[list[2]].state&stateDisabled) && !(result.controls[list[0]].state&stateDisabled),"synthetic dialog: the faint button is disabled");
    n=Collect(result,Role::field,list);
    for(int i=1;i<n;++i)for(int j=i;j>0 && result.controls[list[j]].rect.y<result.controls[list[j-1]].rect.y;--j) {int s=list[j];list[j]=list[j-1];list[j-1]=s;}
    Check(n==2,"synthetic dialog: two fields");
    Check(n==2 && Same(result.controls[list[0]].label,"Name:") && result.controls[list[0]].labelSource==LabelSource::beside && Same(result.controls[list[0]].value,"charon"),"synthetic dialog: field caption beside, content as value");
    Check(n==2 && (result.controls[list[1]].state&stateCaret) && !result.controls[list[1]].label[0],"synthetic dialog: the field with a caret, no caption -> none");
    n=Collect(result,Role::checkbox,list);
    for(int i=1;i<n;++i)for(int j=i;j>0 && result.controls[list[j]].rect.y<result.controls[list[j-1]].rect.y;--j) {int s=list[j];list[j]=list[j-1];list[j-1]=s;}
    Check(n==2,"synthetic dialog: two check boxes");
    Check(n==2 && (result.controls[list[0]].state&stateChecked) && !(result.controls[list[1]].state&stateChecked),"synthetic dialog: first checked, second not");
    Check(n==2 && Same(result.controls[list[0]].label,"Speak") && Same(result.controls[list[1]].label,"Braille"),"synthetic dialog: check box captions beside");
    n=Collect(result,Role::slider,list);
    Check(n==1,"synthetic dialog: one slider");
    Check(n==1 && result.controls[list[0]].percent>=38 && result.controls[list[0]].percent<=42,"synthetic dialog: slider at 40 percent");
    if(n==1) {
        const Point p=SliderPoint(result.controls[list[0]],100),q=ClickPoint(result.controls[list[0]]);
        Check(p.x==519 && q.x==370,"synthetic dialog: slider click points (100 % at the right end, centre)");
    } else Check(false,"synthetic dialog: slider click points");
    // Reading order: fields, check boxes, slider, then the buttons left to right.
    uint8_t order[maxControls];const size_t count=ReadingOrder(result,order,maxControls);
    bool ordered=count==result.count;
    for(size_t i=1;i<count && ordered;++i) {
        const Rect& a=result.controls[order[i-1]].rect;const Rect& b=result.controls[order[i]].rect;
        if(b.y+b.h/2<a.y && b.x<a.x)ordered=false;
    }
    Check(ordered && count>=8 && result.controls[order[count-1]].role==Role::button && Same(result.controls[order[count-1]].label,"Exit")
        && result.controls[order[0]].role==Role::field,"synthetic dialog: reading order ends with the last button");
    Check(Step(result,-1,1)==order[0] && Step(result,order[0],1)==order[1] && Step(result,order[count-1],1)==-1 && Step(result,order[1],-1)==order[0],"synthetic dialog: stepping through the controls");
    // Focus shown only by a frame, found from one frame (the framed button) and from two.
    Dialog(canvas2,0);
    Analyse(Synthetic(canvas2),dialogLines,7,ws,result2);if(dump)Dump("dialog frame",result2);
    n=Collect(result2,Role::button,list);
    Check(n==3 && SelectedAmong(result2,list,n)==1,"synthetic dialog: focus frame around button 1, single frame");
    Dialog(canvas,2);
    FocusChange change;
    Analyse(Synthetic(canvas),dialogLines,7,ws,result);
    const bool found=FocusFromDifference(Synthetic(canvas2),Synthetic(canvas),ws,result,change);
    n=Collect(result,Role::button,list);
    Check(found && n==3 && change.control==list[2] && result.focused==list[2],"synthetic dialog: focus frame moved to button 3, two frames");
}
static void Tabs(uint32_t* c,bool filled,int open) {
    const uint32_t desk=filled?0x00205080:0x00d0d0d0,panel=filled?0x00d0d0d0:desk,line=0x00303030;
    Fill(c,0,0,SW,SH,desk);
    Fill(c,100,130,500,300,panel);Outline(c,100,130,500,300,1,line);
    for(int i=0;i<3;++i) {
        const int x=100+i*90;
        Fill(c,x,104,90,26,i==open?panel:(filled?0x00a0a0a0:panel));
        Fill(c,x,104,90,1,line);Fill(c,x,104,1,27,line);Fill(c,x+89,104,1,27,line);
        if(i==open)Fill(c,x+1,130,88,1,panel);   // no line between the open tab and the panel
        Ink(c,x+25,112,40,12,0x00000000);
    }
    Fill(c,400,380,120,30,0x00f0f0f0);Outline(c,400,380,120,30,1,line);Ink(c,440,389,40,12,0x00000000);
}
static void TestTabs() {
    const TextLine lines[]={{125,112,40,12,u"Main"},{215,112,40,12,u"Boot"},{305,112,40,12,u"Exit"},{440,389,40,12,u"Save"}};
    for(int style=0;style<2;++style) {
        Tabs(canvas,style==1,1);
        Analyse(Synthetic(canvas),lines,4,ws,result);if(dump)Dump(style?"tabs filled":"tabs outlined",result);
        int list[maxControls];const int n=Collect(result,Role::tab,list);
        Check(n==3,style?"synthetic tabs (filled): three tabs":"synthetic tabs (outlined): three tabs");
        Check(SelectedAmong(result,list,n)==2,style?"synthetic tabs (filled): the tab joined to the panel (2) is open":"synthetic tabs (outlined): the tab joined to the panel (2) is open");
        Check(n==3 && Same(result.controls[list[0]].label,"Main") && Same(result.controls[list[2]].label,"Exit") && result.controls[list[2]].index==3,style?"synthetic tabs (filled): labels and positions":"synthetic tabs (outlined): labels and positions");
        const int b=Collect(result,Role::button,list);
        Check(b==1 && Same(result.controls[list[0]].label,"Save"),style?"synthetic tabs (filled): the button inside the panel":"synthetic tabs (outlined): the button inside the panel");
    }
}
static void TestTiles() {
    Fill(canvas,0,0,SW,SH,0x00101010);
    for(int i=0;i<4;++i) {Fill(canvas,80+i*170,200,150,110,i==3?0x000078d7:0x00404040);Disc(canvas,80+i*170+75,240,20,0x00ffffff);Ink(canvas,80+i*170+45,285,60,12,0x00ffffff);}
    const TextLine lines[]={{125,285,60,12,u"Windows"},{295,285,60,12,u"Linux"},{465,285,60,12,u"Setup"},{635,285,60,12,u"Off"}};
    Analyse(Synthetic(canvas),lines,4,ws,result);if(dump)Dump("tiles",result);
    int list[maxControls];const int n=Collect(result,Role::tile,list);
    Check(n==4,"synthetic tiles: four tiles");
    Check(SelectedAmong(result,list,n)==4,"synthetic tiles: the tile of another colour (4) is selected");
    Check(n==4 && Same(result.controls[list[1]].label,"Linux") && result.controls[list[1]].labelSource==LabelSource::inside,"synthetic tiles: caption inside the tile");
}
static void TestList() {
    Fill(canvas,0,0,SW,SH,0x00000000);
    Fill(canvas,100,196,500,24,0x00c0c0c0);
    const TextLine lines[]={{110,170,120,14,u"Windows Setup"},{110,201,150,14,u"Memory Diagnostic"},{110,232,90,14,u"Firmware"},{300,400,200,14,u"ENTER=Choose"}};
    for(int i=0;i<4;++i)Ink(canvas,lines[i].x,lines[i].y,lines[i].w,lines[i].h,i==1?0x00000000:0x00c0c0c0);
    Analyse(Synthetic(canvas),lines,4,ws,result);if(dump)Dump("list",result);
    int list[maxControls];int n=0;
    for(int i=0;i<result.count;++i)if(result.controls[i].role==Role::listItem)list[n++]=i;
    Check(n==3,"synthetic list: three rows (the far line is not one)");
    Check(n==3 && (result.controls[list[1]].state&stateSelected) && !(result.controls[list[0]].state&stateSelected) && result.controls[list[1]].index==2 && result.controls[list[1]].count==3,"synthetic list: the row under the bar is selected, 2 of 3");
    Check(n==3 && Same(result.controls[list[1]].label,"Memory Diagnostic") && Same(result.controls[list[2]].label,"Firmware"),"synthetic list: row texts");
}
static void TestPointer() {
    Fill(canvas,0,0,SW,SH,0x00bfbfbf);Disc(canvas,400,300,30,0x00208020);
    memcpy(canvas2,canvas,sizeof(canvas));
    Arrow(canvas,100,100,0x00000000);Arrow(canvas2,160,130,0x00000000);
    PointerFind p;
    Check(FindPointer(Synthetic(canvas),Synthetic(canvas2),ws,p) && p.x==160 && p.y==130,"synthetic pointer: found at its new place");
    Check(p.found && p.hadPrevious && p.previous.x==100 && p.previous.y==100 && p.confidence>=confident,"synthetic pointer: old place known, confident");
    Check(!FindPointer(Synthetic(canvas),Synthetic(canvas),ws,p) && !p.found,"synthetic pointer: no movement -> not found");
    // A short move: the two pictures overlap.
    memcpy(canvas2,canvas,sizeof(canvas));Arrow(canvas2,100,100,0x00bfbfbf);Arrow(canvas2,104,103,0x00000000);
    Check(FindPointer(Synthetic(canvas),Synthetic(canvas2),ws,p) && Abs(p.x-104)<=1 && Abs(p.y-103)<=1,"synthetic pointer: overlapping move found");
    // A whole icon appearing is not a pointer.
    memcpy(canvas2,canvas,sizeof(canvas));Disc(canvas2,600,300,60,0x00802020);
    Check(!FindPointer(Synthetic(canvas),Synthetic(canvas2),ws,p),"synthetic pointer: a large change is not a pointer");
    // Closed loop: a program that moves 3 pixels per 2 counts; target 300 right, 90 up.
    PointerWalk walk;Point at;at.x=100;at.y=400;Point target;target.x=400;target.y=310;int steps=0;
    for(;steps<12 && (Abs(at.x-target.x)>1 || Abs(at.y-target.y)>1);++steps) {
        int32_t cx=0,cy=0;walk.Next(at,target,8,200,cx,cy);
        const int mx=cx*3/2,my=cy*3/2;walk.Learn(cx,cy,mx,my);at.x+=mx;at.y+=my;
    }
    Check(Abs(at.x-target.x)<=1 && Abs(at.y-target.y)<=1 && steps<=6,"pointer walk: reaches the target in a few closed-loop steps");
    PointerFind found;found.found=true;found.rect.x=5;found.rect.y=6;found.rect.w=12;found.rect.h=16;found.confidence=80;
    result.count=0;
    Check(AppendPointer(result,found) && result.count==1 && result.controls[0].role==Role::pointer && On(result.controls[0],Point{6,7}),"pointer as a control");
}
static void TestBusyBackground() {
    uint32_t seed=12345;
    for(int i=0;i<SW*SH;++i) {seed=seed*1664525u+1013904223u;canvas[i]=seed>>8;}
    Analyse(Synthetic(canvas),nullptr,0,ws,result);
    bool anyConfident=false;for(int i=0;i<result.count;++i)if(result.controls[i].confidence>=confident || result.controls[i].stateConfidence>=confident)anyConfident=true;
    Check(result.confidence<confident && !anyConfident && result.focused==-1,"noise picture: nothing is claimed with confidence");
    Frame bad;Analyse(bad,nullptr,0,ws,result);
    Check(result.count==0 && result.confidence==0,"no frame: empty result");
}

// --- recorded screens -----------------------------------------------------
static uint32_t* LoadPpm(const char* directory,const char* name,Frame& frame) {
    char path[600];snprintf(path,sizeof(path),"%s/%s",directory,name);
    FILE* file=fopen(path,"rb");if(!file) {printf("cannot open %s\n",path);return nullptr;}
    int width=0,height=0,maximum=0;char magic[3]={};
    if(fscanf(file,"%2s",magic)!=1 || strcmp(magic,"P6")) {fclose(file);return nullptr;}
    int values[3],got=0;
    while(got<3) {
        int ch=fgetc(file);
        if(ch==EOF) {fclose(file);return nullptr;}
        if(ch=='#') {while(ch!='\n' && ch!=EOF)ch=fgetc(file);continue;}
        if(ch>='0' && ch<='9') {int v=0;while(ch>='0' && ch<='9') {v=v*10+ch-'0';ch=fgetc(file);}values[got++]=v;}
    }
    width=values[0];height=values[1];maximum=values[2];
    if(maximum!=255 || width<=0 || height<=0 || width>4096 || height>4096) {fclose(file);return nullptr;}
    uint32_t* pixels=(uint32_t*)malloc(size_t(width)*height*4);
    unsigned char* row=(unsigned char*)malloc(size_t(width)*3);
    for(int y=0;y<height;++y) {
        if(fread(row,3,size_t(width),file)!=size_t(width)) {free(row);free(pixels);fclose(file);return nullptr;}
        for(int x=0;x<width;++x)pixels[size_t(y)*width+x]=uint32_t(row[x*3])<<16|uint32_t(row[x*3+1])<<8|row[x*3+2];
    }
    free(row);fclose(file);
    frame.pixels=pixels;frame.width=uint32_t(width);frame.height=uint32_t(height);frame.stride=uint32_t(width);
    return pixels;
}
// One icon row of a recorded screen: count and which is selected (0 = none), by eye.
static void Row(const char* screen,const char* what,const Result& r,int y0,int y1,int expectedCount,int expectedSelected,bool mustBeConfident) {
    int list[maxControls];int n=Collect(r,Role::iconItem,list,y0,y1);
    if(!n)n=Collect(r,Role::tile,list,y0,y1);
    char text[200];
    snprintf(text,sizeof(text),"%s: %s has %d icons (found %d)",screen,what,expectedCount,n);Check(n==expectedCount,text);
    const int selected=SelectedAmong(r,list,n);
    snprintf(text,sizeof(text),"%s: %s selected %d (found %d)",screen,what,expectedSelected,selected);Check(selected==expectedSelected,text);
    bool positions=n==expectedCount;
    for(int i=0;i<n && positions;++i)if(r.controls[list[i]].index!=i+1 || r.controls[list[i]].count!=n)positions=false;
    snprintf(text,sizeof(text),"%s: %s positions 1..%d of %d",screen,what,expectedCount,expectedCount);Check(positions,text);
    if(mustBeConfident && expectedSelected>0) {
        const bool ok=selected==expectedSelected && r.controls[list[selected-1]].stateConfidence>=confident && r.controls[list[selected-1]].confidence>=(n>1?confident:40);
        snprintf(text,sizeof(text),"%s: %s selection is confident",screen,what);Check(ok,text);
    }
}
static void TestRecorded(const char* directory) {
    Frame a,b;uint32_t* pa;uint32_t* pb;
    // rEFInd 0.14.2, 1280x800: a row of three large OS icons (plate under the selected one), a row of six small tool icons.
    pa=LoadPpm(directory,"refind-00-start.ppm",a);pb=LoadPpm(directory,"refind-01-right.ppm",b);
    Check(pa && pb,"rEFInd pictures load");
    if(pa && pb) {
        const TextLine lines[]={{480,568,312,14,u"Boot EFI\\probe\\GPROBE.EFI from GRUBTEST"},{528,586,224,14,u"Automatic boot in 19 seconds"},
            {460,758,360,14,u"Use arrow keys to move cursor; Enter to boot;"},{384,772,512,14,u"Insert, Tab, or F2 for more options; Esc or Backspace to refresh"}};
        Analyse(a,lines,4,ws,result);if(dump)Dump("refind 00",result);
        Row("rEFInd start","OS row",result,300,420,3,1,true);
        Row("rEFInd start","tool row",result,480,540,6,0,false);
        Check(result.focused>=0 && result.controls[result.focused].rect.x<430 && result.controls[result.focused].rect.y<340,"rEFInd start: focused control is the first OS icon");
        Check(result.focused>=0 && result.controls[result.focused].labelSource==LabelSource::status && Same(result.controls[result.focused].label,"Boot EFI\\probe\\GPROBE.EFI from GRUBTEST"),"rEFInd start: the line under the rows names the selection (status)");
        Check(result.confidence>=confident,"rEFInd start: picture confidence");
        int labelled=0;for(int i=0;i<result.count;++i)if(result.controls[i].label[0])++labelled;
        Check(labelled==1,"rEFInd start: no other control got a label");
        const TextLine lines2[]={{484,568,304,14,u"Boot EFI\\probe\\PROBE.EFI from GRUBTEST"}};
        Analyse(b,lines2,1,ws,result2);if(dump)Dump("refind 01",result2);
        Row("rEFInd right","OS row",result2,300,420,3,2,true);
        Row("rEFInd right","tool row",result2,480,540,6,0,false);
        // Without any OCR line: same icons (the unread text must not become icons).
        Analyse(b,nullptr,0,ws,result2);if(dump)Dump("refind 01 no ocr",result2);
        Row("rEFInd right, no OCR","OS row",result2,300,420,3,2,true);
        Row("rEFInd right, no OCR","tool row",result2,480,540,6,0,false);
        FocusChange change;const bool found=FocusFromDifference(a,b,ws,result2,change);
        Check(found && change.control>=0 && result2.controls[change.control].rect.x>540 && result2.controls[change.control].rect.x<600 && result2.controls[change.control].index==2,"rEFInd start -> right, two frames: focus went to OS icon 2");
        Check(found && change.hasLost && change.lost.x<440,"rEFInd start -> right, two frames: it left OS icon 1");
        PointerFind pointer;Check(!FindPointer(a,b,ws,pointer),"rEFInd start -> right: no pointer reported (none moved)");
    }
    free(pa);free(pb);
    // Clover, 1600x900: one OS entry (plate when selected), a row of five tool icons; a pointer at 400,228.
    pa=LoadPpm(directory,"clover-00-start.ppm",a);pb=LoadPpm(directory,"clover-01-right.ppm",b);
    Check(pa && pb,"Clover pictures load");
    if(pa && pb) {
        const TextLine lines[]={{634,584,334,14,u"Boot Microsoft EFI Boot from GRUBTEST"},{16,870,64,12,u"F1:Help"}};
        Analyse(a,lines,2,ws,result);if(dump)Dump("clover 00",result);
        Row("Clover start","OS row",result,320,480,1,1,false);
        Row("Clover start","tool row",result,490,540,5,0,false);
        Check(result.focused>=0 && Same(result.controls[result.focused].label,"Boot Microsoft EFI Boot from GRUBTEST") && result.controls[result.focused].labelSource==LabelSource::status,"Clover start: status line names the selected entry");
        const TextLine lines2[]={{716,584,170,14,u"Clover Boot Options"}};
        Analyse(b,lines2,1,ws,result2);if(dump)Dump("clover 01",result2);
        Row("Clover right","OS row",result2,320,480,1,0,false);
        Row("Clover right","tool row",result2,490,540,5,1,true);
        FocusChange change;const bool found=FocusFromDifference(a,b,ws,result2,change);
        Check(found && change.control>=0 && result2.controls[change.control].rect.y>480 && result2.controls[change.control].index==1 && result2.controls[change.control].count==5,"Clover start -> right, two frames: focus went to tool icon 1 of 5");
    }
    free(pa);free(pb);
    // OpenCanopy, 1280x800: three volume icons with captions under them, plate under the selected; two buttons at the bottom.
    pa=LoadPpm(directory,"canopy-00-start.ppm",a);pb=LoadPpm(directory,"canopy-01-right.ppm",b);
    Check(pa && pb,"OpenCanopy pictures load");
    if(pa && pb) {
        const TextLine lines[]={{466,465,46,11,u"CHARON"},{612,465,58,11,u"GRUBTEST"},{771,465,44,11,u"Windows"},{1131,771,100,11,u"REL-108-2026-09-27"}};
        Analyse(a,lines,4,ws,result);if(dump)Dump("canopy 00",result);
        Row("OpenCanopy start","picker",result,300,400,3,1,true);
        int list[maxControls];const int n=Collect(result,Role::iconItem,list,300,400);
        Check(n==3 && Same(result.controls[list[0]].label,"CHARON") && Same(result.controls[list[1]].label,"GRUBTEST") && Same(result.controls[list[2]].label,"Windows")
            && result.controls[list[1]].labelSource==LabelSource::below,"OpenCanopy start: captions under the three icons");
        Row("OpenCanopy start","bottom buttons",result,700,760,2,0,false);
        Analyse(b,lines,4,ws,result2);if(dump)Dump("canopy 01",result2);
        Row("OpenCanopy right","picker",result2,300,400,3,2,true);
        Check(result2.focused>=0 && Same(result2.controls[result2.focused].label,"GRUBTEST"),"OpenCanopy right: focused control is GRUBTEST");
        FocusChange change;const bool found=FocusFromDifference(a,b,ws,result2,change);
        Check(found && change.control>=0 && Same(result2.controls[change.control].label,"GRUBTEST"),"OpenCanopy start -> right, two frames: focus went to GRUBTEST");
    }
    free(pa);free(pb);
    // Windows Boot Manager, 1280x800 (drawn text): a highlight bar over the first of two entries.
    pa=LoadPpm(directory,"winmenu-00-start.ppm",a);
    Check(pa!=nullptr,"Windows Boot Manager picture loads");
    if(pa) {
        const TextLine lines[]={{496,34,240,18,u"Windows Boot Manager"},{220,184,156,18,u"Windows Setup"},{220,214,300,18,u"Windows Memory Diagnostic"},{160,544,72,18,u"Tools:"},
            {220,604,300,18,u"Windows Memory Diagnostic"}};
        Analyse(a,lines,5,ws,result);if(dump)Dump("winmenu 00",result);
        int list[maxControls];int n=0;for(int i=0;i<result.count;++i)if(result.controls[i].role==Role::listItem)list[n++]=i;
        Check(n==2 && (result.controls[list[0]].state&stateSelected) && Same(result.controls[list[0]].label,"Windows Setup") && result.controls[list[0]].count==2,"Windows Boot Manager: list of two, 'Windows Setup' selected");
        int buttons=0;for(int i=0;i<result.count;++i)if(result.controls[i].role==Role::button && result.controls[i].confidence>=confident)++buttons;
        Check(buttons==0,"Windows Boot Manager: the title and key bars are not buttons");
    }
    free(pa);
    // GRUB with a theme, 800x600: a photograph as background. Nothing may be claimed with confidence.
    pa=LoadPpm(directory,"grubtheme-00-start.ppm",a);
    Check(pa!=nullptr,"GRUB theme picture loads");
    if(pa) {
        Analyse(a,nullptr,0,ws,result);if(dump)Dump("grubtheme 00",result);
        bool sure=false;for(int i=0;i<result.count;++i)if((result.controls[i].state&(stateSelected|stateFocused)) && result.controls[i].stateConfidence>=confident)sure=true;
        Check(!sure,"GRUB theme (photograph background): no selection claimed with confidence");
        printf("  note: GRUB theme picture confidence %d, background share %d%%, %d controls\n",result.confidence,result.backgroundCoverage,result.count);
    }
    free(pa);
}
int main(int argc,char** argv) {
    const char* directory="tests/charon-screens/pixel";
    for(int i=1;i<argc;++i) {if(!strcmp(argv[i],"--dump"))dump=true;else directory=argv[i];}
    printf("Workspace %u KB, Result %u KB\n",unsigned(sizeof(Workspace)/1024),unsigned(sizeof(Result)/1024));
    TestIconFrame();TestIconGlow();TestDialog();TestTabs();TestTiles();TestList();TestPointer();TestBusyBackground();
    TestRecorded(directory);
    printf("%d checks, %d failed\n",checks,failed);
    return failed;
}
