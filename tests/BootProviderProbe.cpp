// Verify actual kit mapping in a running process, independently of focus.
#include <OS.h>
#include <image.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    char* path=realpath(argv[2],nullptr);
    if(!path)return 3;
    image_info info;int32 cookie=0;int count=0;
    while(get_next_image_info(atoi(argv[1]),&cookie,&info)==B_OK) {
        if(!strcmp(info.name,path)){++count;printf("MAPPED %s\n",info.name);}
    }
    free(path);
    if(count!=1)return 4;
    puts("PASS exactly one kit image in service process");
    return 0;
}
