// Proportional TrueType text recognition for Charon's resident reader
// (r361). GPL-2.0-or-later.
//
// Boot programs such as Windows Boot Manager draw anti-aliased proportional
// text with their own TrueType fonts (Segoe UI from \EFI\Microsoft\Boot\Fonts).
// The reader loads those fonts from the user's disk (they are never shipped
// with Charon), renders glyph templates with stb_truetype (CharonTtf.c) and
// reads each text line on a plain background:
//  - rows that differ from their background form a line block; the text
//    colour is the luminance farthest from the background and every pixel
//    becomes a coverage value between them;
//  - the baseline is where most glyph columns end, the cap and x heights give
//    the font size, and each loaded font is tried at the sizes these heights
//    allow (the best of a prefix of the line wins);
//  - a dynamic program along the line places glyph templates (left edge on an
//    inked column, baseline aligned within one row, neighbours may touch or
//    overlap one or two columns) so that the coverage difference plus all
//    unexplained ink is smallest;
//  - spaces come from the gap between one glyph's advance and the next pen
//    position; lines whose glyphs differ too much from the templates, or
//    without two letters or digits, are dropped.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "CharonTtf.h"
#include "CharonPf2Fonts.h"
namespace hakefi {
#ifdef CHARON_TTF_DEBUG
inline int debugFont=0,debugQuarter=0,debugColumn=0;
#endif
class TtfOcr {
public:
    bool LastBold() const {return boldLine_;}   // r393: the last line was in a bold face
    // r396: the block caret the last Recognize found (screen pixels), once.
    bool TakeCaret(size_t& x,size_t& y,uint32_t& ink) {if(!caretFound_)return false;caretFound_=false;x=caretX_;y=caretY_;ink=caretInk_;return true;}
    using AllocFn=void*(*)(void* context,size_t bytes);
    using FreeFn=void(*)(void* context,void* block);
    void SetAllocator(AllocFn alloc,FreeFn release,void* context) {alloc_=alloc;free_=release;allocContext_=context;}
    // The font data must stay valid while the reader runs.
    bool AddFont(const uint8_t* data,size_t size) {
        if(fontCount_>=maxFonts || !data)return false;
        for(size_t i=0;i<fontCount_;++i)if(fonts_[i].data==data)return true;
        Font& f=fonts_[fontCount_];f.cell=nullptr;f.pf2=nullptr;
        if(!charon_ttf_init(&f.info,data,size))return false;
        f.data=data;f.size=size;f.strikes=0;size_t found=0;
        for(size_t i=0;i<charCount;++i) {f.glyph[i]=stbtt_FindGlyphIndex(&f.info,chars[i]);if(f.glyph[i])++found;}
        f.space=stbtt_FindGlyphIndex(&f.info,' ');
        if(found<60 || !f.space)return false;
        // Heights per em of capitals and ascenders (H, l, d) and of 'x'.
        const float unit=stbtt_ScaleForMappingEmToPixels(&f.info,1.0f);
        int x0,y0,x1,y1;f.capH=f.xH=0;
        for(const char c:"Hldb")if(c && stbtt_GetCodepointBox(&f.info,c,&x0,&y0,&x1,&y1) && y1*unit>f.capH)f.capH=y1*unit;
        if(stbtt_GetCodepointBox(&f.info,'H',&x0,&y0,&x1,&y1))f.hH=y1*unit;else f.hH=f.capH;
        if(stbtt_GetCodepointBox(&f.info,'x',&x0,&y0,&x1,&y1))f.xH=y1*unit;
        // r362: embedded bitmap strikes (wgl4_boot: Windows Boot Manager's
        // legacy text screens and menus; its outlines are empty).
        ParseStrikes(f);
        if((f.capH<0.3f || f.xH<0.2f) && !f.strikes)return false;
        ++fontCount_;return true;
    }
    size_t FontCount() const {return fontCount_;}
    // r367: a GRUB PF2 bitmap font (theme fonts, CharonPf2Fonts.h): one size,
    // proportional advances.
    bool AddPf2Font(const Pf2Font& font) {
        if(fontCount_>=maxFonts)return false;
        for(size_t i=0;i<fontCount_;++i)if(fonts_[i].pf2==&font)return true;
        Font& f=fonts_[fontCount_];f={};
        f.pf2=&font;f.strikes=1;f.strikePpem[0]=font.pointSize;++bitmapFonts_;
        f.bold=false;for(const char* n=font.name;*n && !f.bold;++n)f.bold=n[0]=='B' && n[1]=='o' && n[2]=='l' && n[3]=='d';
        for(size_t i=0;i<charCount;++i) {
            f.glyph[i]=0;
            for(size_t k=0;k<font.count;++k)if(font.glyphs[k].code==chars[i]) {f.glyph[i]=int(k)+1;break;}
        }
        f.space=0;for(size_t k=0;k<font.count;++k)if(font.glyphs[k].code==u' ')f.space=int(k)+1;
        f.capH=f.hH=0.7f;f.xH=0.5f;
        ++fontCount_;return true;
    }
    // r363/r365: a console cell font of 256 glyphs (code page order),
    // width <= 16 and height <= 24 pixels, one 16-bit row each with the
    // leftmost pixel in bit 15 (Limine's and OpenCore's terminal fonts,
    // Clover's embedded font image). Drawn with `advance` pixels per glyph or
    // one more (VGA's 9-dot cells), at their size or doubled (OpenCore's
    // UIScale 2): four sizes keyed by their pixel advance.
    bool AddCellFont(const uint16_t* table,size_t width,size_t height,size_t advance) {
        if(fontCount_>=maxFonts || !table || !width || width>16 || !height || height>24 || advance<4 || advance>20)return false;
        for(size_t i=0;i<fontCount_;++i) {
            // The same font found again (a second image of one boot manager).
            if(fonts_[i].cell==table)return true;
            if(fonts_[i].cell && fonts_[i].cellHeight==height && fonts_[i].strikePpem[0]==advance) {
                bool same=true;for(size_t k=0;k<256*height && same;++k)same=fonts_[i].cell[k]==table[k];
                if(same)return false;  // caller releases its copy
            }
        }
        Font& f=fonts_[fontCount_];f={};
        f.cell=table;f.cellHeight=uint8_t(height);f.strikes=4;++bitmapFonts_;
        f.strikePpem[0]=uint8_t(advance);f.strikePpem[1]=uint8_t(advance+1);
        f.strikePpem[2]=uint8_t(2*advance);f.strikePpem[3]=uint8_t(2*advance+2);
        for(size_t i=0;i<charCount;++i)f.glyph[i]=chars[i]<0x7f?int(chars[i]):0;
        f.space=' ';f.capH=f.hH=0.7f;f.xH=0.5f;
        ++fontCount_;return true;
    }
#ifdef CHARON_TTF_DEBUG
    void DumpGlyph(size_t font,uint16_t quarter,char16_t ch) {
        Buffers();Set* s=Templates(font,quarter,quarter,std::getenv("TTF_PHASE")?size_t(std::atoi(std::getenv("TTF_PHASE"))):0);
        for(size_t v=0;v<charCount*shifts;++v) {
            if(chars[v/shifts]!=ch)continue;
            const Glyph& g=s->g[v];
            std::printf("glyph '%c' shift%zu x0 %d y0 %d w%u h%u adv %.2f\n",char(ch),v%shifts,g.x0,g.y0,g.w,g.h,g.adv);
            for(size_t y=0;y<g.h;++y) {for(size_t x=0;x<g.w;++x)std::printf("%4d",s->pool[g.offset+y*g.w+x]);std::putchar('\n');}
        }
    }
#endif
    // emit(const char16_t* text,size_t x,size_t y,uint32_t fg,uint32_t bg,size_t height)
    template<class Emit,class Idle> size_t Recognize(const uint32_t* pixels,size_t width,size_t height,size_t stride,size_t left,size_t top,Emit emit,Idle idle) {
        if(!fontCount_ || !pixels || width<16 || height<6 || !Buffers())return 0;
        idleContext_=&idle;idleFn_=[](void* context) {(*static_cast<Idle*>(context))();};
        if(width>maxWidth-2)width=maxWidth-2;
        if(height>maxScan)height=maxScan;
        // r396: a block caret (Limine's editor) is one cell drawn inverted:
        // the letter under it is read from a copy with that cell inverted
        // back ("path" was read "Bath").
        if(!caretPass_)caretFound_=false;
        if(!caretPass_ && !inPicture_) {
            size_t cx=0,cy=0,cw=0;uint32_t ink=0,paper=0;
            if(FindBlockCaret(pixels,width,height,stride,cx,cy,cw,ink,paper) && CaretBuffer(width*height)) {
                for(size_t y=0;y<height;++y) {const uint32_t* in=pixels+y*stride;uint32_t* out=caretCopy_+y*width;for(size_t x=0;x<width;++x)out[x]=in[x];}
                for(size_t y=cy;y<cy+16;++y)for(size_t x=cx;x<cx+cw;++x) {uint32_t& p=caretCopy_[y*width+x];p=(p&0xffffff)==ink?paper:ink;}
                caretPass_=true;caretFound_=true;caretX_=left+cx;caretY_=top+cy;caretInk_=ink;
                const size_t n=Recognize(caretCopy_,width,height,width,left,top,emit,idle);
                caretPass_=false;
                return n;
            }
        }
        for(size_t y=0;y<height;++y) {
            if(y && !(y&127))idle();
            const uint32_t* row=pixels+y*stride;
            uint32_t samples[32];
            for(size_t i=0;i<32;++i)samples[i]=row[(i*width)/32]&0xffffff;
            uint32_t best=samples[0];size_t bestCount=0;
            for(size_t i=0;i<32;++i) {size_t c=0;for(size_t j=0;j<32;++j)if(samples[j]==samples[i])++c;if(c>bestCount) {bestCount=c;best=samples[i];}}
            rowBg_[y]=best;rowInk_[y]=false;rowPicture_[y]=bestCount<16;
            if(bestCount<16)continue;  // pictures: see below
            const int bg=Lum(best);
            for(size_t x=0;x<width;++x) {const int d=Lum(row[x])-bg;if(d>40 || d<-40) {rowInk_[y]=true;break;}}
        }
        // r396: a run of inked rows of three cells holds more than one line. A
        // frame's vertical edges (Limine's entry editor) ink every row inside
        // it; their pixels, in vertical runs longer than any glyph, are left
        // out of the row test then, so the lines inside the frame separate.
        {size_t run=0;bool tall=false;
         for(size_t y=0;y<height && !tall;++y) {run=rowInk_[y]?run+1:0;tall=run>=48;}   // three cells: more than one line
         if(tall)SplitFramedRows(pixels,width,height,stride,idle);}
        size_t recognized=0,pictureRows=0;scanHeight_=height;
        for(size_t y=0;y<height;++y)if(!rowInk_[y] && rowPicture_[y])++pictureRows;
        for(size_t y=0;y<height;) {
            if(!rowInk_[y]) {++y;continue;}
            // One block per run of inked rows; its background is that of the
            // first row (a highlight bar's edge carries no text, while its text
            // rows may hold more dark pixels than bar colour).
            size_t end=y;while(end<height && rowInk_[end])++end;
            if(end-y>=5 && end-y+2<=maxBlock)recognized+=Block(pixels,width,stride,left,top,y,end,emit,idle);
            y=end;
        }
        // r367: bitmap text over a picture (GRUB themes: PF2 glyphs drawn in
        // one exact colour over a photo). Pixels of exactly black or white
        // are copied onto a plain grey page and read like any other text.
#ifdef CHARON_TTF_DEBUG
        if(std::getenv("TTF_TRACE"))std::printf("  scan h%zu pictureRows %zu bitmapFonts %zu inPicture %d\n",height,pictureRows,bitmapFonts_,int(inPicture_));
#endif
        if(pictureRows>=6 && bitmapFonts_ && !inPicture_ && SyntheticBuffer(width*height)) {
            inPicture_=true;
            for(size_t y=0;y<height;++y)pictureMask_[y]=rowPicture_[y];  // the nested reads reuse rowPicture_
            static const uint32_t keys[]={0x000000,0xffffff};
            for(const uint32_t key:keys) {
                const uint32_t page=key?0x404040:0xc0c0c0;
                for(size_t y=0;y<height;++y) {
                    if(y && !(y&127))idle();
                    const uint32_t* row=pixels+y*stride;uint32_t* out=synthetic_+y*width;
                    for(size_t x=0;x<width;++x)out[x]=pictureMask_[y] && (row[x]&0xffffff)==key?key:page;
                }
                idle();
                recognized+=Recognize(synthetic_,width,height,width,left,top,emit,idle);
            }
            inPicture_=false;
        }
        return recognized;
    }
private:
    // r396: a block caret: a rectangle 8 or 9 pixels wide and 16 high of one
    // colour whose first two rows and last row are solid, with the colour
    // ending on both sides, and the letter cut out between them.
    bool caretPass_=false,caretFound_=false;size_t caretX_=0,caretY_=0;uint32_t caretInk_=0;uint32_t* caretCopy_=nullptr;size_t caretCapacity_=0;
    bool CaretBuffer(size_t pixels) {
        if(caretCopy_ && caretCapacity_>=pixels)return true;
        if(caretCopy_ && free_)free_(allocContext_,caretCopy_);
        caretCopy_=static_cast<uint32_t*>(Alloc(pixels*4));caretCapacity_=caretCopy_?pixels:0;
        return caretCopy_!=nullptr;
    }
    static bool FindBlockCaret(const uint32_t* pixels,size_t width,size_t height,size_t stride,size_t& cx,size_t& cy,size_t& cw,uint32_t& ink,uint32_t& paper) {
        if(height<16 || width<12)return false;
        // r397: outside the area counts as a different colour (a caret in the
        // first column of a line area started there: "Bath", "Brotocol").
        auto px=[&](size_t x,size_t y)->uint32_t {return x<width && y<height?pixels[y*stride+x]&0xffffff:0xff000000u;};
        auto solid=[&](size_t x,size_t y,size_t w,uint32_t c) {
            for(size_t k=0;k<w;++k)if(px(x+k,y)!=c)return false;
            return px(x-1,y)!=c && px(x+w,y)!=c;
        };
        for(size_t y=0;y+16<=height;++y)for(size_t x=0;x+10<width;) {
            const uint32_t c=px(x,y);
            size_t w=1;while(x+w<width && px(x+w,y)==c)++w;
            if((w==8 || w==9) && px(x-1,y)!=c && solid(x,y+1,w,c) && solid(x,y+15,w,c) && px(x-1,y+8)!=c) {
                bool cut=false;for(size_t r=y+2;r<y+15 && !cut;++r)for(size_t k=0;k<w && !cut;++k)cut=px(x+k,r)!=c;
                // r396d: an inverted cell is mostly its colour, with both side
                // columns nearly solid (glyphs keep a margin); a thick stroke of a
                // scaled-up letter (OpenCore's doubled font) is not.
                size_t fill=0,leftSide=0,rightSide=0;
                for(size_t r=y;r<y+16;++r) {for(size_t k=0;k<w;++k)if(px(x+k,r)==c)++fill;if(px(x,r)==c)++leftSide;if(px(x+w-1,r)==c)++rightSide;}
                size_t above=0,below=0;for(size_t k=0;k<w;++k) {if(y && px(x+k,y-1)==c)++above;if(y+16<height && px(x+k,y+16)==c)++below;}
                if(cut && fill*2>=w*16 && rightSide>=13 && (leftSide>=13 || w==9) &&   /* r397: 9-dot cells (Limine) draw 'p' from the first column; the ninth is padding */
                    !(above*2>=w && below*2>=w)) {
                    // r396f: a stroke runs on above AND below; Limine's caret
                    // under the frame's edge has ink above only.
                    cx=x;cy=y;cw=w;ink=c;paper=x?px(x-1,y):px(x+w,y+1);return true;
                }
            }
            x+=w;
        }
        return false;
    }
    // r396: see Recognize. Ink = luminance 40 away from the area's background
    // (the background most rows have): a frame's horizontal line makes its
    // own rows' background the line colour, which would cut the edges' runs.
    template<class Idle> void SplitFramedRows(const uint32_t* pixels,size_t width,size_t height,size_t stride,Idle& idle) {
        static uint32_t inkCount[2400],edgeCount[2400];   // maxScan
        static uint16_t runStart[2560];                    // maxWidth
        constexpr size_t edgeRun=16;                       // a whole 16-row cell: box edges, a block caret
        uint32_t areaBg=rowBg_[0];
        {uint32_t seen[8]={};size_t counts[8]={},kinds=0,best=0;
         for(size_t y=0;y<height;++y) {
             size_t k=0;while(k<kinds && seen[k]!=rowBg_[y])++k;
             if(k==kinds) {if(kinds==8)continue;seen[kinds++]=rowBg_[y];}
             if(++counts[k]>best) {best=counts[k];areaBg=seen[k];}
         }}
        const int bg=Lum(areaBg);
        for(size_t y=0;y<height;++y)inkCount[y]=edgeCount[y]=0;
        for(size_t x=0;x<width;++x)runStart[x]=0xffff;
        auto close=[&](size_t x,size_t end) {
            const size_t start=runStart[x];runStart[x]=0xffff;
            if(start!=0xffff && end-start>=edgeRun)for(size_t y=start;y<end;++y)++edgeCount[y];
        };
        for(size_t y=0;y<height;++y) {
            if(y && !(y&127))idle();
            const uint32_t* row=pixels+y*stride;const bool usable=rowInk_[y] && !rowPicture_[y];
            for(size_t x=0;x<width;++x) {
                const int d=usable?Lum(row[x])-bg:0;
                if(d>40 || d<-40) {++inkCount[y];if(runStart[x]==0xffff)runStart[x]=uint16_t(y);}
                else if(runStart[x]!=0xffff)close(x,y);
            }
        }
        for(size_t x=0;x<width;++x)if(runStart[x]!=0xffff)close(x,height);
        for(size_t y=0;y<height;++y)if(rowInk_[y] && inkCount[y]<=edgeCount[y])rowInk_[y]=false;
#ifdef CHARON_TTF_DEBUG
        if(std::getenv("TTF_TRACE"))for(size_t y=78;y<118 && y<height;++y)std::printf("  split y%zu ink%u edge%u -> %d\n",y,inkCount[y],edgeCount[y],int(rowInk_[y]));
#endif
    }
    static constexpr size_t maxFonts=12,maxSets=8,maxWidth=2560,maxBlock=120,maxScan=2400,maxPlaced=400,candidates=8;
    // Templates at three horizontal sub-pixel positions: hinted text keeps
    // stems on whole pixels, the templates are not hinted.
    static constexpr size_t shifts=4,scratchSize=256*256;
    static constexpr char16_t chars[]=u"!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"
        u"ąćęłńóśźżĄĆĘŁŃÓŚŹŻ’“”–—…©®°";
    static constexpr size_t charCount=sizeof(chars)/sizeof(chars[0])-1;
    struct Font {stbtt_fontinfo info;const uint8_t* data;size_t size;int glyph[charCount];int space;float capH,hH,xH;
        size_t eblc,ebdt,ebdtSize,strikes;size_t strikeRecord[4];uint8_t strikePpem[4];
        const uint16_t* cell;uint8_t cellHeight;const Pf2Font* pf2;bool bold;};  // r363: cell font (AddCellFont), no TrueType data
    static uint32_t Be32(const uint8_t* p) {return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
    static uint16_t Be16(const uint8_t* p) {return uint16_t(p[0]<<8|p[1]);}
    static bool Table(const Font& f,const char* tag,size_t& offset,size_t& length) {
        const size_t tables=Be16(f.data+4);
        for(size_t i=0;i<tables && 12+16*i+16<=f.size;++i) {
            const uint8_t* t=f.data+12+16*i;
            if(t[0]==tag[0] && t[1]==tag[1] && t[2]==tag[2] && t[3]==tag[3]) {
                offset=Be32(t+8);length=Be32(t+12);return offset<=f.size && length<=f.size-offset;
            }
        }
        return false;
    }
    // Strikes of one bit per pixel listed in EBLC (version 2, BitmapSize
    // records of 48 bytes).
    static void ParseStrikes(Font& f) {
        size_t eblcLength=0,ebdtOffset=0;
        if(!Table(f,"EBLC",f.eblc,eblcLength) || !Table(f,"EBDT",ebdtOffset,f.ebdtSize) || eblcLength<8)return;
        f.ebdt=ebdtOffset;
        const size_t sizes=Be32(f.data+f.eblc+4);
        for(size_t i=0;i<sizes && f.strikes<4;++i) {
            const size_t record=f.eblc+8+48*i;
            if(record+48>f.eblc+eblcLength)break;
            const uint8_t ppem=f.data[record+45],depth=f.data[record+46];
            if(depth!=1 || ppem<8)continue;
            f.strikeRecord[f.strikes]=record;f.strikePpem[f.strikes]=ppem;++f.strikes;
        }
    }
    // One glyph of a strike as coverage 0/255; false when absent.
    bool StrikeGlyph(const Font& f,size_t strike,int glyph,uint8_t* out,size_t capacity,CharonTtfBox& box,float& advance) const {
        if(f.pf2) {
            if(glyph<1 || glyph>int(f.pf2->count))return false;
            const Pf2Glyph& g=f.pf2->glyphs[glyph-1];
            advance=g.advance;
            box.x0=g.xOffset;box.y0=-(int(g.yOffset)+int(g.height));box.width=g.width;box.height=g.height;
            if(!g.width || !g.height) {box.width=box.height=0;return true;}
            if(size_t(g.width)*g.height>capacity)return false;
            const uint8_t* bits=f.pf2->bits+g.bits;
            if(f.pf2->coverage) {const uint8_t* cov=f.pf2->coverage+size_t(g.bits)*8;for(size_t i=0;i<size_t(g.width)*g.height;++i)out[i]=cov[i];return true;}
            for(size_t y=0;y<g.height;++y)for(size_t x=0;x<g.width;++x) {const size_t b=y*g.width+x;out[y*g.width+x]=(bits[b>>3]>>(7-(b&7)))&1?255:0;}
            return true;
        }
        if(f.cell) {
            if(glyph<0 || glyph>255)return false;
            const size_t height=f.cellHeight;
            const uint16_t* rows=f.cell+height*size_t(glyph);
            size_t top=height,bottom=0,left=16,right=0;
            for(size_t y=0;y<height;++y)for(size_t x=0;x<16;++x)if(rows[y]&(0x8000>>x)) {if(y<top)top=y;if(y>bottom)bottom=y;if(x<left)left=x;if(x>right)right=x;}
            const size_t scale=strike>=2?2:1;
            advance=f.strikePpem[strike];
            if(top==height) {box={0,0,0,0};return true;}
            const size_t w=(right-left+1)*scale,h=(bottom-top+1)*scale;
            if(w*h>capacity)return false;
            for(size_t y=0;y<h;++y)for(size_t x=0;x<w;++x)out[y*w+x]=rows[top+y/scale]&(0x8000>>(left+x/scale))?255:0;
            box.x0=int(left*scale);box.y0=int(top*scale)-int(height*scale);box.width=int(w);box.height=int(h);  // bottom aligned to 'H' later
            return true;
        }
        const uint8_t* d=f.data;const size_t record=f.strikeRecord[strike];
        const size_t array=f.eblc+Be32(d+record),count=Be32(d+record+8);
        for(size_t k=0;k<count && array+8*k+8<=f.size;++k) {
            const uint8_t* a=d+array+8*k;
            const size_t first=Be16(a),last=Be16(a+2),sub=array+Be32(a+4);
            if(size_t(glyph)<first || size_t(glyph)>last || sub+8>f.size)continue;
            const size_t indexFormat=Be16(d+sub),imageFormat=Be16(d+sub+2),imageData=f.ebdt+Be32(d+sub+4);
            size_t from=0,to=0;const size_t i=size_t(glyph)-first;
            if(indexFormat==3 && sub+8+2*i+4<=f.size) {from=Be16(d+sub+8+2*i);to=Be16(d+sub+10+2*i);}
            else if(indexFormat==1 && sub+8+4*i+8<=f.size) {from=Be32(d+sub+8+4*i);to=Be32(d+sub+12+4*i);}
            else return false;
            if(to<=from || imageData+to>f.ebdt+f.ebdtSize)return false;
            const uint8_t* m=d+imageData+from;const size_t bytes=to-from;
            size_t header=0;
            if(imageFormat==1 || imageFormat==2)header=5;else if(imageFormat==6 || imageFormat==7)header=8;else return false;
            if(bytes<header)return false;
            const size_t h=m[0],w=m[1];
            box.x0=int8_t(m[2]);box.y0=-int(int8_t(m[3]));box.width=int(w);box.height=int(h);advance=m[4];
            if(!w || !h || w*h>capacity)return w==0 || h==0;
            const uint8_t* bits=m+header;
            const bool aligned=imageFormat==1 || imageFormat==6;
            const size_t rowBytes=(w+7)/8,need=aligned?rowBytes*h:(w*h+7)/8;
            if(bytes<header+need)return false;
            for(size_t y=0;y<h;++y)for(size_t x=0;x<w;++x) {
                const size_t bit=aligned?y*rowBytes*8+x:y*w+x;
                out[y*w+x]=(bits[bit>>3]>>(7-(bit&7)))&1?255:0;
            }
            return true;
        }
        return false;
    }
    struct Glyph {int16_t x0,y0;uint8_t w,h;uint32_t offset,colOffset,mass;float adv;};
    struct Set {uint8_t font,phase;bool probe;uint16_t quarter,quarterY;uint32_t stamp;float scale,scaleY,space;uint8_t* pool;size_t poolSize;Glyph g[charCount*shifts];bool ready;};
    struct Cand {uint16_t glyph;int8_t dy;uint32_t cost,observed;};
    struct Placed {uint16_t x;uint16_t glyph;int8_t dy;float err,norm;};
    static int Lum(uint32_t p) {return int(((p>>16)&0xff)*299+((p>>8)&0xff)*587+(p&0xff)*114)/1000;}
    void* Alloc(size_t bytes) {return alloc_?alloc_(allocContext_,bytes):nullptr;}
    bool Buffers() {
        if(alpha_)return true;
        alpha_=static_cast<uint8_t*>(Alloc(maxWidth*maxBlock));
        raw_=static_cast<uint8_t*>(Alloc(maxWidth*maxBlock));
        scratch_=static_cast<uint8_t*>(Alloc(scratchSize));
        if(!raw_ || !scratch_) {alpha_=nullptr;return false;}
        colInk_=static_cast<uint32_t*>(Alloc(maxWidth*4));
        dp_=static_cast<float*>(Alloc((maxWidth+2)*4));
        back_=static_cast<uint32_t*>(Alloc((maxWidth+2)*4));
        pen_=static_cast<float*>(Alloc((maxWidth+2)*4));
        inkPrefix_=static_cast<uint32_t*>(Alloc((maxWidth+2)*4));
        cand_=static_cast<Cand*>(Alloc(maxWidth*candidates*sizeof(Cand)));
        candCount_=static_cast<uint8_t*>(Alloc(maxWidth));
        if(!alpha_ || !colInk_ || !dp_ || !back_ || !pen_ || !inkPrefix_ || !cand_ || !candCount_) {alpha_=nullptr;return false;}
        return true;
    }
    // Templates of one font at one size (quarter pixels per em across and
    // down: boot programs may scale the two directions slightly apart),
    // cached.
    // Size search compares sizes on the most frequent characters only (a
    // quarter of the rendering); the winner is rendered in full.
    static bool ProbeChar(char16_t c) {
        constexpr char16_t probe[]=u"etaoinsrhldcumfpgwybvkTAISWCPEMRBDFNOHL0123.,'";
        for(const char16_t p:probe)if(p==c)return true;
        return false;
    }
    Set* Templates(size_t font,uint16_t quarter,uint16_t quarterY,size_t phase,bool probe=false) {
        if(fonts_[font].strikes)phase=0;  // bitmaps sit on whole pixels
        Set* free=nullptr;Set* oldest=nullptr;
        for(auto& s:sets_) {
            if(s.ready && s.font==font && s.quarter==quarter && s.quarterY==quarterY && s.phase==phase && (s.probe==probe || !s.probe)) {s.stamp=++clock_;return &s;}
            if(!s.ready && !free)free=&s;
            if(s.ready && (!oldest || s.stamp<oldest->stamp))oldest=&s;
        }
        Set* s=free?free:oldest;
        if(!s)return nullptr;
        s->ready=false;
        const size_t em=(quarter>quarterY?quarter:quarterY)/4+1,setPool=charCount*shifts*(em*em/2+8*em+64);
        if(s->pool && s->poolSize<setPool) {if(free_)free_(allocContext_,s->pool);s->pool=nullptr;}
        if(!s->pool) {s->pool=static_cast<uint8_t*>(Alloc(setPool));s->poolSize=s->pool?setPool:0;}
        if(!s->pool)return nullptr;
        const Font& f=fonts_[font];
        s->font=uint8_t(font);s->phase=uint8_t(phase);s->probe=probe;s->quarter=quarter;s->quarterY=quarterY;s->scale=(f.cell || f.pf2)?1.0f:stbtt_ScaleForMappingEmToPixels(&f.info,quarter/4.0f);s->scaleY=(f.cell || f.pf2)?1.0f:stbtt_ScaleForMappingEmToPixels(&f.info,quarterY/4.0f);

        int advance=0,lsb=0;if(!f.cell && !f.pf2)stbtt_GetGlyphHMetrics(&f.info,f.space,&advance,&lsb);s->space=advance*s->scale;
        uint32_t used=0;
        size_t strike=SIZE_MAX;
        for(size_t k=0;k<f.strikes;++k)if(f.strikePpem[k]*4u==quarter)strike=k;
        if(strike!=SIZE_MAX) {
            // Bitmap strike: exact pixels, one position (no sub-pixel variants).
            s->space=0;float spaceAdvance=0;
            for(size_t v=0;v<charCount*shifts;++v) {
                Glyph& g=s->g[v];g={};
                const size_t i=v/shifts;
                if(v%shifts || !f.glyph[i])continue;
                CharonTtfBox box{};float advance=0;
                if(!StrikeGlyph(f,strike,f.glyph[i],scratch_,scratchSize,box,advance) || !box.width || !box.height)continue;
                if(s->space<advance)s->space=advance;
                const int w=box.width,h=box.height;
                if(w>255 || h>255 || size_t(used)+size_t(w)*(h+4)+16>setPool)continue;
                for(int k=0;k<w*h;++k)s->pool[used+k]=scratch_[k];
                g.x0=int16_t(box.x0);g.y0=int16_t(box.y0);g.w=uint8_t(w);g.h=uint8_t(h);g.offset=used;g.adv=advance;
                used+=uint32_t(w*h);used=(used+3)&~3u;
                g.colOffset=used;uint32_t* col=reinterpret_cast<uint32_t*>(s->pool+used);used+=uint32_t(w*4);
                const uint8_t* bits=s->pool+g.offset;g.mass=0;
                for(int x=0;x<w;++x) {uint32_t sum=0;for(int y=0;y<h;++y)sum+=bits[y*w+x];col[x]=sum;g.mass+=sum;}
                if(!g.mass)g.w=0;
            }
            // The space's own advance (proportional PF2 fonts); monospace
            // strikes keep the widest advance.
            {CharonTtfBox box{};if(f.space && StrikeGlyph(f,strike,f.space,scratch_,scratchSize,box,spaceAdvance) && spaceAdvance>0)s->space=spaceAdvance;}
            // Strike metrics may count from the bottom of the line box
            // (wgl4_boot: descent 0): put the bottom of 'H' on the baseline.
            for(size_t i=0;i<charCount;++i)if(chars[i]==u'H' && s->g[i*shifts].w) {
                const int shift=-(s->g[i*shifts].y0+s->g[i*shifts].h);
                for(auto& g:s->g)if(g.w)g.y0=int16_t(g.y0+shift);
                break;
            }
            s->stamp=++clock_;s->ready=true;
            return s;
        }
        for(size_t v=0;v<charCount*shifts;++v) {
            const size_t i=v/shifts;
            Glyph& g=s->g[v];g={};
            if(!f.glyph[i] || (probe && !ProbeChar(chars[i])))continue;
            CharonTtfBox box;
            if(!charon_ttf_render(&f.info,f.glyph[i],s->scale,s->scaleY,float(v%shifts)/shifts,float(phase)/4.0f,scratch_,int(scratchSize),&box) || !box.width || !box.height)continue;
            // Blurred like the screen (one pixel more on each side).
            const int pad=blur_?1:0,w=box.width+2*pad,h=box.height+2*pad;
            if(w>255 || h>255 || size_t(used)+size_t(w)*(h+4)+16>setPool)continue;
            Blur(scratch_,box.width,box.height,s->pool+used,blur_);
            stbtt_GetGlyphHMetrics(&f.info,f.glyph[i],&advance,&lsb);
            g.x0=int16_t(box.x0-pad);g.y0=int16_t(box.y0-pad);g.w=uint8_t(w);g.h=uint8_t(h);g.offset=used;g.adv=advance*s->scale;
            used+=uint32_t(w*h);used=(used+3)&~3u;
            g.colOffset=used;uint32_t* col=reinterpret_cast<uint32_t*>(s->pool+used);used+=uint32_t(w*4);
            const uint8_t* bits=s->pool+g.offset;g.mass=0;
            for(int x=0;x<w;++x) {uint32_t sum=0;for(int y=0;y<h;++y)sum+=bits[y*w+x];col[x]=sum;g.mass+=sum;}
            if(!g.mass)g.w=0;
        }
        s->stamp=++clock_;s->ready=true;
        return s;
    }
    // 3x3 binomial blur of a w x h coverage map into (w+2) x (h+2): hinted
    // screen text and unhinted templates differ by up to half a pixel in
    // stem positions and heights; both are compared blurred.
    static void Blur(const uint8_t* in,int w,int h,uint8_t* out,bool blur) {
        const int ow=w+2,oh=h+2;
        if(!blur) {for(int i=0;i<w*h;++i)out[i]=in[i];return;}
        for(int y=0;y<oh;++y)for(int x=0;x<ow;++x) {
            int sum=0;
            for(int dy=-1;dy<=1;++dy) {
                const int sy=y-1+dy;if(sy<0 || sy>=h)continue;
                const int ky=dy?1:2;
                for(int dx=-1;dx<=1;++dx) {const int sx=x-1+dx;if(sx<0 || sx>=w)continue;sum+=in[sy*w+sx]*ky*(dx?1:2);}
            }
            out[y*ow+x]=uint8_t((sum+8)/16);
        }
    }
    // Cost of glyph g with its left column at x and baseline row `base`:
    // coverage difference inside its box plus the block's ink in the same
    // columns outside the box.
    uint32_t Cost(const Set& s,const Glyph& g,size_t x,int base,uint32_t* observed) const {
        const uint8_t* bits=s.pool+g.offset;uint32_t diff=0,seen=0;
        for(size_t i=0;i<g.w;++i) {
            uint32_t inside=0;const size_t col=x+i;
            for(size_t j=0;j<g.h;++j) {
                const int row=base+g.y0+int(j);
                const int obs=row>=0 && row<int(blockHeight_)?alpha_[size_t(row)*maxWidth+col]:0;
                const int t=bits[j*g.w+i];
                diff+=uint32_t(obs>t?obs-t:t-obs);inside+=uint32_t(obs);
            }
            diff+=colInk_[col]-inside;seen+=colInk_[col];
        }
        if(observed)*observed=seen;
        return diff;
    }
    // Dynamic program over glyph placements in [xs,xe). A state is the
    // column after the last glyph's box together with the pen position the
    // font's advance predicts for the next glyph; a glyph whose pen lies off
    // that prediction (beyond kerning and hinting, short of a space) costs
    // extra, so pieces of letters cannot pose as narrow glyphs. Returns the
    // error ratio (differences and unexplained ink over all ink seen) and
    // fills placed_.
    float Line(const Set& s,size_t xs,size_t xe,int base) {
        const float inf=3.0e38f,none=-1.0e30f;
        const float em=s.quarter/4.0f,pxPenalty=255.0f*em*0.2f,glyphPenalty=255.0f;
        // The baseline and its sub-pixel phase are measured: a glyph one row
        // off must fit clearly better (l and I differ by less than a row).
        const uint32_t dyPenalty=uint32_t(255.0f*em*0.15f);
        inkPrefix_[xs]=0;
        for(size_t x=xs;x<xe;++x)inkPrefix_[x+1]=inkPrefix_[x]+colInk_[x];
        // Candidates at each start column: column profiles choose a few,
        // then the full comparison (baseline within one row).
        for(size_t x=xs;x<xe;++x) {
            candCount_[x]=0;
            if(!((x-xs)&63) && idleFn_)idleFn_(idleContext_);  // keep audio fed on long lines
            if(colInk_[x]<48 && (x+1>=xe || colInk_[x+1]<48))continue;
            // Column profiles, relative to the ink they cover, pick a few
            // characters; all their sub-pixel variants are compared in full.
            uint32_t bestCost[candidates];uint16_t bestGlyph[candidates];size_t n=0;
            for(size_t c=0;c<charCount;++c) {
                uint32_t pc=UINT32_MAX;
                for(size_t v=c*shifts;v<c*shifts+shifts;++v) {
                    const Glyph& g=s.g[v];
                    if(!g.w || x+g.w>xe+1)continue;
                    const uint32_t* col=reinterpret_cast<const uint32_t*>(s.pool+g.colOffset);uint32_t p=0,total=1;
                    for(size_t k=0;k<g.w;++k) {const uint32_t o=x+k<xe?colInk_[x+k]:0;p+=o>col[k]?o-col[k]:col[k]-o;total+=o+col[k];}
                    const uint32_t r=uint32_t(uint64_t(p)*1000/total);
                    if(r<pc)pc=r;
                }
                if(pc==UINT32_MAX)continue;
                if(n<candidates || pc<bestCost[n-1]) {
                    size_t at=n<candidates?n++:n-1;
                    while(at>0 && bestCost[at-1]>pc) {bestCost[at]=bestCost[at-1];bestGlyph[at]=bestGlyph[at-1];--at;}
                    bestCost[at]=pc;bestGlyph[at]=uint16_t(c);
                }
            }
            for(size_t k=0;k<n;++k) {
                uint32_t best=UINT32_MAX,seen=0;int bestDy=0;uint16_t bestVariant=0;
                for(size_t v=bestGlyph[k]*shifts;v<bestGlyph[k]*shifts+shifts;++v) {
                    const Glyph& g=s.g[v];
                    if(!g.w || x+g.w>xe+1)continue;
                    for(int dy=-1;dy<=1;++dy) {uint32_t o=0;const uint32_t e=Cost(s,g,x,base+dy,&o)+(dy?dyPenalty:0);if(e<best) {best=e;bestDy=dy;seen=o;bestVariant=uint16_t(v);}}
                }
                cand_[x*candidates+k]={bestVariant,int8_t(bestDy),best,seen};
            }
            candCount_[x]=uint8_t(n);
#ifdef CHARON_TTF_DEBUG
            if(debugColumn && size_t(debugColumn)==x)for(size_t k=0;k<n;++k) {const Cand& cd=cand_[x*candidates+k];const Glyph& g=s.g[cd.glyph];
                std::printf("  x%zu cand '%c' shift%zu w%u cost%u mass%u obs%u dy%d profile%u\n",x,char(chars[cd.glyph/shifts]),size_t(cd.glyph%shifts),g.w,cd.cost,g.mass,cd.observed,cd.dy,bestCost[k]);}
#endif
        }
        for(size_t x=xs;x<=xe+1;++x) {dp_[x]=inf;pen_[x]=none;back_[x]=0;}
        dp_[xs]=0;
        const size_t far=size_t(s.space*4.0f)+3;
        float final=inf;size_t finalX=xs;
        for(size_t x=xs;x<=xe+1;++x) {
            const float c=dp_[x];
            if(c>=inf)continue;
            const size_t from=x<xe?x:xe;
            {const float end=c+float(inkPrefix_[xe]-inkPrefix_[from]);if(end<final) {final=end;finalX=x;}}
            if(x>=xe)continue;
            size_t next=x;while(next<xe && colInk_[next]<48)++next;
            if(next>=xe)continue;
            // After a long blank run (or at the start) the pen is free.
            const bool free=pen_[x]==none || next-x>far;
            const size_t first=free?(next>xs?next-1:next):(x>=xs+2?x-2:xs);
            const size_t last=free?next:(x+far<xe?x+far:xe-1);
            for(size_t at=first;at<=last;++at) {
                const float unexplained=at>x?float(inkPrefix_[at]-inkPrefix_[x]):0.0f;
                for(size_t k=0;k<candCount_[at];++k) {
                    const Cand& cd=cand_[at*candidates+k];const Glyph& g=s.g[cd.glyph];
                    if(at+g.w<=x)continue;
                    const float pen=float(at)-g.x0+float(cd.glyph%shifts)/shifts;
                    float penalty=0;
                    if(!free) {
                        const float dev=pen-pen_[x];
                        if(dev<-3.0f)continue;
                        if(dev<-1.0f)penalty=(-1.0f-dev)*pxPenalty;
                        else if(dev>1.0f && dev<s.space*0.5f)penalty=(dev-1.0f)*pxPenalty;
                    }
                    const float total=c+unexplained+float(cd.cost)+glyphPenalty+penalty;
                    const size_t end=at+g.w;
                    if(end<=xe+1 && total<dp_[end]) {dp_[end]=total;pen_[end]=pen+g.adv;back_[end]=1u|uint32_t(k)<<1|uint32_t(at)<<8|uint32_t(x)<<20;}
                }
            }
        }
        // Back track.
        placedCount_=0;
        if(final>=inf)return 1.0f;
        uint64_t err=0,seen=0;
        {const uint64_t tail=inkPrefix_[xe]-inkPrefix_[finalX<xe?finalX:xe];err+=tail;seen+=tail;}
        Placed reversed[maxPlaced];size_t count=0;size_t x=finalX;
        while(x>xs && (back_[x]&1)) {
            const uint32_t b=back_[x];const size_t k=(b>>1)&0x7f,at=(b>>8)&0xfff,from=b>>20;
            const Cand& cd=cand_[at*candidates+k];const Glyph& g=s.g[cd.glyph];
            if(count<maxPlaced)reversed[count++]={uint16_t(at),cd.glyph,cd.dy,float(cd.cost),float(cd.cost)/float(g.mass+cd.observed+1)};
            err+=cd.cost;seen+=g.mass+cd.observed;
            if(at>from) {const uint64_t u=inkPrefix_[at]-inkPrefix_[from];err+=u;seen+=u;}
            x=from;
        }
        for(size_t i=0;i<count;++i)placed_[i]=reversed[count-1-i];
        placedCount_=count;(void)err;(void)seen;
        // Over the ink on screen only: the same for every font and size.
        const float ink=float(inkPrefix_[xe]-inkPrefix_[xs]);
        return ink>0?final/ink:1.0f;
    }
    // Horizontal size from the read itself: pen distances between letters of
    // a word over the font's advances (dozens of glyphs give the scale to a
    // fraction of a percent); then the vertical size that fits best with it.
    template<class Idle> bool Refine(Set*& best,size_t xs,size_t xe,size_t prefixEnd,int base,size_t phase,Idle& idle) {
        (void)xe;
        const Font& f=fonts_[best->font];
        if(f.strikes)return false;  // bitmap strikes have their own size
        float observed=0,units=0;size_t pairs=0;
        for(size_t i=1;i<placedCount_;++i) {
            const Placed& q=placed_[i-1];const Placed& p=placed_[i];
            const Glyph& h=best->g[q.glyph];const Glyph& g=best->g[p.glyph];
            const float a=float(q.x)-h.x0+float(q.glyph%shifts)/shifts,b=float(p.x)-g.x0+float(p.glyph%shifts)/shifts;
            if(b-a>h.adv+best->space*0.5f || q.norm>0.3f || p.norm>0.3f)continue;  // a space or a doubtful glyph
            int advance,lsb;stbtt_GetGlyphHMetrics(&f.info,f.glyph[q.glyph/shifts],&advance,&lsb);
            observed+=b-a;units+=float(advance+stbtt_GetGlyphKernAdvance(&f.info,f.glyph[q.glyph/shifts],f.glyph[p.glyph/shifts]));++pairs;
        }
        if(pairs<8 || units<=0)return false;
        const float em=observed/units/stbtt_ScaleForMappingEmToPixels(&f.info,1.0f);
        const uint16_t quarterX=uint16_t(em*4.0f+0.5f);
#ifdef CHARON_TTF_DEBUG
        if(std::getenv("TTF_TRACE"))std::printf("  refine pairs%zu em %.2f qx%u (was q%u)\n",pairs,em,quarterX,best->quarter);
#endif
        if(quarterX==best->quarter || quarterX+8<best->quarter || quarterX>best->quarter+8)return false;
        const size_t font=best->font;const uint16_t center=best->quarterY;
        float bestScore=2.0f;uint16_t bestY=0;
        for(int dy=-2;dy<=2;++dy) {
            const uint16_t quarterY=uint16_t(int(center)+dy);
            Set* s=Templates(font,quarterX,quarterY,phase,true);
            if(!s)continue;
            const float score=Line(*s,xs,prefixEnd,base);
            idle();
#ifdef CHARON_TTF_DEBUG
            if(std::getenv("TTF_TRACE"))std::printf("  refine qx%u qy%u score%.3f\n",quarterX,quarterY,score);
#endif
            if(score<bestScore) {bestScore=score;bestY=quarterY;}
        }
        if(!bestY)return false;
        Set* s=Templates(font,quarterX,bestY,phase);
        if(!s)return false;
        best=s;return true;
    }
    template<class Emit,class Idle> size_t Block(const uint32_t* pixels,size_t width,size_t stride,size_t left,size_t top,size_t y0,size_t y1,Emit& emit,Idle& idle) {
        const uint32_t bgColor=rowBg_[y0];const int bg=Lum(bgColor);
        int far=0;uint32_t fgColor=bgColor;
        for(size_t y=y0;y<y1;++y)for(size_t x=0;x<width;++x) {const int d=Lum(pixels[y*stride+x])-bg;if((d<0?-d:d)>(far<0?-far:far)) {far=d;fgColor=pixels[y*stride+x]&0xffffff;}}
        if(far<60 && far>-60)return 0;
        // One blank row above and below for the blur.
        const size_t inkTop=y0,inkEnd=y1;
        y0=y0?y0-1:0;y1=y1<scanHeight_?y1+1:y1;
        if(y1-y0>maxBlock)return 0;
        blockHeight_=y1-y0;
        for(size_t x=0;x<width;++x)colInk_[x]=0;
        for(size_t y=y0;y<y1;++y) {
            const uint32_t* row=pixels+y*stride;uint8_t* out=raw_+(y-y0)*maxWidth;
            // The blank rows added for the blur belong to the block only when
            // they share its background (a bar's edge above or below is not
            // ink).
            if((y<inkTop || y>=inkEnd) && rowBg_[y]!=bgColor) {for(size_t x=0;x<width;++x)out[x]=0;continue;}
            for(size_t x=0;x<width;++x) {
                int a=(Lum(row[x])-bg)*255/far;
                a=a<0?0:a>255?255:a;out[x]=uint8_t(a);colInk_[x]+=uint32_t(a);
            }
        }
        // r362: text without anti-aliasing (bitmap fonts) has a handful of
        // exact colours; any clear ink is a full pixel then, also in a paler
        // colour on the same line ("Description:" grey, its text white).
        {
            uint32_t seen[5];size_t distinct=0;
            for(size_t y=inkTop;y<inkEnd && distinct<=4;++y)for(size_t x=0;x<width && distinct<=4;++x) {
                const uint32_t c=pixels[y*stride+x]&0xffffff;bool known=false;
                for(size_t k=0;k<distinct;++k)if(seen[k]==c)known=true;
                if(!known) {if(distinct<5)seen[distinct]=c;++distinct;}
            }
            if(distinct<=4) {
                for(size_t x=0;x<width;++x)colInk_[x]=0;
                for(size_t y=y0;y<y1;++y) {
                    uint8_t* out=raw_+(y-y0)*maxWidth;
                    for(size_t x=0;x<width;++x) {out[x]=out[x]>=40?255:0;colInk_[x]+=out[x];}
                }
            }
        }
        // r362: a bar narrower than the row (a selected option in Windows
        // Memory Diagnostic's options): a run of one other colour along the
        // block's first and last inked rows. Inside it the bar colour is the
        // background and its text is the ink.
        barCount_=0;
        if(inkEnd-inkTop>=6) {
            const uint32_t* edge=pixels+inkTop*stride;const uint32_t* last=pixels+(inkEnd-1)*stride;
            for(size_t x=0;x<width;) {
                const uint32_t c=edge[x]&0xffffff;
                size_t e=x+1;while(e<width && (edge[e]&0xffffff)==c)++e;
                size_t same=0;for(size_t k=x;k<e;++k)if((last[k]&0xffffff)==c)++same;
                if(c!=bgColor && e-x>=16 && same*10>=(e-x)*9) {
                    const int barLum=Lum(c);int barFar=0;uint32_t barText=c;
                    for(size_t y=inkTop;y<inkEnd;++y)for(size_t k=x;k<e;++k) {
                        const int d=Lum(pixels[y*stride+k])-barLum;if((d<0?-d:d)>(barFar<0?-barFar:barFar)) {barFar=d;barText=pixels[y*stride+k]&0xffffff;}}
                    // Text on the bar keeps the bar's colours (a selection).
                    if((barFar>=60 || barFar<=-60) && barCount_<sizeof(bars_)/sizeof(bars_[0]))bars_[barCount_++]={x,e,c,barText};
                    for(size_t y=y0;y<y1;++y)for(size_t k=x;k<e;++k) {
                        int a=0;
                        if(y>=inkTop && y<inkEnd && (barFar>=60 || barFar<=-60)) {a=(Lum(pixels[y*stride+k])-barLum)*255/barFar;a=a<0?0:a>255?255:a;}
                        raw_[(y-y0)*maxWidth+k]=uint8_t(a);
                    }
                    for(size_t k=x;k<e;++k) {uint32_t sum=0;for(size_t y=0;y<blockHeight_;++y)sum+=raw_[y*maxWidth+k];colInk_[k]=sum;}
                }
                x=e;
            }
        }
        // Columns of one solid colour other than the background through the
        // whole block (the margins beside a highlight or title bar) are no
        // text.
        for(size_t x=0;x<width;++x) {
            const uint32_t c=pixels[y0*stride+x]&0xffffff;
            if(c==bgColor)continue;
            bool solid=true;for(size_t y=y0+1;y<y1 && solid;++y)solid=(pixels[y*stride+x]&0xffffff)==c;
            if(solid) {colInk_[x]=0;for(size_t y=0;y<blockHeight_;++y)raw_[y*maxWidth+x]=0;}
        }
        // Blurred copy for matching (screen edges count as blank).
        for(size_t y=0;y<blockHeight_;++y)for(size_t x=0;x<width;++x) {
            int sum=0;
            for(int dy=-1;dy<=1;++dy) {
                const int sy=int(y)+dy;if(sy<0 || sy>=int(blockHeight_))continue;
                const uint8_t* r=raw_+size_t(sy)*maxWidth;const int ky=dy?1:2;
                sum+=ky*(2*r[x]+(x?r[x-1]:0)+(x+1<width?r[x+1]:0));
            }
            alpha_[y*maxWidth+x]=blur_?uint8_t((sum+8)/16):raw_[y*maxWidth+x];
        }
        // Clusters of inked columns: their bottoms give the baseline, their
        // tops the cap (or ascender) and x heights.
        size_t xs=SIZE_MAX,xe=0;uint32_t bottoms[maxBlock+1]={},tops[maxBlock+1]={};
        for(size_t x=0;x<width;) {
            if(colInk_[x]<48) {++x;continue;}
            size_t end=x;while(end<width && colInk_[end]>=48)++end;
            if(xs==SIZE_MAX)xs=x;xe=end;
            size_t first=SIZE_MAX,last=0;
            for(size_t y=0;y<blockHeight_;++y)for(size_t c=x;c<end;++c)if(raw_[y*maxWidth+c]>=128) {if(first==SIZE_MAX)first=y;last=y;break;}
            if(first!=SIZE_MAX) {bottoms[last+1]+=uint32_t(end-x);tops[first]+=uint32_t(end-x);}
            x=end;
        }
        if(xs==SIZE_MAX || xe-xs<4)return 0;
        size_t base=0,capTop=SIZE_MAX;
        for(size_t y=1;y<=blockHeight_;++y)if(bottoms[y]>bottoms[base])base=y;
        for(size_t y=0;y<blockHeight_;++y)if(tops[y]) {capTop=y;break;}
        if(!base || capTop==SIZE_MAX || base<=capTop+3)return 0;
        // Vertical sub-pixel position of the baseline: glyphs drawn a
        // fraction of a pixel low leave a faint row under the last full one.
        size_t phase=0;
        if(base<blockHeight_) {
            uint32_t full=0,faint=0;
            for(size_t x=0;x<width;++x)if(raw_[(base-1)*maxWidth+x]>=128) {full+=raw_[(base-1)*maxWidth+x];faint+=raw_[base*maxWidth+x];}
            if(full) {phase=(faint*4+full/2)/full;if(phase>=4) {phase=0;++base;}}
        }
        // x height: the most common top clearly below the cap top.
        size_t xTop=0;uint32_t xWeight=0;
        for(size_t y=capTop+2;y<base;++y)if(tops[y]>xWeight) {xWeight=tops[y];xTop=y;}
        const float capPx=float(base-capTop),xPx=xWeight?float(base-xTop):0.0f;
        for(size_t x=0;x<width;++x) {uint32_t sum=0;for(size_t y=0;y<blockHeight_;++y)sum+=alpha_[y*maxWidth+x];colInk_[x]=sum;}
        xs=xs?xs-1:0;xe=xe+1<width?xe+1:width;
        // Candidate fonts and sizes; the font read last goes first.
        float bestScore=2.0f;Set* best=nullptr;size_t bestFont=0;uint16_t bestQuarter=0;size_t prefixEnd=xe;
        {size_t clusters=0;for(size_t x=xs;x<xe && clusters<14;++x)if(colInk_[x]>=48 && (x==0 || colInk_[x-1]<48))++clusters,prefixEnd=x;
         if(prefixEnd<=xs+4)prefixEnd=xe;}
        // A line with the same letter heights as one read before (same
        // screen style) tries that font and size first.
        const uint8_t capKey=uint8_t(base-capTop),xKey=uint8_t(xWeight?base-xTop:0);
        Learned* learned=nullptr;
        for(auto& l:learned_)if(l.quarter && l.cap==capKey && l.x==xKey)learned=&l;
        if(learned) {
            Set* s=Templates(learned->font,learned->quarter,learned->quarterY,phase);
            if(s) {
                const float score=Line(*s,xs,prefixEnd,int(base));
                idle();
                // Bitmap fonts match almost exactly: a looser fit is another
                // font of the theme (regular vs bold).
                if(score<=(fonts_[learned->font].strikes?0.15f:0.32f)) {best=s;bestScore=score;learned->stamp=++clock_;}
            }
        }
        for(size_t order=0;!best && order<fontCount_ && bestScore>0.12f;++order) {
            const size_t font=order==0?lastFont_:(order<=lastFont_?order-1:order);
            const Font& f=fonts_[font];
            uint16_t tried[16];size_t triedCount=0;
            float sizes[6];size_t sizeCount=0;
            if(f.strikes) {for(size_t k=0;k<f.strikes;++k)sizes[sizeCount++]=f.strikePpem[k];}
            else {
                sizes[sizeCount++]=capPx/f.capH;sizes[sizeCount++]=capPx/f.hH;
                if(xPx>0)sizes[sizeCount++]=xPx/f.xH;
            }
            float fontBest=2.0f;size_t worse=0;
            for(size_t i=0;i<sizeCount && bestScore>0.12f;++i,worse=0,fontBest=2.0f)for(int step=-3;step<=1 && bestScore>0.12f && worse<2;++step) {
                if(f.strikes && step)continue;  // strikes have exact sizes
                float em=sizes[i]+step*0.5f;
                if(em<7.0f || em>160.0f)continue;  // titles at 2560x1600 reach 85 px
                const uint16_t quarter=uint16_t(int(em*2.0f+0.5f)*2);
                bool seen=false;for(size_t t=0;t<triedCount;++t)if(tried[t]==quarter)seen=true;
                if(seen || triedCount>=16)continue;
                tried[triedCount++]=quarter;
                Set* s=Templates(font,quarter,quarter,phase,true);
                if(!s)continue;
                const float score=Line(*s,xs,prefixEnd,int(base));
                idle();
#ifdef CHARON_TTF_DEBUG
                if(std::getenv("TTF_TRACE"))std::printf("  try font%zu q%u prefix%zu-%zu score%.3f\n",font,quarter,xs,prefixEnd,score);
#endif
                if(score<bestScore) {bestScore=score;bestFont=font;bestQuarter=quarter;}
                // Sizes go up within one estimate: two worse in a row end it.
                if(score<fontBest) {fontBest=score;worse=0;} else ++worse;
            }


        }
        // The template cache may have reused the winner's slot meanwhile.
        const bool searched=!best;
        if(!best && bestQuarter)best=Templates(bestFont,bestQuarter,bestQuarter,phase);
#ifdef CHARON_TTF_DEBUG
        if(debugQuarter) {best=Templates(size_t(debugFont),uint16_t(debugQuarter),uint16_t(std::getenv("TTF_YQ")?std::atoi(std::getenv("TTF_YQ")):debugQuarter),phase);bestScore=0;}
#endif
#ifdef CHARON_TTF_DEBUG
        std::printf("block y%zu-%zu base%zu cap%zu x%zu font%d q%d score%.3f\n",y0,y1,base,capTop,xTop,best?best->font:-1,best?best->quarter:0,bestScore);
#endif
        if(!best || bestScore>0.38f)return 0;
        lastFont_=best->font;
        if(fonts_[best->font].strikes) {
            // Bitmap text has no anti-aliasing: any clear ink is a full
            // pixel, also in a paler colour on the same line ("Status:").
            for(size_t x=0;x<width;++x) {
                uint32_t sum=0;
                for(size_t y=0;y<blockHeight_;++y) {uint8_t& a=alpha_[y*maxWidth+x];a=a>=64?255:0;sum+=a;}
                colInk_[x]=sum;
            }
        }
        float score=Line(*best,xs,xe,int(base));
#ifdef CHARON_TTF_DEBUG
        if(!debugQuarter)
#endif
        if(searched && Refine(best,xs,xe,prefixEnd,int(base),phase,idle))score=Line(*best,xs,xe,int(base));
        if(searched && score<=0.30f) {
            Learned* slot=&learned_[0];
            for(auto& l:learned_) {if(l.cap==capKey && l.x==xKey) {slot=&l;break;}if(l.stamp<slot->stamp)slot=&l;}
            *slot={capKey,xKey,best->font,best->quarter,best->quarterY,++clock_};
        }
#ifdef CHARON_TTF_DEBUG
        if(std::getenv("TTF_TRACE"))std::printf("  full score %.3f\n",score);
        if(std::getenv("TTF_TRACE"))for(size_t i=0;i<placedCount_;++i)std::printf("  placed x%u '%c' shift%zu dy%d norm%.2f\n",placed_[i].x,char(chars[placed_[i].glyph/shifts]),size_t(placed_[i].glyph%shifts),placed_[i].dy,placed_[i].norm);
#endif
        idle();
        // Lines read right score up to 0.25 on Windows Boot Manager screens;
        // monospace text of other programs matched to these fonts 0.35.
        if(score>0.30f || !placedCount_)return 0;
        // The line must be mostly close matches with at least two letters or
        // digits; only then is anything emitted.
        size_t good=0,alnum=0;
        for(size_t i=0;i<placedCount_;++i) {
            if(placed_[i].norm<=0.40f)++good;
            const char16_t ch=chars[placed_[i].glyph/shifts];
            if((ch>='0' && ch<='9') || (ch>='A' && ch<='Z') || (ch>='a' && ch<='z') || ch>0x7f)++alnum;
        }
        if(good*10<placedCount_*7 || alnum<2)return 0;
        // Text with spaces from the pen positions; far gaps start a new part.
        // Parts are placed at their baseline row, which stays put when a
        // highlight bar (taller than the text) comes or goes.
        const size_t lineY=top+y0+(base?base-1:0);
        boldLine_=fonts_[best->font].bold;
        // Frame edges at the screen's left border (GRUB's terminal box) are
        // no punctuation.
        {size_t drop=0;
         while(drop<placedCount_ && left+placed_[drop].x<3+drop*4) {
             const char16_t c=chars[placed_[drop].glyph/shifts];
             // Only thin marks a frame's edge resembles; '*' (OpenCore's
             // selection) and letters stay.
             if(!(c==u'-' || c==0x2013 || c==0x2014 || c==0x27 || c==0x2019 || c==u'`' || c==u'|' || c==u',' || c==u'.' || c==u':' || c==u';'))break;
             ++drop;
         }
         if(drop && drop<placedCount_) {for(size_t i=drop;i<placedCount_;++i)placed_[i-drop]=placed_[i];placedCount_-=drop;}}
        // r367: a proportional font placed in fixed cells (GRUB's terminal
        // with a theme's DejaVu): pens on a regular pitch wider than the
        // glyphs' advances. Spaces are then counted in cells.
        float pitch=0;
        if(placedCount_>=6) {
            int counts[64]={};
            for(size_t i=1;i<placedCount_;++i) {const int d=int(placed_[i].x)-int(placed_[i-1].x)+int(best->g[placed_[i-1].glyph].x0)-int(best->g[placed_[i].glyph].x0);if(d>3 && d<64)counts[d]++;}
            int mode=0;for(int d=4;d<64;++d)if(counts[d]>counts[mode])mode=d;
            if(mode && size_t(counts[mode]+counts[mode-1]+(mode<63?counts[mode+1]:0))*10>=(placedCount_-1)*7)pitch=float(mode);
            float meanAdvance=0;for(size_t i=0;i<placedCount_;++i)meanAdvance+=best->g[placed_[i].glyph].adv;
            meanAdvance/=float(placedCount_);
            if(pitch<meanAdvance*1.15f)pitch=0;  // proportional text keeps its own spacing
        }
        const Font& f=fonts_[best->font];
        size_t recognized=0,length=0,startX=placed_[0].x;
        for(size_t i=0;i<placedCount_;++i) {
            const Placed& p=placed_[i];const Glyph& g=best->g[p.glyph];
            const char16_t ch=chars[p.glyph/shifts];
            if(i) {
                const Placed& q=placed_[i-1];const Glyph& h=best->g[q.glyph];
                const float kern=f.strikes?0.0f:stbtt_GetGlyphKernAdvance(&f.info,f.glyph[q.glyph/shifts],f.glyph[p.glyph/shifts])*best->scale;
                const float expected=float(q.x)-h.x0+h.adv+kern,actual=float(p.x)-g.x0;
                float gap=actual-expected;
                if(pitch>0) {
                    // Cells: one pitch per glyph; a space is a whole empty cell.
                    const float cells=(actual-(float(q.x)-h.x0))/pitch;
                    gap=(cells-1.0f)*best->space;
                    if(cells<1.5f)gap=0;
                }
                if(gap>best->space*4.0f && length) {
                    text_[length]=0;EmitPart(emit,left,startX,lineY,fgColor,bgColor);++recognized;length=0;startX=p.x;
                } else if(gap>=best->space*0.55f) {
                    size_t spaces=size_t(gap/best->space+0.5f);if(!spaces)spaces=1;if(spaces>3)spaces=3;
                    while(spaces-- && length+1<maxPlaced)text_[length++]=u' ';
                }
            }
            if(length+1<maxPlaced)text_[length++]=ch==u'\u2019'?u'\'':ch;
        }
        if(length) {text_[length]=0;EmitPart(emit,left,startX,lineY,fgColor,bgColor);++recognized;}
        return recognized;
    }
    template<class Emit> void EmitPart(Emit& emit,size_t left,size_t x,size_t y,uint32_t fg,uint32_t bg) {
        // A far part without letters or digits is a border or a mark, not
        // text (the edge of GRUB's progress bar after its message).
        bool word=false;
        for(const char16_t* p=text_;*p && !word;++p)word=(*p>=u'0' && *p<=u'9') || (*p>=u'A' && *p<=u'Z') || (*p>=u'a' && *p<=u'z') || *p==0xd3 || *p==0xf3 || (*p>=0x104 && *p<=0x17c);
        if(!word)return;
        for(size_t i=0;i<barCount_;++i)if(x+2>=bars_[i].from && x<bars_[i].to) {fg=bars_[i].text;bg=bars_[i].colour;break;}
        // A theme shows the chosen entry in a bold font: report it as a
        // selection (another background) like a highlight bar.
        // Text read off a picture has one common background whatever its
        // colour (black and white text are read on different pages).
        if(inPicture_)bg=0xc0c0c0;
        if(boldLine_)bg=bg==0x0000a0?0x00a000:0x0000a0;
        emit(text_,left+x,y,fg,bg,blockHeight_);
    }
    struct Bar {size_t from,to;uint32_t colour,text;};
    Bar bars_[8]={};size_t barCount_=0;
    bool blur_=false;
    AllocFn alloc_=nullptr;FreeFn free_=nullptr;void* allocContext_=nullptr;
    void (*idleFn_)(void*)=nullptr;void* idleContext_=nullptr;
    Font fonts_[maxFonts]={};size_t fontCount_=0,lastFont_=0;
    Set sets_[maxSets]={};uint32_t clock_=0;
    struct Learned {uint8_t cap,x,font;uint16_t quarter,quarterY;uint32_t stamp;};
    Learned learned_[8]={};
    uint8_t* alpha_=nullptr;uint8_t* raw_=nullptr;uint8_t* scratch_=nullptr;uint32_t* colInk_=nullptr;float* dp_=nullptr;uint32_t* back_=nullptr;float* pen_=nullptr;uint32_t* inkPrefix_=nullptr;Cand* cand_=nullptr;uint8_t* candCount_=nullptr;size_t blockHeight_=0,scanHeight_=0;
    uint32_t rowBg_[maxScan]={};bool rowInk_[maxScan]={},rowPicture_[maxScan]={},pictureMask_[maxScan]={};
    bool inPicture_=false,boldLine_=false;size_t bitmapFonts_=0;uint32_t* synthetic_=nullptr;size_t syntheticCapacity_=0;
    bool SyntheticBuffer(size_t pixels) {
        if(synthetic_ && syntheticCapacity_>=pixels)return true;
        if(synthetic_ && free_)free_(allocContext_,synthetic_);
        synthetic_=static_cast<uint32_t*>(Alloc(pixels*4));syntheticCapacity_=synthetic_?pixels:0;
        return synthetic_!=nullptr;
    }
    Placed placed_[maxPlaced]={};size_t placedCount_=0;char16_t text_[maxPlaced]={};
};
}  // namespace hakefi
