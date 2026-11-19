// Bounded, read-only HII snapshot decoder. No firmware calls or allocations.
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace charonhii {
enum class Status { Ok, Invalid, Limit, UnsupportedStrings };
enum class Kind : uint8_t { Enum, Checkbox, Numeric, String, Password, Action, OrderedList, Reference, Date, Time };
enum Reason : uint32_t { ReadOnly=1, Callback=2, Conditional=4, Unsupported=8,
    Protected=16, Dynamic=32, ResetRequired=64, NoStorage=128, BoundsUnresolved=256 };
constexpr uint16_t None=0xffff;
struct FormSet { uint8_t guid[16]; uint16_t title,help; uint32_t reasons; };
struct Form { uint16_t set,id,title; uint32_t reasons; };
struct Question {
    Kind kind; uint8_t opcode,flags,detailFlags,width;
    uint16_t set,form,id,prompt,help,store,storeInfo;
    uint32_t reasons,offset;
    uint64_t minimum,maximum,step; // encoded integer bits; signedness in detailFlags
    uint16_t minLength,maxLength,targetForm,configString;
};
struct Option { uint16_t question,text; uint8_t type,flags; uint64_t value; uint32_t reasons; };
struct Store { uint16_t set,id,size; uint8_t kind,guid[16]; uint32_t attributes,nameOffset,nameBytes; };
struct String { uint16_t id; uint32_t offset,units; };
struct Model {
    static constexpr unsigned MaxSets=16,MaxForms=128,MaxQuestions=512,MaxOptions=2048,MaxStores=64,MaxStrings=2048;
    FormSet sets[MaxSets];Form forms[MaxForms];Question questions[MaxQuestions];
    Option options[MaxOptions];Store stores[MaxStores];String strings[MaxStrings];
    uint16_t setCount=0,formCount=0,questionCount=0,optionCount=0,storeCount=0,stringCount=0;
    const uint8_t* snapshot=nullptr;size_t bytes=0;
    uint32_t globalReasons=0;
    bool stringsAvailable=false,unsupportedStrings=false;
    void Clear();
    const String* FindString(uint16_t id) const;
    // Copies UTF16 code units including trailing NUL. Query returns required
    // units including NUL, 0 means unresolved ID. No partial destination writes.
    size_t CopyString(uint16_t id,uint16_t* destination,size_t capacity) const;
};
// Exactly one exported package list, bounded to 4MiB. Caller retains immutable
// bytes and HII-handle/generation identity. language is exact ASCII tag, max63.
// Model invalidated on any malformed/capacity failure; never publish partial
// results. Unsupported compressed strings preserve IFR model but no partial
// string-ID table. No parsed question implies permission to read/write values.
Status Parse(const uint8_t* bytes,size_t size,const char* language,Model& out);
}
