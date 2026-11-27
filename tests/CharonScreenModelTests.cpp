// r410 host test of src/efi/CharonScreenModel.h on recorded screens.
// Each tests/charon-screens/<set>/<name>.grid is a screen as Charon's reader held it in a
// test machine (written by a HAK_CHARON_GRID_DUMP build, see CharonResident.h DumpGrid):
//   CHARON grid <rows> <columns> <cursorRow> <cursorColumn> <cursorShown> <program>
//   Grr <attribute runs n*hex,...>|<font runs>|<UTF-8 text>
//   CHARON grid end
// The test prints one summary line per screen and compares it with expected.txt
// (same folder as the sets). A screen whose line starts with '?' in expected.txt is
// a known wrong or unsure reading and is counted apart.
// Build and run:
//   clang++ -std=c++17 -O1 tests/CharonScreenModelTests.cpp -o model-tests.exe
//   model-tests.exe tests/charon-screens [--write]
#include "../src/efi/CharonScreenModel.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
using namespace hakefi::screenmodel;
namespace fs=std::filesystem;
struct Screen16 {
    static constexpr size_t stride=160,maxRows=64;
    char16_t text[maxRows][stride];uint8_t attr[maxRows][stride];
    size_t rows=0,columns=0,cursorRow=0,cursorCol=0;bool shown=false;std::u16string program;
    Screen16() {for(auto& r:text)for(auto& c:r)c=u' ';memset(attr,0,sizeof(attr));}
};
static std::u16string FromUtf8(const std::string& s) {
    std::u16string out;
    for(size_t i=0;i<s.size();) {
        const unsigned char c=(unsigned char)s[i];
        if(c<0x80) {out.push_back(c);++i;}
        else if((c&0xe0)==0xc0 && i+1<s.size()) {out.push_back(char16_t(((c&0x1f)<<6)|(s[i+1]&0x3f)));i+=2;}
        else if((c&0xf0)==0xe0 && i+2<s.size()) {out.push_back(char16_t(((c&0x0f)<<12)|((s[i+1]&0x3f)<<6)|(s[i+2]&0x3f)));i+=3;}
        else {out.push_back(u'?');++i;}
    }
    return out;
}
static std::string ToUtf8(const std::u16string& s) {
    std::string o;
    for(char16_t ch:s) {
        if(ch<0x80)o.push_back(char(ch));
        else if(ch<0x800) {o.push_back(char(0xc0|(ch>>6)));o.push_back(char(0x80|(ch&63)));}
        else {o.push_back(char(0xe0|(ch>>12)));o.push_back(char(0x80|((ch>>6)&63)));o.push_back(char(0x80|(ch&63)));}
    }
    return o;
}
static bool Load(const fs::path& path,Screen16& s) {
    std::ifstream in(path,std::ios::binary);if(!in)return false;
    std::string line;bool head=false;
    while(std::getline(in,line)) {
        if(!line.empty() && line.back()=='\r')line.pop_back();
        if(line.rfind("CHARON grid end",0)==0)break;
        if(line.rfind("CHARON grid ",0)==0) {
            std::istringstream is(line.substr(12));int shown=0;
            is>>s.rows>>s.columns>>s.cursorRow>>s.cursorCol>>shown;s.shown=shown!=0;
            std::string rest;std::getline(is,rest);while(!rest.empty() && rest[0]==' ')rest.erase(0,1);
            s.program=FromUtf8(rest);head=true;continue;
        }
        if(!head || line.size()<4 || line[0]!='G')continue;
        const size_t r=std::stoul(line.substr(1,2),nullptr,16);
        const size_t bar1=line.find('|'),bar2=line.find('|',bar1+1);
        if(r>=Screen16::maxRows || bar1==std::string::npos || bar2==std::string::npos)return false;
        size_t c=0;std::istringstream runs(line.substr(4,bar1-4));std::string run;
        while(std::getline(runs,run,',')) {const size_t star=run.find('*');const size_t n=std::stoul(run.substr(0,star));const unsigned a=std::stoul(run.substr(star+1),nullptr,16);
            for(size_t k=0;k<n && c<Screen16::stride;++k)s.attr[r][c++]=uint8_t(a);}
        const std::u16string t=FromUtf8(line.substr(bar2+1));
        for(size_t k=0;k<t.size() && k<Screen16::stride;++k)s.text[r][k]=t[k];
    }
    return head && s.rows && s.columns && s.rows<=Screen16::maxRows && s.columns<=Screen16::stride;
}
static uint8_t Majority(const Screen16& s) {
    size_t n[8]={};for(size_t r=0;r<s.rows;++r)for(size_t c=0;c<s.columns;++c)if(s.attr[r][c]&0x80)++n[(s.attr[r][c]>>4)&7];
    uint8_t best=0;for(uint8_t b=1;b<8;++b)if(n[b]>n[best])best=b;return best;
}
static const char* managerNames[]={"unknown","grub","systemd-boot","refind","limine","opencore","clover","syslinux","ipxe","windows","memdiag","shell","memtest","veracrypt","firmware"};
static const char* screenNames[]={"unknown","menu","editor","commandline","help","error","message","progress","options","pager"};
static const char* alertNames[]={"none","information","question","warning","error"};
static std::string Cells(const Screen16& s,size_t r,size_t a,size_t b) {
    std::u16string t;for(size_t c=a;c<b && c<s.columns;++c)t.push_back(s.text[r][c]);
    while(!t.empty() && t.back()==u' ')t.pop_back();return ToUtf8(t);
}
static std::string Summary(const Screen16& s,bool counting) {
    View v;v.text=&s.text[0][0];v.attr=&s.attr[0][0];v.stride=Screen16::stride;v.rows=s.rows;v.columns=s.columns;
    v.cursorRow=s.cursorRow;v.cursorCol=s.cursorCol;v.cursorShown=s.shown;v.majorityBg=Majority(s);v.countingDown=counting;
    Model m;Build(v,s.program.c_str(),m);
    std::ostringstream o;
    o<<managerNames[size_t(m.manager)]<<' '<<screenNames[size_t(m.screen)];
    if(m.titleRow!=none8)o<<" title"<<(m.titleSure?"":"?")<<"="<<int(m.titleRow);
    if(m.list.found) {
        o<<" list"<<(m.list.sure?"":"?")<<"=";
        if(m.list.selected!=none8)o<<int(m.list.selected)+1;else o<<'-';
        o<<'/'<<int(m.list.count);
        if(m.list.defaultIndex!=none8)o<<" default="<<int(m.list.defaultIndex)+1;
        if(m.list.depth)o<<" depth="<<int(m.list.depth);
        if(m.list.moreAbove)o<<" more-above";if(m.list.moreBelow)o<<" more-below";
        std::string flags;for(size_t i=0;i<m.list.count;++i) {const uint8_t f=m.list.items[i].flags;if(f&itemSubmenu)flags+=" sub"+std::to_string(i+1);if(f&itemTool)flags+=" tool"+std::to_string(i+1);if(f&itemDisabled)flags+=" off"+std::to_string(i+1);}
        o<<flags;
        if(m.list.selected!=none8 && m.list.items[m.list.selected].row!=none8) {const Item& it=m.list.items[m.list.selected];o<<" sel=\""<<Cells(s,it.row,it.left,it.right)<<"\"";}
    }
    if(m.countdown.found)o<<" countdown"<<(m.countdown.sure?"":"?")<<"="<<m.countdown.seconds;
    if(m.helpCount) {o<<" help=";for(size_t i=0;i<m.helpCount;++i)o<<(i?",":"")<<int(m.help[i]);}
    if(m.edit.found)o<<" edit"<<(m.edit.sure?"":"?")<<(m.edit.multiline?"*":"")<<"="<<int(m.edit.caretRow)<<','<<int(m.edit.caretCol)<<" text=\""<<(m.edit.multiline?std::string("..."):Cells(s,m.edit.row,m.edit.left,m.edit.right))<<"\"";
    if(m.progress.found)o<<" progress="<<int(m.progress.percent);
    if(m.message.found)o<<" message"<<(m.message.sure?"":"?")<<"="<<alertNames[size_t(m.message.kind)];
    if(m.windowCount)o<<" windows="<<int(m.windowCount);
    return o.str();
}
int main(int argc,char** argv) {
    if(argc<2) {std::fprintf(stderr,"usage: model-tests DIR [--write]\n");return 2;}
    const fs::path root=argv[1];const bool write=argc>2 && !strcmp(argv[2],"--write");
    std::vector<fs::path> files;
    for(auto& e:fs::recursive_directory_iterator(root))if(e.is_regular_file() && e.path().extension()==".grid")files.push_back(e.path());
    std::sort(files.begin(),files.end());
    std::map<std::string,std::string> expected;std::map<std::string,bool> doubtful;
    {std::ifstream in(root/"expected.txt");std::string line;
     while(std::getline(in,line)) {if(!line.empty() && line.back()=='\r')line.pop_back();if(line.empty() || line[0]=='#')continue;
        bool q=false;if(line[0]=='?') {q=true;line.erase(0,1);}
        const size_t colon=line.find(": ");if(colon==std::string::npos)continue;
        expected[line.substr(0,colon)]=line.substr(colon+2);doubtful[line.substr(0,colon)]=q;}}
    size_t passed=0,failed=0,missing=0,unsure=0,unreadable=0;std::ostringstream out;
    for(auto& f:files) {
        Screen16* s=new Screen16;
        const std::string key=fs::relative(f,root).generic_string();
        if(!Load(f,*s)) {++unreadable;std::printf("UNREADABLE %s\n",key.c_str());delete s;continue;}
        // A set's first screen ("00-start") was recorded before any key: the program may be counting down.
        const bool counting=f.filename().string().rfind("00-",0)==0;
        const std::string got=Summary(*s,counting);delete s;
        out<<(doubtful[key]?"?":"")<<key<<": "<<got<<"\n";
        auto it=expected.find(key);
        if(it==expected.end()) {++missing;if(!write)std::printf("NEW  %s: %s\n",key.c_str(),got.c_str());}
        else if(it->second!=got) {++failed;std::printf("FAIL %s\n  expected: %s\n  got:      %s\n",key.c_str(),it->second.c_str(),got.c_str());}
        else {++passed;if(doubtful[key])++unsure;}
    }
    if(write) {std::ofstream o(root/"expected.txt",std::ios::binary);o<<"# r410 screen model: expected reading of every recorded screen. '?' in front: known wrong or unsure reading.\n"<<out.str();}
    std::printf("screens %zu, passed %zu (of them marked doubtful %zu), failed %zu, without expectation %zu, unreadable %zu\n",files.size(),passed,unsure,failed,missing,unreadable);
    return failed || unreadable || (missing && !write)?1:0;
}
