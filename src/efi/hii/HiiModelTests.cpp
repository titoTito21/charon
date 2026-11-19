#include "HiiModel.h"
#include <vector>
#include <stdio.h>
#include <string.h>
#include <fstream>
#include <iterator>
using namespace charonhii;
using Bytes=std::vector<uint8_t>;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);return 1;}}while(0)
static void le(Bytes& b,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)b.push_back(uint8_t(v>>(8*i)));}
static void set(Bytes& b,size_t p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)b[p+i]=uint8_t(v>>(8*i));}
static void op(Bytes& b,uint8_t code,const Bytes& data={},bool scope=false){b.push_back(code);b.push_back(uint8_t(data.size()+2)|(scope?128:0));b.insert(b.end(),data.begin(),data.end());}
static Bytes question(unsigned id,unsigned offset=0){Bytes q;le(q,2,2);le(q,0,2);le(q,id,2);le(q,1,2);le(q,offset,2);q.push_back(0);return q;}
static void package(Bytes& list,unsigned type,const Bytes& payload){le(list,payload.size()+4,3);list.push_back(uint8_t(type));list.insert(list.end(),payload.begin(),payload.end());}
static void text(Bytes& b,const char* t){b.push_back(0x14);while(*t)le(b,uint8_t(*t++),2);le(b,0,2);}
static Bytes fixture(bool conditional=false){
    Bytes list(20),strings(42);const char* lang="en-US";strings.insert(strings.end(),lang,lang+6);
    set(strings,0,strings.size()+4,4);set(strings,4,strings.size()+4,4);
    text(strings,"Settings");text(strings,"Toggle");text(strings,"Choice");
    strings.push_back(0x20);le(strings,3,2);strings.push_back(0x22);strings.push_back(2);text(strings,"Seven");strings.push_back(0);
    package(list,4,strings);
    Bytes f,fs(16);le(fs,1,2);le(fs,0,2);fs.push_back(0);op(f,0xe,fs,true);
    Bytes store(16);le(store,1,2);le(store,64,2);store.insert(store.end(),{'C','f','g',0});op(f,0x24,store);
    Bytes form;le(form,1,2);le(form,1,2);op(f,1,form,true);
    auto q=question(10);q.push_back(0);op(f,6,q);
    q=question(11,1);q.insert(q.end(),{0x10,0,2,1});op(f,5,q,true);
    Bytes option;le(option,3,2);option.insert(option.end(),{0,0,1});op(f,9,option);
    option.back()=2;op(f,9,option);op(f,0x29);
    q=question(12,4);q.push_back(0x12);le(q,10,4);le(q,100,4);le(q,5,4);op(f,7,q);
    q=question(13,8);q.insert(q.end(),{1,12,0});op(f,0x1c,q);
    q=question(14,34);le(q,0,2);le(q,8,2);op(f,8,q);
    q=question(15);set(q,6,0,2);q[10]=4;op(f,0xc,q);
    q=question(16);q.insert(q.end(),{3,1});op(f,0x23,q,true);op(f,9,option);op(f,0x29);
    if(conditional){op(f,0xa,{},true);op(f,0x46);q=question(17);q.push_back(0);op(f,6,q);op(f,0x29);}
    op(f,0x29);op(f,0x29);package(list,2,f);package(list,0xdf,{});set(list,16,list.size(),4);return list;
}
static Model model;
int main(int argc,char** argv){
    auto data=fixture();CHECK(Parse(data.data(),data.size(),"en-US",model)==Status::Ok);
    CHECK(model.formCount==1&&model.setCount==1&&model.questionCount==7&&model.optionCount==3&&model.storeCount==1);
    CHECK(model.questions[0].kind==Kind::Checkbox&&model.questions[0].reasons==0);
    CHECK(model.questions[1].kind==Kind::Enum&&model.options[0].value==1&&model.options[1].value==2);
    CHECK(model.questions[2].kind==Kind::Numeric&&model.questions[2].width==4&&model.questions[2].minimum==10&&model.questions[2].maximum==100&&model.questions[2].step==5);
    CHECK(model.questions[3].kind==Kind::String&&model.questions[3].maxLength==12);
    CHECK(model.questions[4].kind==Kind::Password&&(model.questions[4].reasons&Protected));
    CHECK(model.questions[5].kind==Kind::Action&&(model.questions[5].reasons&Callback));
    CHECK(model.questions[6].kind==Kind::OrderedList&&model.questions[6].maxLength==3);
    CHECK(model.FindString(4)&&!model.FindString(5)&&!model.FindString(6)&&model.FindString(7));
    uint16_t output[32];for(auto& x:output)x=0x7777;
    CHECK(model.CopyString(4,nullptr,0)==7);CHECK(model.CopyString(4,output,6)==7&&output[0]==0x7777);
    CHECK(model.CopyString(4,output,32)==7&&output[0]=='C'&&output[6]==0);
    CHECK(Parse(data.data(),data.size(),"pl-PL",model)==Status::Ok&&!model.stringsAvailable&&model.stringCount==0);
    auto condition=fixture(true);CHECK(Parse(condition.data(),condition.size(),"en-US",model)==Status::Ok);
    CHECK(model.questions[7].reasons&Conditional);CHECK(model.questions[7].reasons&Unsupported);
    // Every byte truncation must fail closed, with or without a forged new list length.
    for(size_t n=0;n<data.size();n++){
        CHECK(Parse(data.data(),n,"en-US",model)!=Status::Ok);CHECK(!model.snapshot&&model.questionCount==0);
        auto cut=Bytes(data.begin(),data.begin()+n);if(n>=20)set(cut,16,n,4);
        CHECK(Parse(cut.data(),cut.size(),"en-US",model)!=Status::Ok);CHECK(!model.snapshot&&model.questionCount==0);
    }
    // Corrupt package/opcode lengths, unmatched END, unterminated strings.
    auto bad=data;set(bad,20,0,3);CHECK(Parse(bad.data(),bad.size(),"en-US",model)==Status::Invalid);
    bad=data;set(bad,20,0xffffff,3);CHECK(Parse(bad.data(),bad.size(),"en-US",model)==Status::Invalid);
    bad=data;bad[73]=0;bad[74]=0xd8; // Unpaired high surrogate at first code unit.
    CHECK(Parse(bad.data(),bad.size(),"en-US",model)==Status::Invalid);
    bad=data;bad[72]=0x10; // SCSU is deliberately unsupported, not decoded as UCS2.
    CHECK(Parse(bad.data(),bad.size(),"en-US",model)==Status::UnsupportedStrings);
    CHECK(model.questionCount==7&&model.stringCount==0&&!model.stringsAvailable);
    // Valid package container, unmatched END in IFR.
    Bytes unmatched(20),ifr;op(ifr,0x29);package(unmatched,2,ifr);package(unmatched,0xdf,{});set(unmatched,16,unmatched.size(),4);
    CHECK(Parse(unmatched.data(),unmatched.size(),"en-US",model)==Status::Invalid);
    // Depth exhaustion is deterministic and cannot overrun the context stack.
    Bytes deep(20);ifr.clear();for(unsigned i=0;i<65;i++)op(ifr,0x5f,{},true);
    for(unsigned i=0;i<65;i++)op(ifr,0x29);package(deep,2,ifr);package(deep,0xdf,{});set(deep,16,deep.size(),4);
    CHECK(Parse(deep.data(),deep.size(),"en-US",model)==Status::Limit);
    Bytes crowded(20);ifr.clear();Bytes fs(16);le(fs,1,2);le(fs,0,2);fs.push_back(0);
    op(ifr,0xe,fs,true);Bytes form;le(form,1,2);le(form,1,2);op(ifr,1,form,true);
    for(unsigned i=1;i<=Model::MaxQuestions+1;i++){auto q=question(i);q.push_back(0);op(ifr,6,q);}
    op(ifr,0x29);op(ifr,0x29);package(crowded,2,ifr);package(crowded,0xdf,{});set(crowded,16,crowded.size(),4);
    CHECK(Parse(crowded.data(),crowded.size(),"en-US",model)==Status::Limit);
    CHECK(!model.snapshot&&model.questionCount==0);
    // Length-preserving mutations exercise the parser on hostile structures.
    uint32_t seed=346;for(unsigned i=0;i<12000;i++){
        bad=data;seed=seed*1664525+1013904223;size_t at=20+seed%(bad.size()-20);seed=seed*1664525+1013904223;bad[at]=uint8_t(seed>>24);
        Status status=Parse(bad.data(),bad.size(),"en-US",model);
        if(status==Status::Invalid||status==Status::Limit)CHECK(!model.snapshot&&!model.questionCount&&!model.stringCount);
        else{CHECK(model.questionCount<=Model::MaxQuestions&&model.optionCount<=Model::MaxOptions);
            for(unsigned j=0;j<model.questionCount;j++)CHECK(model.questions[j].form<model.formCount&&model.questions[j].set<model.setCount);
            for(unsigned j=0;j<model.stringCount;j++)CHECK(model.strings[j].offset+(size_t(model.strings[j].units)+1)*2<=model.bytes);}
    }
    CHECK(Parse(nullptr,0,"en-US",model)==Status::Invalid);
    if(argc==2){
        std::ifstream file(argv[1],std::ios::binary);CHECK(bool(file));
        Bytes compiled((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
        CHECK(compiled.size()>4&&compiled[3]==2);
        Bytes exported(20);exported.insert(exported.end(),compiled.begin(),compiled.end());
        package(exported,0xdf,{});set(exported,16,exported.size(),4);
        CHECK(Parse(exported.data(),exported.size(),"en-US",model)==Status::Ok);
        CHECK(model.setCount==1&&model.formCount==1&&model.questionCount==5&&model.optionCount==2);
        CHECK(model.stores[0].size==52);
        CHECK(model.questions[0].id==1&&model.questions[0].kind==Kind::Checkbox&&model.questions[0].reasons==0);
        CHECK(model.questions[2].id==3&&model.questions[2].kind==Kind::Numeric&&model.questions[2].width==4&&model.questions[2].maximum==100);
        CHECK(model.questions[4].kind==Kind::Password&&(model.questions[4].reasons&Protected));
        puts("PASS actual pinned AUDK VfrCompile package decoded");
    }
    printf("PASS %u HII package/IFR/string/model bounds and malformed-input checks\n",checks);return 0;
}
