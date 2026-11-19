/* MIT-0. Arena-owned value containers for the TVox frontend.
 * All destructors are trivial: failure can leave the algorithm through the
 * explicit boundary without skipping C++ cleanup. Arena releases everything.
 * No standard-library allocation, exceptions, locale or UTF8 approximations.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <initializer_list>
#include <type_traits>
#include <new>
#ifdef _MSC_VER
#include <math.h>
#else
extern "C" {double floor(double);double fmod(double,double);double cos(double);
double exp(double);double log(double);double pow(double,double);double sqrt(double);}
#endif
extern void* tvox_value_allocate(size_t);
extern void tvox_value_checkpoint();
[[noreturn]] extern void tvox_value_fail(int);
namespace tvx {
using ::floor;using ::fmod;using ::cos;using ::exp;using ::log;using ::pow;using ::sqrt;
inline bool isfinite(double value){static_assert(sizeof(value)==sizeof(uint64_t),"IEEE754 binary64 required");uint64_t bits=0;auto out=(unsigned char*)&bits;auto in=(const unsigned char*)&value;for(size_t i=0;i<sizeof(bits);++i)out[i]=in[i];return (bits&UINT64_C(0x7ff0000000000000))!=UINT64_C(0x7ff0000000000000);}
inline size_t strlen(const char* p){size_t n=0;while(p[n]){if(!(n%64))tvox_value_checkpoint();++n;}return n;}
inline void* memcpy(void* to,const void* from,size_t count){auto a=(unsigned char*)to;auto b=(const unsigned char*)from;for(size_t i=0;i<count;++i)a[i]=b[i];return to;}
template<class T>const T& min(const T& a,const T& b){return b<a?b:a;}
template<class T>const T& max(const T& a,const T& b){return a<b?b:a;}
template<class T,size_t N>T* begin(T(&v)[N]){return v;}
template<class T,size_t N>T* end(T(&v)[N]){return v+N;}
template<class It,class V>It find(It a,It b,const V& v){size_t work=0;while(a!=b&& !(*a==v)){if(!(work++%64))tvox_value_checkpoint();++a;}return a;}
template<class It,class V>size_t count(It a,It b,const V& v){size_t n=0;size_t work=0;for(;a!=b;++a){if(!(work++%64))tvox_value_checkpoint();if(*a==v)++n;}return n;}
template<class It,class V>void fill(It a,It b,const V& v){size_t work=0;for(;a!=b;++a){if(!(work++%64))tvox_value_checkpoint();*a=v;}}
template<class It,class V,class Compare>It lower_bound(It a,It b,const V& v,Compare cmp){tvox_value_checkpoint();size_t n=size_t(b-a);while(n){size_t half=n/2;It mid=a+half;if(cmp(*mid,v)){a=mid+1;n-=half+1;}else n=half;}return a;}
template<class It,class V,class Compare>It upper_bound(It a,It b,const V& v,Compare cmp){tvox_value_checkpoint();size_t n=size_t(b-a);while(n){size_t half=n/2;It mid=a+half;if(!cmp(v,*mid)){a=mid+1;n-=half+1;}else n=half;}return a;}
template<class T,size_t N>struct array {T elements[N];T& operator[](size_t i){return elements[i];}const T& operator[](size_t i)const{return elements[i];}T* begin(){return elements;}const T* begin()const{return elements;}T* end(){return elements+N;}const T* end()const{return elements+N;}};
template<class T> class vector {
    static_assert(std::is_trivially_destructible<T>::value,"arena elements must have trivial destructors");
    T* p=nullptr;size_t n=0,cap=0;
    void reserve(size_t need){if(need<=cap)return;if(need>SIZE_MAX/sizeof(T))tvox_value_fail(-2);size_t next=cap?cap*2:4;if(next<need||next<cap||next>SIZE_MAX/sizeof(T))next=need;T* q=(T*)tvox_value_allocate(next*sizeof(T));for(size_t i=0;i<n;++i){if(!(i%64))tvox_value_checkpoint();new(q+i)T(p[i]);}p=q;cap=next;}
public:
    vector()=default;
    vector(size_t count):vector(count,T()){}
    vector(size_t count,const T& value){reserve(count);for(size_t i=0;i<count;++i){if(!(i%64))tvox_value_checkpoint();new(p+i)T(value);}n=count;}
    vector(std::initializer_list<T> list){reserve(list.size());for(const auto& value:list)new(p+n++)T(value);}
    vector(const vector& other){reserve(other.n);for(auto& value:other){if(!(n%64))tvox_value_checkpoint();new(p+n++)T(value);}}
    vector(vector&& other) noexcept:p(other.p),n(other.n),cap(other.cap){other.p=nullptr;other.n=other.cap=0;}
    vector& operator=(const vector& other){if(this!=&other){vector copy(other);swap(copy);}return *this;}
    vector& operator=(vector&& other) noexcept {p=other.p;n=other.n;cap=other.cap;other.p=nullptr;other.n=other.cap=0;return *this;}
    void swap(vector& other){auto a=p;p=other.p;other.p=a;auto b=n;n=other.n;other.n=b;b=cap;cap=other.cap;other.cap=b;}
    size_t size()const{return n;}bool empty()const{return !n;}
    T* begin(){return p;}const T* begin()const{return p;}
    T* end(){return p?p+n:p;}const T* end()const{return p?p+n:p;}
    struct reverse {const T* ptr;const T& operator*()const{return ptr[-1];}reverse& operator++(){--ptr;return *this;}bool operator!=(reverse other)const{return ptr!=other.ptr;}};
    reverse rbegin()const{return {end()};}reverse rend()const{return {begin()};}
    T& operator[](size_t i){return p[i];}const T& operator[](size_t i)const{return p[i];}
    T& front(){return p[0];}const T& front()const{return p[0];}
    T& back(){return p[n-1];}const T& back()const{return p[n-1];}
    void clear(){n=0;}void pop_back(){--n;}
    void push_back(const T& value){if(!(n%64))tvox_value_checkpoint();T copy(value);reserve(n+1);new(p+n++)T(static_cast<T&&>(copy));}
    template<class... Args>void emplace_back(Args&&... args){reserve(n+1);new(p+n++)T(static_cast<Args&&>(args)...);}
    template<class It>void insert(T* at,It first,It last){size_t index=p?size_t(at-p):0;vector extra;for(;first!=last;++first)extra.push_back(*first);reserve(n+extra.n);for(size_t i=n;i>index;--i){if(!(i%64))tvox_value_checkpoint();new(p+i+extra.n-1)T(p[i-1]);}for(size_t i=0;i<extra.n;++i){if(!(i%64))tvox_value_checkpoint();new(p+index+i)T(extra[i]);}n+=extra.n;}
};
template<class C>class basic_string {
    vector<C> chars;
    static size_t length(const C* p){size_t n=0;while(p[n]){if(!(n%64))tvox_value_checkpoint();++n;}return n;}
public:
    static constexpr size_t npos=SIZE_MAX;
    basic_string()=default;
    basic_string(const C* s){for(size_t i=0;s[i];++i)chars.push_back(s[i]);}
    basic_string(size_t n,C c):chars(n,c){}
    basic_string(const C* s,size_t n){for(size_t i=0;i<n;++i)chars.push_back(s[i]);}
    size_t size()const{return chars.size();}bool empty()const{return chars.empty();}
    C* begin(){return chars.begin();}const C* begin()const{return chars.begin();}
    C* end(){return chars.end();}const C* end()const{return chars.end();}
    C& operator[](size_t i){return chars[i];}const C& operator[](size_t i)const{return chars[i];}
    C& back(){return chars.back();}const C& back()const{return chars.back();}
    void clear(){chars.clear();}void pop_back(){chars.pop_back();}
    basic_string& operator+=(C c){chars.push_back(c);return *this;}
    basic_string& operator+=(const basic_string& other){size_t n=other.size();for(size_t i=0;i<n;++i)chars.push_back(other[i]);return *this;}
    basic_string& operator+=(const C* s){return *this+=basic_string(s);}
    basic_string substr(size_t pos=0,size_t count=npos)const{if(pos>size())tvox_value_fail(-1);count=min(count,size()-pos);return basic_string(begin()?begin()+pos:nullptr,count);}
    int compare(size_t pos,size_t count,const basic_string& other)const{if(pos>size())tvox_value_fail(-1);count=min(count,size()-pos);for(size_t i=0;i<min(count,other.size());++i){if(!(i%64))tvox_value_checkpoint();if(chars[pos+i]!=other[i])return chars[pos+i]<other[i]?-1:1;}return count==other.size()?0:count<other.size()?-1:1;}
    size_t find(const basic_string& other,size_t pos=0)const{if(pos>size()||other.size()>size()-pos)return npos;for(size_t i=pos;i<=size()-other.size();++i)if(compare(i,other.size(),other)==0)return i;return npos;}
    size_t find(C c,size_t pos=0)const{for(size_t i=pos;i<size();++i){if(!(i%64))tvox_value_checkpoint();if(chars[i]==c)return i;}return npos;}
    size_t find_first_of(const basic_string& set,size_t pos=0)const{for(size_t i=pos;i<size();++i)if(set.find(chars[i])!=npos)return i;return npos;}
    basic_string& replace(size_t pos,size_t count,const basic_string& other){basic_string result=substr(0,pos);result+=other;result+=substr(pos+min(count,size()-pos));*this=static_cast<basic_string&&>(result);return *this;}
    basic_string& insert(size_t pos,const basic_string& other){return replace(pos,0,other);}
    const C* c_str()const{C* p=(C*)tvox_value_allocate((size()+1)*sizeof(C));for(size_t i=0;i<size();++i){if(!(i%64))tvox_value_checkpoint();p[i]=chars[i];}p[size()]=0;return p;}
    friend bool operator==(const basic_string& a,const basic_string& b){return a.compare(0,a.size(),b)==0;}
    friend bool operator!=(const basic_string& a,const basic_string& b){return !(a==b);}
    friend bool operator<(const basic_string& a,const basic_string& b){return a.compare(0,a.size(),b)<0;}
    friend basic_string operator+(basic_string a,const basic_string& b){a+=b;return a;}
};
using u32string=basic_string<char32_t>;using string=basic_string<char>;
inline string to_string(size_t n){char buf[32];size_t i=sizeof(buf);do{buf[--i]=char('0'+n%10);n/=10;}while(n);return string(buf+i,sizeof(buf)-i);}
static_assert(std::is_trivially_destructible<u32string>::value,"no unwinding cleanup");
static_assert(std::is_trivially_destructible<vector<u32string>>::value,"no nested cleanup");
}
