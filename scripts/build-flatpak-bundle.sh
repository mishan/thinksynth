#!/bin/bash
# Build a self-contained .flatpak bundle of thinksynth.
#
# Usage:
#   scripts/build-flatpak-bundle.sh                 # build with defaults
#   scripts/build-flatpak-bundle.sh --clean         # wipe the build dir and
#                                                   # the local repo first
#   scripts/build-flatpak-bundle.sh -o foo.flatpak  # choose the output name
#
# This is the two commands you would otherwise type by hand:
#
#   1. flatpak-builder      builds the app into a local OSTree repo (./repo)
#                           from org.thinksynth.thinksynth.yml
#   2. flatpak build-bundle exports it from that repo into a single file
#
# The bundle deliberately does NOT contain the runtime. Including it would
# take the file from a handful of megabytes to the better part of a gigabyte,
# and every machine that installs a Flatpak can pull org.gnome.Platform from
# Flathub itself. Whoever receives the file installs it with:
#
#   flatpak remote-add --user --if-not-exists flathub \
#           https://flathub.org/repo/flathub.flatpakrepo
#   flatpak install --user thinksynth.flatpak
#   flatpak run org.thinksynth.thinksynth
#
# The first install pulls the GNOME runtime, which is around 600 MB and is
# shared with every other Flatpak they have.

set -euo pipefail

# Work from the repository root whatever directory this was invoked from.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
cd "$REPO_ROOT"

MANIFEST="org.thinksynth.thinksynth.yml"

# Parsed out of the manifest rather than repeated here, so the two cannot
# drift apart. Both are checked: an unparsed RUNTIME_VER would otherwise reach
# the `flatpak info' below as an empty version, which does not fail -- it asks
# about a ref like "org.gnome.Sdk/x86_64/" and reports it missing, so the
# script would warn that the SDK is not installed on a machine where it is.
APP_ID=$(awk '/^app-id:/ {print $2; exit}' "$MANIFEST")
RUNTIME_VER=$(awk '/^runtime-version:/ {gsub(/"/,"",$2); print $2; exit}' "$MANIFEST")

for pair in "app-id:$APP_ID" "runtime-version:$RUNTIME_VER"; do
    if [ -z "${pair#*:}" ]; then
        echo "error: could not parse ${pair%%:*} from $MANIFEST" >&2
        exit 1
    fi
done

BUILD_DIR="build-flatpak"
REPO_DIR="repo"
OUTPUT="thinksynth.flatpak"
CLEAN=0

while [ $# -gt 0 ]; do
    case "$1" in
        --clean)      CLEAN=1; shift ;;
        -o|--output)  OUTPUT="$2"; shift 2 ;;
        -h|--help)    sed -n '2,30s/^# \?//p' "$0"; exit 0 ;;
        *)
            echo "error: unknown argument: $1" >&2
            echo "use --help for usage" >&2
            exit 1
            ;;
    esac
done

if ! command -v flatpak-builder >/dev/null 2>&1; then
    echo "error: flatpak-builder is not on PATH" >&2
    echo "  Debian/Ubuntu: sudo apt install flatpak-builder" >&2
    echo "  Fedora:        sudo dnf install flatpak-builder" >&2
    echo "  Arch:          sudo pacman -S flatpak-builder" >&2
    exit 1
fi

# flatpak-builder fails on a missing SDK a few seconds in, with a message
# about refs rather than about what to install. Say it up front instead.
if ! flatpak info "org.gnome.Sdk/$(uname -m)/${RUNTIME_VER}" >/dev/null 2>&1 \
   && ! flatpak info --user "org.gnome.Sdk/$(uname -m)/${RUNTIME_VER}" \
        >/dev/null 2>&1; then
    echo "warning: org.gnome.Sdk//${RUNTIME_VER} does not look installed." >&2
    echo "         flatpak install --user flathub \\" >&2
    echo "                 org.gnome.Platform//${RUNTIME_VER} \\" >&2
    echo "                 org.gnome.Sdk//${RUNTIME_VER}" >&2
    echo >&2
fi

if [ "$CLEAN" -eq 1 ]; then
    printf 'cleaning %s/ and %s/\n' "$BUILD_DIR" "$REPO_DIR"
    rm -rf "$BUILD_DIR" "$REPO_DIR"
fi

# --user keeps everything under $XDG_DATA_HOME, so no sudo. --force-clean
# wipes the build tree each run. --disable-rofiles-fuse avoids the FUSE mount,
# which is not available inside most containers and buys nothing for a
# single-app build.
printf 'building %s into %s/\n' "$APP_ID" "$REPO_DIR"
flatpak-builder \
    --user \
    --force-clean \
    --disable-rofiles-fuse \
    --repo="$REPO_DIR" \
    "$BUILD_DIR" \
    "$MANIFEST"

printf 'exporting %s\n' "$OUTPUT"
flatpak build-bundle "$REPO_DIR" "$OUTPUT" "$APP_ID"

printf '\nbundle:  %s\nsize:    %s\nsha256:  %s\n\n' \
    "$OUTPUT" \
    "$(du -h "$OUTPUT" | cut -f1)" \
    "$(sha256sum "$OUTPUT" | cut -d' ' -f1)"
printf 'install with:\n'
printf '  flatpak install --user %s\n' "$OUTPUT"
printf '  flatpak run %s\n' "$APP_ID"
