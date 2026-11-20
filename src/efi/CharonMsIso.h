#pragma once
// r401: the official Windows 11 (x64, multi-edition) ISO link, straight from
// Microsoft, on Charon's own HTTPS client (CharonHttps.h).
//
// This is the request sequence of Microsoft's own download page
// (https://www.microsoft.com/software-download/windows11), as documented by the
// open-source Fido script (pbatard/Fido, GPL-3.0; only the sequence of public
// URLs and parameters is reused here, no code):
//   1. the download page itself                       -> product edition id of the
//      x64 ISO and the page's table of SHA-256 sums (optional; a constant
//      edition id is the fallback)
//   2. vlscppe.microsoft.com/tags?org_id&session_id   -> whitelists a fresh session id
//   3. ov-df.microsoft.com/mdt.js, then ov-df.microsoft.com/?...w&mdt&rticks
//                                                     -> second half of the whitelisting
//   4. .../software-download-connector/api/getskuinformationbyproductedition
//                                                     -> languages and their SKU ids
//   5. .../software-download-connector/api/GetProductDownloadLinksBySku (with Referer)
//                                                     -> time-limited direct ISO address (24 h)
//   6. HEAD on the ISO address                        -> size and resume validator
// Microsoft refuses step 5 for addresses it considers automated or sanctioned
// ("Sentinel", message code 715-123130): that comes back as Result::refused
// with Microsoft's own text in Link::error and the session id to quote.
// The API returns no checksum; the SHA-256 comes from the page's table in step 1
// when the page could be read, and is Microsoft's published value for that
// language - compare it with the hash of the finished download.
// No cookies are needed (verified 2026-11-19). GPL-2.0-or-later.
#include "CharonHttps.h"
namespace hakefi {
namespace msiso {
enum class Result : uint8_t {
    ok,
    network,          // an HTTPS request failed: Link::transport, Link::stage, Link::httpStatus
    noEntropy,        // no random source for the session id
    noClock,
    badAnswer,        // Microsoft answered something this code does not understand (the flow changed)
    noSuchLanguage,   // the language is not offered; Link::error lists what is
    refused,          // Microsoft's anti-bot / sanctions refusal (Sentinel, 715-123130)
    serverError,      // another error object from the API: Link::error
    noLink,           // no x64 download option in the answer
};
struct Link {
    char url[https::urlUnits];   // direct, time-limited ISO address
    char fileName[128];
    uint64_t size;bool hasSize;
    char sha256[65];bool hasSha256;   // upper-case hex from Microsoft's page table
    char validator[160];              // ETag / Last-Modified for resuming (https::Download)
    char language[64];char product[160];char skuId[16];char editionId[16];
    char sessionId[37];char expires[48];
    // diagnostics
    const char* stage;https::Result transport;int httpStatus;
    char error[600];char errorKey[96];int errorType;
};
constexpr const char* orgId="y6jn8c31";
constexpr const char* profileId="606624d44113";
constexpr const char* instanceId="560dc9f3-1aa5-4a2f-b63c-9e18f8d0e175";
// (3816 is the ARM64 id of the same release in Rufus's table; Charon is x64 only.)
constexpr const char* windows10EditionId="2618"; // Windows 10 22H2 Home/Pro/Edu x64, as in Rufus (Fido 1.72)
constexpr const char* referer10="Referer: https://www.microsoft.com/software-download/windows10ISO\r\n";
constexpr size_t maxLanguages=64;
constexpr const char* fallbackEditionId="3813";   // Windows 11 26H2 x64 (2026-09); the page is asked first
constexpr const char* pageUrl="https://www.microsoft.com/en-us/software-download/windows11";
constexpr const char* referer="Referer: https://www.microsoft.com/software-download/windows11\r\n";
using https::Appender;using https::Length;using https::Lower;
inline bool SameNoCase(const char* a,const char* b) {for(size_t i=0;;++i) {if(Lower(a[i])!=Lower(b[i]))return false;if(!a[i])return true;}}
inline const char* Find(const char* from,const char* end,const char* needle) {
    const size_t n=Length(needle);
    for(;from+n<=end;++from) {size_t i=0;while(i<n && from[i]==needle[i])++i;if(i==n)return from;}
    return nullptr;
}
// ---- minimal JSON scanning ----
// End of the object or array that starts at `at` ('{' or '['), strings respected; null when unbalanced.
inline const char* Balanced(const char* at,const char* end) {
    int depth=0;bool text=false;
    for(const char* p=at;p<end;++p) {
        if(text) {if(*p=='\\')++p;else if(*p=='"')text=false;continue;}
        if(*p=='"')text=true;
        else if(*p=='{' || *p=='[')++depth;
        else if(*p=='}' || *p==']') {if(--depth==0)return p+1;}
    }
    return nullptr;
}
inline int Hex(char c) {return c>='0' && c<='9'?c-'0':Lower(c)>='a' && Lower(c)<='f'?Lower(c)-'a'+10:-1;}
// Value of "key" inside [from,end): strings are unescaped, numbers/true/false/null copied as text.
inline bool Value(const char* from,const char* end,const char* key,char* out,size_t cap) {
    if(cap)out[0]=0;
    const size_t keySize=Length(key);
    for(const char* p=from;(p=Find(p,end,"\""))!=nullptr;++p) {
        if(size_t(end-p)<keySize+3 || p[keySize+1]!='"')continue;
        bool same=true;for(size_t i=0;i<keySize;++i)if(p[1+i]!=key[i]) {same=false;break;}
        if(!same)continue;
        const char* v=p+keySize+2;while(v<end && (*v==' ' || *v=='\t' || *v=='\r' || *v=='\n'))++v;
        if(v>=end || *v!=':')continue;
        ++v;while(v<end && (*v==' ' || *v=='\t' || *v=='\r' || *v=='\n'))++v;
        Appender a(out,cap);
        if(v<end && *v=='"') {
            for(++v;v<end && *v!='"';++v) {
                char c=*v;
                if(c=='\\' && v+1<end) {
                    c=*++v;
                    if(c=='n')c='\n';else if(c=='r')c='\r';else if(c=='t')c='\t';else if(c=='b' || c=='f')c=' ';
                    else if(c=='u' && v+4<end) {
                        unsigned u=0;bool good=true;for(int i=1;i<=4;++i) {const int h=Hex(v[i]);if(h<0)good=false;else u=u*16+unsigned(h);}
                        if(!good)return false;
                        v+=4;
                        if(u<0x80) {c=char(u);}
                        else if(u<0x800) {const char two[2]={char(0xc0|(u>>6)),char(0x80|(u&63))};a.Add(two,2);continue;}
                        else {const char three[3]={char(0xe0|(u>>12)),char(0x80|((u>>6)&63)),char(0x80|(u&63))};a.Add(three,3);continue;}
                    }
                }
                a.Add(&c,1);
            }
            return v<end && !a.overflow;
        }
        while(v<end && *v!=',' && *v!='}' && *v!=']' && *v!=' ' && *v!='\r' && *v!='\n')a.Add(v++,1);
        return a.n>0 && !a.overflow;
    }
    return false;
}
// First non-empty "Errors":[{...}] of an API answer -> key, text, type. False: no error object.
inline bool ApiError(const char* json,const char* end,Link& out) {
    for(const char* p=json;(p=Find(p,end,"\"Errors\""))!=nullptr;p+=8) {
        const char* v=p+8;while(v<end && (*v==' ' || *v==':' || *v=='\r' || *v=='\n'))++v;
        if(v>=end || *v!='[')continue;
        ++v;while(v<end && (*v==' ' || *v=='\r' || *v=='\n'))++v;
        if(v>=end || *v!='{')continue;
        const char* stop=Balanced(v,end);if(!stop)stop=end;
        char type[16];
        Value(v,stop,"Key",out.errorKey,sizeof(out.errorKey));Value(v,stop,"Value",out.error,sizeof(out.error));
        out.errorType=-1;
        if(Value(v,stop,"Type",type,sizeof(type))) {uint64_t n=0;if(https::ParseNumber(type,n))out.errorType=int(n);}
        return true;
    }
    return false;
}
// The refusal Microsoft gives to requests it takes for automated or sanctioned.
inline bool IsRefusal(const Link& l) {
    return l.errorType==9 || Find(l.error,l.error+Length(l.error),"Sentinel") || Find(l.errorKey,l.errorKey+Length(l.errorKey),"Sentinel")
        || Find(l.error,l.error+Length(l.error),"715-123130");
}
// ---- the download page: edition id and SHA-256 table, scanned as it streams ----
struct PageScan {
    char labels[3][80];size_t labelCount;     // "<td>Polish 64-bit</td>" candidates
    char edition[16];char sha256[65];
    char window[512+https::bodyUnits+1];size_t size;
};
inline void ScanWindow(PageScan& s) {
    const char* begin=s.window;const char* end=s.window+s.size;
    if(!s.edition[0]) {
        const char* at=Find(begin,end,"\">Windows 11 (multi-edition ISO for x64");
        if(at) {
            const char* d=at;while(d>begin && d[-1]>='0' && d[-1]<='9')--d;
            if(d<at && size_t(at-d)<sizeof(s.edition) && d-begin>=7 && Find(d-7,d,"value=\""))https::Copy(s.edition,sizeof(s.edition),d,size_t(at-d));
        }
    }
    for(size_t i=0;i<s.labelCount && !s.sha256[0];++i) {
        const char* at=Find(begin,end,s.labels[i]);if(!at)continue;
        at+=Length(s.labels[i]);
        // The sum is the next 64 hex digits, within the next cell.
        for(const char* p=at;p+64<=end && p<at+48;++p) {
            size_t k=0;while(k<64 && Hex(p[k])>=0)++k;
            if(k==64 && (p+64==end || Hex(p[64])<0)) {
                for(size_t j=0;j<64;++j)s.sha256[j]=p[j]>='a' && p[j]<='f'?char(p[j]-32):p[j];
                s.sha256[64]=0;break;}
        }
    }
}
inline bool PageSink(void* context,const uint8_t* data,size_t size) {
    auto& s=*static_cast<PageScan*>(context);
    while(size) {
        const size_t room=sizeof(s.window)-1-s.size;const size_t take=size<room?size:room;
        for(size_t i=0;i<take;++i)s.window[s.size+i]=char(data[i]);
        s.size+=take;data+=take;size-=take;
        ScanWindow(s);
        if(s.edition[0] && (s.sha256[0] || !s.labelCount))return false;   // all found
        // Keep the tail: a match cut by the end of this piece is found whole next time.
        if(s.size>512) {const size_t drop=s.size-512;for(size_t i=0;i<512;++i)s.window[i]=s.window[drop+i];s.size=512;}
    }
    return true;
}
// Row labels of the page's hash table for an API language name.
inline void PageLabels(PageScan& s,const char* language) {
    s.labelCount=0;
    auto add=[&](const char* name) {
        if(s.labelCount>=3)return;
        Appender a(s.labels[s.labelCount],sizeof(s.labels[0]));a.Add("<td>");a.Add(name);a.Add(" 64-bit</td>");
        if(!a.overflow)++s.labelCount;
    };
    if(SameNoCase(language,"English (United Kingdom)")) {add("English International");return;}
    add(language);
    char plain[64];size_t n=0;bool changed=false;
    for(size_t i=0;language[i] && n+1<sizeof(plain);++i) {if(language[i]=='(' || language[i]==')') {changed=true;continue;}plain[n++]=language[i];}
    plain[n]=0;
    if(changed)add(plain);   // "Chinese (Simplified)" is "Chinese Simplified" in the table
}
// The API's name for what a user may call the language.
inline const char* ApiLanguage(const char* language) {
    if(SameNoCase(language,"English (United States)") || SameNoCase(language,"English (US)") || SameNoCase(language,"English US") || SameNoCase(language,"English United States"))return "English";
    if(SameNoCase(language,"English International") || SameNoCase(language,"English (UK)") || SameNoCase(language,"English UK"))return "English (United Kingdom)";
    if(SameNoCase(language,"Chinese Simplified"))return "Chinese (Simplified)";
    if(SameNoCase(language,"Chinese Traditional"))return "Chinese (Traditional)";
    if(SameNoCase(language,"Portuguese (Brazil)"))return "Brazilian Portuguese";
    return language;
}
inline bool SessionId(https::Client& c,char* out) {
    uint8_t r[16];
    if(!c.transport.entropy || !c.transport.entropy(c.transport.context,r,sizeof(r)))return false;
    r[6]=uint8_t((r[6]&0x0f)|0x40);r[8]=uint8_t((r[8]&0x3f)|0x80);   // UUID version 4
    static const char hex[]="0123456789abcdef";size_t n=0;
    for(size_t i=0;i<16;++i) {if(i==4 || i==6 || i==8 || i==10)out[n++]='-';out[n++]=hex[r[i]>>4];out[n++]=hex[r[i]&15];}
    out[n]=0;return true;
}
// Options of the lookup. Defaults are the normal flow.
struct Options {
    bool readPage=true;        // step 1 (edition id and SHA-256 table)
    bool whitelist=true;       // steps 2-3; false only to demonstrate Microsoft's refusal
    bool probeSize=true;       // step 6
    const char* locale="en-US";
    const char* editionId=nullptr;   // r401: ask this product edition id instead of the page's
    const char* refererLine=nullptr; // r401: the Referer of another product page (Windows 10)
    // r401: as in Rufus, the language is chosen from the list Microsoft offers for
    // this very edition. Called with the offered names; `index` comes in as the
    // entry matching `language` (or 0). False: the user gave up.
    bool (*choose)(void* context,const char (*names)[64],size_t count,size_t& index)=nullptr;
    void* chooseContext=nullptr;
};
// Fills `out` with the direct ISO address for `language` ("Polish",
// "English (United States)", "German" ... the names of Microsoft's page).
// About 6 HTTPS requests. Uses large static work buffers: not reentrant.
inline Result Windows11Link(https::Client& c,const char* language,Link& out,const Options& options=Options()) {
    static char text[40960];static char url[1024];static PageScan page;
    out=Link{};out.errorType=-1;out.transport=https::Result::ok;
    const char* wanted=ApiLanguage(language);
    https::Response rs;
    auto fail=[&](const char* stage,https::Result r) {
        out.stage=stage;out.transport=r;out.httpStatus=rs.status;
        return r==https::Result::noClock?Result::noClock:r==https::Result::noEntropy?Result::noEntropy:Result::network;
    };
    https::UtcTime now{};
    if(!c.transport.clock || !c.transport.clock(c.transport.context,&now))return fail("clock",https::Result::noClock);
    if(!SessionId(c,out.sessionId)) {out.stage="session id";return Result::noEntropy;}
    // 1. The page: edition id and the checksum table. Optional - Microsoft's
    //    front end sometimes refuses non-browser clients here while the API works.
    https::Copy(out.editionId,sizeof(out.editionId),fallbackEditionId);
    page.size=0;page.labelCount=0;page.edition[0]=0;page.sha256[0]=0;   // (no PageScan{} temporary: it is 17 KiB)
    if(options.readPage) {
        PageLabels(page,wanted);
        https::Request rq;rq.url=pageUrl;
        const https::Result r=https::Get(c,rq,rs,PageSink,&page);
        if((r==https::Result::ok || r==https::Result::stopped) && page.edition[0] && !options.editionId)https::Copy(out.editionId,sizeof(out.editionId),page.edition);
        if(r==https::Result::noClock || r==https::Result::certificateExpired || r==https::Result::certificateUntrusted || r==https::Result::certificateWrongHost)return fail("download page",r);
    }
    if(options.editionId)https::Copy(out.editionId,sizeof(out.editionId),options.editionId);
    // Two seconds between repeated questions, as Rufus waits: the first answers
    // after whitelisting may be empty, and three questions within a second all were.
    auto pause=[&]() {
        https::UtcTime from{},t{};if(!c.transport.clock(c.transport.context,&from))return;
        const uint64_t start=https::UnixSeconds(from);
        for(unsigned spin=0;spin<4000000;++spin) {
            if((spin&1023)==0 && c.transport.clock(c.transport.context,&t) && https::UnixSeconds(t)>=start+2)break;
            __asm__ volatile("pause");
        }
    };
    if(options.whitelist) {
        // 2. Whitelist the session id.
        {Appender a(url,sizeof(url));a.Add("https://vlscppe.microsoft.com/tags?org_id=");a.Add(orgId);a.Add("&session_id=");a.Add(out.sessionId);}
        https::Request rq;rq.url=url;rq.maxRedirects=0;
        https::Result r=https::Get(c,rq,rs,nullptr,nullptr);
        if(r!=https::Result::ok)return fail("session whitelisting (vlscppe)",r);
        // 3. The second half: mdt.js gives w and rticks, which are sent back with the time.
        {Appender a(url,sizeof(url));a.Add("https://ov-df.microsoft.com/mdt.js?instanceId=");a.Add(instanceId);a.Add("&PageId=si&session_id=");a.Add(out.sessionId);}
        r=https::GetText(c,rq,rs,text,sizeof(text));
        if(r!=https::Result::ok)return fail("session whitelisting (ov-df script)",r);
        const char* end=text+Length(text);char w[40];char rticks[24];size_t n=0;
        const char* p=Find(text,end,"&w=");if(!p)p=Find(text,end,"?w=");
        if(p)for(p+=3;Hex(*p)>=0 && n+1<sizeof(w);++p)w[n++]=*p;
        w[n]=0;
        size_t m=0;p=Find(text,end,"rticks=\"");
        if(p) {p+=8;if(*p=='+')++p;for(;*p>='0' && *p<='9' && m+1<sizeof(rticks);++p)rticks[m++]=*p;}
        rticks[m]=0;
        if(!n || !m) {out.stage="session whitelisting (ov-df script)";https::Copy(out.error,sizeof(out.error),"w or rticks not found in mdt.js");return Result::badAnswer;}
        {Appender a(url,sizeof(url));a.Add("https://ov-df.microsoft.com/?session_id=");a.Add(out.sessionId);a.Add("&CustomerId=");a.Add(instanceId);
         c.transport.clock(c.transport.context,&now);   // r401: the time of this very reply, not of the start
         a.Add("&PageId=si&w=");a.Add(w);a.Add("&mdt=");a.Number(https::UnixSeconds(now)*1000);a.Add("&rticks=");a.Add(rticks);}
        r=https::Get(c,rq,rs,nullptr,nullptr);
        if(r!=https::Result::ok)return fail("session whitelisting (ov-df reply)",r);
    }
    // 4. Languages of the edition. The first answers after whitelisting may be empty or an error.
    {Appender a(url,sizeof(url));a.Add("https://www.microsoft.com/software-download-connector/api/getskuinformationbyproductedition?profile=");a.Add(profileId);
     a.Add("&productEditionId=");a.Add(out.editionId);a.Add("&SKU=undefined&friendlyFileName=undefined&Locale=");a.Add(options.locale);a.Add("&sessionID=");a.Add(out.sessionId);}
    bool found=false;
    for(unsigned attempt=0;attempt<3 && !found;++attempt) {
        if(attempt)pause();
        https::Request rq;rq.url=url;rq.deliverErrorBody=true;
        const https::Result r=https::GetText(c,rq,rs,text,sizeof(text));
        if(r!=https::Result::ok) {if(attempt<2)continue;return fail("language list",r);}
        const char* end=text+Length(text);
        if(ApiError(text,end,out)) {if(attempt<2)continue;out.stage="language list";out.httpStatus=rs.status;return IsRefusal(out)?Result::refused:Result::serverError;}
        const char* list=Find(text,end,"\"Skus\"");
        if(!list) {if(attempt<2)continue;out.stage="language list";out.httpStatus=rs.status;https::Copy(out.error,sizeof(out.error),text);return Result::badAnswer;}
        if(options.choose) {
            static char names[maxLanguages][64];static char ids[maxLanguages][16];static char products[maxLanguages][160];size_t count=0,index=0;
            for(const char* p=Find(list,end,"{");p && count<maxLanguages;) {
                const char* stop=Balanced(p,end);if(!stop)break;
                char local[64];names[count][0]=0;ids[count][0]=0;products[count][0]=0;
                Value(p,stop,"Language",names[count],sizeof(names[count]));Value(p,stop,"LocalizedLanguage",local,sizeof(local));
                Value(p,stop,"Id",ids[count],sizeof(ids[count]));Value(p,stop,"ProductDisplayName",products[count],sizeof(products[count]));
                if(names[count][0] && ids[count][0]) {
                    if(SameNoCase(names[count],wanted) || SameNoCase(local,wanted) || SameNoCase(names[count],language))index=count;
                    ++count;
                }
                p=Find(stop,end,"{");
            }
            if(!count) {out.stage="language list";return Result::noSuchLanguage;}
            if(!options.choose(options.chooseContext,names,count,index) || index>=count) {out.stage="language list";https::Copy(out.error,sizeof(out.error),"cancelled");return Result::noSuchLanguage;}
            https::Copy(out.language,sizeof(out.language),names[index]);https::Copy(out.skuId,sizeof(out.skuId),ids[index]);
            https::Copy(out.product,sizeof(out.product),products[index]);found=true;break;
        }
        Appender offered(out.error,sizeof(out.error));
        for(const char* p=Find(list,end,"{");p;) {
            const char* stop=Balanced(p,end);if(!stop)break;
            char name[64],local[64];
            Value(p,stop,"Language",name,sizeof(name));Value(p,stop,"LocalizedLanguage",local,sizeof(local));
            if(SameNoCase(name,wanted) || SameNoCase(local,wanted) || SameNoCase(name,language)) {
                https::Copy(out.language,sizeof(out.language),name);Value(p,stop,"Id",out.skuId,sizeof(out.skuId));
                Value(p,stop,"ProductDisplayName",out.product,sizeof(out.product));found=out.skuId[0]!=0;break;}
            if(offered.n)offered.Add(", ");
            offered.Add(name);
            p=Find(stop,end,"{");
        }
        if(!found) {out.stage="language list";return Result::noSuchLanguage;}   // out.error lists the offered names
    }
    out.error[0]=0;
    // 5. The link. The Referer is required.
    {Appender a(url,sizeof(url));a.Add("https://www.microsoft.com/software-download-connector/api/GetProductDownloadLinksBySku?profile=");a.Add(profileId);
     a.Add("&productEditionId=undefined&SKU=");a.Add(out.skuId);a.Add("&friendlyFileName=undefined&Locale=");a.Add(options.locale);a.Add("&sessionID=");a.Add(out.sessionId);}
    {
        https::Request rq;rq.url=url;rq.headers=options.refererLine?options.refererLine:referer;rq.deliverErrorBody=true;
        const https::Result r=https::GetText(c,rq,rs,text,sizeof(text));
        if(r!=https::Result::ok)return fail("download link",r);
        const char* end=text+Length(text);out.stage="download link";out.httpStatus=rs.status;
        if(ApiError(text,end,out))return IsRefusal(out)?Result::refused:Result::serverError;
        const char* list=Find(text,end,"\"ProductDownloadOptions\"");
        if(!list) {https::Copy(out.error,sizeof(out.error),text);return Result::badAnswer;}
        const char* array=Find(list,end,"[");const char* arrayEnd=array?Balanced(array,end):nullptr;
        if(!arrayEnd)return Result::noLink;
        static char fallback[https::urlUnits];static char uri[https::urlUnits];fallback[0]=0;
        for(const char* p=Find(array,arrayEnd,"{");p;) {
            const char* stop=Balanced(p,arrayEnd);if(!stop)break;
            char type[8];
            if(Value(p,stop,"Uri",uri,sizeof(uri)) && https::StartsNoCase(uri,"https://")) {
                Value(p,stop,"DownloadType",type,sizeof(type));
                if(type[0]=='1' && !type[1]) {https::Copy(out.url,sizeof(out.url),uri);break;}   // 1 = x64 (0 x86, 2 ARM64)
                if(!fallback[0] && Find(uri,uri+Length(uri),"x64"))https::Copy(fallback,sizeof(fallback),uri);
            }
            p=Find(stop,arrayEnd,"{");
        }
        if(!out.url[0])https::Copy(out.url,sizeof(out.url),fallback);
        if(!out.url[0])return Result::noLink;
        Value(text,end,"DownloadExpirationDatetime",out.expires,sizeof(out.expires));
    }
    {   // File name: last path component.
        size_t from=0,to=0;
        for(size_t i=0;out.url[i] && out.url[i]!='?';++i) {if(out.url[i]=='/')from=i+1;to=i+1;}
        https::Copy(out.fileName,sizeof(out.fileName),out.url+from,to-from);
    }
    if(page.sha256[0]) {https::Copy(out.sha256,sizeof(out.sha256),page.sha256);out.hasSha256=true;}
    // 6. Size and resume validator.
    if(options.probeSize) {
        const https::Result r=https::Probe(c,out.url,rs);
        if(r!=https::Result::ok)return fail("ISO size",r);
        if(rs.totalLength!=https::unknownLength) {out.size=rs.totalLength;out.hasSize=true;}
        https::Copy(out.validator,sizeof(out.validator),rs.validator);
    }
    out.stage="done";
    return Result::ok;
}
// r401: one question with the caller's options (version, language chooser).
inline Result Windows11LinkAny(https::Client& c,const char* language,Link& out,const Options& options=Options()) {
    return Windows11Link(c,language,out,options);
}
}  // namespace msiso
}  // namespace hakefi
