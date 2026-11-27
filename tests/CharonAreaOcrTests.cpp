// Host test of the reader's whole screen-text path on recorded frame buffers (r410).
//
// The resident reader cannot be built on the host (it lives on UEFI protocols), so this file MIRRORS the part of
// CharonResident.h that turns pixels into the text grid - RecognizeArea (the crop, the three readers in their
// order, the line filters, the row a baseline goes to, the row's right end) and CaptureCells - around the REAL
// readers CharonOcr.h, CharonAaOcr.h and CharonTtfOcr.h. When RecognizeArea changes, change AreaReader::Area here.
// Colours are left out (the cases below are about text).
//
//   CharonAreaOcrTests DIR                      the recorded cases (DIR = tests/charon-screens/pixel); PASS or FAIL
//   CharonAreaOcrTests --dump MANAGER FILE.ppm [--image FILE]... [--ttf DIR] [--after PREVIOUS.ppm]
//        prints the grid the reader would hold after reading FILE.ppm as one area (after PREVIOUS: only the
//        16-row bands that differ, as the reader's poll does). MANAGER: GRUB, Clover, Limine, OpenCore, Windows,
//        rEFInd, none. --image: a program image (or a font PNG) searched for console fonts as the reader does.
//   Define AREA_OCR_R409_RULES to build with the placement rules as they were before this batch.
// Link with CharonTtf.c (stb_truetype).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "CharonOcr.h"
#include "CharonAaOcr.h"
#include "CharonTtfOcr.h"
#include "CharonPng.h"
using namespace hakefi;

static std::vector<uint8_t> ReadFile(const std::string& path) {
    std::vector<uint8_t> data;FILE* f=std::fopen(path.c_str(),"rb");
    if(!f)return data;
    std::fseek(f,0,SEEK_END);data.resize(size_t(std::ftell(f)));std::fseek(f,0,SEEK_SET);
    if(std::fread(data.data(),1,data.size(),f)!=data.size())data.clear();
    std::fclose(f);return data;
}
struct Picture {std::vector<uint32_t> px;size_t w=0,h=0;};
static bool ReadPpm(const std::string& path,Picture& p) {
    std::vector<uint8_t> d=ReadFile(path);
    if(d.size()<16 || d[0]!='P' || d[1]!='6')return false;
    size_t pos=2,values[3],n=0;
    while(n<3) {
        while(pos<d.size() && (d[pos]==' ' || d[pos]=='\n' || d[pos]=='\r' || d[pos]=='\t'))++pos;
        if(d[pos]=='#') {while(pos<d.size() && d[pos]!='\n')++pos;continue;}
        size_t v=0;while(pos<d.size() && d[pos]>='0' && d[pos]<='9')v=v*10+(d[pos++]-'0');
        values[n++]=v;
    }
    ++pos;p.w=values[0];p.h=values[1];
    if(d.size()<pos+p.w*p.h*3)return false;
    p.px.resize(p.w*p.h);
    for(size_t i=0;i<p.w*p.h;++i)p.px[i]=uint32_t(d[pos+i*3])<<16|uint32_t(d[pos+i*3+1])<<8|d[pos+i*3+2];
    return true;
}
static std::string Utf8(const char16_t* t,size_t n) {
    std::string s;
    for(size_t i=0;i<n;++i) {
        const unsigned c=t[i];
        if(c<0x80)s+=char(c);
        else if(c<0x800) {s+=char(0xc0|(c>>6));s+=char(0x80|(c&0x3f));}
        else {s+=char(0xe0|(c>>12));s+=char(0x80|((c>>6)&0x3f));s+=char(0x80|(c&0x3f));}
    }
    return s;
}
static void* HostAlloc(void*,size_t bytes) {return std::calloc(1,bytes);}
static void HostFree(void*,void* block) {std::free(block);}

struct AreaReader {
    static constexpr size_t maxColumns=160,maxRows=64;
    char16_t text_[maxRows][maxColumns];uint16_t cellFont_[maxRows][maxColumns];uint16_t captureFont_=0;
    size_t caretLineX_[maxRows];uint16_t lineBaseline_[maxRows];
    PixelOcr ocr_;AlphaOcr aaOcr_;TtfOcr ttfOcr_;
    const char16_t* bootManager_=nullptr;
    bool suspectRead_=false;unsigned suspectRetries_=0;
    std::vector<std::vector<uint8_t>> keep_;   // font data must outlive the reader
    AreaReader() {Clear();ttfOcr_.SetAllocator(HostAlloc,HostFree,nullptr);}
    void Clear() {
        for(size_t r=0;r<maxRows;++r) {for(size_t c=0;c<maxColumns;++c) {text_[r][c]=u' ';cellFont_[r][c]=0;}caretLineX_[r]=SIZE_MAX;lineBaseline_[r]=0;}
    }
    static bool GrubFamily(const char16_t* name) {return name && (name[0]==u'G' || (name[0]==u'V' && name[1]==u'e' && name[2]==u'n'));}
    void Manager(const char* name) {
        static char16_t wide[32];size_t n=0;
        if(!std::strcmp(name,"none")) {bootManager_=nullptr;return;}
        for(;name[n] && n<31;++n)wide[n]=char16_t(name[n]);
        wide[n]=0;bootManager_=wide;
        if(GrubFamily(bootManager_))for(unsigned k=0;k<pf2FontCount;++k)ttfOcr_.AddPf2Font(pf2Fonts[k]);
    }
    // As the reader's LoadFontsFrom + LoadGridFont: console fonts inside a program image.
    void Image(const std::string& path) {
        keep_.push_back(ReadFile(path));const uint8_t* image=keep_.back().data();const size_t size=keep_.back().size();
        struct Style {uint8_t a[8];size_t stride;bool lowBitLeft;};
        static const Style styles[]={{{0x38,0x6c,0xc6,0xc6,0xc6,0xc6,0xfe,0xc6},16,false},{{0x10,0x38,0x6c,0xc6,0xc6,0xfe,0xc6,0xc6},16,false},{{0x3e,0x63,0x63,0x63,0x7f,0x63,0x63,0x63},14,true}};
        for(const Style& st:styles)for(size_t i=0;i+8<=size;++i) {
            if(image[i]!=st.a[0] || image[i+1]!=st.a[1])continue;
            bool match=true;for(size_t k=2;k<8 && match;++k)match=image[i+k]==st.a[k];
            if(!match)continue;
            const size_t lead=st.stride==16?2:0;
            if(i<lead+33*st.stride || i-lead+62*st.stride+st.stride>size)continue;
            const size_t glyphA=i-lead,space=glyphA-33*st.stride,b=glyphA+st.stride;
            bool empty=true;for(size_t k=0;k<st.stride;++k)empty=empty && !image[space+k];
            bool inked=false;for(size_t k=0;k<st.stride;++k)inked=inked || image[b+k];
            if(!empty || !inked)continue;
            uint16_t* copy=static_cast<uint16_t*>(std::calloc(4096,2));
            for(size_t c=32;c<127;++c)for(size_t r=0;r<st.stride && r<16;++r) {
                uint8_t v=image[glyphA+(c-65)*st.stride+r];
                if(st.lowBitLeft) {uint8_t m=0;for(int bit=0;bit<8;++bit)if(v&(1<<bit))m|=uint8_t(0x80>>bit);v=m;}
                copy[c*16+r]=uint16_t(v<<8);
            }
            if(!ttfOcr_.AddCellFont(copy,8,16,8))std::free(copy);
            break;
        }
        static const uint8_t signature[8]={0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a};
        for(size_t i=0;i+33<=size;++i) {
            if(image[i]!=0x89 || image[i+1]!='P')continue;
            bool match=true;for(size_t k=2;k<8 && match;++k)match=image[i+k]==signature[k];
            if(!match)continue;
            const uint8_t* h=image+i+16;
            const size_t width=size_t(h[0])<<24|size_t(h[1])<<16|size_t(h[2])<<8|h[3],height=size_t(h[4])<<24|size_t(h[5])<<16|size_t(h[6])<<8|h[7];
            if(h[9]!=3 || width%16 || height%16 || width<64 || width>256 || height<96 || height>384)continue;
            const size_t rowBytes=(width*h[8]+7)/8,workSize=height*(1+rowBytes)+size_t(64)*1024;
            std::vector<uint8_t> work(workSize),index(width*height);uint16_t* table=static_cast<uint16_t*>(std::calloc(256*24,2));
            PngImage png;size_t cellWidth=0,cellHeight=0;
            if(DecodePalettePng(image+i,size-i,work.data(),workSize,index.data(),width*height,png) && GridFont(index.data(),png,table,cellWidth,cellHeight)) {
                if(!ttfOcr_.AddCellFont(table,cellWidth,cellHeight,cellWidth>8?cellWidth-2:cellWidth))std::free(table);
                return;
            }
            std::free(table);
        }
    }
    void Ttf(const std::string& dir) {
        for(const char* name:{"segoe_slboot.ttf","segoen_slboot.ttf","segmono_boot.ttf","wgl4_boot.ttf"}) {
            keep_.push_back(ReadFile(dir+"/"+name));
            if(!keep_.back().empty())ttfOcr_.AddFont(keep_.back().data(),keep_.back().size());
        }
    }
    void CaptureCells(size_t x,size_t y,const char16_t* text) {
        size_t column=x/8;const size_t row=y/16;
        if(row>=maxRows || column>=maxColumns)return;
        const uint16_t font=captureFont_;captureFont_=0;
        for(;*text && column<maxColumns;++text) {
            if(*text<32 || *text==127)continue;
            text_[row][column]=*text;cellFont_[row][column]=font;++column;
        }
    }
    // ---- mirror of ResidentReader::RecognizeArea -----------------------------------------------------------
    size_t Area(const uint32_t* area,size_t width,size_t height,size_t stride,size_t left,size_t top) {
        const size_t right=left+width,bottom=top+height,clearLeft=left,clearTop=top;
        {
            const uint32_t c=area[0]&0xffffff;size_t minX=width,maxX=0,minY=height,maxY=0;
            for(size_t y=0;y<height;++y) {
                const uint32_t* row=area+y*stride;size_t x=0;
                while(x<width && (row[x]&0xffffff)==c)++x;
                if(x==width)continue;
                if(x<minX)minX=x;
                size_t e=width;while(e>x && (row[e-1]&0xffffff)==c)--e;
                if(e-1>maxX)maxX=e-1;
                if(minY==height)minY=y;
                maxY=y;
            }
            if(minY==height)return 0;
            const size_t y0=minY>=16?minY-16:0,y1=maxY+17<height?maxY+17:height;
            size_t x0=minX>=16?minX-16:0,x1=maxX+17<width?maxX+17:width;
            {const size_t ink=maxX+1-minX,want=ink*2<width?ink*2:width;
             if(x1-x0<want) {const size_t more=want-(x1-x0),leftMore=more/2<x0?more/2:x0;x0-=leftMore;x1=x1+(more-leftMore)<width?x1+(more-leftMore):width;
                 if(x1-x0<want)x0=x1>=want?x1-want:0;}}
            area+=y0*stride+x0;left+=x0;top+=y0;width=x1-x0;height=y1-y0;
        }
        size_t lines=0;
        bool rowCleared[maxRows]={};
        bool baselineRows=false;
        auto emit=[&](const char16_t* text,size_t x,size_t y,uint32_t,uint32_t) {
            {bool strokes=true,any=false;
             for(const char16_t* p=text;*p && strokes;++p) {if(*p==u' ')continue;any=true;strokes=*p==u'l' || *p==u'I' || *p==u'|' || *p==u'!';}
             if(any && strokes)return;}
            char16_t inner[maxColumns+1];
            {size_t n=0;while(text[n])++n;
             auto stroke=[](char16_t c) {return c==u'l' || c==u'I' || c==u'|' || c==u'j';};
             if(n>=5 && n<=maxColumns && stroke(text[0]) && text[1]==u' ' && text[n-1]==text[0] && text[n-2]==u' ') {
                 size_t s=2,e=n-2;while(s<e && text[s]==u' ')++s;while(e>s && text[e-1]==u' ')--e;
                 if(e>s) {for(size_t k=s;k<e;++k)inner[k-s]=text[k];inner[e-s]=0;x+=s*8;text=inner;}
             }}
            if(!lines++) {
                const size_t firstRow=baselineRows?(clearTop+35)/16:(clearTop+15)/16;
                const size_t endRow=baselineRows?(bottom>=20?(bottom-20)/16+1:0):(bottom>=31?(bottom-31)/16+1:0);
                const size_t firstColumn=(clearLeft+7)/8,endColumn=right>=15?(right-15)/8+1:0;
                for(size_t r=firstRow;r<endRow && r<maxRows;++r)for(size_t c=firstColumn;c<endColumn && c<maxColumns;++c) {text_[r][c]=u' ';cellFont_[r][c]=0;}
            }
            {const size_t row=y/16;
             if(row<maxRows && !rowCleared[row]) {
                 rowCleared[row]=true;
                 const size_t c0=(clearLeft+7)/8,c1=right>=15?(right-15)/8+1:0;
                 for(size_t c=c0;c<c1 && c<maxColumns;++c) {text_[row][c]=u' ';cellFont_[row][c]=0;}
             }}
#ifndef AREA_OCR_R409_RULES
            // r410: a line that ends past the grid's last column keeps the right end of its row.
            {size_t n=0;while(text[n])++n;const size_t row=y/16;
             if(row<maxRows && n && n<maxColumns && x/8+n>maxColumns) {
                 const size_t start=maxColumns-n;bool free=true;
                 for(size_t c=start-1;c<maxColumns && free;++c)free=text_[row][c]==u' ' || (cellFont_[row][c]&0x8000);
                 if(free) {
                     for(size_t c=0;c<maxColumns;++c)if(cellFont_[row][c]&0x8000) {text_[row][c]=u' ';cellFont_[row][c]=0;}
                     x=start*8;captureFont_|=0x8000;
                 }
             }}
#endif
            CaptureCells(x,y,text);
        };
        size_t wordLines=0;
        auto unifont=[&](const char16_t* text,size_t x,size_t y,uint32_t fg,uint32_t bg) {
            bool word=false;
            for(const char16_t* p=text;*p && !word;++p)word=(*p>=u'0' && *p<=u'9') || (*p>=u'A' && *p<=u'Z') || (*p>=u'a' && *p<=u'z') || (*p>=0xc0 && *p<0x2500);
            if(!word)return;
            ++wordLines;captureFont_=uint16_t((1<<8)|16);emit(text,x,y,fg,bg);
        };
        ocr_.bandsEnabled=!bootManager_ || GrubFamily(bootManager_);
        ocr_.bandsAlways=GrubFamily(bootManager_);
        size_t recognized=ocr_.Recognize(area,width,height,stride,left,top,unifont,[]() {});
        if(!wordLines)recognized=0;
        if(!recognized) {
            auto blended=[&](const char16_t* text,size_t x,size_t y,uint32_t fg,uint32_t bg,size_t h) {captureFont_=uint16_t((3<<8)|(h<255?h:255));emit(text,x,y,fg,bg);};
            auto placed=[&](const char16_t* text,size_t x,size_t y,uint32_t fg,uint32_t bg,size_t h) {
                captureFont_=uint16_t((4<<8)|(h<255?h:255)|(ttfOcr_.LastBold()?0x4000:0));
                size_t row=y/16;const size_t column=x/8;
#ifndef AREA_OCR_R409_RULES
                const size_t baseline=y;
                auto moved=[&](size_t r) {const size_t was=lineBaseline_[r];return was && (was>baseline?was-baseline:baseline-was)<=2;};
#else
                auto moved=[&](size_t) {return true;};
#endif
                if(row<maxRows && column<maxColumns && text_[row][column]==u' ') {
                    if(row && text_[row-1][column]!=u' ' && (y%16)<=1 && moved(row-1))y-=16;
                    else if(row+1<maxRows && text_[row+1][column]!=u' ' && (y%16)>=14 && moved(row+1))y+=16;
                }
                {bool suspect=false;
                 for(const char16_t* p=text;*p && !suspect;++p)if(*p==u'!') {
                    const char16_t next=p[1];
                    suspect=(next>=u'a' && next<=u'z') || next==u'!';}
                 if(suspect && suspectRetries_<2) {suspectRead_=true;return;}}
                {const size_t r2=y/16;if(r2<maxRows && caretLineX_[r2]==SIZE_MAX)caretLineX_[r2]=x;}
#ifndef AREA_OCR_R409_RULES
                {const size_t r2=y/16;if(r2<maxRows)lineBaseline_[r2]=uint16_t(baseline);}
#endif
                emit(text,x,y,fg,bg);
            };
            auto fonts=[&]() {
                for(size_t r2=0;r2<maxRows;++r2)caretLineX_[r2]=SIZE_MAX;
                baselineRows=true;
                const size_t n=ttfOcr_.Recognize(area,width,height,stride,left,top,placed,[]() {});
                baselineRows=false;
                {size_t cx=0,cy=0;uint32_t ink=0;ttfOcr_.TakeCaret(cx,cy,ink);}
                return n;
            };
            const bool ownFirst=ttfOcr_.FontCount() && bootManager_ && bootManager_[0]!=u'r';
            if(ownFirst)recognized=fonts();
            if(!recognized) {
                recognized=aaOcr_.Recognize(area,width,height,stride,left,top,blended,[]() {});
                if(!recognized && ttfOcr_.FontCount() && !ownFirst)recognized=fonts();
            }
        }
        return recognized;
    }
    // The reader's poll: the 16-row bands that differ from the picture before, two bands of margin.
    size_t Poll(const Picture& now,const Picture* before) {
        size_t first=SIZE_MAX,last=0;
        for(size_t band=0;band*16<now.h;++band) {
            bool differs=!before;
            for(size_t y=band*16;y<band*16+16 && y<now.h && !differs;++y)differs=std::memcmp(&now.px[y*now.w],&before->px[y*now.w],now.w*4)!=0;
            if(differs) {if(first==SIZE_MAX)first=band;last=band;}
        }
        if(first==SIZE_MAX)return 0;
        const size_t top=first>=2?first*16-32:0,bottom=(last+3)*16<now.h?(last+3)*16:now.h;
        suspectRead_=false;
        return Area(now.px.data()+top*now.w,now.w,bottom-top,now.w,0,top);
    }
    std::string Row(size_t r) const {
        size_t n=maxColumns;while(n && text_[r][n-1]==u' ')--n;
        size_t s=0;while(s<n && text_[r][s]==u' ')++s;
        return Utf8(text_[r]+s,n-s);
    }
    size_t Column(size_t r) const {size_t s=0;while(s<maxColumns && text_[r][s]==u' ')++s;return s;}
    // The rows as lines of a .grid file (attributes and fonts left out), so tests/charon-ocr-measure.py reads them.
    void Dump() const {
        std::puts("CHARON grid host");
        for(size_t r=0;r<maxRows;++r) {
            size_t n=maxColumns;while(n && text_[r][n-1]==u' ')--n;
            if(n)std::printf("G%02zx -|-|%s\n",r,Utf8(text_[r],n).c_str());
        }
        std::puts("CHARON grid end");
    }
    bool Has(const char* line) const {for(size_t r=0;r<maxRows;++r)if(Row(r)==line)return true;return false;}
    size_t RowOf(const char* line) const {for(size_t r=0;r<maxRows;++r)if(Row(r)==line)return r;return SIZE_MAX;}
    size_t Rows() const {size_t n=0;for(size_t r=0;r<maxRows;++r)if(!Row(r).empty())++n;return n;}
};

static int checks=0,failed=0;
static void Check(bool ok,const char* what) {++checks;if(!ok) {++failed;std::printf("FAIL %s\n",what);}}
static AreaReader* NewReader(const char* manager) {AreaReader* r=new AreaReader;r->Manager(manager);return r;}

int main(int argc,char** argv) {
    if(argc>=4 && !std::strcmp(argv[1],"--dump")) {
        AreaReader* r=NewReader(argv[2]);Picture now,before;bool after=false;
        if(!ReadPpm(argv[3],now)) {std::fprintf(stderr,"cannot read %s\n",argv[3]);return 2;}
        for(int i=4;i+1<argc;i+=2) {
            if(!std::strcmp(argv[i],"--image"))r->Image(argv[i+1]);
            else if(!std::strcmp(argv[i],"--ttf"))r->Ttf(argv[i+1]);
            else if(!std::strcmp(argv[i],"--after")) {after=ReadPpm(argv[i+1],before);if(after)r->Poll(before,nullptr);}
        }
        r->Poll(now,after?&before:nullptr);
        if(r->suspectRead_) {++r->suspectRetries_;r->Poll(now,nullptr);}
        r->Dump();
        return 0;
    }
    if(argc<2) {std::fprintf(stderr,"usage: CharonAreaOcrTests DIR | --dump MANAGER FILE.ppm [--image FILE] [--ttf DIR] [--after PREVIOUS.ppm]\n");return 2;}
    const std::string dir=argv[1];
    // ---- 1. GRUB theme, entry editor (terminal in a dashed box, lines 15 pixels apart, underline caret) -----
    {
        Picture p;Check(ReadPpm(dir+"/grubtheme-04-e.ppm",p),"grubtheme-04-e.ppm is there");
        AreaReader* r=NewReader("GRUB");
        if(!p.px.empty()) {
            r->Poll(p,nullptr);
            const size_t a=r->RowOf("setparams 'Charon test entry two'"),b=r->RowOf("echo \"Entry two selected\""),c=r->RowOf("sleep 3");
            Check(a!=SIZE_MAX,"editor: the first line, with the caret under its first letter, is read");
            Check(b!=SIZE_MAX,"editor: echo \"Entry two selected\" is read and stays");
            Check(c!=SIZE_MAX && b!=SIZE_MAX && c==b+1,"editor: sleep 3 has the row under echo (it used to take echo's row)");
            Check(r->Has("GNU GRUB  version 2.12-9+deb13u4") && r->Has("Minimum Emacs-like screen editing is supported. TAB") && r->Has("lists completions. Press Ctrl-x or F10 to boot,")
                && r->Has("Ctrl-c or F2 for a command-line or ESC to discard") && r->Has("edits and return to the GRUB menu."),"editor: title and the four help lines");
            Check(r->Rows()==8,"editor: eight rows of text, nothing from the frame or the page's border");
            // the line under echo read again alone (a redrawn rectangle: rows 117-149): echo keeps its row
            r->Area(p.px.data()+117*p.w,p.w,33,p.w,0,117);
            if(std::getenv("AREA_OCR_DUMP"))r->Dump();
            Check(r->RowOf("echo \"Entry two selected\"")==b && r->RowOf("sleep 3")==c,"editor: a line read again alone does not move onto its neighbour");
        }
        delete r;
    }
    // ---- 2. GRUB theme, text menu and editor in the box (18-pixel lines; the box was read as "j" at both ends) --
    {
        Picture p;Check(ReadPpm(dir+"/grubthemepl-02-menu.ppm",p),"grubthemepl-02-menu.ppm is there");
        AreaReader* r=NewReader("GRUB");
        if(!p.px.empty()) {
            r->Poll(p,nullptr);
            Check(r->Has(u8"Opcje zaawansowane: źródła i ścieżki"),"boxed menu: second entry without the frame");
            Check(r->Has(u8"Pamięć: test pamięci (Memtest86+)"),"boxed menu: third entry without the frame");
            Check(r->Has(u8"Ustawienia oprogramowania układowego UEFI"),"boxed menu: fourth entry without the frame");
            Check(r->Has(u8"*Zażółć gęślą jaźń"),"boxed menu: the chosen entry with GRUB's asterisk");
            bool frame=false;
            for(size_t k=0;k<AreaReader::maxRows;++k) {const std::string t=r->Row(k);
                if(t.size()>=2 && ((t[0]=='j' && t[1]==' ') || (t[t.size()-1]=='j' && t[t.size()-2]==' ') || t=="j" || t=="l"))frame=true;}
            Check(!frame,"boxed menu: no row begins or ends with the frame read as a letter");
            Check(r->Has("automatically in 20s."),"boxed menu: the countdown's second line");
        }
        delete r;
        // the same menu after Down: the chosen entry is black on a grey bar that the line above touches
        Picture d;Check(ReadPpm(dir+"/grubthemepl-03-down.ppm",d),"grubthemepl-03-down.ppm is there");
        r=NewReader("GRUB");
        if(!d.px.empty()) {
            r->Poll(d,nullptr);
            const size_t first=r->RowOf(u8"Zażółć gęślą jaźń"),chosen=r->RowOf(u8"*Opcje zaawansowane: źródła i ścieżki");
            Check(first!=SIZE_MAX && chosen==first+1,"boxed menu after Down: the chosen entry on its bar and the entry above it are both read");
            Check(r->Has(u8"Pamięć: test pamięci (Memtest86+)") && r->Has(u8"Ustawienia oprogramowania układowego UEFI"),"boxed menu after Down: the entries below");
        }
        delete r;
        Picture e;Check(ReadPpm(dir+"/grubthemepl-06-e.ppm",e),"grubthemepl-06-e.ppm is there");
        r=NewReader("GRUB");
        if(!e.px.empty()) {
            r->Poll(e,nullptr);
            Check(r->Has(u8"setparams 'Opcje zaawansowane: źródła i ścieżki\\"),"boxed editor: first line (was \"jsetparams ... j\" with a wrong first letter)");
            Check(r->Has(u8"menuentry \"Tryb awaryjny, jądro 6.12\" {") && r->Has("linux /vmlinuz root=/dev/sda2 ro S") && r->Has("initrd /initrd.img"),"boxed editor: the other lines without the frame");
        }
        delete r;
        Picture c;Check(ReadPpm(dir+"/grubthemepl-01-c.ppm",c),"grubthemepl-01-c.ppm is there");
        r=NewReader("GRUB");
        if(!c.px.empty()) {
            r->Poll(c,nullptr);
            Check(r->Has("Anywhere else TAB lists possible device or file") && r->Has("pager=1\". ESC at any time exits."),"command line: no marks from the page's border before the text");
            Check(r->Rows()==6,"command line: six rows");
        }
        delete r;
    }
    // ---- 3. GRUB theme, countdown label inside the progress bar --------------------------------------------
    {
        Picture p;Check(ReadPpm(dir+"/grubtheme-00-start.ppm",p),"grubtheme-00-start.ppm is there");
        AreaReader* r=NewReader("GRUB");
        const char* want="The highlighted entry will be executed automatically in 3s.";
        if(!p.px.empty()) {
            r->Poll(p,nullptr);
            Check(r->Has(want),"countdown: the recorded picture (the bar's edge on the u of automatically) reads right");
            // The bar's edge at every column it can have: the label is black on white left of it and on
            // 0x66aacc right of it (rows 481-506, columns 119-676 of the recorded picture; only the paper changes).
            size_t wrong=0,first=0,wrongWhole=0,firstWhole=0,wholeReads=0;Picture q=p;std::string got,gotWhole;AreaReader* whole=NewReader("GRUB");
            const bool full=std::getenv("AREA_OCR_FULL")!=nullptr;
            Picture other=p;for(size_t y=481;y<=506;++y)for(size_t x=119;x<=676;++x)other.px[y*other.w+x]=0x123456;   // "the picture before": the bar's rows always differ from it
            for(size_t edge=118;edge<=676;++edge) {
                for(size_t y=481;y<=506;++y)for(size_t x=119;x<=676;++x) {uint32_t& v=q.px[y*q.w+x];if(v!=0)v=x<=edge?0xffffff:0x66aacc;}
                // as the poll reads it: the bands that changed, into an empty grid (nothing left from a read before)
                r->Clear();r->Poll(q,&other);
                if(r->Row(31)!=want || r->Rows()!=1) {if(!wrong++) {first=edge;got=r->Row(31);}}
                // and the whole screen at once (slow: every eighth position unless AREA_OCR_FULL is set)
                if(full || edge%8==0 || edge==118 || edge==676) {
                    ++wholeReads;whole->Clear();whole->Poll(q,nullptr);
                    if(whole->Row(31)!=want || whole->Rows()!=9) {if(!wrongWhole++) {firstWhole=edge;gotWhole=whole->Row(31);}}
                }
            }
            std::printf("  countdown sweep: changed bands %zu wrong of 559; whole screen %zu wrong of %zu\n",wrong,wrongWhole,wholeReads);
            if(wrong)std::printf("  changed bands, first wrong at column %zu: \"%s\"\n",first,got.c_str());
            if(wrongWhole)std::printf("  whole screen, first wrong at column %zu: \"%s\"\n",firstWhole,gotWhole.c_str());
            Check(!wrong,"countdown: the label, and nothing else, for every position of the bar's edge, read as changed bands (559 positions)");
            Check(!wrongWhole,"countdown: the same on a whole-screen read, with the menu's eight other rows");
            delete whole;
        }
        delete r;
    }
    // ---- 4. Clover at 1600x900: the line at the right edge of the row of F1:Help ----------------------------
    {
        Picture p;Check(ReadPpm(dir+"/clover-00-start.ppm",p),"clover-00-start.ppm is there");
        AreaReader* r=NewReader("Clover");r->Image(dir+"/clover-font.png");
        Check(r->ttfOcr_.FontCount()==1,"clover-font.png gives the grid font");
        if(!p.px.empty()) {
            r->Poll(p,nullptr);
            Check(r->Has("Boot Microsoft EFI Boot from GRUBTEST"),"Clover: the entry's line");
            size_t row=SIZE_MAX;for(size_t k=0;k<AreaReader::maxRows;++k)if(r->Row(k).rfind("F1:Help",0)==0)row=k;
            Check(row!=SIZE_MAX,"Clover: F1:Help");
            const std::string t=row!=SIZE_MAX?r->Row(row):std::string();const std::string tail="5176 GRUBTEST\\EFI\\CLOVER";
            Check(t.size()>tail.size() && t.compare(t.size()-tail.size(),tail.size(),tail)==0,"Clover: 5176 GRUBTEST\\EFI\\CLOVER at the right end of that row (it starts at pixel 1370, past the grid's 160 cells)");
            Check(row!=SIZE_MAX && r->text_[row][AreaReader::maxColumns-1]==u'R' && r->text_[row][AreaReader::maxColumns-tail.size()-1]==u' ',"Clover: whole, ending in the last cell, a blank before it");
            r->Poll(p,nullptr);
            Check(row!=SIZE_MAX && r->Row(row)==t,"Clover: read again, the row is the same (no second copy)");
        }
        delete r;
    }
    std::printf("%s area OCR: %d checks, %d failed\n",failed?"FAIL":"PASS",checks,failed);
    return failed?1:0;
}
