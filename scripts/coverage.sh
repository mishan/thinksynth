#!/bin/sh
# Line and branch coverage of the native tree under ctest.
#
#   scripts/coverage.sh [BUILD_DIR]
#
# Builds BUILD_DIR (build-coverage by default) with THINK_COVERAGE, runs
# every ctest gate, and writes BUILD_DIR/coverage/index.html, one page per
# source file, and summary.txt. The harnesses themselves are left out: a
# line of gencheck.cpp run is not a line of the engine tested.
#
# -O0, so that a line the report calls unrun was not merely inlined away.
# An audio loop runs some lines billions of times, which gcovr takes for
# gcov's old overflow bug; those are warned about, not fatal. libthink is
# compiled three times (shared, static, embedded) under different macros,
# so one function can start on two lines; each is reported.
# Exit status is ctest's; the report is written either way.

set -eu

top=$(cd "$(dirname "$0")/.." && pwd)
build=${1:-$top/build-coverage}

cmake -S "$top" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DTHINK_COVERAGE=ON
cmake --build "$build"

# Counts add up across runs; a report is of this one.
find "$build" -name '*.gcda' -delete

status=0
ctest --test-dir "$build" --output-on-failure --timeout 900 -j "$(nproc)" ||
    status=$?

mkdir -p "$build/coverage"
gcovr --root "$top" --object-directory "$build" \
      --exclude "$top/scripts/" --exclude "$build/" \
      --gcov-ignore-parse-errors=suspicious_hits.warn_once_per_file \
      --merge-mode-functions=separate \
      --html-details "$build/coverage/index.html" \
      --txt "$build/coverage/summary.txt" \
      --print-summary -j "$(nproc)"

exit $status
