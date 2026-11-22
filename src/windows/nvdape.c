/* nvdape.exe INSTALLER DESTINATION [LANGUAGE]
   Unpacks NVDA's installer (an NSIS launcher: one solid LZMA stream) into a folder, inside Windows
   Setup's WinPE. The launcher itself is a 32-bit program and WinPE runs no 32-bit programs, so NVDA
   cannot unpack itself there. Only the program is written: no documentation, no libraries for other
   processors, and of the translations only English and LANGUAGE (e.g. pl).
   Uses kernel32 only; no C runtime. Exit code 0: NVDA is in DESTINATION. */
typedef unsigned char u8;typedef unsigned short u16;typedef unsigned int u32;typedef unsigned long long u64;
typedef unsigned short wch;
#define W __declspec(dllimport)
W void* __stdcall CreateFileW(const wch* name,u32 access,u32 share,void* security,u32 creation,u32 flags,void* templ);
W int __stdcall ReadFile(void* file,void* buffer,u32 bytes,u32* got,void* overlapped);
W int __stdcall WriteFile(void* file,const void* buffer,u32 bytes,u32* put,void* overlapped);
W int __stdcall CloseHandle(void* handle);
W int __stdcall GetFileSizeEx(void* file,u64* size);
W void* __stdcall VirtualAlloc(void* address,u64 size,u32 type,u32 protect);
W int __stdcall CreateDirectoryW(const wch* name,void* security);
W wch* __stdcall GetCommandLineW(void);
W void __stdcall ExitProcess(u32 code);
#define INVALID ((void*)(long long)-1)

void* memcpy(void* to,const void* from,u64 n) {u8* t=(u8*)to;const u8* f=(const u8*)from;while(n--)*t++=*f++;return to;}
void* memset(void* to,int value,u64 n) {u8* t=(u8*)to;while(n--)*t++=(u8)value;return to;}
static void* Alloc(u64 bytes) {return VirtualAlloc(0,bytes,0x3000,4);}   /* commit+reserve, read/write */
static u32 Get32(const u8* p) {return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);}

/* ---- where the bytes go: the unpacked stream is [length][header][data: (length, bytes) per file] ---- */
enum {maxPath=600,maxFiles=8192};
typedef struct {u32 offset;u32 name;u32 dir;} Item;   /* name, dir: offsets into names[] */
static Item* items;static u32 itemCount;
static wch* names;static u32 namesUsed,namesCapacity;
static wch destination[maxPath];static u32 destinationLength;
static wch keepLanguage[16];
static u8* header;static u32 headerLength,headerGot;
static u8 small[4];static u32 smallGot;
static int phase;                 /* 0 header length, 1 header, 2 data */
static u64 dataPosition;          /* offset inside the data part */
static u32 nextItem;static u32 fileLeft;static int inFile,inLength;
enum {maxCopies=16};static void* outFiles[maxCopies];static u32 outCount;   /* a file stored once may be wanted under several names */
static int failed;static u32 written;
static wch lastDir[maxPath];

static u32 Length(const wch* s) {u32 n=0;while(s[n])++n;return n;}
static wch Lower(wch c) {return (c>='A' && c<='Z')?(wch)(c+32):c;}
/* does path (starting with a backslash) begin with the component list prefix? */
static int Under(const wch* path,const char* prefix) {
    u32 i=0;for(;prefix[i];++i)if(Lower(path[i])!=(wch)prefix[i])return 0;
    return path[i]==0 || path[i]=='\\';
}
static int SameNoCase(const wch* a,const wch* b,u32 n) {for(u32 i=0;i<n;++i)if(Lower(a[i])!=Lower(b[i]))return 0;return 1;}
/* which parts of NVDA are not needed to speak in Setup */
static int Unwanted(const wch* dir) {
    if(Under(dir,"\\documentation") || Under(dir,"\\_synthdrivers32"))return 1;
    if(Under(dir,"\\lib")) {   /* \lib\VERSION\{x64,x86,arm64,arm64ec}: this processor's only */
        const wch* p=dir+4;if(*p!='\\')return 0;
        ++p;while(*p && *p!='\\')++p;
        if(Under(p,"\\x86") || Under(p,"\\arm64") || Under(p,"\\arm64ec"))return 1;
        return 0;
    }
    if(Under(dir,"\\locale") && dir[7]=='\\') {
        const wch* p=dir+8;u32 n=0;while(p[n] && p[n]!='\\')++n;
        if(n==2 && Lower(p[0])=='e' && Lower(p[1])=='n')return 0;
        u32 k=Length(keepLanguage);
        if(k && n>=k && SameNoCase(p,keepLanguage,k) && (n==k || p[k]=='_'))return 0;
        return 1;
    }
    return 0;
}
static u32 AddName(const wch* s,u32 n) {
    if(namesUsed+n+1>namesCapacity) {failed=1;return 0;}
    const u32 at=namesUsed;memcpy(names+at,s,(u64)n*2);names[at+n]=0;namesUsed+=n+1;return at;
}
/* An NSIS string (UTF-16; 1..4 start a two-unit code, 3 is a variable). The text goes to out; the
   first variable met is reported once in *variable (0xffff: none) and leaves no text. False: the
   string holds something this program does not resolve. */
static int NsisString(u32 stringsAt,u32 offset,wch* out,u32 capacity,u32* variable) {
    const u8* p=header+stringsAt+(u64)offset*2;const u8* end=header+headerLength;u32 n=0;*variable=0xffff;
    for(;;) {
        if(p+2>end)return 0;
        const wch c=(wch)(p[0]|(p[1]<<8));p+=2;
        if(!c)break;
        if(c<=4) {
            if(p+2>end)return 0;
            const wch v=(wch)(p[0]|(p[1]<<8));p+=2;
            if(c==3 && n==0 && *variable==0xffff) {*variable=v&0x7fff;continue;}
            if(c==4) {if(n+1>=capacity)return 0;out[n++]=v;continue;}
            return 0;
        }
        if(n+1>=capacity)return 0;
        out[n++]=c;
    }
    out[n]=0;return 1;
}
/* The header: every "extract file" entry with the folder that is current when it runs. The launcher
   sets "$PLUGINSDIR\app" and then, for each folder below, "$_OUTDIR\folder". */
static int ParseHeader(void) {
    if(headerLength<4+8*8)return 0;
    const u32 entriesAt=Get32(header+4+2*8),entryCount=Get32(header+4+2*8+4),stringsAt=Get32(header+4+3*8);
    if(entriesAt>headerLength || stringsAt>headerLength || (u64)entriesAt+(u64)entryCount*28>headerLength)return 0;
    static wch text[maxPath],root[maxPath],current[maxPath];int have=0;u32 pluginsVariable=0xffff;current[0]=0;root[0]=0;
    for(u32 i=0;i<entryCount && !failed;++i) {
        const u8* e=header+entriesAt+(u64)i*28;const u32 which=Get32(e);u32 variable;
        if(which==11 && Get32(e+8)) {   /* CreateDirectory that also becomes the current folder */
            have=0;
            if(!NsisString(stringsAt,Get32(e+4),text,maxPath,&variable) || variable==0xffff)continue;
            if(pluginsVariable==0xffff)pluginsVariable=variable;
            if(variable==pluginsVariable) {   /* "$PLUGINSDIR\app..." */
                if(!Under(text,"\\app"))continue;
                memcpy(root,text+4,((u64)Length(text+4)+1)*2);memcpy(current,root,((u64)Length(root)+1)*2);have=1;
            } else {   /* "$_OUTDIR\folder": below the last folder set through $PLUGINSDIR */
                const u32 a=Length(root),b=Length(text);
                if(a+b+1>maxPath)continue;
                memcpy(current,root,(u64)a*2);memcpy(current+a,text,((u64)b+1)*2);have=1;
            }
        } else if(which==20 && have) {   /* extract a file */
            if(!NsisString(stringsAt,Get32(e+8),text,maxPath,&variable) || variable!=0xffff || !text[0])continue;
            if(Unwanted(current))continue;
            if(itemCount>=maxFiles) {failed=1;break;}
            Item* it=&items[itemCount++];it->offset=Get32(e+12);
            it->dir=AddName(current,Length(current));it->name=AddName(text,Length(text));
        }
    }
    if(failed || !itemCount)return 0;
    /* by position in the stream (they are almost in order already) */
    for(u32 i=1;i<itemCount;++i) {
        const Item x=items[i];u32 j=i;
        while(j && items[j-1].offset>x.offset) {items[j]=items[j-1];--j;}
        items[j]=x;
    }
    return 1;
}
static void MakeFolders(wch* path) {   /* every folder of a full path; path is changed and restored */
    for(u32 i=destinationLength?destinationLength:1;path[i];++i)if(path[i]=='\\') {path[i]=0;CreateDirectoryW(path,0);path[i]='\\';}
}
static void OpenItem(const Item* it) {
    static wch path[maxPath*2];
    const wch* dir=names+it->dir;const wch* name=names+it->name;
    const u32 a=destinationLength,b=Length(dir),c=Length(name);
    if(a+b+c+2>maxPath*2) {failed=1;return;}
    memcpy(path,destination,(u64)a*2);memcpy(path+a,dir,(u64)b*2);path[a+b]='\\';memcpy(path+a+b+1,name,((u64)c+1)*2);
    if(b>=maxPath || !SameNoCase(dir,lastDir,b+1)) {MakeFolders(path);if(b<maxPath)memcpy(lastDir,dir,((u64)b+1)*2);}
    if(outCount>=maxCopies) {failed=1;return;}
    void* file=CreateFileW(path,0x40000000,0,0,2,0x80,0);   /* write, create always */
    if(file==INVALID) {failed=1;return;}
    outFiles[outCount++]=file;
}
static void CloseOutputs(void) {for(u32 i=0;i<outCount;++i) {CloseHandle(outFiles[i]);++written;}outCount=0;}
static void Sink(const u8* data,u32 size) {
    while(size && !failed) {
        if(phase==0) {
            small[smallGot++]=*data++;--size;
            if(smallGot==4) {
                headerLength=Get32(small);smallGot=0;
                if(headerLength<64 || headerLength>(16u<<20) || !(header=(u8*)Alloc(headerLength))) {failed=1;return;}
                phase=1;
            }
        } else if(phase==1) {
            u32 n=headerLength-headerGot;if(n>size)n=size;
            memcpy(header+headerGot,data,n);headerGot+=n;data+=n;size-=n;
            if(headerGot==headerLength) {if(!ParseHeader()) {failed=1;return;}phase=2;}
        } else if(inFile) {
            u32 n=fileLeft;if(n>size)n=size;
            for(u32 i=0;i<outCount;++i) {u32 put=0;if(!WriteFile(outFiles[i],data,n,&put,0) || put!=n) {failed=1;return;}}
            data+=n;size-=n;dataPosition+=n;fileLeft-=n;
            if(!fileLeft) {CloseOutputs();inFile=0;}
        } else if(inLength) {
            small[smallGot++]=*data++;--size;++dataPosition;
            if(smallGot==4) {
                smallGot=0;inLength=0;fileLeft=Get32(small)&0x7fffffff;inFile=1;
                const u32 offset=items[nextItem].offset;
                while(nextItem<itemCount && items[nextItem].offset==offset && !failed)OpenItem(&items[nextItem++]);
                if(!fileLeft) {CloseOutputs();inFile=0;}
            }
        } else {
            while(nextItem<itemCount && items[nextItem].offset<dataPosition)++nextItem;   /* cannot happen in a sound file */
            if(nextItem>=itemCount) {dataPosition+=size;return;}
            const u64 gap=items[nextItem].offset-dataPosition;
            if(gap) {const u32 n=gap>size?size:(u32)gap;data+=n;size-=n;dataPosition+=n;}
            else inLength=1;
        }
    }
}

/* ---- LZMA (the stream format of xz/7-Zip's LZMA1, as written by NSIS: 5 property bytes, no length) ---- */
typedef u16 Prob;
static const u8* in;static const u8* inEnd;
static u32 range,code;static int inputEnded;
static u8 Next(void) {if(in<inEnd)return *in++;inputEnded=1;return 0;}
static u32 Bit(Prob* p) {
    const u32 bound=(range>>11)**p;u32 bit;
    if(code<bound) {range=bound;*p=(Prob)(*p+((2048-*p)>>5));bit=0;}
    else {range-=bound;code-=bound;*p=(Prob)(*p-(*p>>5));bit=1;}
    if(range<(1u<<24)) {range<<=8;code=(code<<8)|Next();}
    return bit;
}
static u32 Direct(int count) {
    u32 result=0;
    while(count--) {
        range>>=1;code-=range;const u32 t=0u-(code>>31);code+=range&t;
        if(range<(1u<<24)) {range<<=8;code=(code<<8)|Next();}
        result=(result<<1)+(t+1);
    }
    return result;
}
static u32 Tree(Prob* p,int bits) {u32 m=1;for(int i=0;i<bits;++i)m=(m<<1)|Bit(&p[m]);return m-(1u<<bits);}
static u32 ReverseTree(Prob* p,int bits) {u32 m=1,result=0;for(int i=0;i<bits;++i) {const u32 b=Bit(&p[m]);m=(m<<1)|b;result|=b<<i;}return result;}
typedef struct {Prob choice,choice2;Prob low[16][8];Prob mid[16][8];Prob high[256];} LengthCoder;
static u32 DecodeLength(LengthCoder* l,u32 posState) {
    if(!Bit(&l->choice))return Tree(l->low[posState],3);
    if(!Bit(&l->choice2))return 8+Tree(l->mid[posState],3);
    return 16+Tree(l->high,8);
}
static void Fill(Prob* p,u32 count) {while(count--)*p++=1024;}
static Prob isMatch[12][16],isRep[12],isRepG0[12],isRepG1[12],isRepG2[12],isRep0Long[12][16];
static Prob posSlot[4][64],specPos[1+128-14],align[16];
static LengthCoder lengthCoder,repLengthCoder;
/* 0 ok (end marker met), else an error */
static int Lzma(const u8* stream,u64 size) {
    if(size<5+5)return 1;
    const u32 properties=stream[0];if(properties>=9*5*5)return 2;
    const int lc=(int)(properties%9),lp=(int)((properties/9)%5),pb=(int)(properties/45);
    u32 dictSize=Get32(stream+1);if(dictSize<4096)dictSize=4096;
    if(dictSize>(256u<<20))return 3;
    u8* dict=(u8*)Alloc(dictSize);Prob* literal=(Prob*)Alloc((u64)(0x300u<<(lc+lp))*sizeof(Prob));
    if(!dict || !literal)return 4;
    Fill(literal,0x300u<<(lc+lp));Fill(&isMatch[0][0],12*16);Fill(isRep,12);Fill(isRepG0,12);Fill(isRepG1,12);Fill(isRepG2,12);
    Fill(&isRep0Long[0][0],12*16);Fill(&posSlot[0][0],4*64);Fill(specPos,1+128-14);Fill(align,16);
    Fill((Prob*)&lengthCoder,sizeof(lengthCoder)/sizeof(Prob));Fill((Prob*)&repLengthCoder,sizeof(repLengthCoder)/sizeof(Prob));
    in=stream+5;inEnd=stream+size;inputEnded=0;
    if(Next()!=0)return 5;
    range=0xffffffffu;code=0;for(int i=0;i<4;++i)code=(code<<8)|Next();
    u32 rep0=0,rep1=0,rep2=0,rep3=0,state=0,position=0;u64 total=0;int full=0;
    const u32 pbMask=(1u<<pb)-1,lpMask=(1u<<lp)-1;
    for(;;) {
        if(inputEnded || failed)return 6;
        const u32 posState=(u32)total&pbMask;
        if(!Bit(&isMatch[state][posState])) {
            const u8 previous=total?dict[position?position-1:dictSize-1]:0;
            Prob* p=literal+(u64)0x300*((((u32)total&lpMask)<<lc)+(previous>>(8-lc)));
            u32 symbol=1;
            if(state>=7) {
                u32 match=dict[position>rep0?position-rep0-1:position+dictSize-rep0-1];
                do {
                    const u32 matchBit=(match>>7)&1;match<<=1;
                    const u32 bit=Bit(&p[((1+matchBit)<<8)+symbol]);symbol=(symbol<<1)|bit;
                    if(matchBit!=bit)break;
                } while(symbol<0x100);
            }
            while(symbol<0x100)symbol=(symbol<<1)|Bit(&p[symbol]);
            dict[position++]=(u8)symbol;++total;
            if(position==dictSize) {Sink(dict,dictSize);position=0;full=1;}
            state=state<4?0:state<10?state-3:state-6;
            continue;
        }
        u32 length;
        if(Bit(&isRep[state])) {
            if(!total)return 7;
            if(!Bit(&isRepG0[state])) {
                if(!Bit(&isRep0Long[state][posState])) {
                    state=state<7?9:11;
                    dict[position]=dict[position>rep0?position-rep0-1:position+dictSize-rep0-1];++position;++total;
                    if(position==dictSize) {Sink(dict,dictSize);position=0;full=1;}
                    continue;
                }
            } else {
                u32 distance;
                if(!Bit(&isRepG1[state]))distance=rep1;
                else {if(!Bit(&isRepG2[state]))distance=rep2;else {distance=rep3;rep3=rep2;}rep2=rep1;}
                rep1=rep0;rep0=distance;
            }
            length=DecodeLength(&repLengthCoder,posState);state=state<7?8:11;
        } else {
            rep3=rep2;rep2=rep1;rep1=rep0;
            length=DecodeLength(&lengthCoder,posState);state=state<7?7:10;
            const u32 slot=Tree(posSlot[length<4?length:3],6);
            if(slot<4)rep0=slot;
            else {
                const int direct=(int)(slot>>1)-1;rep0=(2|(slot&1))<<direct;
                if(slot<14)rep0+=ReverseTree(specPos+rep0-slot,direct);
                else {rep0+=Direct(direct-4)<<4;rep0+=ReverseTree(align,4);}
            }
            if(rep0==0xffffffffu)break;   /* the end marker */
        }
        length+=2;
        if(rep0>=dictSize || (!full && rep0>=position))return 8;
        while(length--) {
            dict[position]=dict[position>rep0?position-rep0-1:position+dictSize-rep0-1];++position;++total;
            if(position==dictSize) {Sink(dict,dictSize);position=0;full=1;}
        }
    }
    if(position)Sink(dict,position);
    return failed?9:0;
}

/* ---- the program ---- */
static const wch* Argument(const wch* p,wch* out,u32 capacity) {
    while(*p==' ' || *p=='\t')++p;
    u32 n=0;int quoted=0;
    for(;*p && (quoted || (*p!=' ' && *p!='\t'));++p) {
        if(*p=='"') {quoted=!quoted;continue;}
        if(n+1<capacity)out[n++]=*p;
    }
    out[n]=0;return p;
}
void entry(void) {
    static wch program[maxPath],installer[maxPath],language[maxPath];
    const wch* p=GetCommandLineW();
    p=Argument(p,program,maxPath);p=Argument(p,installer,maxPath);p=Argument(p,destination,maxPath);Argument(p,language,maxPath);
    if(!installer[0] || !destination[0])ExitProcess(2);
    destinationLength=Length(destination);
    while(destinationLength && destination[destinationLength-1]=='\\')destination[--destinationLength]=0;
    for(u32 i=0;i<15 && language[i] && language[i]!='-' && language[i]!='_';++i) {keepLanguage[i]=language[i];keepLanguage[i+1]=0;}
    void* file=CreateFileW(installer,0x80000000,1,0,3,0,0);   /* read, open existing */
    if(file==INVALID)ExitProcess(3);
    u64 size=0;if(!GetFileSizeEx(file,&size) || size<4096 || size>(1ull<<31))ExitProcess(4);
    u8* data=(u8*)Alloc(size);if(!data)ExitProcess(5);
    for(u64 got=0;got<size;) {u32 n=0;const u32 want=size-got>(8u<<20)?(8u<<20):(u32)(size-got);if(!ReadFile(file,data+got,want,&n,0) || !n)ExitProcess(6);got+=n;}
    CloseHandle(file);
    /* the NSIS first header: on a 512-byte boundary behind the program; flags, 0xDEADBEEF, "NullsoftInst", header length, length of all that follows */
    u64 at=0;int found=0;static const char mark[]="NullsoftInst";
    for(at=512;at+28<=size;at+=512) {
        if(Get32(data+at+4)!=0xdeadbeefu)continue;
        int same=1;for(int i=0;i<12;++i)if(data[at+8+i]!=(u8)mark[i])same=0;
        if(same) {found=1;break;}
    }
    if(!found)ExitProcess(7);
    const u32 following=Get32(data+at+24);
    if(following<28+16 || at+following>size)ExitProcess(8);
    const u8* stream=data+at+28;
    if(stream[0]!=0x5d)ExitProcess(9);   /* not one solid LZMA stream */
    items=(Item*)Alloc((u64)maxFiles*sizeof(Item));namesCapacity=maxFiles*64;names=(wch*)Alloc((u64)namesCapacity*2);
    if(!items || !names)ExitProcess(5);
    lastDir[0]=1;lastDir[1]=0;
    CreateDirectoryW(destination,0);
    const int result=Lzma(stream,following-28);
    for(u32 i=0;i<outCount;++i)CloseHandle(outFiles[i]);
    if(result || failed || phase!=2 || !written)ExitProcess(20u+(u32)result);
    ExitProcess(0);
}
