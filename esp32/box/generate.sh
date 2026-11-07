#!/bin/sh
# The voices and their profiles, from the patches, at the BOX's rate, and
# the list the firmware steps through. Run from a thinksynth build
# directory's parent, after building scripts/dspcompile:
#   esp32/box/generate.sh [build]
set -e
build=${1:-build}
here=$(dirname "$0")
patches="dxbell grab index rhodes pluck clav glass stylophone"
list="$here/main/patches.h"

echo "/* The patches the BOX steps through. Written by generate.sh. */" > "$list"
for p in $patches; do
    name=$(echo "$p" | sed 's/^./\U&/')
    "$build/scripts/dspcompile" -p "$build/plugins/" -r 32000 -c box,marbles \
        "$here/../../dsp/$p.dsp" "$name" > "$here/main/thv$name.h"
    echo "#include \"thv$name.h\"" >> "$list"
done
# The percussion a game's walls play, on channel 2.
"$build/scripts/dspcompile" -p "$build/plugins/" -r 32000 -c box,marbles \
    "$here/../../dsp/clave.dsp" Clave > "$here/main/thvClave.h"
echo "#include \"thvClave.h\"" >> "$list"
echo "#define PERCUSSION Clave" >> "$list"
# Each patch scaled so one voice of any peaks at `level' (level.cpp):
# dxbell's, as it has been heard, a chord of all its voices already
# reaching the output's clamp; any louder, chords clip more. The
# percussion too.
level=0.8
gain () {
    ${CXX:-c++} -O2 -std=gnu++17 -I"$here/main" -I"$here/../../libthink" \
        -I"$here/../../plugins" -I"$here/../../src" -include "thv$1.h" \
        -DVOICE="thv$1" "$here/level.cpp" -o "$build/boxlevel"
    "$build/boxlevel" | awk -v level=$level '{ printf "%.3ff", level / $1 }'
}
g=$(gain Clave)
echo "#define PERCUSSION_GAIN $g" >> "$list"
echo "#define PATCHES \\" >> "$list"
for p in $patches; do
    name=$(echo "$p" | sed 's/^./\U&/')
    g=$(gain "$name")
    echo "    PATCH($name, \"$p\", $g) \\" >> "$list"
done
echo "" >> "$list"
