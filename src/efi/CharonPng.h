// Minimal PNG decoding for Charon's resident reader (r365). GPL-2.0-or-later.
//
// Boot managers keep their console font as a PNG image inside their program
// (Clover's embedded theme: a 16x16 grid of 11x18 glyphs, one bit per
// pixel). The reader decodes such an image from the started program: zlib
// inflate (stored, fixed and dynamic Huffman blocks, RFC 1951), the five PNG
// row filters and palette images of 1, 2, 4 or 8 bits. Everything is bounds
// checked; a broken image is rejected, never read past.
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace hakefi {
class Inflate {
public:
    // Decompresses a zlib stream into out (capacity bytes); returns the
    // number of bytes written or SIZE_MAX on any error.
    size_t Run(const uint8_t* in,size_t size,uint8_t* out,size_t capacity) {
        in_=in;size_=size;pos_=2;bit_=0;bits_=0;out_=out;cap_=capacity;len_=0;
        if(size<2 || (in[0]&0x0f)!=8 || ((in[0]<<8)|in[1])%31 || (in[1]&0x20))return SIZE_MAX;
        for(;;) {
            const int last=Bits(1),type=Bits(2);
            if(last<0 || type<0)return SIZE_MAX;
            if(type==0) {
                bits_=0;bit_=0;
                if(pos_+4>size_)return SIZE_MAX;
                const size_t n=size_t(in_[pos_])|size_t(in_[pos_+1])<<8,nn=size_t(in_[pos_+2])|size_t(in_[pos_+3])<<8;pos_+=4;
                if((n^0xffff)!=nn || pos_+n>size_ || len_+n>cap_)return SIZE_MAX;
                for(size_t i=0;i<n;++i)out_[len_++]=in_[pos_++];
            } else if(type==1) {
                uint8_t lengths[320];
                for(int i=0;i<144;++i)lengths[i]=8;for(int i=144;i<256;++i)lengths[i]=9;for(int i=256;i<280;++i)lengths[i]=7;for(int i=280;i<288;++i)lengths[i]=8;
                for(int i=0;i<30;++i)lengths[288+i]=5;
                if(!Build(lit_,lengths,288) || !Build(dist_,lengths+288,30) || !Block())return SIZE_MAX;
            } else if(type==2) {
                if(!Dynamic() || !Block())return SIZE_MAX;
            } else return SIZE_MAX;
            if(last)break;
        }
        return len_;
    }
private:
    struct Table {uint16_t count[16];uint16_t symbol[320];};
    int Bits(int n) {
        while(bit_<n) {if(pos_>=size_)return -1;bits_|=uint32_t(in_[pos_++])<<bit_;bit_+=8;}
        const int v=int(bits_&((1u<<n)-1));bits_>>=n;bit_-=n;return v;
    }
    static bool Build(Table& t,const uint8_t* lengths,int n) {
        for(auto& c:t.count)c=0;
        for(int i=0;i<n;++i)t.count[lengths[i]]++;
        t.count[0]=0;
        uint16_t offs[16];offs[1]=0;
        int left=1;for(int len=1;len<16;++len) {left<<=1;left-=t.count[len];if(left<0)return false;}
        for(int len=1;len<15;++len)offs[len+1]=uint16_t(offs[len]+t.count[len]);
        for(int i=0;i<n;++i)if(lengths[i])t.symbol[offs[lengths[i]]++]=uint16_t(i);
        return true;
    }
    int Decode(const Table& t) {
        int code=0,first=0,index=0;
        for(int len=1;len<16;++len) {
            const int b=Bits(1);if(b<0)return -1;
            code|=b;const int count=t.count[len];
            if(code-count<first)return t.symbol[index+(code-first)];
            index+=count;first+=count;first<<=1;code<<=1;
        }
        return -1;
    }
    bool Dynamic() {
        static const uint8_t order[19]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
        const int hlit=Bits(5),hdist=Bits(5),hclen=Bits(4);
        if(hlit<0 || hdist<0 || hclen<0)return false;
        const int nlit=hlit+257,ndist=hdist+1;
        if(nlit>286 || ndist>30)return false;
        uint8_t lengths[320]={};
        for(int i=0;i<hclen+4;++i) {const int v=Bits(3);if(v<0)return false;lengths[order[i]]=uint8_t(v);}
        Table codes;if(!Build(codes,lengths,19))return false;
        uint8_t all[320]={};
        for(int i=0;i<nlit+ndist;) {
            int sym=Decode(codes);if(sym<0)return false;
            if(sym<16) {all[i++]=uint8_t(sym);continue;}
            int repeat=0;uint8_t value=0;
            if(sym==16) {if(!i)return false;value=all[i-1];const int r=Bits(2);if(r<0)return false;repeat=3+r;}
            else if(sym==17) {const int r=Bits(3);if(r<0)return false;repeat=3+r;}
            else {const int r=Bits(7);if(r<0)return false;repeat=11+r;}
            if(i+repeat>nlit+ndist)return false;
            while(repeat--)all[i++]=value;
        }
        if(!all[256])return false;
        return Build(lit_,all,nlit) && Build(dist_,all+nlit,ndist);
    }
    bool Block() {
        static const uint16_t lbase[29]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
        static const uint8_t lextra[29]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
        static const uint16_t dbase[30]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
        static const uint8_t dextra[30]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
        for(;;) {
            int sym=Decode(lit_);
            if(sym<0)return false;
            if(sym<256) {if(len_>=cap_)return false;out_[len_++]=uint8_t(sym);continue;}
            if(sym==256)return true;
            sym-=257;if(sym>=29)return false;
            const int le=Bits(lextra[sym]);if(le<0)return false;
            const size_t length=size_t(lbase[sym]+le);
            const int ds=Decode(dist_);if(ds<0 || ds>=30)return false;
            const int de=Bits(dextra[ds]);if(de<0)return false;
            const size_t distance=size_t(dbase[ds]+de);
            if(distance>len_ || len_+length>cap_)return false;
            for(size_t i=0;i<length;++i) {out_[len_]=out_[len_-distance];++len_;}
        }
    }
    const uint8_t* in_=nullptr;size_t size_=0,pos_=0;uint32_t bits_=0;int bit_=0;
    uint8_t* out_=nullptr;size_t cap_=0,len_=0;
    Table lit_{},dist_{};
};

// A palette PNG (colour type 3) of up to 8 bits as one index per pixel.
// work must hold height*(1+rowBytes) bytes; pixels width*height bytes.
struct PngImage {size_t width=0,height=0,depth=0;uint8_t palette[256][3]={};size_t paletteSize=0;};
inline bool DecodePalettePng(const uint8_t* png,size_t size,uint8_t* work,size_t workSize,uint8_t* pixels,size_t pixelCapacity,PngImage& image) {
    static const uint8_t signature[8]={0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a};
    if(size<33)return false;
    for(size_t i=0;i<8;++i)if(png[i]!=signature[i])return false;
    auto be32=[](const uint8_t* p) {return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];};
    // Collect the IDAT data in place at the front of work, after the rows.
    size_t pos=8,idat=0;const uint8_t* zlib=nullptr;bool split=false;
    uint8_t* joined=nullptr;size_t joinedCap=0;
    for(;;) {
        if(pos+12>size)return false;
        const size_t length=be32(png+pos);const uint8_t* type=png+pos+4;const uint8_t* data=png+pos+8;
        if(length>size-pos-12)return false;
        if(type[0]=='I' && type[1]=='H' && type[2]=='D' && type[3]=='R') {
            if(length<13)return false;
            image.width=be32(data);image.height=be32(data+4);image.depth=data[8];
            // Palette, or (r384) 8-bit greyscale such as OpenCanopy's font atlas.
            if((data[9]!=3 && !(data[9]==0 && data[8]==8)) || data[10] || data[11] || data[12])return false;  // deflate, no interlace
            if(data[9]==0) {image.paletteSize=256;for(size_t i=0;i<256;++i)for(size_t c=0;c<3;++c)image.palette[i][c]=uint8_t(i);}
            if(image.depth!=1 && image.depth!=2 && image.depth!=4 && image.depth!=8)return false;
            if(!image.width || !image.height || image.width>4096 || image.height>4096 || image.width*image.height>pixelCapacity)return false;
        } else if(type[0]=='P' && type[1]=='L' && type[2]=='T' && type[3]=='E') {
            image.paletteSize=length/3>256?256:length/3;
            for(size_t i=0;i<image.paletteSize;++i)for(size_t c=0;c<3;++c)image.palette[i][c]=data[i*3+c];
        } else if(type[0]=='I' && type[1]=='D' && type[2]=='A' && type[3]=='T') {
            if(!zlib) {zlib=data;idat=length;}
            else {
                // Several IDAT chunks: join them behind the row buffer.
                const size_t rowBytes=(image.width*image.depth+7)/8,rows=image.height*(1+rowBytes);
                if(!split) {
                    if(rows+idat>workSize)return false;
                    joined=work+rows;joinedCap=workSize-rows;
                    for(size_t i=0;i<idat;++i)joined[i]=zlib[i];
                    zlib=joined;split=true;
                }
                if(idat+length>joinedCap)return false;
                for(size_t i=0;i<length;++i)joined[idat+i]=data[i];
                idat+=length;
            }
        } else if(type[0]=='I' && type[1]=='E' && type[2]=='N' && type[3]=='D')break;
        pos+=12+length;
    }
    if(!zlib || !image.width)return false;
    const size_t rowBytes=(image.width*image.depth+7)/8,rows=image.height*(1+rowBytes);
    if(rows>workSize)return false;
    Inflate inflate;
    if(inflate.Run(zlib,idat,work,rows)!=rows)return false;
    // Undo the row filters (one byte per pixel or less: bpp = 1).
    for(size_t y=0;y<image.height;++y) {
        uint8_t* row=work+y*(1+rowBytes);const uint8_t filter=row[0];uint8_t* line=row+1;
        const uint8_t* prior=y?work+(y-1)*(1+rowBytes)+1:nullptr;
        for(size_t x=0;x<rowBytes;++x) {
            const int a=x?line[x-1]:0,b=prior?prior[x]:0,c=(x && prior)?prior[x-1]:0;
            int add=0;
            if(filter==1)add=a;else if(filter==2)add=b;else if(filter==3)add=(a+b)/2;
            else if(filter==4) {const int p=a+b-c,pa=p>a?p-a:a-p,pb=p>b?p-b:b-p,pc=p>c?p-c:c-p;add=(pa<=pb && pa<=pc)?a:(pb<=pc?b:c);}
            else if(filter)return false;
            line[x]=uint8_t(line[x]+add);
        }
        for(size_t x=0;x<image.width;++x) {
            const size_t bit=x*image.depth;
            pixels[y*image.width+x]=uint8_t((line[bit/8]>>(8-image.depth-bit%8))&((1u<<image.depth)-1));
        }
    }
    return true;
}
// A 16x16 grid of glyphs (code page order) as cell rows for
// TtfOcr::AddCellFont: cells of width/16 x height/16 pixels (at most 16x24);
// the ink is the darker of the palette's two most used colours.
inline bool GridFont(const uint8_t* pixels,const PngImage& image,uint16_t* table,size_t& cellWidth,size_t& cellHeight) {
    if(image.width%16 || image.height%16)return false;
    cellWidth=image.width/16;cellHeight=image.height/16;
    if(cellWidth<4 || cellWidth>16 || cellHeight<6 || cellHeight>24 || image.paletteSize<2)return false;
    size_t counts[256]={};
    for(size_t i=0;i<image.width*image.height;++i)counts[pixels[i]]++;
    size_t first=0,second=1;
    if(counts[second]>counts[first]) {first=1;second=0;}
    for(size_t i=2;i<256;++i) {if(counts[i]>counts[first]) {second=first;first=i;} else if(counts[i]>counts[second])second=i;}
    if(!counts[second] || first>=image.paletteSize || second>=image.paletteSize)return false;
    auto lum=[&](size_t i) {return image.palette[i][0]*299+image.palette[i][1]*587+image.palette[i][2]*114;};
    const size_t ink=lum(first)<lum(second)?first:second;
    for(size_t c=0;c<256;++c)for(size_t r=0;r<cellHeight;++r) {
        uint16_t row=0;
        const size_t y=(c/16)*cellHeight+r,x0=(c%16)*cellWidth;
        for(size_t x=0;x<cellWidth;++x)if(pixels[y*image.width+x0+x]==ink)row|=uint16_t(0x8000>>x);
        table[c*cellHeight+r]=row;
    }
    return true;
}
}  // namespace hakefi
