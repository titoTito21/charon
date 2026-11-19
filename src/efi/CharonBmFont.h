// OpenCanopy (OpenCore's graphical picker) label font for the resident
// reader (r384). GPL-2.0-or-later.
//
// OpenCanopy draws its labels with a BMFont: an 8-bit greyscale PNG atlas
// (Resources/Font/Font_1x.png, Font_2x.png) and a binary description
// (.bin: "BMF" 3, block 2 = common with line height and base, block 4 = chars
// of 20 bytes: id, x, y, width, height, xoffset, yoffset, xadvance, page,
// channel). The files are read from the disk OpenCore starts from; nothing
// is shipped with Charon. Glyphs ' '..'~' become PF2-style 1-bit templates
// (coverage of half or more) on the base line for TtfOcr::AddPf2Font.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "CharonPf2Fonts.h"
#include "CharonPng.h"
namespace hakefi {
struct BmFontAllocator {void* (*alloc)(void* context,size_t bytes);void (*release)(void* context,void* block);void* context;};
// On success `font` and the glyph/bit arrays it points to are allocated
// with `a` and stay owned by the caller (the OCR keeps pointers to them).
inline bool BuildBmFont(const uint8_t* png,size_t pngSize,const uint8_t* bin,size_t binSize,const BmFontAllocator& a,Pf2Font*& font) {
    font=nullptr;
    if(!png || !bin || binSize<4 || bin[0]!='B' || bin[1]!='M' || bin[2]!='F' || bin[3]!=3)return false;
    auto u16=[](const uint8_t* p) {return uint16_t(p[0]|(p[1]<<8));};
    auto u32=[](const uint8_t* p) {return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;};
    size_t lineHeight=0,base=0;const uint8_t* chars=nullptr;size_t charCount=0;
    for(size_t i=4;i+5<=binSize;) {
        const uint8_t type=bin[i];const size_t length=u32(bin+i+1);
        if(length>binSize-i-5)return false;
        if(type==2 && length>=4) {lineHeight=u16(bin+i+5);base=u16(bin+i+7);}
        if(type==4) {chars=bin+i+5;charCount=length/20;}
        i+=5+length;
    }
    if(!chars || !charCount || charCount>512 || !lineHeight || lineHeight>64 || !base || base>lineHeight || pngSize<33)return false;
    const size_t width=size_t(png[16])<<24|size_t(png[17])<<16|size_t(png[18])<<8|png[19];
    const size_t height=size_t(png[20])<<24|size_t(png[21])<<16|size_t(png[22])<<8|png[23];
    if(!width || !height || width>2048 || height>2048)return false;
    const size_t workSize=height*(1+width*4)+pngSize+1024;
    uint8_t* work=static_cast<uint8_t*>(a.alloc(a.context,workSize));
    uint8_t* pixels=static_cast<uint8_t*>(a.alloc(a.context,width*height));
    PngImage image;
    const bool decoded=work && pixels && DecodePalettePng(png,pngSize,work,workSize,pixels,width*height,image) && image.width==width && image.height==height;
    if(work)a.release(a.context,work);
    if(!decoded) {if(pixels)a.release(a.context,pixels);return false;}
    size_t totalBits=0,count=0;
    for(size_t c=0;c<charCount;++c) {
        const uint8_t* e=chars+c*20;const uint32_t id=u32(e);
        if(id<32 || id>126)continue;
        totalBits+=(size_t(u16(e+8))*u16(e+10)+7)/8*8;++count;
    }
    const size_t bitBytes=totalBits/8+1;
    Pf2Font* f=static_cast<Pf2Font*>(a.alloc(a.context,sizeof(Pf2Font)));
    Pf2Glyph* glyphs=static_cast<Pf2Glyph*>(a.alloc(a.context,sizeof(Pf2Glyph)*(count+1)));
    uint8_t* bits=static_cast<uint8_t*>(a.alloc(a.context,bitBytes));
    uint8_t* coverage=static_cast<uint8_t*>(a.alloc(a.context,bitBytes*8));
    bool ok=f && glyphs && bits && coverage;
    size_t g=0;
    if(ok) {
        for(size_t i=0;i<bitBytes;++i)bits[i]=0;
        for(size_t i=0;i<bitBytes*8;++i)coverage[i]=0;
        // Glyph boxes carry empty margins; templates are cut to their ink
        // (coverage 32 or more), and the base line is where 'H' ends, so
        // that capitals sit on it as the recogniser measures it on screen.
        auto ink=[&](const uint8_t* e,size_t& x0,size_t& y0,size_t& x1,size_t& y1) {
            const size_t x=u16(e+4),y=u16(e+6),w=u16(e+8),h=u16(e+10);
            x0=w;y0=h;x1=0;y1=0;
            if(x+w>width || y+h>height)return false;
            for(size_t yy=0;yy<h;++yy)for(size_t xx=0;xx<w;++xx)if(pixels[(y+yy)*width+x+xx]>=32) {
                if(xx<x0)x0=xx;if(yy<y0)y0=yy;if(xx+1>x1)x1=xx+1;if(yy+1>y1)y1=yy+1;}
            return x1>x0;
        };
        int baseline=int(base);
        for(size_t c=0;c<charCount;++c) {const uint8_t* e=chars+c*20;size_t x0,y0,x1,y1;
            if(u32(e)=='H' && ink(e,x0,y0,x1,y1))baseline=int16_t(u16(e+14))+int(y1);}
        size_t at=0;
        for(size_t c=0;c<charCount && ok;++c) {
            const uint8_t* e=chars+c*20;const uint32_t id=u32(e);
            if(id<32 || id>126)continue;
            size_t x=u16(e+4),y=u16(e+6),w=u16(e+8),h=u16(e+10);
            int xoff=int16_t(u16(e+12)),yoff=int16_t(u16(e+14));const int adv=int16_t(u16(e+16));
            if(x+w>width || y+h>height || w>64 || h>64 || adv<0 || adv>127) {ok=false;break;}
            size_t x0,y0,x1,y1;
            if(ink(e,x0,y0,x1,y1)) {x+=x0;y+=y0;xoff+=int(x0);yoff+=int(y0);w=x1-x0;h=y1-y0;}
            else {w=0;h=0;}
            Pf2Glyph& gl=glyphs[g++];
            gl.code=char16_t(id);gl.width=uint8_t(w);gl.height=uint8_t(h);gl.xOffset=int8_t(xoff);
            gl.yOffset=int8_t(baseline-yoff-int(h));gl.advance=int8_t(adv);gl.bits=uint32_t(at/8);
            for(size_t yy=0;yy<h;++yy)for(size_t xx=0;xx<w;++xx) {
                const uint8_t v=pixels[(y+yy)*width+x+xx];coverage[at+yy*w+xx]=v;
                if(v>=128)bits[(at+yy*w+xx)>>3]|=uint8_t(0x80>>((at+yy*w+xx)&7));}
            at+=(w*h+7)/8*8;   // each glyph starts on a byte (Pf2Glyph::bits is a byte index)
        }
    }
    a.release(a.context,pixels);
    if(!ok || !g) {if(f)a.release(a.context,f);if(glyphs)a.release(a.context,glyphs);if(bits)a.release(a.context,bits);if(coverage)a.release(a.context,coverage);return false;}
    f->name="OpenCanopy";f->pointSize=uint8_t(lineHeight);f->ascent=uint8_t(base);f->count=uint16_t(g);f->glyphs=glyphs;f->bits=bits;f->coverage=coverage;
    font=f;return true;
}
}
