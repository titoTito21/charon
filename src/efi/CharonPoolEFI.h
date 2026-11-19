#pragma once
#include <efi/system-table.h>
#include "../tts/runtime/CharonPool.h"

namespace charon {
// r397: a reserve for speech when the firmware has no memory left. Windows
// Memory Diagnostic takes all free memory for its test; every utterance then
// failed with EFI_OUT_OF_RESOURCES and the reader was mute in that tool. The
// reserve is taken once at start and used only after AllocatePool fails:
// a first-fit list of blocks, neighbours joined on release.
struct SpeechReserve {
    struct Block {size_t size;size_t used;};   // size of the payload after this header
    static constexpr size_t bytes=24u*1024u*1024u;
    uint8_t* base=nullptr;
    bool Inside(const void* p) const {return base && static_cast<const uint8_t*>(p)>=base && static_cast<const uint8_t*>(p)<base+bytes;}
    void Start(efi_boot_services* boot) {
        if(base || !boot)return;
        void* p=nullptr;
        if(boot->AllocatePool(EfiLoaderData,bytes,&p)!=EFI_SUCCESS || !p)return;
        base=static_cast<uint8_t*>(p);
        Block* first=reinterpret_cast<Block*>(base);first->size=bytes-sizeof(Block);first->used=0;
    }
    void* Take(size_t want) {
        if(!base || !want)return nullptr;
        want=(want+15)&~size_t(15);
        for(uint8_t* at=base;at<base+bytes;) {
            Block* b=reinterpret_cast<Block*>(at);
            if(!b->used) {
                // join the free blocks that follow
                for(;;) {uint8_t* next=at+sizeof(Block)+b->size;if(next>=base+bytes)break;Block* n=reinterpret_cast<Block*>(next);if(n->used)break;b->size+=sizeof(Block)+n->size;}
                if(b->size>=want) {
                    if(b->size>=want+sizeof(Block)+64) {
                        Block* rest=reinterpret_cast<Block*>(at+sizeof(Block)+want);
                        rest->size=b->size-want-sizeof(Block);rest->used=0;b->size=want;
                    }
                    b->used=1;return at+sizeof(Block);
                }
            }
            at+=sizeof(Block)+b->size;
        }
        return nullptr;
    }
    void Give(void* p) {reinterpret_cast<Block*>(static_cast<uint8_t*>(p)-sizeof(Block))->used=0;}
};
inline SpeechReserve speechReserve;
inline void* AllocateSpeechPool(void* context,size_t bytes) {
    void* pointer=nullptr;
    auto* boot=static_cast<efi_boot_services*>(context);
    if(boot->AllocatePool(EfiLoaderData,bytes,&pointer)==EFI_SUCCESS && pointer)return pointer;
    return speechReserve.Take(bytes);
}
inline void ReleaseSpeechPool(void* context,void* pointer) {
    if(speechReserve.Inside(pointer)) {speechReserve.Give(pointer);return;}
    static_cast<efi_boot_services*>(context)->FreePool(pointer);
}
inline bool InitializeSpeechPool(CharonPool& pool,efi_boot_services* boot,size_t limit) {
    speechReserve.Start(boot);
    return boot && charon_pool_init(&pool,boot,AllocateSpeechPool,ReleaseSpeechPool,limit);
}
}
