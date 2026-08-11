#!/bin/sh
#
# stackcheck -- is the branch stack sound?
#
# Checks, for the whole chain listed in STACK below -- master through the node
# editor branches and on into the porting ones:
#
#   - each branch is a strict ancestor of the next, so the stack is linear
#     and every branch can be reviewed as the diff against its parent
#   - no commit anywhere in the stack has conflict markers in a source file
#   - no commit tracks a build product or an editor droppings file
#
# Building every commit is left to the caller (it is slow); this is the part
# worth running every time. Written after a session in which a rebase left
# conflict markers inside two committed files and a filter-branch silently
# duplicated thirteen commits.
#
#   sh scripts/stackcheck.sh

set -e

# Every branch above master, in order. Merged ones drop off: revive-thinksynth,
# gtkmm3-port and gtkmm4-scope are in master now, and gain-staging's three
# commits ride along inside node-editor.
#
# Keeping this current is the whole job. The contents checks run over
# master..<last entry>, so a branch missing from here is a branch nothing looks
# at -- which is how three 190K shared libraries sat in `Delete autotools'
# from the moment it was written. The tip used to be hardcoded and fell two
# branches behind; now it is the last entry, and the list itself is the thing
# that has to be kept honest.
STACK="master node-editor \
       node-editor-interaction node-editor-params node-editor-writer \
       node-editor-wires node-editor-controls node-editor-authoring \
       node-editor-attached node-editor-layout node-editor-workingcopy \
       node-editor-multiselect \
       porting-scope porting-cmake porting-cleanup porting-audio \
       porting-midi porting-macwin porting-package \
       ui-tabs patch-selector node-visualizers"

fail=0

echo "== stacking"

prev=""
for b in $STACK; do
    if ! git rev-parse --verify -q "$b" >/dev/null; then
        echo "  MISSING  $b"
        fail=1
        continue
    fi

    if [ -n "$prev" ]; then
        if git merge-base --is-ancestor "$prev" "$b"; then
            n=$(git rev-list --count "$prev".."$b")
            printf "  ok  %-24s %2d commit(s) on top of %s\n" "$b" "$n" "$prev"
        else
            printf "  NO  %-24s does not descend from %s\n" "$b" "$prev"
            fail=1
        fi
    else
        printf "  --  %-24s base\n" "$b"
    fi

    prev="$b"
done

echo "== contents"

# The tip is the last branch in STACK, not a name written out here. It was
# written out here, and it was left at node-editor-controls while three more
# branches were added on top -- so the whole contents check silently stopped
# covering the newest work, which is exactly where mistakes are. dspnew and
# dsplayout were committed as multi-megabyte binaries into that blind spot and
# this script called the stack sound the entire time.
base=$(git rev-parse master)
for tip in $STACK; do :; done

markers=0
junk=0

for c in $(git rev-list "$base".."$tip"); do
    for f in $(git ls-tree -r "$c" --name-only); do
        case "$f" in
            *.cpp|*.h|*.hh|*.yy|*.ll|*.md)
                if git show "$c:$f" 2>/dev/null |
                   grep -q '^<<<<<<< \|^>>>>>>> '; then
                    echo "  MARKERS  $(git log -1 --format=%h "$c")  $f"
                    markers=$((markers + 1))
                fi
                ;;
        esac

        case "$f" in
            *'#'*|.'#'*|*~|*.o|*.so|*.a)
                echo "  JUNK     $(git log -1 --format=%h "$c")  $f"
                junk=$((junk + 1))
                ;;
        esac
    done
done

# Extensions are not enough to find a build product. The compiled harnesses
# have no suffix at all, and two of them -- dspnew and dsplayout -- sat in the
# history as multi-megabyte blobs while this script reported the stack sound.
# What actually means "build product" is the ELF magic number, so read the
# first four bytes.
#
# Over every blob in every commit that would be tens of thousands of probes,
# so this walks unique blob hashes instead: commits in a stack share almost
# all of their trees, and a blob only has to be judged once.
echo "  ...scanning $(git rev-list --count "$base".."$tip") commits for binaries"

for c in $(git rev-list "$base".."$tip"); do
    git ls-tree -r "$c" | sed "s|^|$c |"
done | awk '{ print $1, $4, $5 }' | sort -u -k2,2 | while read -r c sha path; do
    [ "$(git cat-file -s "$sha")" -gt 4 ] || continue

    if [ "$(git cat-file blob "$sha" | dd bs=1 count=4 2>/dev/null |
            od -An -c | tr -d ' \n')" = "177ELF" ]; then
        echo "  BINARY   $(git log -1 --format=%h "$c")  $path"
    fi
done > /tmp/stackcheck-elf.$$

if [ -s /tmp/stackcheck-elf.$$ ]; then
    cat /tmp/stackcheck-elf.$$
    junk=$((junk + $(wc -l < /tmp/stackcheck-elf.$$)))
fi

rm -f /tmp/stackcheck-elf.$$

[ "$markers" = 0 ] && echo "  ok  no conflict markers in any commit"
[ "$junk" = 0 ] && echo "  ok  no build products or editor files tracked"

[ "$markers" = 0 ] || fail=1
[ "$junk" = 0 ] || fail=1

echo "== authorship"

others=$(git log --format='%ae%n%ce' "$base".."$tip" | sort -u |
         grep -v '^misha@nasledov\.com$' || true)

if [ -n "$others" ]; then
    echo "  UNEXPECTED: $others"
    fail=1
else
    echo "  ok  all misha@nasledov.com"
fi

if [ "$fail" = 0 ]; then
    echo
    echo "stack is sound"
else
    echo
    echo "stack has problems"
fi

exit $fail
