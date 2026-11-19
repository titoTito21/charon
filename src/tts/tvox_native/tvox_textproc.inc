/* Exact finite-pattern translation of built-in textproc.normalise(PL/EN).
 * Included after UTF8/string helpers in tvox_pl.cpp. MIT-0. */
struct Range {uint32_t first,last;};struct Digit {uint32_t code,value;};
struct Unit {S key;S forms[4];};struct Spoken {S stem,replacement;bool end,hyphen;};
#include "tvox_textproc_data.inc"
template<size_t N> static bool prop(const Range(&a)[N],char32_t c){auto it=tvx::upper_bound(tvx::begin(a),tvx::end(a),(uint32_t)c,[](uint32_t c,Range r){return c<r.first;});return it!=tvx::begin(a)&&c<=(--it)->last;}
static bool wc(char32_t c){return c==U'_'||prop(UC_ALNUM,c);}
static bool letter(char32_t c){return prop(UC_ALNUM,c)&&!prop(UC_DECIMAL,c);}
static int digit(char32_t c){auto it=tvx::lower_bound(tvx::begin(UC_DIGITS),tvx::end(UC_DIGITS),(uint32_t)c,[](Digit d,uint32_t c){return d.code<c;});return it!=tvx::end(UC_DIGITS)&&it->code==c?(int)it->value:-1;}
static W textlower(const W& s){W r;for(size_t i=0;i<s.size();++i){if(s[i]!=U'Σ'){r+=lower(W(1,s[i]));continue;}bool before=false,after=false;
    for(size_t j=i;j-->0;){if(prop(UC_CASED,s[j])){before=true;break;}if(!prop(UC_IGNORABLE,s[j]))break;}
    for(size_t j=i+1;j<s.size();++j){if(prop(UC_CASED,s[j])){after=true;break;}if(!prop(UC_IGNORABLE,s[j]))break;}
    r+=before&&!after?U'ς':U'σ';}return r;}
static char32_t folded(char32_t c){if(c==U'İ'||c==U'ı')return U'i';if(c==U'ſ')return U's';W v=lower(W(1,c));return v.size()==1?v[0]:c;}
static bool ci(const W& text,size_t p,const W& value){if(p+value.size()>text.size())return false;for(size_t j=0;j<value.size();++j)if(folded(text[p+j])!=folded(value[j]))return false;return true;}
static bool leftBoundary(const W& t,size_t p){return p==0||!wc(t[p-1]);}
static W join(const Words& words){W r;for(auto w:words){if(!r.empty())r+=U' ';r+=w;}return r;}
static Words whitespace(const W& s){Words r;W w;for(auto c:s){if(prop(UC_SPACE,c)){if(!w.empty()){r.push_back(w);w.clear();}}else w+=c;}if(!w.empty())r.push_back(w);return r;}
static Words below_en(unsigned n){Words r;if(n>=100){r.push_back(TP__EN_ONES[n/100]);r.push_back(U"hundred");n%=100;}if(n>=20){r.push_back(TP__EN_TENS[n/10]);n%=10;if(n)r.push_back(TP__EN_ONES[n]);}else if(n)r.push_back(TP__EN_ONES[n]);return r;}
static Words below_pl(unsigned n){Words r;const char* names[4];size_t count=0;if(n>=100){names[count++]=tvox_hund[n/100];n%=100;}if(n>=20){names[count++]=tvox_tens[n/10];n%=10;if(n)names[count++]=tvox_ones[n];}else if(n>=10)names[count++]=tvox_teens[n-10];else if(n)names[count++]=tvox_ones[n];for(size_t i=0;i<count;++i)r.push_back(decode(names[i],tvx::strlen(names[i])));return r;}
static Words number(const W& digits,bool pl){tvx::vector<unsigned> groups;unsigned value=0,power=1;for(size_t i=digits.size();i-->0;){int d=digit(digits[i]);if(d<0)TVOX_INVALID("decimal");value+=(unsigned)d*power;power*=10;if(power==1000){groups.push_back(value);value=0;power=1;}}if(power!=1)groups.push_back(value);
    while(!groups.empty()&&!groups.back())groups.pop_back();if(groups.empty())return {pl?U"zero":U"zero"};if(!pl&&groups.size()>4)TVOX_INVALID("English number exceeds original groups");
    Words r;S en[]={U"",U"thousand",U"million",U"billion"};
    for(size_t i=groups.size();i-->0;){unsigned g=groups[i];if(!g)continue;if(pl&&i){auto forms=tvox_big[i>3?2:i-1];if(g==1){r.push_back(decode(forms[0],tvx::strlen(forms[0])));continue;}auto w=below_pl(g);r.insert(r.end(),w.begin(),w.end());unsigned f=g%10>=2&&g%10<=4&&!(g%100>=12&&g%100<=14)?1:2;r.push_back(decode(forms[f],tvx::strlen(forms[f])));}else{auto w=pl?below_pl(g):below_en(g);r.insert(r.end(),w.begin(),w.end());if(i)r.push_back(en[i]);}}
    return r;}
static W ordinal(unsigned n,int kase){auto one=[&](W stem){bool soft=ends(stem,U"drug")||ends(stem,U"trzec");return stem+(kase==0?(soft?U"i":U"y"):kase==1?(soft?U"iego":U"ego"):(soft?U"im":U"ym"));};if(n<20)return one(TP__ORD[n]);W r=one(TP__ORD_TENS[n/10%10]);if(n%10)r+=U" "+one(TP__ORD[n%10]);return r;}
static W spoken(W text){for(auto rule:PL_SPOKEN){W out,stem=rule.stem;for(size_t i=0;i<text.size();){size_t n=stem.size();bool hit=leftBoundary(text,i)&&ci(text,i,stem);
        if(!hit&&rule.hyphen&&leftBoundary(text,i)){size_t at=W(rule.stem)==U"email"?1:2;W hy=stem;hy.insert(at,U"-");if(ci(text,i,hy)){hit=true;n=hy.size();}}
        if(hit&&rule.end&&i+n<text.size()&&wc(text[i+n]))hit=false;
        if(hit){out+=rule.replacement;i+=n;}else out+=text[i++];}text=out;}
    W out;for(size_t i=0;i<text.size();){if(leftBoundary(text,i)&&text.compare(i,2,U"OK")==0&&(i+2==text.size()||!wc(text[i+2]))){out+=U"okej";i+=2;}else out+=text[i++];}return out;}
static W centuries(const W& t){W out;for(size_t i=0;i<t.size();){size_t p=i;W prep;if(leftBoundary(t,p)&&p+2<t.size()&&(t[p]==U'w'||t[p]==U'W')&&t[p+1]==U' '){prep=t.substr(p,2);p+=2;}size_t start=p;
    if(!leftBoundary(t,p)){out+=t[i++];continue;}while(p<t.size()&&W(U"IVXLC").find(t[p])!=W::npos&&p-start<7)++p;
    if(p==start||p==t.size()||!prop(UC_SPACE,t[p])){out+=t[i++];continue;}size_t romanEnd=p;while(p<t.size()&&prop(UC_SPACE,t[p]))++p;
    W noun;for(auto v:{U"wiekach",U"wiekiem",U"wieku",U"wieki",U"wiek",U"w."}){W s=v;if(t.compare(p,s.size(),s)==0&&(s==U"w."||p+s.size()==t.size()||!wc(t[p+s.size()]))){noun=s;break;}}
    if(noun.empty()){out+=t[i++];continue;}unsigned total=0,prev=0;for(size_t j=romanEnd;j-->start;){unsigned v=t[j]==U'I'?1:t[j]==U'V'?5:t[j]==U'X'?10:t[j]==U'L'?50:100;total=v<prev?total-v:total+v;prev=tvx::max(prev,v);}
    if(!total||total>=100){out+=t.substr(i,p+noun.size()-i);i=p+noun.size();continue;}size_t next=p+noun.size();if(noun==U"w.")noun=prep.empty()?U"wiek":U"wieku";int kase=noun==U"wiek"?0:!prep.empty()||noun==U"wiekiem"||noun==U"wiekach"?2:1;out+=prep+ordinal(total,kase)+U" "+noun;i=next;}return out;}
static W units(const W& text,bool pl){W out;for(size_t i=0;i<text.size();){if(digit(text[i])<0){out+=text[i++];continue;}size_t end=i;while(end<text.size()&&digit(text[end])>=0)++end;size_t fraction=end;
    if(end+1<text.size()&&(text[end]==U'.'||text[end]==U',')&&digit(text[end+1])>=0){fraction=end+2;while(fraction<text.size()&&digit(text[fraction])>=0)++fraction;}
    bool done=false;for(size_t finish:{fraction,end}){size_t at=finish;if(at<text.size()&&prop(UC_SPACE,text[at]))++at;W best;size_t bestEnd=0;const Unit* chosen=nullptr;
        if(pl){for(auto& item:PL_UNITS){W key=item.key;if(key==U"kmh")key=U"km/h";if(ci(text,at,key)){size_t q=at+key.size();if(q<text.size()&&text[q]==U'.')++q;if(q<text.size()&&(wc(text[q])||text[q]==U'°')){q=at+key.size();if(q<text.size()&&(wc(text[q])||text[q]==U'°'))continue;}if(key.size()>best.size()){best=key;bestEnd=q;chosen=&item;}}}}
        else for(auto item:TP_EN_UNITS){W key=item.first;if(ci(text,at,key)&&(at+key.size()==text.size()||(!wc(text[at+key.size()])&&text[at+key.size()]!=U'°'))&&key.size()>best.size()){best=key;bestEnd=at+key.size();}}
        if(best.empty())continue;W rawKey=textlower(text.substr(at,best.size()));if(rawKey!=best)TVOX_INVALID("original unit key unavailable");W value;if(pl){unsigned n=0;bool one=true;for(size_t k=i;k<end;++k){n=(n*10+digit(text[k]))%1000;if(k+1<end&&digit(text[k])!=0)one=false;}unsigned form=finish!=end?3:one&&n==1?0:n%10>=2&&n%10<=4&&!(n%100>=12&&n%100<=14)?1:2;value=chosen->forms[form];}else value=get(TP_EN_UNITS,rawKey);
        out+=text.substr(i,finish-i)+U" "+value;i=bestEnd;done=true;break;}
        if(!done)out+=text[i++];}return out;}
static W abbreviations(const W& text){W out;for(size_t i=0;i<text.size();){if(i&&(wc(text[i-1])||text[i-1]==U'.')){out+=text[i++];continue;}const Pair* chosen=nullptr;size_t finish=0;bool dot=false;
    for(auto& item:TP_PL_ABBREV){W key=item.first;if(!ci(text,i,key))continue;size_t q=i+key.size();bool d=q<text.size()&&text[q]==U'.';if(d)++q;if(q<text.size()&&wc(text[q])){if(!d)continue;--q;d=false;if(q<text.size()&&wc(text[q]))continue;}if(!chosen||key.size()>W(chosen->first).size()){chosen=&item;finish=q;dot=d;}}
    if(!chosen){out+=text[i++];continue;}W key=chosen->first,value=chosen->second;if(textlower(text.substr(i,key.size()))!=key)TVOX_INVALID("original abbreviation key unavailable");if(has(TP__PL_ABBREV_DOT_ONLY,key)&&!dot){out+=text.substr(i,finish-i);i=finish;continue;}
    size_t q=finish;while(q<text.size()&&prop(UC_SPACE,text[q]))++q;
    if(key==U"ok"&&(q==text.size()||digit(text[q])<0))value=dot?U"okej.":U"okej";
    else {if(prop(UC_UPPER,text[i])){W first=value.substr(0,1); // Original replacement initials are ASCII or Polish.
            for(auto entry:LOWER)if(W(entry.second)==first&&prop(UC_UPPER,entry.first[0])){value.replace(0,1,entry.first);break;}}
        if(dot&&has(TP__PL_ABBREV_END,key)&&(q==text.size()||W(U"ABCDEFGHIJKLMNOPQRSTUVWXYZĄĆĘŁŃÓŚŹŻ").find(text[q])!=W::npos))value+=U'.';}
    out+=value;i=finish;}return out;}
static W english_abbreviations(W text){for(auto item:{Pair{U"e.g.",U"for example"},Pair{U"i.e.",U"that is"},Pair{U"etc.",U"et cetera."}}){W out;for(size_t i=0;i<text.size();){if(leftBoundary(text,i)&&ci(text,i,item.first)){out+=item.second;i+=W(item.first).size();}else out+=text[i++];}text=out;}
    W out;for(size_t i=0;i<text.size();){bool found=false;for(auto item:{Pair{U"Mr.",U"mister"},Pair{U"Mrs.",U"missus"},Pair{U"Dr.",U"doctor"},Pair{U"St.",U"saint"}})if(leftBoundary(text,i)&&text.compare(i,W(item.first).size(),item.first)==0){out+=item.second;i+=W(item.first).size();found=true;break;}if(!found)out+=text[i++];}return out;}
static Words spell(const W& word,bool pl){W all;for(auto c:textlower(word)){W v=pl?get(TP_PL_LETTERS,W(1,c)):get(TP_EN_LETTERS,W(1,c));if(!v.empty())all+=v+U" ";}return whitespace(all);}
static bool shouldSpell(const W& token,bool pl){(void)pl;W letters;for(auto c:token)if(letter(c))letters+=c;if(letters.empty())return false;W low=textlower(letters);if(has(TP_WORD_ACRONYMS,low))return false;if(has(TP_ALWAYS_SPELL,low)&&token.find(U'\'')==W::npos)return true;if(letters.size()==1||has(TP_INTERJECTIONS,low))return false;
    bool upper=false;for(auto c:letters)if(prop(UC_CASED,c)){if(!prop(UC_UPPER,c)){upper=false;break;}upper=true;}if(upper)return true;return low.find_first_of(U"aąeęioóuy")==W::npos;}
struct Phrase {Words words;char32_t punctuation;};
static tvx::vector<Phrase> normalise(W text,bool pl){text=replace(replace(text,U"’",U"'"),U"‘",U"'");if(pl)text=abbreviations(units(centuries(spoken(text)),true));else text=english_abbreviations(units(text,false));
    W expanded;for(size_t i=0;i<text.size();){if(i+2<text.size()&&prop(UC_SPACE,text[i])&&text[i+1]==U'&'&&prop(UC_SPACE,text[i+2])){expanded+=pl?U" i ":U" and ";i+=3;continue;}
        if(pl&&digit(text[i])>=0){size_t q=i+1;if(q<text.size()&&prop(UC_SPACE,text[q]))++q;if(q<text.size()&&(text[q]==U'-'||text[q]==U'–')){size_t begin=++q;if(q<text.size()&&prop(UC_SPACE,text[q]))++q;if(q<text.size()&&digit(text[q])>=0){expanded+=W(1,text[i])+U" do "+text.substr(begin,q-begin+1);i=q+1;continue;}}}expanded+=text[i++];}
    text=expanded;expanded.clear();for(size_t i=0;i<text.size();++i){if(i&&i+1<text.size()&&letter(text[i-1])&&letter(text[i+1])){if(text[i]==U'-'){expanded+=U' ';continue;}if(text[i]==U'.'){expanded+=pl?U" kropka ":U" dot ";continue;}}expanded+=text[i];}text=expanded;
    Words tokens;for(size_t i=0;i<text.size();){size_t start=i;if(digit(text[i])>=0){while(i<text.size()&&digit(text[i])>=0)++i;if(i+1<text.size()&&(text[i]==U'.'||text[i]==U',')&&digit(text[i+1])>=0){i+=2;while(i<text.size()&&digit(text[i])>=0)++i;}tokens.push_back(text.substr(start,i-start));}
        else if(letter(text[i])||text[i]==U'\''||text[i]==U'́'){while(i<text.size()&&(letter(text[i])||text[i]==U'\''||text[i]==U'́'))++i;tokens.push_back(text.substr(start,i-start));}
        else {if(W(U".,!?;:-–—").find(text[i])!=W::npos)tokens.push_back(W(1,text[i]));++i;}}
    size_t count=0;for(auto token:tokens)if(!(token.size()==1&&W(U".,!?;:-–—").find(token[0])!=W::npos))++count;
    tvx::vector<Phrase> result;Words current;for(auto token:tokens){if(token.size()==1&&W(U".,!?;:-–—").find(token[0])!=W::npos){char32_t p=token[0];if(p==U'-'||p==U'–'||p==U'—')p=U',';if(!current.empty()){result.push_back({current,p});current.clear();}else if(!result.empty()&&p!=U',')result.back().punctuation=p;continue;}
        if(digit(token[0])>=0){size_t point=token.find_first_of(U".,");Words value;
            if(point!=W::npos){value=number(token.substr(0,point),pl);value.push_back(pl?U"przecinek":U"point");W frac=token.substr(point+1);if(frac.size()<=3&&frac[0]!=U'0'){auto w=number(frac,pl);value.insert(value.end(),w.begin(),w.end());}else for(auto c:frac){auto w=number(W(1,c),pl);value.insert(value.end(),w.begin(),w.end());}}
            else if(token.size()<13)value=number(token,pl);else for(auto c:token){auto w=number(W(1,c),pl);value.insert(value.end(),w.begin(),w.end());}current.insert(current.end(),value.begin(),value.end());continue;}
        // Original tok[0].isdigit also includes non-decimal superscripts;
        // Python int rejects those, so do not silently pronounce a substitute.
        if(prop(UC_DIGIT,token[0]))TVOX_INVALID("nondecimal numeric token");
        W low=textlower(token);bool one=token.size()==1&&prop(UC_ALPHA,token[0])&&(count==1||(pl?W(U"aiouwz"):W(U"ai")).find(low)==W::npos);
        if(one||shouldSpell(token,pl)){auto value=spell(token,pl);if(one&&value.empty())value.push_back(low);current.insert(current.end(),value.begin(),value.end());}else current.push_back(low);
    }if(!current.empty())result.push_back({current,U'.'});return result;}
extern "C" int tvox_normalise(const char* input,size_t bytes,int language,char* output,size_t capacity,size_t* required){
    if(!input||!required||(!output&&capacity)||bytes>65536||(language!=0&&language!=1))return -1;
    TVOX_TRY{auto phrases=normalise(decode(input,bytes),language==0);W result;for(auto p:phrases)result+=W(1,p.punctuation)+U"\t"+join(p.words)+U"\n";auto utf8=encode(result);const char* data=capacity>=utf8.size()+1?utf8.c_str():nullptr;*required=utf8.size()+1;if(capacity<*required)return 1;tvx::memcpy(output,data,*required);return 0;}TVOX_CATCH}
extern "C" int tvox_pl_text(const char* input,size_t bytes,char* output,size_t capacity,size_t* required){
    if(!input||!required||(!output&&capacity)||bytes>65536)return -1;
    TVOX_TRY {auto phrases=normalise(decode(input,bytes),true);tvx::string result;
        for(auto p:phrases){Words words=merge(p.words);tvx::vector<Words> ph;tvx::vector<Marks> marks;
            for(auto w:words){ph.push_back(phones(w));marks.push_back(stress(ph.back(),w));}phrase(words,ph,marks);
            tvx::string rows;for(size_t i=0;i<words.size();++i){Words known;for(auto value:ph[i])if(has(KNOWN_PL_PHONES,value))known.push_back(value);
                for(size_t k=0;k<known.size();++k)rows+=encode(known[k])+"\t"+tvx::to_string(k<marks[i].size()?marks[i][k]:0)+"\t"+tvx::to_string(i)+"\t"+(k+1==known.size()?"1\n":"0\n");}
            if(!rows.empty())result+="@"+encode(W(1,p.punctuation))+"\t"+tvx::to_string(words.size())+"\n"+rows;
        }const char* data=capacity>=result.size()+1?result.c_str():nullptr;*required=result.size()+1;if(capacity<*required)return 1;tvx::memcpy(output,data,*required);return 0;
    }TVOX_CATCH}
