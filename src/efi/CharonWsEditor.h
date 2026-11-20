#pragma once
// r400: Charon workspace - buffers, the speaking text editor, the hex viewer
// and the calculator. Editing follows Emacs (C-f C-b C-n C-p, C-k, C-y, C-s,
// C-x C-s ...) and plain keys (arrows, Home/End, F2 save, F3 search again);
// speech follows Emacspeak: every move speaks what the point lands on, voices
// change with the kind of text (voice lock) and tones mark events and the
// indentation. GPL-2.0-or-later.
#include "CharonWsCore.h"
#include "CharonFormatting.h"
namespace hakefi {
namespace ws {
// ---- voice lock -----------------------------------------------------------
// A voice change inside an utterance: ESC 'f' + 12 payload characters, the
// same marker the screen reader sends (CharonFormatting.h WriteMark).
inline void Mark(char16_t* out,size_t cap,const formatting::Voice& v,formatting::Event event=formatting::Event::none,char16_t colour=u'-') {
    size_t at=Len(out);if(at+16>=cap)return;
    char16_t m[formatting::payload+1];formatting::WriteMark(m,v,true,colour,0,colour!=u'-',formatting::EventChar(event),u'-');
    if(at && out[at-1]!=u' ')out[at++]=u' ';
    out[at++]=0x001b;out[at++]=u'f';
    for(size_t i=1;i<=formatting::payload;++i)out[at++]=m[i];
    out[at++]=u' ';out[at]=0;
}
inline formatting::Voice Persona(const formatting::Personality& p,unsigned family=0) {
    formatting::Voice v;formatting::Add(v,p);v.family=family;return v;
}
enum class Face : uint8_t {plain,comment,heading,key,string,number,keyword,capitals};
inline formatting::Voice FaceVoice(Face face) {
    using namespace formatting;
    switch(face) {
        case Face::comment:return Persona(voiceLighten,familyDim);
        case Face::heading:return Persona(voiceTitle,familyTitle);
        case Face::key:return Persona(voiceBolden,familyBold);
        case Face::string:return Persona(voiceSmoothen,familyFace);
        case Face::number:return Persona(voiceMonotone);
        case Face::keyword:return Persona(voiceAnimate,familyHighlight);
        case Face::capitals: {Voice v;v.pitch=3;v.stress=2;return v;}
        default:return Voice{};
    }
}
inline bool KeywordAt(const char16_t* s,size_t n,size_t at,size_t& length) {
    static const char16_t* const words[]={u"menuentry",u"submenu",u"linux",u"initrd",u"chainloader",u"set",u"if",u"then",u"else",u"fi",u"for",u"do",u"done",
        u"echo",u"title",u"options",u"default",u"timeout",u"label",u"kernel",u"append",u"loader",u"efi",u"search",u"insmod",u"export",u"source",u"configfile",
        u"goto",u"endif",u"endfor",u"exit",u"protocol",u"cmdline",u"module_path",u"kernel_path",u"image_path",u"path",u"comment",u"volume",u"icon",u"scanfor"};
    if(at && speechtext::IsWord(s[at-1]))return false;
    for(const auto* w:words) {
        size_t k=0;while(w[k] && at+k<n && Fold(s[at+k])==w[k])++k;
        if(!w[k] && (at+k>=n || !speechtext::IsWord(s[at+k]))) {length=k;return true;}
    }
    return false;
}
// One line of text with voice changes for comments, section headings, keys
// before "=" or ":", strings, numbers, known keywords and CAPITAL words.
inline void VoiceLockLine(const char16_t* s,size_t n,char16_t* out,size_t cap,bool voices) {
    out[0]=0;size_t first=0;while(first<n && (s[first]==u' ' || s[first]==u'\t'))++first;
    auto put=[&](size_t a,size_t b) {size_t at=Len(out);for(size_t i=a;i<b && at+2<cap;++i)out[at++]=s[i]==u'\t'?u' ':s[i];out[at]=0;};
    if(!voices || first>=n) {put(first,n);return;}
    auto whole=[&](Face face) {Mark(out,cap,FaceVoice(face));put(first,n);};
    if(s[first]==u'#' || s[first]==u';' || (s[first]==u'/' && first+1<n && s[first+1]==u'/')
        || (first+3<n && Fold(s[first])==u'r' && Fold(s[first+1])==u'e' && Fold(s[first+2])==u'm' && s[first+3]==u' ')) {whole(Face::comment);return;}
    if(s[first]==u'[' || s[first]==u'/' || (s[first]==u':' && first+1<n)) {whole(Face::heading);return;}
    // key = value
    size_t assign=SIZE_MAX;
    for(size_t i=first;i<n;++i) {
        if(s[i]==u'"' || s[i]==u'\'' || s[i]==u' ')break;
        if(s[i]==u'=' || (s[i]==u':' && i+1<n && s[i+1]==u' ')) {assign=i;break;}
    }
    size_t at=first;Face current=Face::plain;
    auto face=[&](Face f) {if(f!=current) {Mark(out,cap,FaceVoice(f));current=f;}};
    if(assign!=SIZE_MAX && assign>first) {face(Face::key);put(first,assign);at=assign;}
    while(at<n) {
        size_t length=0;
        if(s[at]==u'"' || s[at]==u'\'') {
            size_t end=at+1;while(end<n && s[end]!=s[at])++end;
            if(end<n)++end;
            face(Face::string);put(at,end);at=end;continue;
        }
        if(speechtext::IsDigit(s[at]) && (!at || !speechtext::IsWord(s[at-1]))) {
            size_t end=at;while(end<n && (speechtext::IsWord(s[end]) || s[end]==u'.'))++end;
            face(Face::number);put(at,end);at=end;continue;
        }
        if(KeywordAt(s,n,at,length)) {face(Face::keyword);put(at,at+length);at+=length;continue;}
        if(speechtext::UpperCase(s[at]) && at+1<n && speechtext::UpperCase(s[at+1]) && (!at || !speechtext::IsWord(s[at-1]))) {
            size_t end=at;while(end<n && speechtext::IsWord(s[end]))++end;
            face(Face::capitals);put(at,end);at=end;continue;
        }
        size_t end=at+1;
        while(end<n && s[end]!=u'"' && s[end]!=u'\'' && !(speechtext::IsDigit(s[end]) && !speechtext::IsWord(s[end-1]))
            && !(speechtext::IsWord(s[end]) && !speechtext::IsWord(s[end-1])))++end;
        face(Face::plain);put(at,end);at=end;
    }
}
// ---- buffers ----------------------------------------------------------------
constexpr size_t maxBuffers=12,maxFileBytes=4u<<20;
enum class Kind : uint8_t {none,text,hex};
struct Buffer {
    Kind kind;char16_t name[72];FileRef file;bool hasFile,modified,readOnly,crlf,bom,voices;
    char16_t* text;size_t length,capacity;     // text: LF line ends
    size_t point,mark,top,goal;                // mark SIZE_MAX = none; top = offset of the first row shown
    uint8_t* bytes;size_t byteCount;           // hex viewer
    size_t spokenIndent;
};
struct UndoStep {uint8_t buffer,kind;size_t at,length;char16_t* text;};   // kind 1 = inserted, 2 = deleted (text kept)
constexpr size_t maxUndo=200;
struct Editor {
    Buffer buffers[maxBuffers];size_t current;
    char16_t* kill;size_t killLength;
    char16_t search[96];
    UndoStep undo[maxUndo];size_t undoCount;
    bool prefixX,prefixE;       // C-x, C-e (Emacspeak) pending
};
inline Editor& E() {static Editor editor;return editor;}
inline Buffer* Current() {auto& e=E();return e.current<maxBuffers && e.buffers[e.current].kind!=Kind::none?&e.buffers[e.current]:nullptr;}
inline void DropUndo(size_t buffer) {
    auto& e=E();size_t w=0;
    for(size_t i=0;i<e.undoCount;++i) {
        if(e.undo[i].buffer==buffer) {Release(e.undo[i].text);continue;}
        e.undo[w++]=e.undo[i];
    }
    e.undoCount=w;
}
inline void PushUndo(size_t buffer,uint8_t kind,size_t at,const char16_t* text,size_t length) {
    auto& e=E();
    // Typing joins into one step.
    if(kind==1 && length==1 && e.undoCount) {
        auto& last=e.undo[e.undoCount-1];
        if(last.buffer==buffer && last.kind==1 && last.at+last.length==at && last.length<200 && text[0]!=u'\n') {++last.length;return;}
    }
    if(e.undoCount==maxUndo) {Release(e.undo[0].text);for(size_t i=1;i<maxUndo;++i)e.undo[i-1]=e.undo[i];--e.undoCount;}
    auto& step=e.undo[e.undoCount++];step.buffer=uint8_t(buffer);step.kind=kind;step.at=at;step.length=length;step.text=nullptr;
    if(kind==2) {
        step.text=static_cast<char16_t*>(Allocate(length*2+2));
        if(!step.text) {--e.undoCount;return;}
        for(size_t i=0;i<length;++i)step.text[i]=text[i];
    }
}
inline bool Reserve(Buffer& b,size_t extra) {
    if(b.length+extra+1<=b.capacity)return true;
    size_t wanted=(b.length+extra+1)*2;if(wanted<4096)wanted=4096;
    if(wanted>(maxFileBytes*2))return false;
    auto* grown=static_cast<char16_t*>(Allocate(wanted*2));if(!grown)return false;
    for(size_t i=0;i<b.length;++i)grown[i]=b.text[i];
    grown[b.length]=0;Release(b.text);b.text=grown;b.capacity=wanted;return true;
}
inline bool InsertAt(Buffer& b,size_t at,const char16_t* text,size_t n,bool record=true) {
    if(b.readOnly || at>b.length || !Reserve(b,n))return false;
    for(size_t i=b.length;i>at;--i)b.text[i+n-1]=b.text[i-1];
    for(size_t i=0;i<n;++i)b.text[at+i]=text[i];
    b.length+=n;b.text[b.length]=0;b.modified=true;
    if(b.mark!=SIZE_MAX && b.mark>at)b.mark+=n;
    if(record)PushUndo(size_t(&b-E().buffers),1,at,text,n);
    return true;
}
inline bool DeleteAt(Buffer& b,size_t at,size_t n,bool record=true) {
    if(b.readOnly || at>=b.length || !n)return false;
    if(at+n>b.length)n=b.length-at;
    if(record)PushUndo(size_t(&b-E().buffers),2,at,b.text+at,n);
    for(size_t i=at;i+n<b.length;++i)b.text[i]=b.text[i+n];
    b.length-=n;b.text[b.length]=0;b.modified=true;
    if(b.mark!=SIZE_MAX) {if(b.mark>=at+n)b.mark-=n;else if(b.mark>at)b.mark=at;}
    return true;
}
inline size_t LineStart(const Buffer& b,size_t at) {while(at && b.text[at-1]!=u'\n')--at;return at;}
inline size_t LineEnd(const Buffer& b,size_t at) {while(at<b.length && b.text[at]!=u'\n')++at;return at;}
inline size_t LineNumber(const Buffer& b,size_t at) {size_t n=1;for(size_t i=0;i<at && i<b.length;++i)if(b.text[i]==u'\n')++n;return n;}
inline size_t NewBuffer(Kind kind,const char16_t* name) {
    auto& e=E();
    for(size_t i=0;i<maxBuffers;++i)if(e.buffers[i].kind==Kind::none) {
        auto& b=e.buffers[i];memset(&b,0,sizeof(b));
        b.kind=kind;Set(b.name,72,name);b.mark=SIZE_MAX;b.spokenIndent=SIZE_MAX;b.voices=true;
        if(kind==Kind::text && !Reserve(b,0)) {b.kind=Kind::none;return SIZE_MAX;}
        return i;
    }
    return SIZE_MAX;
}
inline void KillBuffer(size_t index) {
    auto& e=E();if(index>=maxBuffers)return;
    auto& b=e.buffers[index];Release(b.text);Release(b.bytes);DropUndo(index);memset(&b,0,sizeof(b));
    if(e.current==index) {e.current=SIZE_MAX;for(size_t i=0;i<maxBuffers;++i)if(e.buffers[i].kind!=Kind::none) {e.current=i;break;}}
}
inline size_t FindBufferFor(const FileRef& ref) {
    auto& e=E();
    for(size_t i=0;i<maxBuffers;++i)if(e.buffers[i].kind!=Kind::none && e.buffers[i].hasFile && e.buffers[i].file.volume==ref.volume
        && SameFold(e.buffers[i].file.path,ref.path))return i;
    return SIZE_MAX;
}
// Opens a file in a buffer: text (UTF-8 or UTF-16 with a byte order mark)
// or, for binary data or on request, the hex viewer. A missing file opens
// an empty buffer that Save will create.
inline size_t OpenBuffer(const FileRef& ref,bool forceHex,efi_status* error) {
    *error=EFI_SUCCESS;
    size_t existing=FindBufferFor(ref);
    if(existing!=SIZE_MAX && (E().buffers[existing].kind==Kind::hex)==forceHex)return existing;
    uint8_t* data=nullptr;size_t size=0;
    auto status=ReadWhole(ref,maxFileBytes,&data,&size);
    const bool missing=status==EFI_NOT_FOUND;
    if(status!=EFI_SUCCESS && !(missing && !forceHex)) {*error=status;return SIZE_MAX;}
    bool binary=forceHex;
    const bool utf16=size>=2 && data[0]==0xff && data[1]==0xfe;
    if(!binary && !utf16)for(size_t i=0;i<size && i<8192;++i)if(!data[i]) {binary=true;break;}
    const size_t index=NewBuffer(binary?Kind::hex:Kind::text,BaseName(ref.path));
    if(index==SIZE_MAX) {Release(data);*error=EFI_OUT_OF_RESOURCES;return SIZE_MAX;}
    auto& b=E().buffers[index];b.file=ref;b.hasFile=true;b.readOnly=VolumeReadOnly(ref.volume);
    if(binary) {b.bytes=data;b.byteCount=size;b.readOnly=true;return index;}
    if(size) {
        if(!Reserve(b,size+2)) {Release(data);KillBuffer(index);*error=EFI_OUT_OF_RESOURCES;return SIZE_MAX;}
        size_t n=0;
        if(utf16) {b.bom=true;for(size_t i=2;i+1<size;i+=2)b.text[n++]=char16_t(data[i]|data[i+1]<<8);b.text[n]=0;b.kind=Kind::text;}
        else {
            const size_t skip=size>=3 && data[0]==0xef && data[1]==0xbb && data[2]==0xbf?3:0;
            n=Utf8ToUtf16(data+skip,size-skip,b.text,b.capacity);
        }
        // CR LF -> LF (remembered for saving).
        size_t w=0;
        for(size_t i=0;i<n;++i) {if(b.text[i]==u'\r' && i+1<n && b.text[i+1]==u'\n') {b.crlf=true;continue;}b.text[w++]=b.text[i];}
        b.length=w;b.text[w]=0;
    }
    // UTF-16 files keep their encoding: mark with crlf/bom flags; bom means UTF-16 here.
    Release(data);b.modified=false;
    return index;
}
inline efi_status SaveBuffer(Buffer& b) {
    if(b.kind!=Kind::text || !b.hasFile)return EFI_INVALID_PARAMETER;
    size_t extra=0;if(b.crlf)for(size_t i=0;i<b.length;++i)if(b.text[i]==u'\n')++extra;
    const size_t cap=b.bom?(b.length+extra)*2+4:(b.length+extra)*3+8;
    uint8_t* bytes=static_cast<uint8_t*>(Allocate(cap));if(!bytes)return EFI_OUT_OF_RESOURCES;
    size_t n=0;
    if(b.bom) {
        bytes[n++]=0xff;bytes[n++]=0xfe;
        for(size_t i=0;i<b.length;++i) {
            if(b.text[i]==u'\n' && b.crlf) {bytes[n++]=13;bytes[n++]=0;}
            bytes[n++]=uint8_t(b.text[i]);bytes[n++]=uint8_t(b.text[i]>>8);}
    } else {
        for(size_t i=0;i<b.length;) {
            size_t end=i;while(end<b.length && b.text[end]!=u'\n')++end;
            n+=Utf16ToUtf8(b.text+i,end-i,bytes+n,cap-n);
            if(end<b.length) {if(b.crlf)bytes[n++]=13;bytes[n++]=10;++end;}
            i=end;
        }
    }
    const auto status=WriteWhole(b.file,bytes,n);Release(bytes);
    if(status==EFI_SUCCESS)b.modified=false;
    return status;
}
// ---- speech -------------------------------------------------------------------
inline void SayLineAt(Buffer& b,size_t at,bool indentTone=true) {
    const size_t start=LineStart(b,at),end=LineEnd(b,at);
    size_t indent=0;for(size_t i=start;i<end && (b.text[i]==u' ' || b.text[i]==u'\t');++i)indent+=b.text[i]==u'\t'?4:1;
    if(indentTone && indent!=b.spokenIndent && end>start) {Tone(300+unsigned(indent>40?40:indent)*35,35);b.spokenIndent=indent;}
    if(end==start) {Say(T(u"pusty",u"blank"));return;}
    static char16_t text[1300];size_t n=end-start;if(n>500)n=500;
    VoiceLockLine(b.text+start,n,text,1300,b.voices);
    if(!text[0])Set(text,1300,T(u"same spacje",u"spaces only"));
    Say(text);
}
inline void SayPointChar(Buffer& b) {
    if(b.point>=b.length) {Play(Icon::edge);Say(T(u"koniec bufora",u"end of buffer"));return;}
    SayChar(b.text[b.point]);
}
inline void WordAt(const Buffer& b,size_t at,size_t& start,size_t& end) {
    start=at;end=at;
    if(at<b.length && speechtext::IsWord(b.text[at])) {
        while(start && speechtext::IsWord(b.text[start-1]))--start;
        while(end<b.length && speechtext::IsWord(b.text[end]))++end;
    } else if(at<b.length)end=at+1;
}
inline void SayWordAt(Buffer& b,size_t at) {
    size_t start,end;WordAt(b,at,start,end);
    if(end==start) {SayPointChar(b);return;}
    if(end-start==1) {SayChar(b.text[start]);return;}
    static char16_t word[200];size_t n=0;for(size_t i=start;i<end && n<198;++i)word[n++]=b.text[i];
    word[n]=0;Say(word);
}
inline void SayRange(Buffer& b,size_t from,size_t to) {
    static char16_t text[1300];size_t n=0;
    for(size_t i=from;i<to && n<1200;++i) {const char16_t c=b.text[i];text[n++]=c==u'\n'?u' ':c==u'\t'?u' ':c;if(c==u'\n' && n<1200) {text[n++]=u'.';text[n++]=u' ';}}
    text[n]=0;Say(n?text:T(u"nic",u"nothing"));
}
inline void SayModeLine(Buffer& b) {
    static char16_t text[400];Set(text,400,b.name);
    Cat(text,400,b.kind==Kind::hex?T(u", podgląd szesnastkowy",u", hex view"):T(u", tekst",u", text"));
    if(b.modified)Cat(text,400,T(u", zmieniony",u", modified"));
    if(b.readOnly)Cat(text,400,T(u", tylko do odczytu",u", read only"));
    if(b.kind==Kind::text) {
        Cat(text,400,T(u", wiersz ",u", line "));CatN(text,400,LineNumber(b,b.point));
        Cat(text,400,T(u", kolumna ",u", column "));CatN(text,400,b.point-LineStart(b,b.point)+1);
        Cat(text,400,T(u", znaków ",u", characters "));CatN(text,400,b.length);
    } else {Cat(text,400,T(u", bajtów ",u", bytes "));CatN(text,400,b.byteCount);}
    Say(text);
}
// ---- drawing ------------------------------------------------------------------
inline void HexDigits(char16_t* out,size_t cap,uint64_t value,unsigned digits) {
    static const char16_t hex[]=u"0123456789ABCDEF";
    for(unsigned i=digits;i--;)CatC(out,cap,hex[(value>>(i*4))&15]);
}
inline void DrawEditor(Buffer& b) {
    const size_t body=BodyRows();auto& u=U();static char16_t line[300];
    Set(line,300,T(u"Środowisko Charona: ",u"Charon workspace: "));Cat(line,300,b.name);Title(line);
    size_t cursorRow=1,cursorColumn=0;
    if(b.kind==Kind::hex) {
        const size_t rowAt=b.point/16;size_t topRow=b.top;
        if(rowAt<topRow)topRow=rowAt;
        if(rowAt>=topRow+body)topRow=rowAt-body+1;
        b.top=topRow;
        for(size_t r=0;r<body;++r) {
            const size_t offset=(topRow+r)*16;line[0]=0;
            if(offset<b.byteCount || (!b.byteCount && !r)) {
                HexDigits(line,300,offset,8);Cat(line,300,u"  ");
                for(size_t k=0;k<16;++k) {if(offset+k<b.byteCount)HexDigits(line,300,b.bytes[offset+k],2);else Cat(line,300,u"  ");Cat(line,300,k==7?u"  ":u" ");}
                Cat(line,300,u" ");
                for(size_t k=0;k<16 && offset+k<b.byteCount;++k) {const uint8_t c=b.bytes[offset+k];CatC(line,300,c>=32 && c<127?char16_t(c):u'.');}
            }
            Row(1+r,line);
            if(topRow+r==rowAt) {cursorRow=1+r;cursorColumn=10+(b.point%16)*3+(b.point%16>7?1:0);}
        }
    } else {
        if(b.point<b.top)b.top=LineStart(b,b.point);
        else {
            size_t lines=0;for(size_t i=b.top;i<b.point;++i)if(b.text[i]==u'\n')++lines;
            while(lines>=body) {b.top=LineEnd(b,b.top)+1;--lines;}
        }
        size_t at=b.top;const size_t width=u.cols-1;
        for(size_t r=0;r<body;++r) {
            line[0]=0;
            if(at<=b.length) {
                const size_t end=LineEnd(b,at);size_t shift=0;
                const bool here=b.point>=at && b.point<=end;
                if(here && b.point-at>=width-1)shift=b.point-at-(width-2);
                size_t n=0;for(size_t i=at+shift;i<end && n<width && n<298;++i)line[n++]=b.text[i]==u'\t'?u' ':b.text[i];
                line[n]=0;
                if(here) {cursorRow=1+r;cursorColumn=b.point-at-shift;}
                at=end+1;
            }
            Row(1+r,line);
        }
    }
    Set(line,300,b.modified?u"-**- ":u"---- ");Cat(line,300,b.name);
    if(b.kind==Kind::text) {Cat(line,300,u"   L");CatN(line,300,LineNumber(b,b.point));Cat(line,300,u" C");CatN(line,300,b.point-LineStart(b,b.point)+1);}
    else {Cat(line,300,u"   0x");HexDigits(line,300,b.point,8);}
    if(b.readOnly)Cat(line,300,u"  %%");
    Cat(line,300,b.kind==Kind::hex?u"  (Hex)":u"  (Text)");
    ModeLine(line);Cursor(true,cursorColumn,cursorRow);
}
// ---- commands -----------------------------------------------------------------
inline void Refuse(Buffer& b) {
    Play(Icon::error);Say(b.readOnly?T(u"Bufor tylko do odczytu",u"The buffer is read only"):T(u"Nie można",u"Cannot do that"));
}
inline void StoreKill(const char16_t* text,size_t n) {
    auto& e=E();Release(e.kill);e.kill=static_cast<char16_t*>(Allocate(n*2+2));e.killLength=0;
    if(!e.kill)return;
    for(size_t i=0;i<n;++i)e.kill[i]=text[i];
    e.killLength=n;
}
inline bool FindForward(Buffer& b,size_t from,const char16_t* pattern,size_t& found) {
    const size_t m=Len(pattern);if(!m)return false;
    for(size_t i=from;i+m<=b.length;++i) {
        size_t k=0;while(k<m && Fold(b.text[i+k])==Fold(pattern[k]))++k;
        if(k==m) {found=i;return true;}
    }
    return false;
}
inline bool FindBackward(Buffer& b,size_t from,const char16_t* pattern,size_t& found) {
    const size_t m=Len(pattern);if(!m || b.length<m)return false;
    size_t i=from<b.length-m?from:b.length-m;
    for(;;) {
        size_t k=0;while(k<m && Fold(b.text[i+k])==Fold(pattern[k]))++k;
        if(k==m) {found=i;return true;}
        if(!i)return false;
        --i;
    }
}
inline void SearchAgain(Buffer& b,bool forward) {
    auto& e=E();size_t found=0;
    if(!e.search[0]) {Say(T(u"Brak szukanego tekstu",u"No search text"));return;}
    bool ok=forward?FindForward(b,b.point+1,e.search,found):(b.point && FindBackward(b,b.point-1,e.search,found));
    bool wrapped=false;
    if(!ok) {ok=forward?FindForward(b,0,e.search,found):FindBackward(b,b.length,e.search,found);wrapped=ok;}
    if(!ok) {Play(Icon::error);Say(T(u"Nie znaleziono",u"Not found"));return;}
    b.point=found;Play(Icon::search);
    if(wrapped)Tone(700,60);
    SayLineAt(b,b.point);
}
inline void Search(Buffer& b,bool forward) {
    auto& e=E();static char16_t text[96];Set(text,96,e.search);
    if(!Prompt(forward?T(u"Szukaj:",u"Search:"):T(u"Szukaj wstecz:",u"Search backward:"),text,96))return;
    Set(e.search,96,text);
    size_t found=0;
    // The first search may match at the point itself.
    if(forward && FindForward(b,b.point,e.search,found)) {b.point=found;Play(Icon::search);SayLineAt(b,b.point);return;}
    SearchAgain(b,forward);
}
inline void ReplaceAll(Buffer& b) {
    static char16_t from[96],to[96];from[0]=0;to[0]=0;
    if(b.readOnly) {Refuse(b);return;}
    if(!Prompt(T(u"Zamień tekst:",u"Replace text:"),from,96) || !from[0])return;
    if(!Prompt(T(u"Na:",u"With:"),to,96))return;
    size_t count=0,at=0,found=0;const size_t m=Len(from),n=Len(to);
    while(FindForward(b,at,from,found) && count<100000) {
        if(!DeleteAt(b,found,m) || (n && !InsertAt(b,found,to,n)))break;
        at=found+n;++count;
    }
    if(b.point>b.length)b.point=b.length;
    static char16_t text[120];Set(text,120,T(u"Zamieniono: ",u"Replaced: "));CatN(text,120,count);Play(Icon::done);Say(text);
}
inline void GotoLine(Buffer& b) {
    static char16_t text[16];text[0]=0;
    if(!Prompt(b.kind==Kind::hex?T(u"Idź do przesunięcia (szesnastkowo):",u"Go to offset (hex):"):T(u"Idź do wiersza:",u"Go to line:"),text,16))return;
    uint64_t value=0;
    for(size_t i=0;text[i];++i) {
        const char16_t c=Fold(text[i]);
        if(b.kind==Kind::hex) {if(c>=u'0' && c<=u'9')value=value*16+(c-u'0');else if(c>=u'a' && c<=u'f')value=value*16+(c-u'a'+10);}
        else if(c>=u'0' && c<=u'9')value=value*10+(c-u'0');
    }
    if(b.kind==Kind::hex) {b.point=value<b.byteCount?size_t(value):(b.byteCount?b.byteCount-1:0);return;}
    size_t at=0;for(uint64_t line=1;line<value && at<b.length;++line) {at=LineEnd(b,at);if(at<b.length)++at;}
    b.point=at;SayLineAt(b,at);
}
inline void Undo(Buffer& b) {
    auto& e=E();const size_t self=size_t(&b-e.buffers);
    for(size_t i=e.undoCount;i--;) {
        if(e.undo[i].buffer!=self)continue;
        UndoStep step=e.undo[i];
        for(size_t k=i+1;k<e.undoCount;++k)e.undo[k-1]=e.undo[k];
        --e.undoCount;
        if(step.kind==1) {DeleteAt(b,step.at,step.length,false);b.point=step.at<b.length?step.at:b.length;}
        else {InsertAt(b,step.at,step.text,step.length,false);b.point=step.at+step.length;Release(step.text);}
        Play(Icon::yank);Say(T(u"Cofnięto",u"Undone"));SayLineAt(b,b.point,false);return;
    }
    Play(Icon::edge);Say(T(u"Nie ma czego cofać",u"Nothing to undo"));
}
// Asks for a file name next to the buffer's file (or on Charon's disk).
inline bool AskFile(Buffer* near,const char16_t* label,FileRef& ref) {
    static char16_t text[pathUnits];text[0]=0;
    if(near && near->hasFile) {ref.volume=near->file.volume;ParentOf(near->file.path,text,pathUnits);if(Len(text)>1)Cat(text,pathUnits,u"\\");}
    else {ref.volume=U().host->selfDevice;Set(text,pathUnits,u"\\");}
    if(!Prompt(label,text,pathUnits-1) || !text[0])return false;
    for(size_t i=0;text[i];++i)if(text[i]==u'/')text[i]=u'\\';
    ref.path[0]=0;if(text[0]!=u'\\')Set(ref.path,pathUnits,u"\\");
    Cat(ref.path,pathUnits,text);
    return Len(ref.path)>1;
}
inline void Save(Buffer& b,bool as) {
    if(b.kind!=Kind::text) {Refuse(b);return;}
    if(as || !b.hasFile) {
        FileRef ref;if(!AskFile(&b,T(u"Zapisz jako:",u"Write file:"),ref))return;
        b.file=ref;b.hasFile=true;Set(b.name,72,BaseName(ref.path));b.readOnly=false;
    }
    const auto status=SaveBuffer(b);
    if(status!=EFI_SUCCESS) {Fail(status==EFI_WRITE_PROTECTED?T(u"Nie zapisano: nośnik tylko do odczytu.",u"Not saved: the medium is read only.")
        :status==EFI_VOLUME_FULL?T(u"Nie zapisano: brak miejsca na dysku.",u"Not saved: the disk is full."):T(u"Nie zapisano pliku.",u"The file was not saved."),status);return;}
    static char16_t text[200];Set(text,200,T(u"Zapisano ",u"Wrote "));Cat(text,200,b.name);Play(Icon::save);Say(text);
}
enum class EditResult {stay,close,global};   // global: the key belongs to the workspace
// One key in the editor. `global` hands workspace keys (M-x, C-x b ...) back.
inline EditResult EditKey(Buffer& b,const Key& key) {
    auto& e=E();
    const bool hex=b.kind==Kind::hex;
    // C-x prefix.
    if(e.prefixX) {
        e.prefixX=false;
        if(key.ctrl && key.ch==u's') {Save(b,false);return EditResult::stay;}
        if(key.ctrl && key.ch==u'w') {Save(b,true);return EditResult::stay;}
        if(Printable(key) && key.ch==u'h' && !hex) {b.mark=0;b.point=b.length;Play(Icon::mark);Say(T(u"Zaznaczono cały bufor",u"Whole buffer marked"));return EditResult::stay;}
        if(Printable(key) && key.ch==u'u' && !hex) {Undo(b);return EditResult::stay;}
        if(Printable(key) && key.ch==u'=') {SayModeLine(b);return EditResult::stay;}
        if(key.ctrl && key.ch==u'x' && !hex) {   // exchange point and mark
            if(b.mark==SIZE_MAX) {Say(T(u"Brak znacznika",u"No mark"));return EditResult::stay;}
            const size_t p=b.point;b.point=b.mark;b.mark=p;SayLineAt(b,b.point);return EditResult::stay;}
        e.prefixX=true;return EditResult::global;   // C-x b, C-x k, C-x C-f, C-x C-c: the workspace
    }
    // C-e prefix: Emacspeak's speech commands.
    if(e.prefixE) {
        e.prefixE=false;
        if(hex) {SayModeLine(b);return EditResult::stay;}
        const char16_t c=Printable(key)?Fold(key.ch):key.ctrl?key.ch:0;
        if(c==u'e') {b.point=LineEnd(b,b.point);SayPointChar(b);}
        else if(c==u'l')SayLineAt(b,b.point,false);
        else if(c==u'w')SayWordAt(b,b.point);
        else if(c==u'c') {
            if(b.point<b.length) {static char16_t text[160];CharText(b.text[b.point],text,100);speechtext::Out o{text,160,Len(text)};o.Puts(u", ");
                if(!speechtext::Phonetic(b.text[b.point],Pl(),false,o))speechtext::CharCode(b.text[b.point],Pl(),o);
                Say(text);} else SayPointChar(b);}
        else if(c==u'b')SayRange(b,0,b.length);
        else if(c==u'n')SayRange(b,b.point,b.length);   // rest of the buffer
        else if(c==u'r') {if(b.mark==SIZE_MAX)Say(T(u"Brak znacznika",u"No mark"));else SayRange(b,b.mark<b.point?b.mark:b.point,b.mark<b.point?b.point:b.mark);}
        else if(c==u'm')SayModeLine(b);
        else if(c==u'i') {size_t s=LineStart(b,b.point),n=0;while(s+n<b.length && (b.text[s+n]==u' ' || b.text[s+n]==u'\t'))++n;
            static char16_t text[80];Set(text,80,T(u"wcięcie ",u"indent "));CatN(text,80,n);Say(text);}
        else if(c==u'v') {b.voices=!b.voices;Say(b.voices?T(u"Głosy formatowania włączone",u"Voice lock on"):T(u"Głosy formatowania wyłączone",u"Voice lock off"));}
        else if(c==u's') {auto* fx=U().host->effects;if(fx && fx->Silence)fx->Silence(fx->context);}
        else Say(T(u"C-e: l wiersz, w słowo, c znak, b bufor, n reszta, r zaznaczenie, m stan, i wcięcie, v głosy, s cisza, e koniec wiersza",
            u"C-e: l line, w word, c character, b buffer, n rest, r region, m mode line, i indent, v voices, s silence, e end of line"));
        return EditResult::stay;
    }
    if(key.ctrl && key.ch==u'x') {e.prefixX=true;return EditResult::stay;}
    if(key.ctrl && key.ch==u'e') {e.prefixE=true;return EditResult::stay;}
    if(key.scan==scEsc)return EditResult::close;
    if(key.scan==scF2) {Save(b,false);return EditResult::stay;}
    if(key.scan==scF4 || (key.alt && key.ch==u'g')) {GotoLine(b);return EditResult::stay;}
    if(key.scan==scF6) {SayModeLine(b);return EditResult::stay;}
    if(hex) {
        const size_t before=b.point,last=b.byteCount?b.byteCount-1:0,body=BodyRows();
        if(key.scan==scRight || (key.ctrl && key.ch==u'f')) {if(b.point<last)++b.point;}
        else if(key.scan==scLeft || (key.ctrl && key.ch==u'b')) {if(b.point)--b.point;}
        else if(key.scan==scDown || (key.ctrl && key.ch==u'n')) {if(b.point+16<=last)b.point+=16;}
        else if(key.scan==scUp || (key.ctrl && key.ch==u'p')) {if(b.point>=16)b.point-=16;}
        else if(key.scan==scPageDown)b.point=b.point+16*body<=last?b.point+16*body:last;
        else if(key.scan==scPageUp)b.point=b.point>=16*body?b.point-16*body:0;
        else if(key.scan==scHome)b.point=key.ctrl?0:b.point/16*16;
        else if(key.scan==scEnd)b.point=key.ctrl?last:(b.point/16*16+15<=last?b.point/16*16+15:last);
        else return EditResult::global;
        if(before==b.point) {Play(Icon::edge);return EditResult::stay;}
        static char16_t text[400];text[0]=0;
        if(!b.byteCount) {Say(T(u"pusty plik",u"empty file"));return EditResult::stay;}
        const bool rowMove=key.scan==scDown || key.scan==scUp || key.scan==scPageDown || key.scan==scPageUp;
        if(rowMove) {
            // A row: its offset, then the bytes as text when printable, else hex.
            const size_t row=b.point/16*16;Cat(text,400,T(u"przesunięcie ",u"offset "));HexDigits(text,400,row,row>0xffff?8:4);Cat(text,400,u": ");
            bool printable=true;for(size_t k=0;k<16 && row+k<b.byteCount;++k)if(b.bytes[row+k]<32 || b.bytes[row+k]>126)printable=false;
            for(size_t k=0;k<16 && row+k<b.byteCount;++k) {
                if(printable)CatC(text,400,char16_t(b.bytes[row+k]));
                else {HexDigits(text,400,b.bytes[row+k],2);Cat(text,400,u" ");}
            }
        } else {
            HexDigits(text,400,b.bytes[b.point],2);
            const uint8_t c=b.bytes[b.point];
            if(c>32 && c<127) {Cat(text,400,u", ");CharText(char16_t(c),text+Len(text),60);}
        }
        Say(text);return EditResult::stay;
    }
    // ---- text ----
    const size_t before=b.point;
    auto shiftMark=[&]() {if(key.shift && b.mark==SIZE_MAX) {b.mark=before;Play(Icon::mark);}};
    auto edge=[&]() {Play(Icon::edge);Say(b.point?T(u"koniec bufora",u"end of buffer"):T(u"początek bufora",u"beginning of buffer"));};
    const bool wordKey=key.ctrl || key.alt;
    if((key.scan==scRight && !wordKey) || (key.ctrl && key.ch==u'f')) {
        shiftMark();if(b.point<b.length) {++b.point;SayPointChar(b);} else edge();return EditResult::stay;}
    if((key.scan==scLeft && !wordKey) || (key.ctrl && key.ch==u'b')) {
        shiftMark();if(b.point) {--b.point;SayPointChar(b);} else edge();return EditResult::stay;}
    if(key.scan==scDown || key.scan==scUp || (key.ctrl && (key.ch==u'n' || key.ch==u'p'))) {
        shiftMark();
        const bool down=key.scan==scDown || (key.ctrl && key.ch==u'n');
        const size_t start=LineStart(b,b.point);
        if(!b.goal || b.goal<b.point-start)b.goal=b.point-start;
        if(down) {
            const size_t end=LineEnd(b,b.point);
            if(end>=b.length) {edge();return EditResult::stay;}
            const size_t next=end+1,nextEnd=LineEnd(b,next);b.point=next+b.goal<nextEnd?next+b.goal:nextEnd;
        } else {
            if(!start) {b.point=0;edge();return EditResult::stay;}
            const size_t previous=LineStart(b,start-1);b.point=previous+b.goal<start-1?previous+b.goal:start-1;
        }
        const size_t goal=b.goal;SayLineAt(b,b.point);b.goal=goal;return EditResult::stay;
    }
    b.goal=0;
    if((key.scan==scRight && wordKey) || (key.alt && key.ch==u'f')) {
        shiftMark();
        if(b.point>=b.length) {edge();return EditResult::stay;}
        while(b.point<b.length && speechtext::IsWord(b.text[b.point]))++b.point;
        while(b.point<b.length && !speechtext::IsWord(b.text[b.point]))++b.point;
        if(b.point>=b.length)edge();else SayWordAt(b,b.point);
        return EditResult::stay;
    }
    if((key.scan==scLeft && wordKey) || (key.alt && key.ch==u'b')) {
        shiftMark();
        if(!b.point) {edge();return EditResult::stay;}
        while(b.point && !speechtext::IsWord(b.text[b.point-1]))--b.point;
        while(b.point && speechtext::IsWord(b.text[b.point-1]))--b.point;
        SayWordAt(b,b.point);return EditResult::stay;
    }
    if(key.scan==scHome || (key.ctrl && key.ch==u'a') || (key.alt && key.ch==u'<')) {
        shiftMark();
        if((key.scan==scHome && key.ctrl) || key.alt) {b.point=0;Tone(500,40);SayLineAt(b,0);}
        else {b.point=LineStart(b,b.point);SayPointChar(b);}
        return EditResult::stay;
    }
    if(key.scan==scEnd || (key.alt && key.ch==u'>')) {
        shiftMark();
        if((key.scan==scEnd && key.ctrl) || key.alt) {b.point=b.length;Tone(500,40);SayLineAt(b,b.point);}
        else {b.point=LineEnd(b,b.point);Say(T(u"koniec wiersza",u"end of line"));}
        return EditResult::stay;
    }
    if(key.scan==scPageDown || key.scan==scPageUp || (key.ctrl && key.ch==u'v') || (key.alt && key.ch==u'v')) {
        shiftMark();
        const bool down=key.scan==scPageDown || (key.ctrl && key.ch==u'v');
        for(size_t i=0;i+1<BodyRows();++i) {
            if(down) {const size_t end=LineEnd(b,b.point);if(end>=b.length)break;b.point=end+1;}
            else {const size_t start=LineStart(b,b.point);if(!start)break;b.point=LineStart(b,start-1);}
        }
        if(before==b.point)edge();else {Tone(500,40);SayLineAt(b,b.point);}
        return EditResult::stay;
    }
    if(key.scan==scF8 || (key.ctrl && key.ch==u' ')) {b.mark=b.point;Play(Icon::mark);Say(T(u"Znacznik ustawiony",u"Mark set"));return EditResult::stay;}
    if(key.scan==scF3) {SearchAgain(b,!key.shift);return EditResult::stay;}
    if(key.ctrl && key.ch==u's') {Search(b,true);return EditResult::stay;}
    if(key.ctrl && key.ch==u'r') {Search(b,false);return EditResult::stay;}
    if(key.alt && key.ch==u'%') {ReplaceAll(b);return EditResult::stay;}
    if(key.ctrl && key.ch==u'l') {SayLineAt(b,b.point,false);return EditResult::stay;}
    if((key.ctrl && (key.ch==u'/' || key.ch==u'z' || key.ch==u'_')) || key.scan==scF9) {Undo(b);return EditResult::stay;}
    if(key.alt && key.ch==u'w') {
        if(b.mark==SIZE_MAX) {Say(T(u"Brak znacznika",u"No mark"));return EditResult::stay;}
        const size_t a=b.mark<b.point?b.mark:b.point,z=b.mark<b.point?b.point:b.mark;
        StoreKill(b.text+a,z-a);b.mark=SIZE_MAX;Play(Icon::mark);Say(T(u"Skopiowano zaznaczenie",u"Region copied"));return EditResult::stay;
    }
    if(key.ctrl && key.ch==u'g') {b.mark=SIZE_MAX;Play(Icon::close);Say(T(u"Anulowano",u"Quit"));return EditResult::stay;}
    // ---- changes ----
    if(key.ctrl && key.ch==u'w') {
        if(b.mark==SIZE_MAX) {Say(T(u"Brak znacznika",u"No mark"));return EditResult::stay;}
        const size_t a=b.mark<b.point?b.mark:b.point,z=b.mark<b.point?b.point:b.mark;
        if(b.readOnly) {Refuse(b);return EditResult::stay;}
        StoreKill(b.text+a,z-a);b.mark=SIZE_MAX;DeleteAt(b,a,z-a);b.point=a;Play(Icon::deleted);Say(T(u"Wycięto zaznaczenie",u"Region killed"));return EditResult::stay;
    }
    if(key.ctrl && key.ch==u'k') {
        if(b.readOnly) {Refuse(b);return EditResult::stay;}
        size_t end=LineEnd(b,b.point);if(end==b.point && end<b.length)++end;
        if(end==b.point) {edge();return EditResult::stay;}
        StoreKill(b.text+b.point,end-b.point);DeleteAt(b,b.point,end-b.point);Play(Icon::deleted);Say(T(u"Wycięto do końca wiersza",u"Killed to end of line"));
        return EditResult::stay;
    }
    if(key.ctrl && key.ch==u'y') {
        if(!e.kill || !e.killLength) {Say(T(u"Schowek jest pusty",u"Nothing to yank"));return EditResult::stay;}
        if(!InsertAt(b,b.point,e.kill,e.killLength)) {Refuse(b);return EditResult::stay;}
        b.point+=e.killLength;Play(Icon::yank);
        SayLineAt(b,b.point,false);return EditResult::stay;
    }
    if(key.scan==scDelete || (key.ctrl && key.ch==u'd')) {
        if(b.point>=b.length) {edge();return EditResult::stay;}
        if(b.readOnly) {Refuse(b);return EditResult::stay;}
        DeleteAt(b,b.point,1);Play(Icon::deleted);SayPointChar(b);return EditResult::stay;
    }
    if(!key.scan && key.ch==8 && !key.alt) {
        if(!b.point) {edge();return EditResult::stay;}
        if(b.readOnly) {Refuse(b);return EditResult::stay;}
        const char16_t gone=b.text[b.point-1];--b.point;DeleteAt(b,b.point,1);Play(Icon::deleted);SayChar(gone);return EditResult::stay;
    }
    if(!key.scan && (key.ch==13 || key.ch==10 || key.ch==9) && !key.alt) {
        char16_t c=key.ch==9?u'\t':u'\n';
        if(!InsertAt(b,b.point,&c,1)) {Refuse(b);return EditResult::stay;}
        ++b.point;Tone(c==u'\n'?620:540,25);
        if(c==u'\n')Say(T(u"nowy wiersz",u"new line"));else Say(u"tab");
        return EditResult::stay;
    }
    if(Printable(key)) {
        const char16_t c=key.ch;
        if(!InsertAt(b,b.point,&c,1)) {Refuse(b);return EditResult::stay;}
        ++b.point;
        // Word echo at a space or punctuation (the word just finished), else the character.
        if(!speechtext::IsWord(c) && b.point>=2 && speechtext::IsWord(b.text[b.point-2])) {
            if(c==u' ')SayWordAt(b,b.point-2);
            else {   // the word, then the sign that ended it
                size_t start,end;WordAt(b,b.point-2,start,end);static char16_t said[260];size_t n=0;
                for(size_t i=start;i<end && n<190;++i)said[n++]=b.text[i];
                said[n++]=u',';said[n++]=u' ';said[n]=0;CharText(c,said+n,60);Say(said);
            }
        }
        else SayChar(c);
        return EditResult::stay;
    }
    return EditResult::global;
}
// ---- calculator -----------------------------------------------------------------
struct Calc {const char16_t* s;bool error;};
inline void CalcSkip(Calc& c) {while(*c.s==u' ')++c.s;}
inline double CalcExpr(Calc& c,int level);
inline double CalcAtom(Calc& c) {
    CalcSkip(c);
    if(*c.s==u'(') {++c.s;const double v=CalcExpr(c,0);CalcSkip(c);if(*c.s==u')')++c.s;else c.error=true;return v;}
    if(*c.s==u'-') {++c.s;return -CalcAtom(c);}
    if(*c.s==u'+') {++c.s;return CalcAtom(c);}
    if(*c.s==u'~') {++c.s;return double(~int64_t(CalcAtom(c)));}
    if(c.s[0]==u'0' && (c.s[1]==u'x' || c.s[1]==u'X')) {
        c.s+=2;uint64_t v=0;bool any=false;
        for(;;++c.s) {const char16_t d=Fold(*c.s);if(d>=u'0' && d<=u'9')v=v*16+(d-u'0');else if(d>=u'a' && d<=u'f')v=v*16+(d-u'a'+10);else break;any=true;}
        if(!any)c.error=true;
        return double(v);
    }
    if(c.s[0]==u'0' && (c.s[1]==u'b' || c.s[1]==u'B')) {
        c.s+=2;uint64_t v=0;bool any=false;
        for(;*c.s==u'0' || *c.s==u'1';++c.s) {v=v*2+(*c.s-u'0');any=true;}
        if(!any)c.error=true;
        return double(v);
    }
    if((*c.s>=u'0' && *c.s<=u'9') || *c.s==u'.' || *c.s==u',') {
        double v=0;while(*c.s>=u'0' && *c.s<=u'9') {v=v*10+(*c.s-u'0');++c.s;}
        if(*c.s==u'.' || *c.s==u',') {++c.s;double scale=0.1;while(*c.s>=u'0' && *c.s<=u'9') {v+=(*c.s-u'0')*scale;scale/=10;++c.s;}}
        // Size suffixes: k, m, g (binary).
        const char16_t unit=Fold(*c.s);
        if(unit==u'k') {v*=1024;++c.s;} else if(unit==u'm') {v*=1048576;++c.s;} else if(unit==u'g') {v*=1073741824.0;++c.s;}
        return v;
    }
    c.error=true;return 0;
}
// Levels: 0 | ^ &   1 << >>   2 + -   3 * / %
inline double CalcExpr(Calc& c,int level) {
    if(level>3)return CalcAtom(c);
    double left=CalcExpr(c,level+1);
    for(;;) {
        CalcSkip(c);const char16_t op=*c.s;
        if(level==0 && (op==u'|' || op==u'^' || op==u'&')) {++c.s;const int64_t r=int64_t(CalcExpr(c,1));const int64_t l=int64_t(left);left=double(op==u'|'?l|r:op==u'^'?l^r:l&r);}
        else if(level==1 && (op==u'<' || op==u'>') && c.s[1]==op) {c.s+=2;const int64_t r=int64_t(CalcExpr(c,2));const int64_t l=int64_t(left);
            if(r<0 || r>62)c.error=true;else left=double(op==u'<'?l<<r:l>>r);}
        else if(level==2 && (op==u'+' || op==u'-')) {++c.s;const double r=CalcExpr(c,3);left=op==u'+'?left+r:left-r;}
        else if(level==3 && (op==u'*' || op==u'/' || op==u'%' || op==u'x' || op==u':')) {
            ++c.s;const double r=CalcExpr(c,4);
            if(op==u'*' || op==u'x')left*=r;
            else if(r==0)c.error=true;
            else if(op==u'%') {const int64_t l=int64_t(left),d=int64_t(r);if(!d)c.error=true;else left=double(l%d);}
            else left/=r;
        }
        else return left;
    }
}
inline bool Calculate(const char16_t* expression,char16_t* out,size_t cap) {
    Calc c{expression,false};const double value=CalcExpr(c,0);CalcSkip(c);
    out[0]=0;
    if(c.error || *c.s)return false;
    double magnitude=value<0?-value:value;
    if(magnitude>9.0e15) {Set(out,cap,T(u"za duża liczba",u"number too large"));return true;}
    if(value<0)Cat(out,cap,T(u"minus ",u"minus "));
    const uint64_t whole=uint64_t(magnitude);uint64_t fraction=uint64_t((magnitude-double(whole))*1000000.0+0.5);
    uint64_t shown=whole;if(fraction>=1000000) {++shown;fraction=0;}
    CatN(out,cap,shown);
    if(fraction) {
        Cat(out,cap,Pl()?u",":u".");char16_t digits[8];for(int i=5;i>=0;--i) {digits[i]=char16_t(u'0'+fraction%10);fraction/=10;}
        int n=6;while(n && digits[n-1]==u'0')--n;
        digits[n]=0;Cat(out,cap,digits);
    } else if(shown>9) {Cat(out,cap,T(u", szesnastkowo ",u", hex "));HexDigits(out,cap,shown,shown>0xffffffffull?16:shown>0xffff?8:4);}
    return true;
}
}  // namespace ws
}  // namespace hakefi
