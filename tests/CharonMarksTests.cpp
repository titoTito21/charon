// r397 host test: selection marks (MarkRow / LeadMark / TrailMark) and Speakup copy.
// CharonSystemTools.h (r409 clock.txt) uses efi_file_protocol before CharonResident.h includes its header.
#include <efi/protocol/file.h>
#define private public
#include "../src/efi/CharonResident.h"
#undef private
#include <cstdio>
#include <initializer_list>
namespace hakefi {inline void Utf8(const char16_t*,char* out,size_t capacity) {if(capacity)out[0]=0;}}
using namespace hakefi;
static int failures=0;
static void Check(bool ok,const char* what) {std::printf("%s %s\n",ok?"PASS":"FAIL",what);if(!ok)++failures;}
static ResidentReader* reader=new ResidentReader();
static void Screen(std::initializer_list<const char16_t*> rows) {
    auto& r=*reader;r.rows_=25;r.columns_=80;
    for(size_t y=0;y<25;++y)for(size_t x=0;x<80;++x) {r.text_[y][x]=u' ';r.attr_[y][x]=0x87;}
    size_t y=0;for(auto* s:rows) {for(size_t x=0;s[x] && x<80;++x)r.text_[y][x]=s[x];++y;}
}
int main() {
    auto& r=*reader;
    Screen({u"  Boot menu",u"",u"  -> Debian GNU/Linux",u"     Windows",u"     UEFI Shell"});
    Check(r.MarkRow(2) && !r.MarkRow(3),"'-> ' marks the selection");
    Check(r.TextStart(2)==5 && r.TextStart(3)==5,"text after '-> ' starts like the others");
    Screen({u"  => Fedora",u"     Arch",u"     Memtest"});Check(r.MarkRow(0),"'=> ' marks the selection");
    Screen({u"  \u00bb Fedora",u"    Arch",u"    Memtest"});Check(r.MarkRow(0),"'» ' marks the selection");
    Screen({u"  Fedora   <",u"  Arch",u"  Memtest"});Check(r.MarkRow(0) && !r.MarkRow(1),"trailing '<' marks the selection");
    Screen({u"  -> Fedora",u"  -> Arch",u"  -> Memtest"});Check(!r.MarkRow(0) && !r.MarkRow(1),"every row marked: a list, no selection");
    Screen({u"(*) UEFI only",u"( ) Legacy"});Check(!r.MarkRow(0),"radio button is no selection mark");
    Screen({u"  -> alone"});Check(!r.MarkRow(0),"no aligned neighbour: no menu");
    std::printf(failures?"FAILED %d\n":"ALL PASSED\n",failures);
    return failures?1:0;
}
