#include "HiiModel.h"
namespace charonhii {
static uint16_t u16(const uint8_t* p){return uint16_t(p[0])|uint16_t(p[1])<<8;}
static uint32_t u24(const uint8_t* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16;}
static uint32_t u32(const uint8_t* p){return u24(p)|uint32_t(p[3])<<24;}
static uint64_t integer(const uint8_t* p,unsigned n){uint64_t v=0;
    for(unsigned i=0;i<n;i++)v|=uint64_t(p[i])<<(i*8);
    return v;}
void Model::Clear(){setCount=formCount=questionCount=optionCount=storeCount=stringCount=0;
    snapshot=nullptr;
    bytes=0;
    globalReasons=0;
    stringsAvailable=unsupportedStrings=false;}
const String* Model::FindString(uint16_t id)const{for(unsigned i=0;i<stringCount;i++)if(strings[i].id==id)return &strings[i];
    return nullptr;}
size_t Model::CopyString(uint16_t id,uint16_t* d,size_t cap)const{
    const auto* s=FindString(id);
    if(!s || !snapshot)return 0;
    size_t n=size_t(s->units)+1;
    if(!d || cap<n)return n;
    for(size_t i=0;i<s->units;i++)d[i]=u16(snapshot+s->offset+i*2);
    d[s->units]=0;
    return n;
}
struct Context {uint16_t set=None,form=None,question=None;
    uint32_t reasons=0;};
static void mark(Model& m,const Context& c,uint32_t reason){
    if(c.question!=None)m.questions[c.question].reasons|=reason;
    else if(c.form!=None)m.forms[c.form].reasons|=reason;
    else if(c.set!=None)m.sets[c.set].reasons|=reason;
    else m.globalReasons|=reason;
}
static bool isQuestion(unsigned op){return (op>=5&&op<=8)||op==0xc||op==0xf||op==0x1a||op==0x1b||op==0x1c||op==0x23;}
static Status forms(const uint8_t* b,size_t n,uint32_t base,Model& m){
    Context current,stack[64];
    unsigned depth=0;
    for(size_t at=0;at<n;){
        if(n-at<2)return Status::Invalid;
        const uint8_t* p=b+at;
        unsigned op=p[0],len=p[1]&127;
        bool scope=(p[1]&128)!=0;
        if(len<2||len>n-at)return Status::Invalid;
        if(op==0x29){if(len!=2||scope||!depth)return Status::Invalid;
            current=stack[--depth];
            at+=len;
            continue;}
        Context next=current;
        if(op==0xe){
            if(len<23||!scope||current.set!=None||len!=23u+16u*(p[22]&3))return Status::Invalid;
            if(m.setCount==Model::MaxSets)return Status::Limit;
            next.set=m.setCount++;
            next.form=next.question=None;
            auto& v=m.sets[next.set];
            v={};
            for(unsigned i=0;i<16;i++)v.guid[i]=p[2+i];
            v.title=u16(p+18);
            v.help=u16(p+20);
            v.reasons=current.reasons;
            if(p[22]&0xfc)v.reasons|=Unsupported;
        }else if(op==1){
            if(len!=6||!scope||current.set==None||current.form!=None)return Status::Invalid;
            uint16_t id=u16(p+2);
            if(!id)return Status::Invalid;
            for(unsigned i=0;i<m.formCount;i++)if(m.forms[i].set==current.set&&m.forms[i].id==id)return Status::Invalid;
            if(m.formCount==Model::MaxForms)return Status::Limit;
            next.form=m.formCount++;
            next.question=None;
            m.forms[next.form]={current.set,id,u16(p+4),current.reasons};
        }else if(isQuestion(op)){
            if(len<13||current.form==None||current.question!=None)return Status::Invalid;
            if(m.questionCount==Model::MaxQuestions)return Status::Limit;
            uint16_t id=u16(p+6);
            if(id)for(unsigned i=0;i<m.questionCount;i++)if(m.questions[i].set==current.set&&m.questions[i].id==id)return Status::Invalid;
            next.question=m.questionCount++;
            auto& q=m.questions[next.question];
            q={};
            q.opcode=uint8_t(op);
            q.set=current.set;
            q.form=current.form;
            q.prompt=u16(p+2);
            q.help=u16(p+4);
            q.id=id;
            q.store=u16(p+8);
            q.storeInfo=u16(p+10);
            q.flags=p[12];
            q.offset=base+uint32_t(at);
            q.reasons=current.reasons;
            if(q.flags&1)q.reasons|=ReadOnly;
            if(q.flags&4)q.reasons|=Callback;
            if(q.flags&0x50)q.reasons|=ResetRequired;
            if(q.flags&0x2a)q.reasons|=Unsupported;
            if(!q.store)q.reasons|=NoStorage;
            if(op==5||op==7){
                if(len<14)return Status::Invalid;
                q.detailFlags=p[13];
                q.width=uint8_t(1u<<(p[13]&3));
                if(len!=14u+3u*q.width)return Status::Invalid;
                q.kind=op==5?Kind::Enum:Kind::Numeric;
                q.minimum=integer(p+14,q.width);
                q.maximum=integer(p+14+q.width,q.width);
                q.step=integer(p+14+2*q.width,q.width);
                if((p[13]&0xcc)||(p[13]&0x30)==0x30)q.reasons|=Unsupported;
                // Signed raw ranges need explicit sign-aware validation by the editor.
                if((p[13]&0x30)==0)q.reasons|=BoundsUnresolved;
                else if(q.minimum>q.maximum)return Status::Invalid;
            }else if(op==6){if(len!=14)return Status::Invalid;
            q.kind=Kind::Checkbox;
            q.width=1;
            q.detailFlags=p[13];}
            else if(op==0x1c){if(len!=16)return Status::Invalid;
                q.kind=Kind::String;
                q.minLength=p[13];
                q.maxLength=p[14];
                q.detailFlags=p[15];
                if(q.minLength>q.maxLength)return Status::Invalid;}
            else if(op==8){if(len!=17)return Status::Invalid;
                q.kind=Kind::Password;
                q.minLength=u16(p+13);
                q.maxLength=u16(p+15);
                q.reasons|=Protected;
                if(q.minLength>q.maxLength)return Status::Invalid;}
            else if(op==0xc){if(len!=13&&len!=15)return Status::Invalid;
                q.kind=Kind::Action;
                q.configString=len==15?u16(p+13):0;
                q.reasons|=Callback;}
            else if(op==0x23){if(len!=15)return Status::Invalid;
                q.kind=Kind::OrderedList;
                q.maxLength=p[13];
                q.detailFlags=p[14];
                if(!q.maxLength)return Status::Invalid;}
            else if(op==0xf){if(len!=13&&len!=15&&len!=17&&len!=33&&len!=35)return Status::Invalid;
                q.kind=Kind::Reference;
                q.targetForm=len>=15?u16(p+13):0;
                q.reasons|=Unsupported;}
            else{if(len!=14)return Status::Invalid;
                q.kind=op==0x1a?Kind::Date:Kind::Time;
                q.reasons|=Unsupported;}
        }else if(op==9){
            if(len<7||current.question==None)return Status::Invalid;
            auto& q=m.questions[current.question];
            if(q.kind!=Kind::Enum&&q.kind!=Kind::OrderedList)return Status::Invalid;
            if(m.optionCount==Model::MaxOptions)return Status::Limit;
            unsigned type=p[5],width=type<=3?1u<<type:type==4?1:0;
            if(!width){q.reasons|=Unsupported;}else if(len!=6+width)return Status::Invalid;
            if(q.kind==Kind::Enum&&(type>3||width!=q.width))q.reasons|=Unsupported;
            auto& option=m.options[m.optionCount++];
            option={current.question,u16(p+2),uint8_t(type),p[4],width?integer(p+6,width):0,current.reasons};
            if(!width)option.reasons|=Unsupported;
        }else if(op==0x24||op==0x25||op==0x26){
            if(current.set==None||current.form!=None||scope)return Status::Invalid;
            if(m.storeCount==Model::MaxStores)return Status::Limit;
            Store v={};
            v.set=current.set;
            v.kind=uint8_t(op);
            unsigned guidAt=op==0x24?2:4,nameAt=op==0x24?22:28;
            if(len<unsigned(op==0x24?23:op==0x25?20:29))return Status::Invalid;
            v.id=u16(p+(op==0x24?18:2));
            if(!v.id)return Status::Invalid;
            for(unsigned i=0;i<m.storeCount;i++)if(m.stores[i].set==v.set&&m.stores[i].id==v.id)return Status::Invalid;
            for(unsigned i=0;i<16;i++)v.guid[i]=p[guidAt+i];
            if(op==0x25){if(len!=20)return Status::Invalid;}
            else{v.size=u16(p+(op==0x24?20:26));
                if(op==0x26)v.attributes=u32(p+20);
                if(p[len-1]!=0)return Status::Invalid;
                for(unsigned i=nameAt;i+1<len;i++)if(!p[i]||p[i]>127)return Status::Invalid;
                v.nameOffset=base+uint32_t(at)+nameAt;
                v.nameBytes=len-nameAt-1;}
            m.stores[m.storeCount++]=v;
        }else if(op==0x5c){if(len!=6||scope)return Status::Invalid;}
        else if(op==0x5b){
            if(current.question==None||len<5)return Status::Invalid;
            unsigned type=p[4],width=type<=3?1u<<type:type==4?1:0;
            if(scope||!width){mark(m,current,Unsupported);
                next.reasons|=Unsupported;}
            else if(len!=5+width)return Status::Invalid;
        }else if(op==0xa||op==0x19||op==0x1e||op==0x10||op==0x11||op==0x63){
            unsigned expected=(op==0x10||op==0x11)?4:op==0x63?5:2;
            if(!scope||len!=expected)return Status::Invalid;
            next.reasons|=Conditional;
            mark(m,current,Conditional);
        }else if(op==0xb){if(len!=2)return Status::Invalid;
        mark(m,current,ReadOnly);
        next.reasons|=ReadOnly;}
        else if(op==0x1d||op==0x62){mark(m,current,Dynamic);
            next.reasons|=Dynamic;}
        else if(op==0x2d||op==0x2e||op==0x18){mark(m,current,Unsupported);
            next.reasons|=Unsupported;}
        else if(op==2||op==3||op==4||op==0x28){
            unsigned expected=op==2?7:op==3?8:op==4?4:2;
            if(len!=expected)return Status::Invalid;
        }else{
            // Includes expression opcodes/vendor extensions; never infer a
            // condition's truth from unexecuted bytecode or ignore write rules.
            mark(m,current,Unsupported);
            next.reasons|=Unsupported;
        }
        if(scope){if(depth==64)return Status::Limit;
            stack[depth++]=current;
            current=next;}
        at+=len;
    }
    return depth?Status::Invalid:Status::Ok;
}
static Status strings(const uint8_t* p,size_t n,uint32_t base,const char* language,Model& m){
    if(n<47)return Status::Invalid;
    uint32_t header=u32(p+4),start=u32(p+8);
    if(header<47||header>n||start<header||start>=n)return Status::Invalid;
    size_t langEnd=46;
    while(langEnd<header&&p[langEnd]){if(p[langEnd]>127)return Status::Invalid;
        ++langEnd;}
    if(langEnd==header||langEnd==46)return Status::Invalid;
    size_t k=0;
    while(language[k]&&46+k<langEnd&&uint8_t(language[k])==p[46+k])++k;
    bool selected=language[k]==0&&46+k==langEnd;
    if(!selected)return Status::Ok;
    if(m.stringsAvailable)return Status::Invalid;
    uint32_t id=1;
    size_t at=start;
    while(at<n){
        unsigned op=p[at++],count=1;
        if(op==0){if(at!=n)return Status::Invalid;
            m.stringsAvailable=true;
            return Status::Ok;}
        if(op==0x21||op==0x22){unsigned width=op==0x21?2:1;
            if(n-at<width)return Status::Invalid;
            id+=unsigned(integer(p+at,width));
            at+=width;
            if(id>65536)return Status::Invalid;
            continue;}
        if(op==0x20){if(n-at<2||id>65535)return Status::Invalid;
            const auto* old=m.FindString(u16(p+at));
            at+=2;
            if(!old)return Status::Invalid;
            if(m.stringCount==Model::MaxStrings)return Status::Limit;
            String value=*old;
            value.id=uint16_t(id++);
            m.strings[m.stringCount++]=value;
            continue;}
        if(op==0x30||op==0x31||op==0x32||op==0x40){
            unsigned width=op==0x30?1:op==0x32?4:2;
            if(n-at<1+width)return Status::Invalid;
            uint32_t length=uint32_t(integer(p+at+1,width));
            if(length<2+width||length>n-(at-1))return Status::Invalid;
            // FONT is the only extension known not to affect StringId numbering.
            if(op!=0x40){m.stringCount=0;
                m.unsupportedStrings=true;
                return Status::UnsupportedStrings;}
            if(length<13||(length-11)%2||u16(p+(at-1)+length-2)!=0)return Status::Invalid;
            at=at-1+length;
            continue;
        }
        if(op!=0x14&&op!=0x15&&op!=0x16&&op!=0x17){m.stringCount=0;
            m.unsupportedStrings=true;
            return Status::UnsupportedStrings;}
        if(op==0x15||op==0x17){if(at==n)return Status::Invalid;
            ++at;}
        if(op==0x16||op==0x17){if(n-at<2)return Status::Invalid;
            count=u16(p+at);
            at+=2;
            if(!count)return Status::Invalid;}
        if(count>65536-id)return Status::Invalid;
        while(count--){
            size_t text=at;
            bool ended=false;
            while(n-at>=2){uint16_t ch=u16(p+at);
                at+=2;
                if(!ch){ended=true;
                    break;}
                if(ch>=0xd800&&ch<=0xdbff){if(n-at<2||u16(p+at)<0xdc00||u16(p+at)>0xdfff)return Status::Invalid;
                    at+=2;}
                else if(ch>=0xdc00&&ch<=0xdfff)return Status::Invalid;
            }
            if(!ended||id>65535)return Status::Invalid;
            if(m.stringCount==Model::MaxStrings)return Status::Limit;
            m.strings[m.stringCount++]={uint16_t(id++),base+uint32_t(text),uint32_t((at-text)/2-1)};
        }
    }
    return Status::Invalid;
}
Status Parse(const uint8_t* b,size_t n,const char* language,Model& out){
    out.Clear();
    if(!b||!language||n<24||n>4*1024*1024)return Status::Invalid;
    unsigned tag=0;
    while(tag<64&&language[tag]){if(uint8_t(language[tag])>127)return Status::Invalid;
        ++tag;}if(!tag||tag==64)return Status::Invalid;
    if(u32(b+16)!=n)return Status::Invalid;
    bool ended=false;
    Status status=Status::Ok;
    for(size_t at=20;at<n;){
        if(n-at<4){status=Status::Invalid;
            break;}size_t length=u24(b+at);
        unsigned type=b[at+3];
        if(length<4||length>n-at){status=Status::Invalid;
            break;}
        if(type==0xdf){if(length!=4||at+4!=n)status=Status::Invalid;
            else ended=true;
            break;}
        if(type==2)status=forms(b+at+4,length-4,uint32_t(at+4),out);
        else if(type==4)status=strings(b+at,length,uint32_t(at),language,out);
        if(status==Status::UnsupportedStrings)status=Status::Ok;
        if(status!=Status::Ok)break;
        at+=length;
    }
    if(status!=Status::Ok||!ended){out.Clear();
        return status==Status::Ok?Status::Invalid:status;}
    for(unsigned i=0;i<out.questionCount;i++){
        auto& q=out.questions[i];
        q.reasons|=out.forms[q.form].reasons|out.sets[q.set].reasons|out.globalReasons;
        if(q.store){const Store* found=nullptr;
            for(unsigned j=0;j<out.storeCount;j++)if(out.stores[j].set==q.set&&out.stores[j].id==q.store)found=&out.stores[j];
            if(!found)q.reasons|=NoStorage;
            else if(found->kind!=0x25){size_t width=q.width;
                if(q.kind==Kind::String||q.kind==Kind::Password)width=(size_t(q.maxLength)+1)*2;
                if(!width)q.reasons|=BoundsUnresolved;
                else if(q.storeInfo>found->size||width>size_t(found->size-q.storeInfo))q.reasons|=BoundsUnresolved;}
        }
    }
    for(unsigned i=0;i<out.optionCount;i++)out.options[i].reasons|=out.questions[out.options[i].question].reasons;
    if(out.unsupportedStrings){out.stringCount=0;
        out.stringsAvailable=false;}
    out.snapshot=b;
    out.bytes=n;
    return out.unsupportedStrings?Status::UnsupportedStrings:Status::Ok;
}
}

