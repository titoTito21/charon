#include "HiiConfig.h"
namespace charonhii {
static efi_guid accessGuid={0x330d4706,0xf2a0,0x4e4f,{0xa3,0x69,0xb6,0x6f,0xa8,0xd5,0x43,0x85}};
static efi_guid routingGuid={0x587e72d7,0xcc50,0x4f79,{0x82,0x09,0xca,0x29,0x1f,0xc1,0xa1,0x0f}};
static efi_guid databaseGuid={0xef9fc172,0xa1b2,0x4693,{0xb3,0x27,0x6d,0x32,0xfc,0x41,0x60,0x42}};
static efi_guid pathGuid={0x09576e91,0x6d3f,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
static bool equal(const uint8_t* a,const uint8_t* b,size_t n){for(size_t i=0;i<n;++i)if(a[i]!=b[i])return false;
    return true;}
static void copy(uint8_t* d,const uint8_t* s,size_t n){for(size_t i=0;i<n;++i)d[i]=s[i];}
static size_t pathSize(const uint8_t* p){
    if(!p)return 0;
    for(size_t n=0;n+4<=1024;){size_t length=size_t(p[n+2])|(size_t(p[n+3])<<8);
        if(length<4||length>1024-n)return 0;
        if(p[n]==0x7f){if(p[n+1]!=0xff||length!=4)return 0;
            return n+4;}n+=length;
    }return 0;
}
static uint32_t reasons(const Model& m,const Question& q){return m.globalReasons|m.sets[q.set].reasons|m.forms[q.form].reasons|q.reasons;}
static int hex(char16_t c){if(c>=u'0'&&c<=u'9')return c-u'0';
    if(c>=u'a'&&c<=u'f')return c-u'a'+10;
    if(c>=u'A'&&c<=u'F')return c-u'A'+10;
    return -1;}
static char16_t lower(char16_t c){return c>=u'A'&&c<=u'Z'?char16_t(c+32):c;}
struct Writer {
    char16_t* p;
    size_t n=0;
    bool good=true;
    void ch(char16_t c){if(n+1>=4096){good=false;
            return;}p[n++]=c;
        p[n]=0;}
    void text(const char16_t* s){while(*s)ch(*s++);}
    void number(uint64_t v,unsigned digits){for(unsigned i=digits;i;--i)ch(u"0123456789abcdef"[(v>>((i-1)*4))&15]);}
};
void Config::Reset(){boot_=nullptr;
    database_=nullptr;
    access_=nullptr;
    routing_=nullptr;
    question_=None;
    width_=pathBytes_=0;
    if(binding_!=UINT64_MAX)++binding_;}
efi_status Config::Fresh(const Model*& m,const Question*& q,const Store*& store){
    if(!boot_||!database_||binding_==UINT64_MAX)return EFI_NOT_READY;
    m=database_->View(identity_);
    if(!m||question_>=m->questionCount)return EFI_MEDIA_CHANGED;
    q=&m->questions[question_];
    if(q->set>=m->setCount||q->form>=m->formCount)return EFI_COMPROMISED_DATA;
    const uint32_t denied=reasons(*m,*q)&~uint32_t(ReadOnly|ResetRequired);
    if(denied||q->kind==Kind::Password)return EFI_UNSUPPORTED;
    store=nullptr;
    for(unsigned i=0;i<m->storeCount;++i)if(m->stores[i].set==q->set&&m->stores[i].id==q->store)store=&m->stores[i];
    if(!store||store->kind!=0x24)return EFI_UNSUPPORTED;
    // Aliased IFR fields must not expose password storage through an otherwise
    // ordinary control. Unresolved protected ranges refuse the whole store.
    for(unsigned i=0;i<m->questionCount;++i){const auto& other=m->questions[i];
        if(other.set!=q->set||other.store!=q->store||other.kind!=Kind::Password)continue;
        const size_t protectedWidth=(size_t(other.maxLength)+1)*2;
        if(other.storeInfo>store->size||protectedWidth>size_t(store->size-other.storeInfo))return EFI_UNSUPPORTED;
        if(size_t(q->storeInfo)<size_t(other.storeInfo)+protectedWidth&&size_t(other.storeInfo)<size_t(q->storeInfo)+width_)return EFI_ACCESS_DENIED;
    }
    DatabaseProtocol* databaseApi=nullptr;
    auto status=boot_->LocateProtocol(&databaseGuid,nullptr,(void**)&databaseApi);
    if(status!=EFI_SUCCESS)return status;
    if(!databaseApi||!databaseApi->driver)return EFI_UNSUPPORTED;
    efi_handle owner=nullptr;
    status=databaseApi->driver(databaseApi,identity_.hii,&owner);
    if(status!=EFI_SUCCESS)return status;
    if(owner!=identity_.driver)return EFI_MEDIA_CHANGED;
    ConfigAccessProtocol* access=nullptr;
    ConfigRoutingProtocol* routing=nullptr;
    uint8_t* path=nullptr;
    status=boot_->HandleProtocol(owner,&accessGuid,(void**)&access);
    if(status!=EFI_SUCCESS)return status;
    status=boot_->LocateProtocol(&routingGuid,nullptr,(void**)&routing);
    if(status!=EFI_SUCCESS)return status;
    status=boot_->HandleProtocol(owner,&pathGuid,(void**)&path);
    if(status!=EFI_SUCCESS)return status;
    if(access!=access_||routing!=routing_||!access||!access->extract||!routing||!routing->route)return EFI_MEDIA_CHANGED;
    if(pathSize(path)!=pathBytes_||!equal(path,path_,pathBytes_))return EFI_MEDIA_CHANGED;
    return database_->View(identity_)?EFI_SUCCESS:EFI_MEDIA_CHANGED;
}
efi_status Config::Bind(efi_boot_services* boot,Database& database,const Database::Identity& identity,uint16_t question){
    Reset();
    if(!boot||!boot->LocateProtocol||!boot->HandleProtocol||!boot->FreePool)return EFI_INVALID_PARAMETER;
    if(binding_==UINT64_MAX)return EFI_OUT_OF_RESOURCES;
    const auto* m=database.View(identity);
    if(!m||question>=m->questionCount)return EFI_MEDIA_CHANGED;
    const auto& q=m->questions[question];
    size_t width=q.width;
    if(q.kind==Kind::String){if(q.maxLength>255)return EFI_UNSUPPORTED;
        width=(size_t(q.maxLength)+1)*2;}
    else if(q.kind!=Kind::Checkbox&&q.kind!=Kind::Enum&&q.kind!=Kind::Numeric)return EFI_UNSUPPORTED;
    if(!width||width>512)return EFI_UNSUPPORTED;
    uint8_t* path=nullptr;
    auto status=boot->HandleProtocol(identity.driver,&pathGuid,(void**)&path);
    if(status!=EFI_SUCCESS)return status;
    size_t pathBytes=pathSize(path);
    if(!pathBytes)return EFI_COMPROMISED_DATA;
    status=boot->HandleProtocol(identity.driver,&accessGuid,(void**)&access_);
    if(status!=EFI_SUCCESS)return status;
    status=boot->LocateProtocol(&routingGuid,nullptr,(void**)&routing_);
    if(status!=EFI_SUCCESS)return status;
    boot_=boot;
    database_=&database;
    identity_=identity;
    question_=question;
    width_=width;
    pathBytes_=pathBytes;
    copy(path_,path,pathBytes);
    const Question* actual;
    const Store* store;
    status=Fresh(m,actual,store);
    if(status==EFI_SUCCESS&&(actual->storeInfo>store->size||width_>size_t(store->size-actual->storeInfo)))status=EFI_COMPROMISED_DATA;
    if(status!=EFI_SUCCESS)Reset();
    return status;
}
efi_status Config::Header(const Model& m,const Question& q,const Store& store,char16_t* text,size_t& length){
    if(store.nameOffset>m.bytes||store.nameBytes>m.bytes-store.nameOffset||store.nameBytes>127)return EFI_COMPROMISED_DATA;
    Writer w{text};
    text[0]=0;
    w.text(u"GUID=");
    for(auto b:store.guid)w.number(b,2);
    w.text(u"&NAME=");
    for(size_t i=0;i<store.nameBytes;++i){auto c=m.snapshot[store.nameOffset+i];
        if(!c||c>127)return EFI_COMPROMISED_DATA;
        w.number(c,4);}
    w.text(u"&PATH=");
    for(size_t i=0;i<pathBytes_;++i)w.number(path_[i],2);
    w.text(u"&OFFSET=");
    w.number(q.storeInfo,4);
    w.text(u"&WIDTH=");
    w.number(width_,4);
    length=w.n;
    return w.good?EFI_SUCCESS:EFI_BAD_BUFFER_SIZE;
}
efi_status Config::Extract(uint8_t* output){
    const Model* m;
    const Question* q;
    const Store* s;
    auto status=Fresh(m,q,s);
    if(status!=EFI_SUCCESS)return status;
    char16_t request[4096];
    size_t length=0;
    status=Header(*m,*q,*s,request,length);
    if(status!=EFI_SUCCESS)return status;
    char16_t* progress=nullptr;
    char16_t* response=nullptr;
    status=access_->extract(access_,request,&progress,&response);
    uint8_t decoded[512]={};
    if(status==EFI_SUCCESS){
        // Only the requested field is accepted; no extra range/ALTCFG/second
        // header can smuggle unrelated or protected bytes into this view.
        if(progress!=request+length||!response)status=EFI_COMPROMISED_DATA;
        else {
            size_t n=0;
            while(n<4096&&response[n])++n;
            if(n!=length+7+width_*2)status=EFI_COMPROMISED_DATA;
            for(size_t i=0;status==EFI_SUCCESS&&i<length;++i)if(lower(response[i])!=lower(request[i]))status=EFI_COMPROMISED_DATA;
            const char16_t value[]=u"&VALUE=";
            for(size_t i=0;status==EFI_SUCCESS&&i<7;++i)if(response[length+i]!=value[i])status=EFI_COMPROMISED_DATA;
            for(size_t i=0;status==EFI_SUCCESS&&i<width_;++i){int high=hex(response[length+7+i*2]),low=hex(response[length+8+i*2]);
                if(high<0||low<0)status=EFI_COMPROMISED_DATA;
                else decoded[width_-1-i]=uint8_t(high*16+low);}
        }
    }
    if(response)boot_->FreePool(response);
    const auto fresh=Fresh(m,q,s);
    if(fresh!=EFI_SUCCESS)return fresh;
    if(status==EFI_SUCCESS)copy(output,decoded,width_);
    return status;
}
efi_status Config::Encode(const ConfigValue& value,uint8_t* bytes){
    const Model* m;
    const Question* q;
    const Store* s;
    auto status=Fresh(m,q,s);
    if(status!=EFI_SUCCESS)return status;
    if(value.kind!=q->kind)return EFI_INVALID_PARAMETER;
    for(size_t i=0;i<width_;++i)bytes[i]=0;
    if(q->kind==Kind::String){
        size_t n=0;
        while(n<256&&value.text[n])++n;
        if(n>q->maxLength||n<q->minLength)return EFI_INVALID_PARAMETER;
        for(size_t i=0;i<n;++i){uint16_t c=value.text[i];
            if(c>=0xd800&&c<=0xdbff){if(i+1==n||value.text[i+1]<0xdc00||value.text[i+1]>0xdfff)return EFI_INVALID_PARAMETER;}
            else if(c>=0xdc00&&c<=0xdfff&&(i==0||value.text[i-1]<0xd800||value.text[i-1]>0xdbff))return EFI_INVALID_PARAMETER;
            bytes[i*2]=uint8_t(c);
            bytes[i*2+1]=uint8_t(c>>8);}
    }else {
        if(width_<8&&value.number>=(uint64_t(1)<<(width_*8)))return EFI_INVALID_PARAMETER;
        if(q->kind==Kind::Checkbox&&value.number>1)return EFI_INVALID_PARAMETER;
        if(q->kind==Kind::Numeric&&(value.number<q->minimum||value.number>q->maximum||(q->step&&(value.number-q->minimum)%q->step)))return EFI_INVALID_PARAMETER;
        if(q->kind==Kind::Enum){bool found=false;
            for(unsigned i=0;i<m->optionCount;++i){const auto& o=m->options[i];
                if(o.question==question_&&!o.reasons&&o.value==value.number)found=true;}if(!found)return EFI_INVALID_PARAMETER;}
        for(size_t i=0;i<width_;++i)bytes[i]=uint8_t(value.number>>(i*8));
    }return EFI_SUCCESS;
}
efi_status Config::Decode(const uint8_t* bytes,ConfigValue& output){
    const Model* m;
    const Question* q;
    const Store* s;
    auto status=Fresh(m,q,s);
    if(status!=EFI_SUCCESS)return status;
    ConfigValue value;
    value.kind=q->kind;
    if(q->kind==Kind::String){bool end=false;
        for(size_t i=0;i<width_/2;++i){value.text[i]=char16_t(bytes[i*2]|uint16_t(bytes[i*2+1])<<8);
            if(!value.text[i]){end=true;
                break;}}if(!end)return EFI_COMPROMISED_DATA;}
    else for(size_t i=0;i<width_;++i)value.number|=uint64_t(bytes[i])<<(i*8);
    uint8_t validated[512];
    status=Encode(value,validated);
    if(status!=EFI_SUCCESS)return status==EFI_INVALID_PARAMETER?EFI_COMPROMISED_DATA:status;
    output=value;
    return EFI_SUCCESS;
}
efi_status Config::Read(ConfigValue& output){uint8_t bytes[512];
    auto status=Extract(bytes);
    return status==EFI_SUCCESS?Decode(bytes,output):status;}
efi_status Config::Propose(const ConfigValue& value,ConfigProposal& output,const ConfigValue* expectedCurrent){
    const Model* m;
    const Question* q;
    const Store* s;
    auto status=Fresh(m,q,s);
    if(status!=EFI_SUCCESS)return status;
    if(reasons(*m,*q)&ReadOnly)return EFI_WRITE_PROTECTED;
    uint8_t encoded[512];
    status=Encode(value,encoded);
    if(status!=EFI_SUCCESS)return status;
    ConfigProposal proposal;
    status=Extract(proposal.baseline);
    if(status!=EFI_SUCCESS)return status;
    ConfigValue current;
    status=Decode(proposal.baseline,current);
    if(status!=EFI_SUCCESS)return status;
    if(expectedCurrent){uint8_t expected[512],actual[512];
        status=Encode(*expectedCurrent,expected);
        if(status!=EFI_SUCCESS)return status;
        status=Encode(current,actual);
        if(status!=EFI_SUCCESS)return status;
        if(!equal(expected,actual,width_))return EFI_MEDIA_CHANGED;
    }
    proposal.owner=this;
    proposal.binding=binding_;
    proposal.value=value;
    proposal.bytes=width_;
    output=proposal;
    return EFI_SUCCESS;
}
efi_status Config::Commit(const ConfigProposal& proposal,bool confirmed,ConfigCommitResult& result){
    result={};
    if(!confirmed)return EFI_ACCESS_DENIED;
    if(proposal.owner!=this||proposal.binding!=binding_||proposal.bytes!=width_)return EFI_MEDIA_CHANGED;
    const Model* m;
    const Question* q;
    const Store* s;
    auto status=Fresh(m,q,s);
    if(status!=EFI_SUCCESS)return status;
    if(reasons(*m,*q)&ReadOnly)return EFI_WRITE_PROTECTED;
    uint8_t desired[512],before[512],after[512];
    status=Encode(proposal.value,desired);
    if(status!=EFI_SUCCESS)return status;
    status=Extract(before);
    if(status!=EFI_SUCCESS)return status;
    if(!equal(before,proposal.baseline,width_))return EFI_MEDIA_CHANGED;
    status=Fresh(m,q,s);
    if(status!=EFI_SUCCESS)return status;
    char16_t request[4096];
    size_t length=0;
    status=Header(*m,*q,*s,request,length);
    if(status!=EFI_SUCCESS)return status;
    Writer w{request,length};
    w.text(u"&VALUE=");
    for(size_t i=width_;i;--i)w.number(desired[i-1],2);
    if(!w.good)return EFI_BAD_BUFFER_SIZE;
    result.resetRequired=(reasons(*m,*q)&ResetRequired)!=0;
    result.attempted=true;
    char16_t* progress=nullptr;
    result.routeStatus=routing_->route(routing_,request,&progress);
    if(result.routeStatus==EFI_SUCCESS&&progress!=request+w.n)result.routeStatus=EFI_COMPROMISED_DATA;
    // RouteConfig can partially change firmware state even on failure. Never
    // roll back blindly; independently re-read and report what was observed.
    result.readbackStatus=Extract(after);
    if(result.readbackStatus==EFI_SUCCESS){result.applied=equal(after,desired,width_);
        result.changed=!equal(after,before,width_);}
    if(result.routeStatus!=EFI_SUCCESS)return result.routeStatus;
    if(result.readbackStatus!=EFI_SUCCESS)return result.readbackStatus;
    return result.applied?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
}
