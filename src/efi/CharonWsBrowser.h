#pragma once
// r402: Charon workspace - a speaking text web browser.
//
// A page is fetched with Charon's own HTTPS client, turned into lines of text
// (CharonHtmlText.h: headings, lists, links numbered in brackets, tables as
// rows, form fields described) and read line by line, like lynx or edbrowse
// under a screen reader. The page stands on the screen, so the reader's
// review keys work on it too. No scripts, no pictures, no cookies; forms are
// sent with GET only (searches work, logins do not).
//
// Keys: Up/Down a line, PageUp/PageDown a screen, Home/End, Tab and Shift+Tab
// links, H and Shift+H headings, F form fields, Enter follow the link on the
// line (or fill the field), numbers + Enter after L follow link N, Backspace
// back, G address, S find, N find next, A read on from here, T title and
// address, F1 the keys, Escape leave.
// GPL-2.0-or-later.
#ifdef HAK_CHARON_HTTPS
#include "CharonHtmlText.h"
namespace hakefi {
namespace ws {
namespace browser {
constexpr size_t pageBytes=size_t(4)<<20,textUnits=size_t(1)<<20,lineCap=40000,linkCap=16384,stringBytes=size_t(1)<<20,fieldCap=1024,formCap=64,anchorCap=4096;
constexpr size_t urlUnits=2048,historyCap=24;
struct State {
    Memory raw,text,lines,links,strings,fields,forms,anchors;
    html::Page page;bool ready;
    char url[urlUnits];char history[historyCap][urlUnits];uint32_t historyLine[historyCap];size_t depth;
    uint32_t line,top;
    char16_t needle[80];
    char values[16][160];uint16_t valueField[16];size_t valueCount;   // text typed into form fields
    size_t rawSize;bool rawFull;
    https::Result last;
};
inline State& B() {static State state;return state;}
inline bool Take(void*,const uint8_t* data,size_t size) {
    auto& b=B();
    if(b.rawSize+size>pageBytes) {size=pageBytes-b.rawSize;b.rawFull=true;}
    memcpy(b.raw.data+b.rawSize,data,size);b.rawSize+=size;
    Pump();
    return !b.rawFull;
}
inline bool Allocate() {
    auto& b=B();if(b.ready)return true;
    Memory* all[]={&b.raw,&b.text,&b.lines,&b.links,&b.strings,&b.fields,&b.forms,&b.anchors};
    const size_t sizes[]={pageBytes,textUnits*2,lineCap*sizeof(html::Line),linkCap*sizeof(html::Link),stringBytes,fieldCap*sizeof(html::Field),formCap*sizeof(html::Form),anchorCap*sizeof(html::Anchor)};
    for(size_t i=0;i<8;++i) {all[i]->reserved=false;all[i]->extra=0;if(!Reserve(*all[i],sizes[i])) {for(size_t k=0;k<i;++k)Free(*all[k]);return false;}}
    auto& p=b.page;memset(&p,0,sizeof(p));
    p.text=reinterpret_cast<char16_t*>(b.text.data);p.textCapacity=uint32_t(textUnits);
    p.lines=reinterpret_cast<html::Line*>(b.lines.data);p.lineCapacity=uint32_t(lineCap);
    p.links=reinterpret_cast<html::Link*>(b.links.data);p.linkCapacity=uint32_t(linkCap);
    p.strings=reinterpret_cast<char*>(b.strings.data);p.stringCapacity=uint32_t(stringBytes);
    p.fields=reinterpret_cast<html::Field*>(b.fields.data);p.fieldCapacity=uint32_t(fieldCap);
    p.forms=reinterpret_cast<html::Form*>(b.forms.data);p.formCapacity=uint32_t(formCap);
    p.anchors=reinterpret_cast<html::Anchor*>(b.anchors.data);p.anchorCapacity=uint32_t(anchorCap);
    b.ready=true;return true;
}
inline void Release() {
    auto& b=B();if(!b.ready)return;
    Memory* all[]={&b.raw,&b.text,&b.lines,&b.links,&b.strings,&b.fields,&b.forms,&b.anchors};
    for(auto* m:all)Free(*m);
    b.ready=false;
}
inline size_t Length(const char* s) {size_t n=0;while(s[n])++n;return n;}
inline void Copy(char* out,size_t cap,const char* in) {size_t n=0;for(;in[n] && n+1<cap;++n)out[n]=in[n];out[n]=0;}
inline void LineText(uint32_t index,char16_t* out,size_t cap,bool spoken) {
    auto& p=B().page;out[0]=0;if(index>=p.lineCount)return;
    const html::Line& l=p.lines[index];
    if(spoken && (l.flags&html::lineFirst)) {
        if(l.kind==html::kindHeading) {Cat(out,cap,T(u"nagłówek ",u"heading "));CatN(out,cap,l.level);Cat(out,cap,u": ");}
        else if(l.kind==html::kindListItem)Cat(out,cap,T(u"punkt: ",u"item: "));
        else if(l.kind==html::kindQuote)Cat(out,cap,T(u"cytat: ",u"quote: "));
        else if(l.kind==html::kindTableRow)Cat(out,cap,T(u"wiersz tabeli: ",u"table row: "));
        else if(l.kind==html::kindForm)Cat(out,cap,T(u"pole formularza: ",u"form field: "));
    }
    if(l.kind==html::kindRule) {Cat(out,cap,T(u"linia pozioma",u"horizontal rule"));return;}
    size_t n=Len(out);
    for(uint32_t i=0;i<l.length && n+1<cap;++i)out[n++]=p.text[l.start+i];
    out[n]=0;
    if(spoken && l.linkCount) {Cat(out,cap,l.linkCount==1?T(u", łącze",u", link"):T(u", łącza",u", links"));}
}
inline void Draw() {
    auto& b=B();auto& p=b.page;const size_t body=BodyRows();
    if(b.line<b.top)b.top=b.line;
    if(b.line>=b.top+body)b.top=uint32_t(b.line-body+1);
    static char16_t row[300];
    {static char16_t title[260];Set(title,260,u" ");size_t n=1;for(uint32_t i=0;i<p.titleLength && n<200;++i)title[n++]=p.title[i];title[n]=0;
     if(!p.titleLength)Cat(title,260,T(u"Przeglądarka Charona",u"Charon's browser"));Title(title);}
    for(size_t r=0;r<body;++r) {
        const uint32_t index=uint32_t(b.top+r);
        if(index<p.lineCount) {LineText(index,row,260,false);Row(r+1,row,index==b.line?0x70:(p.lines[index].kind==html::kindHeading?0x0f:7));}
        else Row(r+1,u"",7);
    }
    static char16_t mode[260];Set(mode,260,T(u" wiersz ",u" line "));CatN(mode,260,p.lineCount?b.line+1:0);Cat(mode,260,T(u" z ",u" of "));CatN(mode,260,p.lineCount);
    Cat(mode,260,T(u", łączy ",u", links "));CatN(mode,260,p.linkCount);Cat(mode,260,u"  ");
    {size_t n=Len(mode);for(size_t i=0;b.url[i] && n<250;++i)mode[n++]=char16_t(uint8_t(b.url[i]));mode[n]=0;}
    ModeLine(mode);
}
inline void SayLine() {
    auto& b=B();static char16_t text[400];
    if(!b.page.lineCount) {Say(T(u"Strona jest pusta.",u"The page is empty."));return;}
    LineText(b.line,text,400,true);
    if(!text[0])Set(text,400,T(u"pusty wiersz",u"blank line"));
    Draw();Say(text);
}
inline void Charset(const char* contentType,html::Charset& hint) {
    hint=html::Charset::Auto;
    for(size_t i=0;contentType[i];++i) {
        static const char key[]="charset=";size_t k=0;
        while(key[k] && (contentType[i+k]|32)==key[k])++k;
        if(key[k])continue;
        const char* name=contentType+i+k;if(*name=='"')++name;
        size_t n=0;while(name[n] && name[n]!=';' && name[n]!='"' && name[n]!=' ')++n;
        hint=html::CharsetFromName(name,n);return;
    }
}
inline bool StartsWith(const char* text,const char* prefix) {for(size_t i=0;prefix[i];++i)if((text[i]|32)!=prefix[i])return false;return true;}
// Fetches and renders `address`. False: said why; the old page stays.
inline bool Load(const char* address,bool remember) {
    auto& b=B();static https::Client client;static https::Response answer;
    if(!install::EnsureNetwork())return false;
    static char target[urlUnits];Copy(target,urlUnits,address);
    static char16_t text[300];Set(text,300,T(u"Otwieram: ",u"Opening: "));{size_t n=Len(text);for(size_t i=0;target[i] && n<290;++i)text[n++]=char16_t(uint8_t(target[i]));text[n]=0;}
    Say(text);
    client.transport=net::tcp::Transport();
    https::Request rq;rq.url=target;rq.headers="Accept: text/html,text/plain;q=0.9,*/*;q=0.1\r\nAccept-Language: pl,en;q=0.8\r\n";
    b.rawSize=0;b.rawFull=false;
    static uint8_t* keep=nullptr;(void)keep;
    b.last=https::Get(client,rq,answer,Take,nullptr);
    if(b.last!=https::Result::ok && !(b.rawFull && b.rawSize)) {net::HttpsFail(client,b.last);return false;}
    if(answer.status>=400) {
        static char16_t fail[200];Set(fail,200,T(u"Serwer odpowiedział błędem ",u"The server answered with error "));CatN(fail,200,uint64_t(answer.status));Cat(fail,200,u".");
        Play(Icon::error);Say(fail);
        if(!b.rawSize)return false;
    }
    if(remember && b.url[0] && b.depth<historyCap) {Copy(b.history[b.depth],urlUnits,b.url);b.historyLine[b.depth]=b.line;++b.depth;}
    else if(remember && b.url[0]) {for(size_t i=1;i<historyCap;++i) {Copy(b.history[i-1],urlUnits,b.history[i]);b.historyLine[i-1]=b.historyLine[i];}
        Copy(b.history[historyCap-1],urlUnits,b.url);b.historyLine[historyCap-1]=b.line;}
    Copy(b.url,urlUnits,client.url[0]?client.url:target);
    html::Charset hint;Charset(answer.contentType,hint);
    html::Options options;options.width=uint32_t(U().cols>20?U().cols-2:78);
    static const char16_t* const wordsPl[]={u"obraz",u"linia pozioma",u"pole tekstowe",u"hasło",u"pole wyboru",u"przycisk opcji",u"przycisk",u"lista",u"pole wielowierszowe",u"plik",u"zaznaczone",u"niezaznaczone",u"wyślij",u"wyczyść"};
    if(Pl()) {auto& w=options.words;w.image=wordsPl[0];w.rule=wordsPl[1];w.edit=wordsPl[2];w.password=wordsPl[3];w.checkbox=wordsPl[4];w.radio=wordsPl[5];w.button=wordsPl[6];
        w.select=wordsPl[7];w.textarea=wordsPl[8];w.file=wordsPl[9];w.checked=wordsPl[10];w.unchecked=wordsPl[11];w.submit=wordsPl[12];w.reset=wordsPl[13];}
    const bool plain=StartsWith(answer.contentType,"text/plain");
    const bool whole=plain?html::RenderPlain(b.raw.data,b.rawSize,hint,b.page,options):html::Render(b.raw.data,b.rawSize,hint,b.page,options);
    b.line=0;b.top=0;b.valueCount=0;Forget();
    static char16_t summary[500];summary[0]=0;
    {size_t n=0;for(uint32_t i=0;i<b.page.titleLength && n<200;++i)summary[n++]=b.page.title[i];summary[n]=0;}
    if(!summary[0])Set(summary,500,T(u"Strona bez tytułu",u"Untitled page"));
    Cat(summary,500,u". ");
    uint32_t headings=0;for(uint32_t i=0;i<b.page.lineCount;++i)if(b.page.lines[i].kind==html::kindHeading && (b.page.lines[i].flags&html::lineFirst))++headings;
    CatN(summary,500,b.page.lineCount);Cat(summary,500,T(u" wierszy, ",u" lines, "));CatN(summary,500,headings);Cat(summary,500,T(u" nagłówków, ",u" headings, "));
    CatN(summary,500,b.page.linkCount);Cat(summary,500,T(u" łączy",u" links"));
    if(b.page.fieldCount) {Cat(summary,500,u", ");CatN(summary,500,b.page.fieldCount);Cat(summary,500,T(u" pól formularza",u" form fields"));}
    if(!whole || b.rawFull)Cat(summary,500,T(u". Strona jest za duża: wczytano początek",u". The page is too large: its beginning was loaded"));
    Cat(summary,500,u".");
    Draw();Play(Icon::done);Say(summary);
    return true;
}
inline bool Follow(uint32_t link) {
    auto& b=B();static char base[urlUnits*3+8];static char out[urlUnits*3+8];static char fragment[256];size_t outLen=0,fragLen=0;
    if(link>=b.page.linkCount)return false;
    if(!html::PageBaseUrl(b.page,b.url,Length(b.url),base,sizeof(base)))Copy(base,sizeof(base),b.url);
    if(!html::ResolveLink(b.page,link,base,Length(base),out,sizeof(out),&outLen,fragment,sizeof(fragment),&fragLen) || outLen>=urlUnits) {
        Fail(T(u"Tego łącza nie da się otworzyć.",u"This link cannot be opened."));return false;}
    out[outLen]=0;fragment[fragLen<sizeof(fragment)?fragLen:sizeof(fragment)-1]=0;
    if(!html::IsHttpUrl(out,outLen)) {Fail(T(u"To łącze nie prowadzi do strony http ani https.",u"This link does not lead to an http or https page."));return false;}
    // The same page, another place on it.
    bool same=fragLen>0;for(size_t i=0;same && (out[i] || b.url[i]);++i)if(out[i]!=b.url[i])same=false;
    if(!same && !Load(out,true))return false;
    if(fragLen) {const uint32_t at=html::FindAnchor(b.page,fragment,fragLen);if(at!=html::kNone) {b.line=at;SayLine();}}
    return true;
}
inline void Address() {
    auto& b=B();static char16_t wide[urlUnits];static char address[urlUnits];
    size_t n=0;for(;b.url[n] && n+1<urlUnits;++n)wide[n]=char16_t(uint8_t(b.url[n]));wide[n]=0;
    if(!Prompt(T(u"Adres strony:",u"Page address:"),wide,urlUnits-10))return;
    size_t k=0;const bool scheme=StartsFold(wide,u"http://") || StartsFold(wide,u"https://");
    if(!scheme) {Copy(address,urlUnits,"https://");k=8;}
    for(size_t i=0;wide[i] && k+1<urlUnits;++i) {if(wide[i]<33 || wide[i]>126) {Fail(T(u"Adres zawiera niedozwolone znaki.",u"The address holds characters that are not allowed."));return;}address[k++]=char(wide[i]);}
    address[k]=0;
    Load(address,true);
}
// A text field: typed value, then the form goes out (GET).
inline void Fill(uint16_t fieldIndex) {
    auto& b=B();auto& p=b.page;if(fieldIndex>=p.fieldCount)return;
    html::Field& f=p.fields[fieldIndex];
    if(f.type==html::fieldCheckbox || f.type==html::fieldRadio) {
        f.flags=uint8_t(f.flags^html::fieldChecked);Say((f.flags&html::fieldChecked)?T(u"zaznaczone",u"checked"):T(u"niezaznaczone",u"not checked"));return;}
    const bool submit=f.type==html::fieldSubmit || f.type==html::fieldButton;
    if(!submit && f.type!=html::fieldText && f.type!=html::fieldTextarea) {Fail(T(u"Tego pola Charon nie umie wypełnić.",u"Charon cannot fill this field."));return;}
    if(f.form>=p.formCount) {Fail(T(u"To pole nie należy do formularza.",u"This field belongs to no form."));return;}
    if(!submit) {
        size_t slot=b.valueCount;for(size_t i=0;i<b.valueCount;++i)if(b.valueField[i]==fieldIndex)slot=i;
        if(slot>=16) {Fail(T(u"Za dużo wypełnionych pól.",u"Too many filled fields."));return;}
        static char16_t wide[160];wide[0]=0;
        if(slot<b.valueCount)Utf8ToUtf16(reinterpret_cast<const uint8_t*>(b.values[slot]),Length(b.values[slot]),wide,160);
        if(!Prompt(T(u"Tekst pola, Enter wysyła formularz:",u"Field text, Enter sends the form:"),wide,150))return;
        const size_t n=Utf16ToUtf8(wide,Len(wide),reinterpret_cast<uint8_t*>(b.values[slot]),159);b.values[slot][n]=0;
        b.valueField[slot]=fieldIndex;if(slot==b.valueCount)++b.valueCount;
    }
    static html::FieldValue values[fieldCap];
    for(uint32_t i=0;i<p.fieldCount && i<fieldCap;++i) {values[i].value=p.strings+p.fields[i].valueStart;values[i].length=p.fields[i].valueLength;}
    for(size_t i=0;i<b.valueCount;++i)if(b.valueField[i]<fieldCap) {values[b.valueField[i]].value=b.values[i];values[b.valueField[i]].length=Length(b.values[i]);}
    static char base[urlUnits*3+8];static char out[urlUnits*3+8];size_t outLen=0;
    if(!html::PageBaseUrl(p,b.url,Length(b.url),base,sizeof(base)))Copy(base,sizeof(base),b.url);
    const bool get=html::BuildFormGetUrl(p,f.form,base,Length(base),values,submit?fieldIndex:html::kNoField,out,sizeof(out),&outLen);
    if(!get || outLen>=urlUnits) {Fail(T(u"Ten formularz wysyła dane sposobem POST albo jest za długi; Charon wysyła tylko proste formularze wyszukiwania.",
        u"This form sends its data with POST or is too long; Charon sends simple search forms only."));return;}
    out[outLen]=0;Load(out,true);
}
inline void Find(bool again) {
    auto& b=B();
    if(!again || !b.needle[0]) {if(!Prompt(T(u"Szukaj na stronie:",u"Find on the page:"),b.needle,78) || !b.needle[0])return;}
    const uint32_t from=again?b.line+1:b.line;
    uint32_t at=from<b.page.lineCount?html::FindText(b.page,b.needle,Len(b.needle),from,1):html::kNone;
    if(at==html::kNone) {Play(Icon::edge);Say(T(u"Nie znaleziono.",u"Not found."));return;}
    b.line=at;SayLine();
}
inline void ReadOn() {
    auto& b=B();auto* fx=U().host->effects;static char16_t text[400];
    for(;b.line<b.page.lineCount;++b.line) {
        LineText(b.line,text,400,true);Draw();
        if(text[0])Say(text);
        for(unsigned i=0;i<600;++i) {
            U().host->boot->Stall(50000);Pump();
            if(EscapePressed() || (U().host->system->ConIn && false))return;
            if(i>3 && fx && fx->SpeechIdle && fx->SpeechIdle(fx->context))break;
        }
    }
    if(b.line>=b.page.lineCount && b.page.lineCount)b.line=b.page.lineCount-1;
    Play(Icon::edge);
}
inline void Keys() {
    Say(T(u"Klawisze przeglądarki. Strzałki w górę i w dół: wiersz. Page Up i Page Down: ekran. Tab i Shift Tab: łącza. H i Shift H: nagłówki. F: pola formularza. Enter: otwórz łącze albo wypełnij pole. L: łącze o numerze. Backspace: wstecz. G: adres. S: szukaj, N: szukaj dalej. A: czytaj od tego miejsca, Escape zatrzymuje. T: tytuł i adres. Escape: wyjście.",
          u"Browser keys. Up and Down arrows: a line. Page Up and Page Down: a screen. Tab and Shift Tab: links. H and Shift H: headings. F: form fields. Enter: open the link or fill the field. L: link by number. Backspace: back. G: address. S: find, N: find next. A: read on from here, Escape stops. T: title and address. Escape: leave."));
}
inline void Run() {
    auto& b=B();
    if(!Allocate()) {Fail(T(u"Za mało pamięci na przeglądarkę.",u"Not enough memory for the browser."));return;}
    Reshape();ReviewMode(true);
    b.page.lineCount=0;b.page.linkCount=0;b.page.titleLength=0;b.page.fieldCount=0;b.url[0]=0;b.depth=0;b.line=b.top=0;
    Draw();
    Say(T(u"Przeglądarka tekstowa Charona. G: adres strony, F1: klawisze, Escape: wyjście. Strony są czytane jako tekst, bez skryptów i obrazów.",
          u"Charon's text browser. G: page address, F1: keys, Escape: leave. Pages are read as text, without scripts and pictures."));
    Address();
    for(;;) {
        const Key key=WaitKey();auto& p=b.page;const char16_t letter=Printable(key) && !key.ctrl && !key.alt?Fold(key.ch):0;
        const uint32_t body=uint32_t(BodyRows());
        if(key.scan==scEsc)break;
        if(key.scan==scF1) {Keys();continue;}
        if(letter==u'g' || (key.ctrl && key.ch==u'l')) {Address();continue;}
        if(!p.lineCount) {if(key.scan || letter)Say(T(u"Nie ma strony. G: adres.",u"No page. G: address."));continue;}
        if(key.scan==scDown) {if(b.line+1<p.lineCount) {++b.line;SayLine();} else {Play(Icon::edge);SayLine();}}
        else if(key.scan==scUp) {if(b.line) {--b.line;SayLine();} else {Play(Icon::edge);SayLine();}}
        else if(key.scan==scPageDown) {b.line=b.line+body<p.lineCount?b.line+body:p.lineCount-1;SayLine();}
        else if(key.scan==scPageUp) {b.line=b.line>body?b.line-body:0;SayLine();}
        else if(key.scan==scHome) {b.line=0;SayLine();}
        else if(key.scan==scEnd) {b.line=p.lineCount-1;SayLine();}
        else if(!key.scan && key.ch==9) {
            const bool back=key.shift;const uint32_t from=back?(b.line?b.line-1:html::kNone):b.line+1;
            const uint32_t at=from==html::kNone || from>=p.lineCount?html::kNone:html::FindNextLink(p,from,back?-1:1);
            if(at==html::kNone) {Play(Icon::edge);Say(T(u"Nie ma więcej łączy.",u"No more links."));} else {b.line=at;SayLine();}
        }
        else if(letter==u'h') {
            const bool back=key.shift || key.ch==u'H';const uint32_t from=back?(b.line?b.line-1:html::kNone):b.line+1;
            const uint32_t at=from==html::kNone || from>=p.lineCount?html::kNone:html::FindNextHeading(p,from,back?-1:1);
            if(at==html::kNone) {Play(Icon::edge);Say(T(u"Nie ma więcej nagłówków.",u"No more headings."));} else {b.line=at;SayLine();}
        }
        else if(letter==u'f') {
            const uint32_t at=b.line+1<p.lineCount?html::FindNextField(p,b.line+1,1):html::kNone;
            if(at==html::kNone) {Play(Icon::edge);Say(T(u"Nie ma więcej pól.",u"No more fields."));} else {b.line=at;SayLine();}
        }
        else if(!key.scan && key.ch==13) {
            const html::Line& l=p.lines[b.line];
            if(l.field!=html::kNoField)Fill(l.field);
            else if(l.linkCount==1)Follow(l.firstLink);
            else if(l.linkCount>1) {
                static char16_t names[12][120];static ask::Choice choices[12];const size_t n=l.linkCount<12?l.linkCount:12;
                for(size_t i=0;i<n;++i) {const html::Link& k=p.links[l.firstLink+i];size_t m=0;
                    for(uint32_t c=0;c<k.textLength && m<110;++c)names[i][m++]=p.text[k.textStart+c];
                    names[i][m]=0;choices[i]={names[i],names[i],""};}
                size_t pick=0;
                if(ask::Choose(T(u"Które łącze?",u"Which link?"),choices,n,pick))Follow(uint32_t(l.firstLink+pick));
                else Draw();
            } else Say(T(u"W tym wierszu nie ma łącza.",u"There is no link on this line."));
        }
        else if(letter==u'l') {
            static char16_t digits[12];digits[0]=0;
            if(Prompt(T(u"Numer łącza:",u"Link number:"),digits,8)) {
                uint32_t value=0;for(size_t i=0;digits[i]>=u'0' && digits[i]<=u'9';++i)value=value*10+uint32_t(digits[i]-u'0');
                if(value && value<=p.linkCount)Follow(value-1);else Fail(T(u"Nie ma łącza o tym numerze.",u"There is no link with this number."));
            }
        }
        else if(!key.scan && key.ch==8) {
            if(!b.depth) {Play(Icon::edge);Say(T(u"To pierwsza strona.",u"This is the first page."));}
            else {--b.depth;static char back[urlUnits];Copy(back,urlUnits,b.history[b.depth]);const uint32_t line=b.historyLine[b.depth];
                if(Load(back,false) && line<b.page.lineCount) {b.line=line;Draw();}}
        }
        else if(letter==u's' || letter==u'/')Find(false);
        else if(letter==u'n')Find(true);
        else if(letter==u'a')ReadOn();
        else if(letter==u't') {
            static char16_t text[600];size_t n=0;for(uint32_t i=0;i<p.titleLength && n<200;++i)text[n++]=p.title[i];text[n]=0;
            Cat(text,600,u". ");n=Len(text);for(size_t i=0;b.url[i] && n<590;++i)text[n++]=char16_t(uint8_t(b.url[i]));text[n]=0;Say(text);
        }
    }
    Release();
}
}  // namespace browser
}  // namespace ws
}  // namespace hakefi
#endif
