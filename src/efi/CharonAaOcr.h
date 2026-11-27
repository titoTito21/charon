// Anti-aliased monospace text recognition for Charon's resident reader
// (r359). GPL-2.0-or-later.
//
// Reads text that a program blends into the frame buffer from an alpha mask
// of a known monospace font (rEFInd: Liberation Mono, its built-in font at
// four sizes, CharonAaFonts.h). Works on a plain background row by row:
// rows that differ from their background form ink blocks; in each block the
// reader finds text lines (font, top row, column offset) whose cells match the
// font's coverage after removing the background and text colour, then reads
// the whole line. Lines where fewer than 70% of the inked cells match are
// dropped: text over pictures or in other fonts is not guessed.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "CharonAaFonts.h"
namespace hakefi {
class AlphaOcr {
public:
    // emit(const char16_t* text,size_t x,size_t y,uint32_t fg,uint32_t bg,size_t cellHeight)
    template<class Emit,class Idle> size_t Recognize(const uint32_t* pixels,size_t width,size_t height,size_t stride,size_t left,size_t top,Emit emit,Idle idle) {
        if(!pixels || width<16 || height<10)return 0;
        Prepare();
        if(height>maxScan)height=maxScan;
        scanWidth_=width;
        // Background of each row: the most frequent of 32 samples; a row has
        // ink when some pixel differs from it clearly in luminance.
        for(size_t y=0;y<height;++y) {
            const uint32_t* row=pixels+y*stride;
            // r410: 64 samples, and a row is a picture only when no colour holds a quarter of them. Until r410
            // (32 samples, half of them) a long line of text in an area cropped to that very text - rEFInd's
            // second hint line, the long lines of its About screen - left fewer than half of the samples on the
            // background: the row counted as wallpaper and the line was not read.
            uint32_t samples[64];size_t n=0;
            for(size_t i=0;i<64;++i)samples[n++]=row[(i*width)/64]&0xffffff;
            uint32_t best=samples[0];size_t bestCount=0;
            for(size_t i=0;i<n;++i) {size_t c=0;for(size_t j=0;j<n;++j)if(samples[j]==samples[i])++c;if(c>bestCount) {bestCount=c;best=samples[i];}}
            rowBg_[y]=best;rowInk_[y]=false;rowWall_[y]=bestCount<16;
            if(rowWall_[y]) {
                // No dominant colour (a picture, a theme wallpaper): ink is a
                // sharp stroke, a pixel far from both neighbours two apart;
                // smooth or noisy pictures have none.
                for(size_t x=2;x+2<width;++x) {
                    const int l=Lum(row[x]),d=l-(Lum(row[x-2])+Lum(row[x+2]))/2;
                    if(d>60 || d<-60) {rowInk_[y]=true;break;}
                }
                continue;
            }
            const int bg=Lum(best);
            for(size_t x=0;x<width;++x) {const int d=Lum(row[x])-bg;if(d>48 || d<-48) {rowInk_[y]=true;break;}}
        }
        size_t recognized=0;
        for(size_t y=0;y<height;) {
            if(!rowInk_[y]) {++y;continue;}
            size_t end=y;while(end<height && rowInk_[end] && rowWall_[end]==rowWall_[y] && (rowWall_[y] || rowBg_[end]==rowBg_[y]))++end;
            // Taller than three lines of the largest font without a clear
            // row: icons and pictures, not text.
            recognized+=Segments(pixels,width,height,stride,left,top,y,end,emit,idle);
            y=end;
        }
        return recognized;
    }
private:
    static constexpr size_t maxScan=2400,maxCells=320;
    // r410: a block of ink rows on a plain background is cut into the parts that at least 24 blank columns
    // separate, and each part is read with its own rows. Until r410 the whole block was one unit: text beside a
    // picture (rEFInd's submenu entries next to the large icon, its About text next to the "i", Clover's
    // lines next to a badge) started at the picture's first column and height, so it was either not found or,
    // when the picture made the block taller than three lines, never looked at.
    template<class Emit,class Idle> size_t Segments(const uint32_t* pixels,size_t width,size_t height,size_t stride,size_t left,size_t top,size_t y,size_t end,Emit& emit,Idle& idle) {
        auto whole=[&]() {return end-y<=96?Block(pixels,width,height,stride,left,top,y,end,emit,idle):size_t(0);};
        if(rowWall_[y])return whole();
        const int bg=Lum(rowBg_[y]);
        size_t from[8],to[8],count=0,blank=24;bool overflow=false;
        for(size_t x=0;x<width && !overflow;++x) {
            bool ink=false;
            for(size_t r=y;r<end && !ink;++r) {const int d=Lum(pixels[r*stride+x])-bg;ink=d>48 || d<-48;}
            if(!ink) {++blank;continue;}
            if(blank>=24) {if(count==8)overflow=true;else {from[count]=x;to[count]=x+1;++count;}}
            else to[count-1]=x+1;
            blank=0;
        }
        if(overflow || !count)return whole();
        size_t recognized=0;const size_t keepWidth=scanWidth_;
        for(size_t i=0;i<count;++i) {
            const size_t x0=from[i]>=8?from[i]-8:0,x1=to[i]+8<width?to[i]+8:width;
            if(x1-x0<16)continue;
            scanWidth_=x1-x0;
            for(size_t r=y;r<end;) {
                auto inked=[&](size_t row) {for(size_t x=from[i];x<to[i];++x) {const int d=Lum(pixels[row*stride+x])-bg;if(d>48 || d<-48)return true;}return false;};
                if(!inked(r)) {++r;continue;}
                size_t e=r;while(e<end && inked(e))++e;
                if(e-r<=96)recognized+=Block(pixels+x0,x1-x0,height,stride,left+x0,top,r,e,emit,idle);
                r=e;
            }
            idle();
        }
        scanWidth_=keepWidth;
        return recognized;
    }
    static int Lum(uint32_t p) {return int(((p>>16)&0xff)*299+((p>>8)&0xff)*587+(p&0xff)*114)/1000;}
    void Prepare() {
        if(prepared_)return;
        for(unsigned f=0;f<aaFontCount;++f) {
            const AaFont& font=aaFonts[f];
            for(unsigned g=0;g<96;++g) {
                uint32_t total=0;
                for(unsigned y=0;y<font.height;++y) {
                    uint32_t sum=0;for(unsigned x=0;x<font.width;++x)sum+=font.alpha[(g*font.height+y)*font.width+x];
                    rowSum_[f][g][y]=uint16_t(sum);total+=sum;
                }
                ink_[f][g]=total;
            }
            // Metrics: highest ink row of any letter or digit, first ink row of 'x'
            // (lower case), last ink row of 'x' (baseline).
            auto firstRow=[&](unsigned g) {for(unsigned y=0;y<font.height;++y)for(unsigned x=0;x<font.width;++x)if(font.alpha[(g*font.height+y)*font.width+x]>=96)return y;return 0u;};
            auto lastRow=[&](unsigned g) {for(unsigned y=font.height;y-->0;)for(unsigned x=0;x<font.width;++x)if(font.alpha[(g*font.height+y)*font.width+x]>=96)return y;return unsigned(font.height-1);};
            {unsigned top=font.height;for(unsigned g=0x10;g<0x5b;++g) {if((g>0x19 && g<0x21) || (g>0x3a && g<0x41))continue;for(unsigned y=0;y<top;++y) {bool any=false;for(unsigned x=0;x<font.width;++x)if(font.alpha[(g*font.height+y)*font.width+x]>=32)any=true;if(any) {top=y;break;}}}capTop_[f]=uint8_t(top);}xTop_[f]=uint8_t(firstRow('x'-0x20));xBottom_[f]=uint8_t(lastRow('x'-0x20));
        }
        prepared_=true;
    }
    struct Line {unsigned font;size_t top,ox,good,inked;uint32_t err;int fg;};
    // A text line has at least two letters or digits among its matched
    // cells (icon details such as a cursor bar read as "_____").
    bool Wordy(const uint32_t* pixels,size_t stride,const Line& line,int bg) const {
        const AaFont& font=aaFonts[line.font];unsigned letters=0;
        for(size_t x=line.ox,n=0;n<maxCells && letters<2;x+=font.width,++n) {
            if(x+font.width>scanWidth_)break;
            uint32_t err;bool inked;const char16_t ch=Cell(pixels+line.top*stride+x,stride,line.font,bg,line.fg,err,inked);
            if(inked && err<=goodErr && ((ch>=u'0' && ch<=u'9') || (ch>=u'A' && ch<=u'Z') || (ch>=u'a' && ch<=u'z')))++letters;
        }
        return letters>=2;
    }
    // One cell: observed coverage from luminance; nearest glyph by row sums,
    // then full comparison of the three closest. err is per mille of ink.
    char16_t Cell(const uint32_t* cell,size_t stride,unsigned f,int bg,int fg,uint32_t& err,bool& inked) const {  // bg: a guess when localBg_
        const AaFont& font=aaFonts[f];
        if(localBg_) {
            // Background under this cell: the 35th percentile of its
            // luminance counted from the side away from the text colour
            // (most of a cell is background).
            uint16_t histogram[256]={};
            for(unsigned y=0;y<font.height;++y)for(unsigned x=0;x<font.width;++x)++histogram[Lum(cell[y*stride+x])];
            const unsigned want=unsigned(font.height)*font.width*35/100;unsigned seen=0;
            if(fg>bg) {for(int l=0;l<256;++l) {seen+=histogram[l];if(seen>want) {bg=l;break;}}}
            else {for(int l=255;l>=0;--l) {seen+=histogram[l];if(seen>want) {bg=l;break;}}}
            if((fg>bg?fg-bg:bg-fg)<60) {inked=false;err=0;return u' ';}
        }
        const int span=fg-bg;
        uint8_t obs[32*32];uint16_t rows[32];uint32_t total=0;
        for(unsigned y=0;y<font.height;++y) {
            uint32_t sum=0;
            for(unsigned x=0;x<font.width;++x) {
                int a=(Lum(cell[y*stride+x])-bg)*255/span;
                a=a<0?0:a>255?255:a;if(localBg_ && a<48)a=0;obs[y*font.width+x]=uint8_t(a);sum+=unsigned(a);
            }
            rows[y]=uint16_t(sum);total+=sum;
        }
        uint8_t strongest=0;for(unsigned p=0;p<unsigned(font.height)*font.width;++p)if(obs[p]>strongest)strongest=obs[p];
        inked=total>=120 && strongest>=160;err=0;
        if(!inked)return u' ';
        unsigned best[3]={0,0,0};uint32_t bestD[3]={~0u,~0u,~0u};
        for(unsigned g=1;g<96;++g) {
            uint32_t d=0;for(unsigned y=0;y<font.height;++y) {const int v=int(rows[y])-int(rowSum_[f][g][y]);d+=uint32_t(v<0?-v:v);}
            for(unsigned i=0;i<3;++i)if(d<bestD[i]) {for(unsigned j=2;j>i;--j) {bestD[j]=bestD[j-1];best[j]=best[j-1];}bestD[i]=d;best[i]=g;break;}
        }
        uint32_t bestFull=~0u;unsigned glyph=0;
        for(unsigned i=0;i<3;++i) {
            if(bestD[i]==~0u)continue;
            const uint8_t* a=font.alpha+best[i]*font.height*font.width;uint32_t d=0;
            for(unsigned p=0;p<unsigned(font.height)*font.width;++p) {const int v=int(obs[p])-int(a[p]);d+=uint32_t(v<0?-v:v);}
            if(d<bestFull) {bestFull=d;glyph=best[i];}
        }
        err=uint32_t(uint64_t(bestFull)*1000/(total+ink_[f][glyph]));
        return char16_t(0x20+glyph);
    }
    // Text colour of a candidate line: the luminance farthest from the
    // background inside it (fully covered stem pixels exist in most text).
    static int Foreground(const uint32_t* pixels,size_t width,size_t stride,size_t top,size_t rows,int bg) {
        int far=bg,farD=0;
        for(size_t y=top;y<top+rows;++y)for(size_t x=0;x<width;++x) {const int l=Lum(pixels[y*stride+x]);const int d=l>bg?l-bg:bg-l;if(d>farD) {farD=d;far=l;}}
        return far;
    }
    // Score a line: up to `sample` inked cells (0 = all).
    Line Score(const uint32_t* pixels,size_t width,size_t stride,unsigned f,size_t lineTop,size_t ox,int bg,int fg,size_t sample,size_t firstInk) const {
        const AaFont& font=aaFonts[f];Line line{f,lineTop,ox,0,0,0,fg};
        // Start at the cell holding the block's first ink column.
        size_t x=ox;
        if(firstInk>ox+font.width)x=ox+((firstInk-ox)/font.width)*font.width;
        for(;x+font.width<=width;x+=font.width) {
            uint32_t err;bool inked;Cell(pixels+lineTop*stride+x,stride,f,bg,fg,err,inked);
            if(!inked)continue;
            ++line.inked;line.err+=err;if(err<=goodErr)++line.good;
            if(sample && line.inked>=sample)break;
            if(sample && line.inked==2 && !line.good)break;  // not this offset
        }
        return line;
    }
    template<class Emit,class Idle> size_t Block(const uint32_t* pixels,size_t width,size_t height,size_t stride,size_t left,size_t top,size_t start,size_t end,Emit& emit,Idle& idle) {
        size_t recognized=0;
        localBg_=rowWall_[start];
        int bg=Lum(rowBg_[start]);
        uint32_t bgColour=rowBg_[start];
        if(!localBg_) {
            // r410: the background of THIS part: its most frequent brightness. A short selection bar (rEFInd's
            // submenu: black text on a white bar 224 pixels wide in a grey screen) is not the row's main colour;
            // read against the grey its blended edges were all too dark and the selected entry was not read.
            // The bar's colour also goes out as the line's background, so the reader sees it as the selection.
            uint32_t histogram[64]={};
            for(size_t y=start;y<end;++y)for(size_t x=0;x<width;++x)++histogram[Lum(pixels[y*stride+x])>>2];
            unsigned mode=0;for(unsigned i=1;i<64;++i)if(histogram[i]>histogram[mode])mode=i;
            if(int(mode)!=(bg>>2) && (int(mode*4)-bg>=24 || bg-int(mode*4+3)>=24)) {
                bool taken=false;
                for(size_t y=start;y<end && !taken;++y)for(size_t x=0;x<width;++x)if(unsigned(Lum(pixels[y*stride+x])>>2)==mode) {bgColour=pixels[y*stride+x]&0xffffff;bg=Lum(bgColour);taken=true;break;}
            }
        }
        if(localBg_) {
            // Mean luminance of the block as the side the text is not on.
            uint64_t sum=0,count=0;
            for(size_t y=start;y<end;++y)for(size_t x=0;x<width;x+=3) {sum+=uint64_t(Lum(pixels[y*stride+x]));++count;}
            bg=int(sum/(count?count:1));
        }
        unsigned misses=0;
        while(start<end) {
            Line best{0,0,0,0,0,0,0};bool found=false;
            const size_t blockRows=end-start;
            size_t firstInk=width;
            if(!localBg_)for(size_t y=start;y<end;++y)for(size_t x=0;x<firstInk;++x) {const int d=Lum(pixels[y*stride+x])-bg;if(d>48 || d<-48) {firstInk=x;break;}}
            if(localBg_)for(size_t y=start;y<end;++y) {
                const uint32_t* row=pixels+y*stride;
                for(size_t x=2;x+2<width && x<firstInk;++x) {const int d=Lum(row[x])-(Lum(row[x-2])+Lum(row[x+2]))/2;if(d>60 || d<-60) {firstInk=x-2;break;}}
            }
            if(firstInk==width)break;
            // Text colour once per block: the luminance farthest from the background.
            const size_t colourTop=start>=4?start-4:0,colourRows=(end+4<height?end+4:height)-colourTop;
            const int blockFg=Foreground(pixels,width,stride,colourTop,colourRows,bg);
            if(blockFg==bg)break;
            // Best quick candidate per font; the font read last is tried
            // first and, when its sample fits completely, alone.
            Line candidates[aaFontCount];unsigned count=0;
            for(unsigned pass=0;pass<2 && !found;++pass) {
            bool shortcut=false;count=0;
            for(unsigned order=0;order<aaFontCount;++order) {
                const unsigned f=order==0?lastFont_:(order<=lastFont_?order-1:order);
                const AaFont& font=aaFonts[f];
                // A line's ink is at least as tall as the font's x-height and
                // starts at its capital height (or x-height for lower case
                // only), measured from the cell top.
                if(blockRows+1<size_t(xBottom_[f]-xTop_[f]+1))continue;
                if(start+font.height>height+2)continue;
                const size_t lowest=start>=xTop_[f]+1u?start-xTop_[f]-1:0,highest=start>=capTop_[f]?start-capTop_[f]+1:0;
                Line fontBest{};bool fontFound=false;
                for(size_t t=lowest;t<=highest && t<=start && t+font.height<=height;++t) {
                    const int fg=blockFg;
                    for(size_t ox=0;ox<font.width;++ox) {
                        const Line line=Score(pixels,width,stride,f,t,ox,bg,fg,4,firstInk);
                        if(line.inked<2)continue;
                        if(!fontFound || line.good*fontBest.inked>fontBest.good*line.inked || (line.good*fontBest.inked==fontBest.good*line.inked && line.err*fontBest.inked<fontBest.err*line.inked)) {fontBest=line;fontFound=true;}
                    }
                }
                if(!fontFound || fontBest.good*10<fontBest.inked*7) {idle();continue;}
                candidates[count++]=fontBest;
                idle();
                if(pass==0 && order==0 && fontBest.good==fontBest.inked && fontBest.inked>=3) {shortcut=true;break;}
            }
            // Full reads in order of the quick score until one passes.
            for(unsigned i=0;i<count;++i)for(unsigned j=i+1;j<count;++j) {
                const Line& a=candidates[i];const Line& b=candidates[j];
                if(b.good*a.inked>a.good*b.inked || (b.good*a.inked==a.good*b.inked && b.err*a.inked<a.err*b.inked)) {const Line t=candidates[i];candidates[i]=candidates[j];candidates[j]=t;}
            }
            for(unsigned i=0;i<count && !found;++i) {
                const Line& c=candidates[i];
                const Line full=Score(pixels,width,stride,c.font,c.top,c.ox,bg,c.fg,0,0);
                if(full.good*10>=full.inked*7 && Wordy(pixels,stride,c,bg)) {best=c;found=true;}
            }
            if(!shortcut)break;  // every font was tried
            }
            idle();
            if(!found) {
                // r410: no line starts at the block's first ink row. Until r410 the rest of the block was given up:
                // text beside a picture that begins higher (rEFInd's submenu entries next to the big icon, the
                // second line of its About text next to the "i") was never read. Now the start moves down, three
                // rows at a time (the search above tolerates that), at most 16 times per block.
                if(++misses>16)break;
                size_t next=start+3;while(next<end && !rowInk_[next])++next;
                if(next>=end)break;
                start=next;continue;
            }
            lastFont_=best.font;
            const AaFont& font=aaFonts[best.font];
            {
                char16_t text[maxCells+1];size_t length=0,first=SIZE_MAX;
                for(size_t x=best.ox;x+font.width<=width && length<maxCells;x+=font.width) {
                    uint32_t err;bool inked;char16_t ch=Cell(pixels+best.top*stride+x,stride,best.font,bg,best.fg,err,inked);
                    if(inked && err>goodErr)ch=u' ';
                    if(ch!=u' ') {++recognized;if(first==SIZE_MAX)first=length;}
                    text[length++]=ch;
                }
                while(length && text[length-1]==u' ')--length;
                text[length]=0;
                if(first!=SIZE_MAX) {
                    const uint32_t fgColor=uint32_t(best.fg)*0x010101u;
                    emit(text+first,left+best.ox+first*font.width,top+best.top,fgColor,bgColour,size_t(font.height));
                }
            }
            // Continue below this line (the next line of a merged block).
            size_t next=best.top+font.height;
            while(next<end && !rowInk_[next])++next;
            if(next<=start)next=start+1;
            start=next;
        }
        return recognized;
    }
    static constexpr uint32_t goodErr=120;  // 12% of the ink
    uint16_t rowSum_[aaFontCount][96][32]={};
    uint32_t ink_[aaFontCount][96]={};uint8_t capTop_[aaFontCount]={},xTop_[aaFontCount]={},xBottom_[aaFontCount]={};
    uint32_t rowBg_[maxScan]={};bool rowInk_[maxScan]={},rowWall_[maxScan]={};bool localBg_=false;unsigned lastFont_=0;size_t scanWidth_=0;
    bool prepared_=false;
};
}
