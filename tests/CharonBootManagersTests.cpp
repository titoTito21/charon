// Host test for src/efi/CharonBootManagers.h (r361): names boot managers in
// real program images and none in other programs or in Charon itself.
//   CharonBootManagersTests FILE=EXPECTED ...   (EXPECTED "-" for none)
#include <cstdio>
#include <string>
#include <vector>
#include "../src/efi/CharonBootManagers.h"
int main(int argc,char** argv) {
    int bad=0;
    for(int i=1;i<argc;++i) {
        std::string arg=argv[i];const size_t eq=arg.rfind('=');
        const std::string path=arg.substr(0,eq),want=arg.substr(eq+1);
        FILE* f=std::fopen(path.c_str(),"rb");if(!f) {std::printf("cannot read %s\n",path.c_str());++bad;continue;}
        std::vector<unsigned char> d;int c;while((c=std::fgetc(f))!=EOF)d.push_back((unsigned char)c);std::fclose(f);
        const char16_t* name=hakefi::DetectBootManager(d.data(),d.size(),[]() {});
        std::string got="-";if(name) {got.clear();for(;*name;++name)got+=char(*name);}
        std::printf("%s %s: %s\n",got==want?"ok  ":"FAIL",path.c_str(),got.c_str());
        if(got!=want)++bad;
    }
    std::printf(bad?"FAIL\n":"PASS\n");return bad?1:0;
}
