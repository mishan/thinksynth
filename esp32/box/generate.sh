#!/bin/sh
# The voice and its profile, from the patch, at the BOX's rate. Run from a
# thinksynth build directory's parent, after building scripts/dspcompile:
#   esp32/box/generate.sh [build]
set -e
build=${1:-build}
here=$(dirname "$0")
"$build/scripts/dspcompile" -p "$build/plugins/" -r 32000 -c box \
    "$here/../../dsp/dxbell.dsp" Dxbell > "$here/main/thvDxbell.h"
