#pragma once
/* Declarations only; definitions are src/tts/runtime/CharonMemory.c. */
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* memcpy(void*,const void*,size_t);
void* memmove(void*,const void*,size_t);
void* memset(void*,int,size_t);
#ifdef __cplusplus
}
#endif
