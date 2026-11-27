#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds the Charon Hypervisor's RHVoice front end (charonhv-rhvoice, one static musl binary) on Alpine Linux x86_64
# from the pinned RHVoice 1.18.5 source (git 6929324d2f0486dc889aebe4e0a630d9ea30b143, the checkout Charon's UEFI
# rhvoice.tts is built from). Needs: apk add build-base boost-dev (official Alpine repository; Boost headers).
#   sh build-rhvoice.sh RHVOICE_SRC_DIR (the checkout's src/) FRONT_END.cpp OUT_DIR
# Same core file set as Charon's UEFI build (tools/build-charon-synths.py rhvoice_sources); no sonic, no packages.
# OUT_DIR gets charonhv-rhvoice and build-info.txt (compiler and musl versions, sha256 of inputs and output).
set -eu
SRC=$1; FRONT=$2; OUT=$3
[ -d "$SRC/core" ] && [ -f "$FRONT" ] || { echo "usage: build-rhvoice.sh RHVOICE_SRC FRONT_END.cpp OUT_DIR" >&2; exit 1; }
mkdir "$OUT"
CFG=$OUT/config
mkdir -p "$CFG/core"
printf '#pragma once\n#ifndef RHVOICE_GLOBAL_CONFIG_INCLUDED\n#define RHVOICE_GLOBAL_CONFIG_INCLUDED\n#define ENABLE_SONIC 0\n#define ENABLE_PKG 0\nconst char VERSION[] = "1.18.5";\nconst char CONFIG_PATH[] = "";\nconst char DATA_PATH[] = "";\n#endif\n' > "$CFG/config.h"
printf '#include "../config.h"\n' > "$CFG/core/config.h"
INC="-I$CFG -I$SRC/include -I$SRC/third-party/utf8 -I$SRC/third-party/rapidxml -I$SRC/hts_engine"
FLAGS="-O2 -ffile-prefix-map=$SRC=rhvoice -ffile-prefix-map=$OUT=out -D_GNU_SOURCE -w"
N=0
for f in "$SRC"/core/*.cpp "$SRC"/lib/lib.cpp; do
  case $(basename "$f") in emoji_data.cpp|unidata.cpp|mage_hts_engine_impl.cpp) continue;; esac
  N=$((N+1)); g++ -std=c++17 $FLAGS $INC -c "$f" -o "$OUT/r$N.o" &
  [ $((N % 4)) -eq 0 ] && wait
done
for f in "$SRC"/hts_engine/HTS_*.c; do
  N=$((N+1)); gcc -std=c11 $FLAGS $INC -c "$f" -o "$OUT/h$N.o" &
  [ $((N % 4)) -eq 0 ] && wait
done
wait
g++ -std=c++17 -O2 -Wall -Wextra -Werror $INC -c "$FRONT" -o "$OUT/front.o"
g++ -static -s -o "$OUT/charonhv-rhvoice" "$OUT/front.o" "$OUT"/r*.o "$OUT"/h*.o
rm -f "$OUT"/*.o
{
  echo "RHVoice 1.18.5 (git 6929324d2f0486dc889aebe4e0a630d9ea30b143), LGPL-2.1-or-later; front end GPL-2.0-or-later"
  echo "objects: $N"
  apk info -v 2>/dev/null | grep -E '^(gcc|g\+\+|musl-dev|musl|libstdc\+\+-dev|binutils)-[0-9]' || true
  echo "front end sha256: $(sha256sum "$FRONT" | cut -d' ' -f1)"
  echo "charonhv-rhvoice sha256: $(sha256sum "$OUT/charonhv-rhvoice" | cut -d' ' -f1)  size: $(wc -c < "$OUT/charonhv-rhvoice")"
} > "$OUT/build-info.txt"
cat "$OUT/build-info.txt"
