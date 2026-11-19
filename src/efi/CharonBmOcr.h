// Exact reader for text drawn with a BMFont atlas (OpenCanopy labels), r384.
// GPL-2.0-or-later.
//
// OpenCanopy copies the atlas coverage of each glyph straight into its
// picture (light text on a dark background, or the reverse), so the screen
// holds the template pixels themselves. Lines are rows of ink separated by
// background rows; along a line the pen starts at the first inked column and
// every glyph is tried at the pen (its own x offset), on baselines a few
// rows around the line's lowest ink; the closest one under a small error
// wins and the pen moves by its advance. Ink left where no glyph fits ends
// the word; a gap of a space advance or more becomes a space.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "CharonPf2Fonts.h"
namespace hakefi {
class BmOcr {
public:
    void SetFont(const Pf2Font* font) {font_=font;}
    bool Ready() const {return font_ && font_->coverage;}
    // emit(text,x,y,fg,bg) per line (x,y: screen position of the line's first ink).
    template<class Emit> size_t Recognize(const uint32_t* pixels,size_t width,size_t height,size_t stride,size_t left,size_t top,Emit emit) {
        if(!Ready() || width<8 || height<6 || width>maxWidth)return 0;
        // Background: the colour of most pixels.
        uint32_t bg=pixels[0]&0xffffff;
        {uint32_t cand[8]={};size_t counts[8]={};size_t n=0;
         for(size_t y=0;y<height;y+=3)for(size_t x=0;x<width;x+=5) {
            const uint32_t c=pixels[y*stride+x]&0xffffff;size_t k=0;while(k<n && cand[k]!=c)++k;
            if(k==n) {if(n==8)continue;cand[n]=c;counts[n++]=0;}
            ++counts[k];}
         size_t best=0;for(size_t k=1;k<n;++k)if(counts[k]>counts[best])best=k;bg=cand[best];}
        const int bgLum=Lum(bg);
        auto ink=[&](size_t x,size_t y) {const int d=Lum(pixels[y*stride+x]&0xffffff)-bgLum;return d<0?-d:d;};
        size_t lines=0;
        for(size_t y=0;y<height;) {
            // A line: consecutive rows with ink, at most 3 glyph heights.
            size_t y0=y;while(y0<height && !RowInk(ink,y0,width))++y0;
            if(y0>=height)break;
            size_t y1=y0;while(y1<height && RowInk(ink,y1,width))++y1;
            y=y1;
            if(y1-y0<3 || y1-y0>48)continue;
            // Parts of a line far apart (labels under separate icons) are
            // emitted separately at their own positions.
            ReadLine(ink,width,height,y0,y1,[&](const char16_t* text,size_t x) {emit(text,left+x,top+y0,0xffffff,bg);++lines;});
        }
        return lines;
    }
private:
    static constexpr size_t maxWidth=4096,maxText=160;
    static int Lum(uint32_t c) {return int(((c>>16)&255)*299+((c>>8)&255)*587+(c&255)*114)/1000;}
    template<class Ink> static bool RowInk(Ink& ink,size_t y,size_t width) {for(size_t x=0;x<width;++x)if(ink(x,y)>=48)return true;return false;}
    // A part is kept when it has two letters or digits and a word longer
    // than one character (icon strokes read as "I I").
    static bool Wordy(const char16_t* t,size_t n) {
        size_t letters=0,run=0,longest=0;
        for(size_t i=0;i<n;++i) {
            const bool l=(t[i]>=u'A' && t[i]<=u'Z') || (t[i]>=u'a' && t[i]<=u'z') || (t[i]>=u'0' && t[i]<=u'9');
            if(l)++letters;
            if(t[i]==u' ')run=0;else if(++run>longest)longest=run;
        }
        return letters>=2 && longest>=2;
    }
    template<class Ink,class Part> void ReadLine(Ink& ink,size_t width,size_t height,size_t y0,size_t y1,Part part) {
        char16_t text[maxText];size_t firstX=SIZE_MAX;
        const Pf2Font& f=*font_;
        // Text colour: the strongest ink of the line, as the 255 point.
        int far=0;for(size_t y=y0;y<y1;++y)for(size_t x=0;x<width;++x) {const int d=ink(x,y);if(d>far)far=d;}
        if(far<96)return;
        size_t spaceAdvance=4;for(size_t k=0;k<f.count;++k)if(f.glyphs[k].code==u' ')spaceAdvance=f.glyphs[k].advance;
        size_t at=0;size_t x=0;size_t lastEnd=SIZE_MAX;
        while(x<width && at+2<maxText) {
            // Next inked column.
            size_t c=x;
            while(c<width) {bool any=false;for(size_t y=y0;y<y1 && !any;++y)any=ink(c,y)>=48;if(any)break;++c;}
            if(c>=width)break;
            if(lastEnd!=SIZE_MAX && at && c>=lastEnd+3*spaceAdvance) {
                while(at && text[at-1]==u' ')--at;
                text[at]=0;if(Wordy(text,at))part(text,firstX);
                at=0;firstX=SIZE_MAX;lastEnd=SIZE_MAX;
            }
            if(lastEnd!=SIZE_MAX && at && c>=lastEnd+spaceAdvance && text[at-1]!=u' ')text[at++]=u' ';
            // Best glyph whose ink starts at column c, on any baseline that
            // puts its bottom within the line.
            int bestErr=0x7fffffff;size_t bestK=SIZE_MAX;int bestPen=0;
            for(size_t k=0;k<f.count;++k) {
                const Pf2Glyph& g=f.glyphs[k];
                if(!g.width || !g.height || g.code==u' ')continue;
                const uint8_t* cov=f.coverage+size_t(g.bits)*8;
                // Leftmost inked template column.
                size_t lx=0;bool found=false;
                for(;lx<g.width && !found;++lx)for(size_t yy=0;yy<g.height;++yy)if(cov[yy*g.width+lx]>=48) {found=true;break;}
                if(!found)continue;
                --lx;
                if(c<lx)continue;
                const size_t gx=c-lx;
                for(size_t gy=y0>3?y0-3:0;gy<=y1 && gy+g.height<=height;++gy) {
                    if(gy+g.height<y0+1 || gy>y1)continue;
                    int err=0,mass=0;
                    for(size_t yy=0;yy<g.height && err<bestErr;++yy)for(size_t xx=0;xx<g.width;++xx) {
                        if(gx+xx>=width)continue;
                        const int want=cov[yy*g.width+xx];int got=ink(gx+xx,gy+yy)*255/far;if(got>255)got=255;
                        err+=want>got?want-got:got-want;mass+=want;
                    }
                    if(!mass)continue;
                    // Normalised by the glyph's own ink: a small glyph inside a
                    // bigger one does not win on a smaller absolute error.
                    const int norm=err*1000/mass;
                    // Ink of the screen columns this glyph covers but does not explain.
                    if(norm<bestErr) {bestErr=norm;bestK=k;bestPen=int(gx)-g.xOffset;}
                }
            }
            if(bestK==SIZE_MAX || bestErr>220) {
                // Unreadable ink: skip this column group.
                size_t e=c;while(e<width) {bool any=false;for(size_t y=y0;y<y1 && !any;++y)any=ink(e,y)>=48;if(!any)break;++e;}
                x=e;lastEnd=SIZE_MAX;
                if(at && text[at-1]!=u' ')text[at++]=u' ';
                continue;
            }
            const Pf2Glyph& g=f.glyphs[bestK];
            if(firstX==SIZE_MAX)firstX=c;
            text[at++]=g.code;
            const size_t inkEnd=size_t(bestPen+g.xOffset+g.width);
            lastEnd=inkEnd;
            const size_t next=size_t(bestPen+g.advance);
            x=next>c?next:c+1;
            if(x<inkEnd && inkEnd-x>2)x=inkEnd-2>c?inkEnd-2:c+1;
        }
        while(at && text[at-1]==u' ')--at;
        text[at]=0;
        if(Wordy(text,at))part(text,firstX);
    }
    const Pf2Font* font_=nullptr;
};
}
