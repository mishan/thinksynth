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
    "$build/scripts/dspcompile" -p "$build/plugins/" -r 32000 -c box \
        "$here/../../dsp/$p.dsp" "$name" > "$here/main/thv$name.h"
    echo "#include \"thv$name.h\"" >> "$list"
done
# Each patch scaled so one voice of any peaks at `level' (level.cpp):
# dxbell's, as it has been heard, a chord of all its voices already
# reaching the output's clamp; any louder, chords clip more.
level=0.8
echo "#define PATCHES \\" >> "$list"
for p in $patches; do
    name=$(echo "$p" | sed 's/^./\U&/')
    ${CXX:-c++} -O2 -std=gnu++17 -I"$here/main" -I"$here/../../libthink" \
        -I"$here/../../plugins" -I"$here/../../src" -include "thv$name.h" \
        -DVOICE="thv$name" "$here/level.cpp" -o "$build/boxlevel"
    gain=$("$build/boxlevel" | awk -v level=$level '{ printf "%.3ff", level / $1 }')
    echo "    PATCH($name, \"$p\", $gain) \\" >> "$list"
done
echo "" >> "$list"
