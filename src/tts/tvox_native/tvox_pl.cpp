/* Exact source translation of Titan Voice g2p_pl.py word/stress/phrase rules.
 * MIT-0; see COPYING and PL-PROVENANCE.json.
 * Shared C++17 algorithm: hosted standard values or explicit arena values.
 */
#ifdef TVOX_FREESTANDING
#include "tvox_value.h"
#define TVOX_TRY
#define TVOX_CATCH
#define TVOX_INVALID(message) tvox_value_fail(-1)
#define TVOX_CHECKPOINT() tvox_value_checkpoint()
#else
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <cstdint>
#include <cstring>
namespace tvx=std;
#define TVOX_TRY try
#define TVOX_CATCH catch(const std::invalid_argument&){return -1;}catch(...){return -2;}
#define TVOX_INVALID(message) throw std::invalid_argument(message)
#define TVOX_CHECKPOINT() ((void)0)
#endif
#include "tvox_pl.h"
using S=const char32_t*;using W=tvx::u32string;using Words=tvx::vector<W>;
using Marks=tvx::vector<int>;
struct Pair {S first,second;};
#include "tvox_pl_data.inc"
template<size_t N> static bool has(const S(&a)[N],const W& s){for(auto x:a)if(s==x)return true;return false;}
template<size_t N> static W get(const Pair(&a)[N],const W& s){for(auto x:a)if(s==x.first)return x.second;return {};}
static bool starts(const W& s,const W& prefix){return s.compare(0,prefix.size(),prefix)==0;}
static bool ends(const W& s,const W& tail){return s.size()>=tail.size() && s.compare(s.size()-tail.size(),tail.size(),tail)==0;}
template<size_t N> static bool startsAny(const W& s,const S(&a)[N]){for(auto x:a)if(starts(s,x))return true;return false;}
template<size_t N> static bool endsAny(const W& s,const S(&a)[N]){for(auto x:a)if(ends(s,x))return true;return false;}
static Words split(const W& s,char32_t sep){Words r;size_t p=0;for(;;){auto q=s.find(sep,p);r.push_back(s.substr(p,q-p));if(q==W::npos)break;p=q+1;}return r;}
static W lower(const W& s){W r;for(char32_t c:s){auto it=tvx::lower_bound(tvx::begin(LOWER),tvx::end(LOWER),c,[](Pair p,char32_t c){return p.first[0]<c;});if(it!=tvx::end(LOWER)&&it->first[0]==c)r+=it->second;else r+=c;}return r;}
static W base(W s){while(!s.empty()&&(s.back()==U'R'||s.back()==U'W'))s.pop_back();return s;}
static W replace(W s,const W& a,const W& b){size_t p=0;while((p=s.find(a,p))!=W::npos){s.replace(p,a.size(),b);p+=b.size();}return s;}
static W mapped(const Pair* a,size_t n,const W& s){for(size_t i=0;i<n;++i)if(s==a[i].first)return a[i].second;return s;}
template<size_t N> static W mapping(const Pair(&a)[N],const W& s){return mapped(a,N,s);}
static Words nasal(const W& vowel,const W& next){
    if(next==U"p"||next==U"b")return {vowel,U"m"};
    if(next==U"t"||next==U"d"||next==U"ts"||next==U"dz"||next==U"tʂ"||next==U"dʐ")return {vowel,U"n"};
    if(next==U"tɕ"||next==U"dʑ")return {vowel,U"ɲ"};
    if(next==U"k"||next==U"g"||next==U"x")return {vowel,U"ŋ"};
    if(next==U"l"||next==U"w")return {vowel};
    return {vowel,U"w̃"};
}
static Words phones(const W& word){
    W lw=lower(word);
    if(word.find_first_of(U"|+")==W::npos){auto it=tvx::lower_bound(tvx::begin(LEXICON),tvx::end(LEXICON),lw,[](Pair p,const W& w){return W(p.first)<w;});if(it!=tvx::end(LEXICON)&&lw==it->first&&*it->second)return split(it->second,U' ');}
    W w;for(auto c:lw)if(has(LETTERS,W(1,c))||c==U'|'||c==U'+')w+=c;
    if(w.empty())return {};
    if(starts(w,U"bezi")||starts(w,U"dezi"))w.insert(3,U"|");
    for(auto stem:RZ_SPLIT)if(w.find(stem)!=W::npos)w=replace(w,stem,replace(stem,U"rz",U"r|z"));
    bool dip=starts(w,U"au")||starts(w,U"eu");
    for(auto stem:DIPHTHONG_STEMS)if(W(stem)!=U"au"&&W(stem)!=U"eu"&&w.find(stem)!=W::npos)dip=true;
    Words out;
    for(size_t i=0;i<w.size();){char32_t c=w[i];bool matched=false;
        // SOFT_I insertion order already has the sole two-letter key first.
        for(auto item:SOFT_I){W g=item.first;if(w.compare(i,g.size()+1,g+U"i")==0){size_t j=i+g.size()+1;out.push_back(item.second);if(!(j<w.size()&&has(VOW_LET,W(1,w[j]))))out.push_back(U"i");i=j;matched=true;break;}}
        if(matched)continue;
        for(auto item:DIGRAPHS){W g=item.first;if(w.compare(i,g.size(),g)==0){out.push_back(item.second);i+=g.size();matched=true;break;}}
        if(matched)continue;
        if(c==U'x'){out.push_back(U"k");out.push_back(U"s");}
        else if(c==U'ą'||c==U'ę')out.push_back(c==U'ą'?U"Ą":U"Ę");
        else if(c==U'i')out.push_back(!out.empty()&&!has(VOWELS,out.back())&&i+1<w.size()&&has(VOW_LET,W(1,w[i+1]))?U"j":U"i");
        else if(c==U'+')out.push_back(U"#");
        else if(c==U'n'&&i+1<w.size()&&(w[i+1]==U'k'||w[i+1]==U'g'))out.push_back(U"ŋ");
        else {W p=get(SINGLE,W(1,c));if(!p.empty())out.push_back(p);}
        ++i;
    }
    if(ends(w,U"ł")&&out.size()>=2&&out.back()==U"w"){
        W p=out[out.size()-2];if(p==U"p"||p==U"b"||p==U"t"||p==U"d"||p==U"k"||p==U"g")out.pop_back();}
    for(size_t i=0;i+1<out.size();++i)if(out[i]==U"n"&&(out[i+1]==U"tɕ"||out[i+1]==U"dʑ"))out[i]=U"ɲ";
    if(dip)for(size_t i=1;i<out.size();++i)if(out[i]==U"u"&&(out[i-1]==U"a"||out[i-1]==U"ɛ")&&(i+1==out.size()||!has(VOWELS,out[i+1])))out[i]=U"w";
    Words seq;
    for(size_t i=0;i<out.size();++i){W p=out[i];if(p==U"Ą"||p==U"Ę"){
        W next=i+1<out.size()?out[i+1]:U"";if(next==U"#")next.clear();next=base(next);
        Words ns=(p==U"Ę"&&next.empty())?Words{U"ɛ"}:nasal(p==U"Ą"?U"ɔ":U"ɛ",next);
        seq.insert(seq.end(),ns.begin(),ns.end());
    }else if(p!=U"#")seq.push_back(p);}
    for(size_t i=seq.size();i-->0;){W p=base(seq[i]);if(!has(OBSTRUENTS,p))continue;W newp=p;
        if(i+1==seq.size())newp=mapping(VOICELESS_OF,p);
        else {W next=seq[i+1],nb=base(next);if(has(OBSTRUENTS,nb)&&(!(ends(next,U"R")||ends(next,U"W"))||!get(VOICED_OF,nb).empty()))newp=!get(VOICELESS_OF,nb).empty()?mapping(VOICED_OF,p):mapping(VOICELESS_OF,p);}
        seq[i]=newp+seq[i].substr(p.size());
    }
    for(size_t i=1;i<seq.size();++i)if(ends(seq[i],U"R")||ends(seq[i],U"W")){W prev=base(seq[i-1]);if(prev==U"r"&&i>=2)prev=base(seq[i-2]);if(!get(VOICED_OF,prev).empty()||prev==U"x")seq[i]=mapping(VOICELESS_OF,base(seq[i]));}
    for(auto& p:seq)p=base(p);return seq;
}
static bool antepenult(const W& w){
    if(has(ANTEPENULT_WORDS,w)||endsAny(w,VERB_ANTEPENULT))return true;
    if(has(YKA_PENULT,w))return false;
    if(ends(w,U"yce"))return true;
    if(ends(w,U"ice"))return startsAny(w,YKA_LEARNED_NIK)||startsAny(w,YKA_LEARNED_ICE);
    if(!endsAny(w,YKA_ENDINGS))return false;
    if(endsAny(w,YKA_GENITIVE))return startsAny(w,YKA_LEARNED_NIK);
    return true;
}
static Marks stress(const Words& ph,const W& word){
    tvx::vector<size_t> idx;for(size_t i=0;i<ph.size();++i)if(has(VOWELS,ph[i]))idx.push_back(i);
    Marks marks(ph.size(),0);if(idx.empty())return marks;W lw=lower(word);
    if(lw.find(U'+')!=W::npos){W host=split(lw,U'+')[0];Words hp=phones(host);Marks hm=stress(hp,host);size_t k=0;for(size_t i=0;i<hp.size()&&k<idx.size();++i)if(has(VOWELS,hp[i]))marks[idx[k++]]=hm[i];if(tvx::find(marks.begin(),marks.end(),2)==marks.end()&&has(STRONG_FUNCTION,host))marks[idx.size()>=2?idx[idx.size()-2]:idx.back()]=2;return marks;}
    if(has(FUNCTION_WORDS,lw))return marks;
    Words parts=split(lw,U'|');W last=parts.back();size_t vowels=0;for(auto p:phones(last))if(has(VOWELS,p))++vowels;
    if(parts.size()>1&&parts[parts.size()-2]!=U"nie"&&!has(PREP_TAKES_STRESS,last)&&!has(FUNCTION_WORDS,last)&&vowels==1){marks[idx.back()]=2;return marks;}
    size_t main=idx.size()>=4&&endsAny(last,VERB_4TH)?idx[idx.size()-4]:idx.size()>=3&&antepenult(last)?idx[idx.size()-3]:idx.size()>=2?idx[idx.size()-2]:idx.back();
    marks[main]=2;if(idx.size()>=4&&idx[0]!=main&&idx[1]!=main)marks[idx[0]]=1;return marks;
}
static size_t spelled(const W& text){W w=lower(text);size_t n=0;for(size_t i=0;i<w.size();++i)if(W(U"aąeęioóuy").find(w[i])!=W::npos&&!(w[i]==U'i'&&i+1<w.size()&&W(U"aąeęoóuy").find(w[i+1])!=W::npos))++n;return n;}
static Words merge(Words input){Words pro,out;
    for(size_t i=0;i<input.size();++i){W w=input[i];while(has(PROCLITICS,split(lower(w),U'|').back())&&i+1<input.size()&&tvx::count(w.begin(),w.end(),U'|')<2)w+=U"|"+input[++i];pro.push_back(w);}
    for(auto w:pro){W lw=lower(w);if(!out.empty()&&has(WEAK_AFTER_MONO,lw)&&out.back().find_first_of(U"+|")==W::npos&&!has(NOT_HOSTS,lower(out.back()))&&spelled(out.back())==1)out.back()+=U"+"+w;
        else if(!out.empty()&&has(ENCLITICS,lw)&&tvx::count(out.back().begin(),out.back().end(),U'+')<3)out.back()+=U"+"+w;else out.push_back(w);}
    return out;
}
static void phrase(const Words& words,const tvx::vector<Words>& ph,tvx::vector<Marks>& marks){
    auto nsyl=[&](size_t i){size_t n=0;for(auto p:ph[i])if(has(VOWELS,p))++n;return n;};
    auto main=[&](size_t i,int level){tvx::fill(marks[i].begin(),marks[i].end(),0);for(size_t j=0;j<ph[i].size();++j)if(has(VOWELS,ph[i][j])){marks[i][j]=level;break;}};
    if(words.empty())return;size_t n=words.size();if(has(STRESSED_ALONE,lower(words.back()))&&nsyl(n-1))main(n-1,2);
    bool any=false;for(auto m:marks)if(tvx::find(m.begin(),m.end(),2)!=m.end())any=true;
    if(!any)for(size_t i=n;i-->0;)if(nsyl(i)){tvx::vector<size_t> v;for(size_t j=0;j<ph[i].size();++j)if(has(VOWELS,ph[i][j]))v.push_back(j);marks[i][v.size()>=2?v[v.size()-2]:v.back()]=2;break;}
    for(size_t i=0;i+1<n;++i)if(nsyl(i)==1&&tvx::find(marks[i].begin(),marks[i].end(),2)!=marks[i].end()&&has(WEAK_BEFORE_STRESS,lower(words[i]))&&tvx::find(marks[i+1].begin(),marks[i+1].end(),2)!=marks[i+1].end())main(i,1);
}
static W decode(const char* p,size_t n){W r;for(size_t i=0;i<n;){uint32_t c=(unsigned char)p[i++];unsigned count=0;uint32_t minimum=0;
    if(c<128){}else if(c>=0xc2&&c<=0xdf){c&=31;count=1;minimum=0x80;}else if(c>=0xe0&&c<=0xef){c&=15;count=2;minimum=0x800;}else if(c>=0xf0&&c<=0xf4){c&=7;count=3;minimum=0x10000;}else TVOX_INVALID("UTF-8");
    while(count--){if(i==n||((unsigned char)p[i]&0xc0)!=0x80)TVOX_INVALID("UTF-8");c=(c<<6)|((unsigned char)p[i++]&63);}if(c<minimum||c>0x10ffff||(c>=0xd800&&c<=0xdfff)||c==0)TVOX_INVALID("UTF-8");r+=(char32_t)c;}return r;}
static tvx::string encode(const W& w){tvx::string r;for(uint32_t c:w){if(c<128)r+=(char)c;else if(c<2048){r+=(char)(0xc0|(c>>6));r+=(char)(0x80|(c&63));}else if(c<65536){r+=(char)(0xe0|(c>>12));r+=(char)(0x80|((c>>6)&63));r+=(char)(0x80|(c&63));}else {r+=(char)(0xf0|(c>>18));r+=(char)(0x80|((c>>12)&63));r+=(char)(0x80|((c>>6)&63));r+=(char)(0x80|(c&63));}}return r;}
/* Input: one word for mode0; newline-separated pre-normalized phrase for mode1.
 * Output: one row per resulting word: original spelling<TAB>phones<TAB>marks\n.
 * No normalization or tokenization is implied. Invalid input/oversize -> -1;
 * insufficient output -> 1 (output untouched); allocation/internal error -> -2. */
extern "C" int tvox_pl_analyze(const char* input,size_t bytes,int mode,char* output,size_t capacity,size_t* required){
    if(!input||!required||(!output&&capacity)||bytes>65536||(mode!=0&&mode!=1))return -1;
    TVOX_TRY {W text=decode(input,bytes);if(mode==0&&text.find_first_of(U"\n\t\r")!=W::npos)return -1;
        Words words=mode==0?Words{text}:merge(text.empty()?Words{}:split(text,U'\n'));
        if(words.size()>1024)return -1;for(auto w:words)if(w.size()>1024||w.find_first_of(U"\t\r")!=W::npos)return -1;
        tvx::vector<Words> ph;tvx::vector<Marks> marks;for(auto w:words){ph.push_back(phones(w));marks.push_back(stress(ph.back(),w));}if(mode==1)phrase(words,ph,marks);
        W result;for(size_t i=0;i<words.size();++i){result+=words[i]+U"\t";for(size_t j=0;j<ph[i].size();++j){if(j)result+=U' ';result+=ph[i][j];}result+=U'\t';for(int m:marks[i])result+=(char32_t)(U'0'+m);result+=U'\n';}
        tvx::string utf8=encode(result);const char* data=capacity>=utf8.size()+1?utf8.c_str():nullptr;*required=utf8.size()+1;if(capacity<*required)return 1;tvx::memcpy(output,data,*required);return 0;
    }TVOX_CATCH
}
#include "tvox_number_data.inc"
#include "tvox_textproc.inc"
#include "tvox_en.inc"
#include "tvox_acoustic.inc"
