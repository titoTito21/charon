#pragma once
#include <stddef.h>
namespace hakefi {
struct ConsoleMenuPage {size_t first=0,count=0,capacity=0;};
inline ConsoleMenuPage MenuViewport(size_t entries,size_t selected,size_t rows) {
    ConsoleMenuPage view;if(rows<7 || (entries && selected>=entries))return view;
    view.capacity=rows-6;view.first=entries ? selected/view.capacity*view.capacity : 0;
    view.count=entries-view.first;if(view.count>view.capacity)view.count=view.capacity;return view;
}
inline void ConsoleNumber(char16_t* text,size_t capacity,size_t value) {
    char16_t digits[32];size_t count=0;do {digits[count++]=char16_t(u'0'+value%10);value/=10;}while(value && count<32);
    size_t at=0;while(at+1<capacity && text[at])++at;
    while(count && at+1<capacity) {text[at++]=digits[--count];}
    text[at]=0;
}
inline void ConsoleTextChunk(const char16_t*& source,char16_t* out,size_t width) {
    size_t count=0,space=0;
    while(count<width && source[count] && source[count]!=u'\n' && source[count]!=u'\r') {
        if(source[count]==u' ')space=count;
        ++count;
    }
    if(count==width && source[count] && space)count=space;
    if(count && source[count-1]>=0xd800 && source[count-1]<=0xdbff)--count;
    for(size_t i=0;i<count;++i)out[i]=source[i];
    out[count]=0;source+=count;
    while(*source==u' ' || *source==u'\n' || *source==u'\r')++source;
}
// Template keeps firmware rendering testable without loading UEFI. One screen
// frame, no newline/scrolling output, and the last console cell stays unused.
template<class Console,class Label>
bool DrawConsoleMenu(Console* console,const char16_t* page,size_t entries,size_t selected,
    Label label,const char16_t* status,const char16_t* audio,const char16_t* keys,const char16_t* popupBody=nullptr) {
    if(!console || !console->Mode || !console->QueryMode || !console->SetAttribute
        || !console->ClearScreen || !console->SetCursorPosition || !console->OutputString)return false;
    size_t columns=0,rows=0;
    if(console->QueryMode(console,console->Mode->Mode,&columns,&rows)!=0 || columns<12 || rows<7)return false;
    const auto view=MenuViewport(entries,selected,rows);if(!view.capacity)return false;
    if(console->SetAttribute(console,7)!=0 || console->ClearScreen(console)!=0)return false;
    if(console->EnableCursor)console->EnableCursor(console,false);
    auto line=[&](size_t row,const char16_t* text,bool focus=false) {
        char16_t clipped[512];size_t length=0,limit=columns-1;if(limit>510)limit=510;
        while(length<limit && text[length]) {auto ch=text[length];clipped[length++]=(ch<32 || ch==127)?u' ':ch;}
        if(length && clipped[length-1]>=0xd800 && clipped[length-1]<=0xdbff)--length;
        while(length<limit) {clipped[length++]=u' ';}
        clipped[length]=0;
        return console->SetAttribute(console,focus?0x70:7)==0
            && console->SetCursorPosition(console,0,row)==0 && console->OutputString(console,clipped)==0;
    };
    if(!line(0,u"Charon") || !line(1,page))return false;
    if(popupBody) {
        const char16_t* cursor=popupBody;size_t width=columns-1;if(width>510)width=510;
        for(size_t i=0;i+1<view.capacity && *cursor;++i) {
            char16_t text[512]={};ConsoleTextChunk(cursor,text,width);
            if(i+2==view.capacity && *cursor) {
                size_t length=0;while(text[length])++length;
                if(length>width-3)length=width-3;
                if(length && text[length-1]>=0xd800 && text[length-1]<=0xdbff)--length;
                text[length++]=u'.';text[length++]=u'.';text[length++]=u'.';text[length]=0;
            }
            if(!line(2+i,text))return false;
        }
        char16_t button[512]={u'>',u' ',0};label(0,button+2,510);
        if(!line(2+view.capacity-1,button,true))return false;
    } else {
        if(!entries) {char16_t text[512]={};label(0,text,512);if(!line(2,text))return false;}
        for(size_t i=0;i<view.count;++i) {
            const auto index=view.first+i;char16_t text[512]={index==selected?u'>':u' ',u' ',0};
            label(index,text+2,510);if(!line(2+i,text,index==selected))return false;
        }
    }
    char16_t position[96]={};if(entries) {ConsoleNumber(position,96,selected+1);
        size_t at=0;while(position[at])++at;position[at++]=u' ';position[at++]=u'/';position[at++]=u' ';position[at]=0;
        ConsoleNumber(position,96,entries);}
    if(!line(rows-4,position) || !line(rows-3,status) || !line(rows-2,audio) || !line(rows-1,keys))return false;
    return console->SetAttribute(console,7)==0;
}
}
