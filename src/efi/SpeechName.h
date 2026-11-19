// Same byte-wise cache identity as HAK's native TVox boot cache.
#pragma once
#include <stdint.h>
namespace hakaudioefi {
inline bool SpeechPath(const char* language,const char* utf8,char16_t (&path)[64]) {
    if(!language || !*language || !utf8 || !*utf8)return false;
    uint64_t hash=14695981039346656037ULL;
    const char* parts[]={language,"\n175/50/100\n",utf8};
    for(const auto* part:parts)
        for(const auto* at=(const uint8_t*)part;*at;++at)hash=(hash^*at)*1099511628211ULL;
    const char16_t prefix[]=u"\\EFI\\HAK\\SPEECH\\";
    unsigned n=0;for(;prefix[n];++n)path[n]=prefix[n];
    const char16_t hex[]=u"0123456789abcdef";
    for(unsigned i=0;i<16;++i)path[n++]=hex[(hash>>(60-4*i))&15];
    for(const auto c:u".pcm")path[n++]=c;
    return true;
}
}
