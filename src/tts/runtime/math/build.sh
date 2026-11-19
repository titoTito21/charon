#!/bin/sh
# Build unchanged musl double-precision math with no hosted headers/libraries.
set -eu
if [ "$#" != 1 ]; then echo 'Usage: math/build.sh NEW_OUTPUT_DIRECTORY' >&2; exit 2; fi
math_source=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir "$1"
math_output=$(CDPATH= cd -- "$1" && pwd)
compiler=${CC:-cc}
case "$($compiler -dumpmachine)" in x86_64*) ;; *) echo 'This build requires x86-64' >&2; exit 2;; esac
mkdir "$math_output/include" "$math_output/include/bits"
sed -f "$math_source/musl/tools/mkalltypes.sed" \
    "$math_source/musl/arch/x86_64/bits/alltypes.h.in" \
    "$math_source/musl/include/alltypes.h.in" > "$math_output/include/bits/alltypes.h"
objects=''
while IFS= read -r source_file; do
    name=$(basename "$source_file" .c).o
    # Preserve upstream's intentional force-evaluation stores and bitwise
    # precedence expressions verbatim; neither changes the numerical algorithm.
    "$compiler" -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-but-set-variable -Wno-parentheses \
        -ffreestanding -fno-builtin -nostdinc -fno-stack-protector -fno-pic -fno-pie \
        -mcmodel=large -mno-red-zone -maccumulate-outgoing-args \
        -fno-fast-math -frounding-math -ffp-contract=off -fexcess-precision=standard \
        -fno-unwind-tables -fno-asynchronous-unwind-tables \
        -I"$math_output/include" -I"$math_source/musl/src/include" \
        -I"$math_source/musl/include" -I"$math_source/musl/src/internal" \
        -I"$math_source/musl/arch/x86_64" -I"$math_source/musl/arch/generic" \
        -c "$math_source/$source_file" -o "$math_output/$name"
    objects="$objects $name"
done < "$math_source/sources.txt"
"$compiler" -c -mno-red-zone "$math_source/fenv-no-red-zone.s" -o "$math_output/fenv.o"
objects="$objects fenv.o"
cd "$math_output"
# All names come from the fixed, versioned source manifest.
ld -r $objects -o hak-math.o
nm -u hak-math.o > undefined.txt
if [ -s undefined.txt ]; then
    echo 'Freestanding closure failed; unresolved symbols:' >&2
    cat undefined.txt >&2
    exit 1
fi
ar rcs libhak-math.a hak-math.o
echo 'PASS freestanding math object: no unresolved OS, libc or libm imports'
