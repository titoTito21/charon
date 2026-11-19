// TrueType rasterization interface for Charon (r361). GPL-2.0-or-later.
#pragma once
#include <stddef.h>
#include "third_party/stb/stb_truetype.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct CharonTtfBox {int x0,y0,width,height;} CharonTtfBox;
// Checks the table directory against the buffer, then stbtt_InitFont.
int charon_ttf_init(stbtt_fontinfo* info,const unsigned char* data,size_t size);
// Renders one glyph's coverage (0-255) at `scale` (horizontal) and `scaleY`
// pixels per font unit,
// moved right/down by shiftX/shiftY pixels (sub-pixel position), into
// `out` (box->width*box->height bytes, row-major); box->x0/y0 are relative to
// the pen position on the baseline (y grows downwards). Returns 0 when the
// glyph does not fit `capacity` or the arena ran out.
int charon_ttf_render(const stbtt_fontinfo* info,int glyph,float scale,float scaleY,float shiftX,float shiftY,unsigned char* out,int capacity,CharonTtfBox* box);
#ifdef __cplusplus
}
#endif
