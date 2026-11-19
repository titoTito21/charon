#!/bin/sh
set -eu
if [ "$#" != 1 ]; then echo 'Usage: math/test.sh BUILT_OUTPUT_DIRECTORY' >&2; exit 2; fi
math_source=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
math_output=$(CDPATH= cd -- "$1" && pwd)
# Rename the SAME freestanding object, rather than test a hosted rebuild.
# This isolates every internal symbol from the host reference math library.
objcopy --prefix-symbols=hak_test_ "$math_output/hak-math.o" "$math_output/hak-math-test.o"
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -fno-fast-math \
    -frounding-math -ffp-contract=off "$math_source/numeric-test.c" \
    "$math_output/hak-math-test.o" -lm -o "$math_output/numeric-test"
"$math_output/numeric-test"
