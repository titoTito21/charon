/* Exact g2p_en.py rules, composition and stress. MIT-0. */
struct ENRule {int left;S letters;int right;S output;};
#include "tvox_en_data.inc"
struct ENPhones {Words phones;Marks marks;};
static Words enArpa(const W& s){return s.empty()?Words{}:split(s,U' ');}
static Words enDictionary(const W& w){auto it=tvx::lower_bound(tvx::begin(EN_DICT),tvx::end(EN_DICT),w,[](Pair p,const W& x){return W(p.first)<x;});return it!=tvx::end(EN_DICT)&&w==it->first?enArpa(it->second):Words{};}
static bool enVowel(char32_t c){return W(U"aeiouy").find(c)!=W::npos;}
static bool enConsonant(char32_t c){return W(U"bcdfghjklmnpqrstvwxz").find(c)!=W::npos;}
static bool enLeft(const W& w,size_t i,int kind){switch(kind){case 0:return true;case 1:return i&&w[i-1]==U' ';case 2:return i&&enConsonant(w[i-1]);case 3:return i>=2&&enVowel(w[i-2]);case 4:return (i&&w[i-1]==U' ')||(i>=2&&w[i-2]==U' '&&enConsonant(w[i-1]));case 5:return i&&enVowel(w[i-1]);}return false;}
static bool enRight(const W& w,size_t i,int kind){switch(kind){case 0:return true;case 1:return i<w.size()&&w[i]==U' ';case 2:return i<w.size()&&w[i]==U't';case 3:return i+2<w.size()&&enConsonant(w[i])&&w[i+1]==U'e'&&w[i+2]==U' ';case 4:return i<w.size()&&enVowel(w[i]);case 5:return i<w.size()&&W(U"eiy").find(w[i])!=W::npos;}return false;}
static Words enRules(const W& word){W w=U" ";for(auto c:textlower(word))if(c>=U'a'&&c<=U'z')w+=c;w+=U' ';Words result;
    for(size_t i=1;i+1<w.size();){bool found=false;for(auto rule:EN_RULES){W letters=rule.letters;if(w.compare(i,letters.size(),letters)!=0||!enLeft(w,i,rule.left)||!enRight(w,i+letters.size(),rule.right))continue;auto out=enArpa(rule.output);result.insert(result.end(),out.begin(),out.end());i+=letters.size();found=true;break;}if(!found)++i;}
    bool seen=false;for(auto& p:result)if(ends(p,U"1")){if(seen)p.back()=U'0';seen=true;}return result;
}
static W enBase(W p){while(!p.empty()&&p.back()>=U'0'&&p.back()<=U'9')p.pop_back();return p;}
static Words enPlural(Words p){W last=p.empty()?W{}:enBase(p.back());if(has(EN_SIBILANT,last)){p.push_back(U"IH0");p.push_back(U"Z");}else p.push_back(has(EN_VOICELESS,last)?U"S":U"Z");return p;}
static Words enPast(Words p){W last=p.empty()?W{}:enBase(p.back());if(last==U"T"||last==U"D"){p.push_back(U"IH0");p.push_back(U"D");}else p.push_back(has(EN_VOICELESS,last)?U"T":U"D");return p;}
static Words enDemote(Words p){for(auto& a:p)if(ends(a,U"1"))a.back()=U'2';return p;}
static Words enLook(const Words& candidates){for(auto c:candidates){W changed=c;if(ends(c,U"ise"))changed=c.substr(0,c.size()-3)+U"iz"+W(1,1);else if(ends(c,U"is"))changed=c.substr(0,c.size()-2)+U"iz"+W(1,1);
        // Preserve source's literal U+0001 replacement, not an invented fix.
        for(auto v:Words{c,changed,replace(c,U"isat",U"izat")})if(v.size()>=2){auto found=enDictionary(v);if(!found.empty())return found;}}
    return {};
}
static Words enStems(const W& w){Words found;if(ends(w,U"es")&&w.size()>=3&&W(U"sxzh").find(w[w.size()-3])!=W::npos){found=enLook({w.substr(0,w.size()-2)});if(!found.empty())return enPlural(found);}
    if(ends(w,U"s")&&!ends(w,U"ss")){found=enLook({w.substr(0,w.size()-1)});if(!found.empty())return enPlural(found);}
    if(ends(w,U"ed")){W doubled=w.size()>4&&w[w.size()-3]==w[w.size()-4]?w.substr(0,w.size()-3):W{};found=enLook({w.substr(0,w.size()-2),w.substr(0,w.size()-1),doubled});if(!found.empty())return enPast(found);}
    for(auto suffix:EN_SUFFIXES){W key=suffix.first;if(ends(w,key)&&w.size()>key.size()+1){W stem=w.substr(0,w.size()-key.size());W doubled=stem.size()>2&&stem.back()==stem[stem.size()-2]?stem.substr(0,stem.size()-1):W{};
        found=enLook({stem,stem+U"e",doubled,ends(stem,U"i")?stem.substr(0,stem.size()-1)+U"y":W{}});if(found.empty())continue;auto tail=enArpa(suffix.second);bool own=false;for(auto p:tail)if(ends(p,U"1"))own=true;if(own)found=enDemote(found);found.insert(found.end(),tail.begin(),tail.end());return found;}}
    return {};
}
static Words enCompose(const W& w,unsigned depth=0){auto found=enDictionary(w);if(!found.empty())return found;if(depth>2||w.size()<4)return {};
    const S endings[]={U"e",U"ed",U"es",U"ing",U"er",U"ers",U"ation",U"ations"};
    for(auto ending:endings){W tail=W(U"is")+ending;if(ends(w,tail)){found=enDictionary(w.substr(0,w.size()-tail.size())+U"iz"+ending);if(!found.empty())return found;break;}}
    found=enStems(w);if(!found.empty())return found;
    for(auto prefix:EN_PREFIXES){W key=prefix.first;if(starts(w,key)&&w.size()-key.size()>=3){auto rest=enCompose(w.substr(key.size()),depth+1);if(!rest.empty()){found=enArpa(prefix.second);found.insert(found.end(),rest.begin(),rest.end());return found;}}}
    for(size_t k=w.size()-3;k>2;--k){auto head=enDictionary(w.substr(0,k));if(head.empty())continue;auto rest=enCompose(w.substr(k),depth+1);if(!rest.empty()){rest=enDemote(rest);head.insert(head.end(),rest.begin(),rest.end());return head;}}
    if(!depth&&w.size()>4){if(ends(w,U"ed")){bool silent=!enVowel(w[w.size()-3])&&enVowel(w[w.size()-4]);found=enRules(w.substr(0,w.size()-(silent?1:2)));if(!found.empty())return enPast(found);}
        if(ends(w,U"s")&&!ends(w,U"ss")&&!ends(w,U"us")&&!ends(w,U"is")){found=enRules(w.substr(0,w.size()-1));if(!found.empty())return enPlural(found);}}
    return {};
}
static ENPhones enPhones(const W& word){W w=textlower(word);while(!w.empty()&&w[0]==U'\'')w=w.substr(1);while(!w.empty()&&w.back()==U'\'')w.pop_back();Words arpa=enArpa(get(EN_SAID,w));if(arpa.empty())arpa=enDictionary(w);
    if(arpa.empty()&&ends(w,U"'s")){arpa=enDictionary(w.substr(0,w.size()-2));if(!arpa.empty())arpa.push_back(U"Z");}
    if(arpa.empty())arpa=enCompose(w);if(arpa.empty())arpa=enRules(w);ENPhones result;bool weak=has(EN_FUNCTION_WORDS,w);
    for(auto a:arpa){size_t n=0;while(n<a.size()&&a[n]>=U'A'&&a[n]<=U'Z')++n;W base=a.substr(0,n);char32_t stress=n<a.size()?a[n]:0;
        W phone=base==U"AH"&&stress==U'0'?W(U"ə"):base==U"ER"&&stress==U'0'?W(U"ɚ"):get(EN_ARPA,base);if(phone.empty())continue;result.phones.push_back(phone);result.marks.push_back(weak?0:stress==U'1'?2:stress==U'2'?1:0);}
    return result;
}
extern "C" int tvox_en_analyze(const char* input,size_t bytes,char* output,size_t capacity,size_t* required){
    if(!input||!required||(!output&&capacity)||bytes>65536)return -1;
    TVOX_TRY {auto value=enPhones(decode(input,bytes));W result;for(size_t i=0;i<value.phones.size();++i){if(i)result+=U' ';result+=value.phones[i];}result+=U'\t';for(int m:value.marks)result+=char32_t(U'0'+m);result+=U'\n';auto utf8=encode(result);const char* data=capacity>=utf8.size()+1?utf8.c_str():nullptr;*required=utf8.size()+1;if(capacity<*required)return 1;tvx::memcpy(output,data,*required);return 0;}TVOX_CATCH
}
