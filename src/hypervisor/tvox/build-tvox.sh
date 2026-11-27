#!/bin/sh
# SPDX-License-Identifier: MIT-0
# Builds charonhv-tvox (Charon's TVox for the Charon Hypervisor, one static musl binary) on Alpine Linux x86_64
# from Charon's own src/tts, the same units the UEFI build links (tools/build-charon-windows.py): the TVox frontend,
# its context switch, the pool, the DSP stream and DSP, LiveVoice and CharonMemory.c. Needs: apk add build-base.
# CharonMemory.c (the firmware's own memset/memcpy/memmove/memcmp/strlen) is compiled as its header asks:
# -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns, so GCC -O2 cannot turn its loops into calls to
# themselves (r410 session 2 measured that recursion as a segfault when it was built with plain -O2).
#   sh build-tvox.sh CHARON_SRC_TTS_DIR FRONT_END.cpp OUT_DIR   (src/tts with src/efi/CharonFormatting.h beside it, as in the tree)
set -eu
TTS=$1; FRONT=$2; OUT=$3
[ -f "$TTS/runtime/CharonLiveVoice.cpp" ] && [ -f "$FRONT" ] || { echo "usage: build-tvox.sh SRC_TTS FRONT_END.cpp OUT_DIR" >&2; exit 1; }
mkdir "$OUT"
N=$TTS/tvox_native; R=$TTS/runtime
FLAGS="-O2 -ffile-prefix-map=$TTS=tts -ffunction-sections -fdata-sections"
INC="-I$N -I$R"
g++ -std=c++17 $FLAGS -fno-exceptions -fno-rtti $INC -c "$N/tvox_freestanding.cpp" -o "$OUT/frontend.o" &
gcc $FLAGS -c "$N/tvox_context_x64.S" -o "$OUT/context.o" &
gcc -std=c11 $FLAGS $INC -c "$R/CharonPool.c" -o "$OUT/pool.o" &
gcc -std=c11 $FLAGS -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -c "$R/CharonMemory.c" -o "$OUT/memory.o" &
wait
gcc -std=c11 $FLAGS $INC -c "$N/tvox_dsp_stream.c" -o "$OUT/stream.o" &
gcc -std=c11 $FLAGS $INC -include stddef.h -include stdlib.h -c "$N/tvox_dsp_mit.c" -o "$OUT/dsp.o" &
g++ -std=c++17 $FLAGS -fno-exceptions -fno-rtti $INC -c "$R/CharonLiveVoice.cpp" -o "$OUT/live.o" &
g++ -std=c++17 -O2 -Wall -Wextra -Werror -isystem "$N" -isystem "$R" -c "$FRONT" -o "$OUT/front.o" &
wait
g++ -static -s -Wl,--gc-sections -o "$OUT/charonhv-tvox" "$OUT"/front.o "$OUT"/live.o "$OUT"/frontend.o "$OUT"/context.o \
    "$OUT"/pool.o "$OUT"/memory.o "$OUT"/stream.o "$OUT"/dsp.o -lm
rm -f "$OUT"/*.o
{
  echo "TVox (Charon src/tts, MIT-0) hosted on Alpine: charonhv-tvox"
  apk info -v 2>/dev/null | grep -E '^(gcc|g\+\+|musl-dev|musl|libstdc\+\+-dev|binutils)-[0-9]' || true
  echo "front end sha256: $(sha256sum "$FRONT" | cut -d' ' -f1)"
  echo "charonhv-tvox sha256: $(sha256sum "$OUT/charonhv-tvox" | cut -d' ' -f1)  size: $(wc -c < "$OUT/charonhv-tvox")"
} > "$OUT/build-info.txt"
cat "$OUT/build-info.txt"
