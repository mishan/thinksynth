#!/bin/sh
#
# Put the release version into the package file names.
#
# CPack names its output from the project version:
#
#     thinksynth-0.1-Darwin-arm64.dmg
#
# which is the right shape and the wrong number for a release -- every build
# of every tag would be called 0.1, and a snapshot would be indistinguishable
# from a release. So the version field is replaced with the tag being built,
# or with snapshot-<short sha> when there is no tag:
#
#     thinksynth-v0.2-Darwin-arm64.dmg
#     thinksynth-snapshot-1a2b3c4-Darwin-arm64.dmg
#
# Usage: name-artifacts.sh <directory of cpack output>
#
# Called from .github/workflows/release.yml on all three platforms, which is
# why it is /bin/sh and not bash: it runs under MSYS2 as well.

set -eu

dir="${1:?usage: name-artifacts.sh <package directory>}"

case "${GITHUB_REF:-}" in
    refs/tags/*) ver="${GITHUB_REF#refs/tags/}" ;;
    *)           ver="snapshot-$(echo "${GITHUB_SHA:-unknown}" | cut -c1-7)" ;;
esac

renamed=0

for path in "$dir"/thinksynth-*; do
    [ -f "$path" ] || continue

    name="$(basename "$path")"

    # thinksynth-<version>-<rest>. Only the first two fields are touched;
    # <rest> is CPack's system and processor, which are already right and are
    # the part that tells two macOS packages apart.
    rest="${name#thinksynth-}"
    rest="${rest#*-}"

    new="thinksynth-${ver}-${rest}"

    [ "$name" = "$new" ] && continue

    mv "$path" "$dir/$new"
    echo "$name -> $new"

    renamed=$((renamed + 1))
done

if [ "$renamed" -eq 0 ]; then
    echo "name-artifacts.sh: no packages found in $dir" >&2
    ls -l "$dir" >&2
    exit 1
fi

# A tag that disagrees with the version compiled into the binary is worth
# saying out loud: `thinksynth -h' will report the project() version whatever
# the file is called. Not fatal -- the tagged release is a draft, so there is
# a chance to look -- but it should not pass unremarked either.
case "$ver" in
    snapshot-*) ;;
    *)
        v="${ver#v}"

        if ! grep -q "VERSION ${v}\$" CMakeLists.txt 2>/dev/null; then
            echo "warning: tag $ver does not match project(VERSION) in" \
                 "CMakeLists.txt; the binary will report the latter" >&2
        fi
        ;;
esac
