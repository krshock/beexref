#!/bin/sh
# Starts a release: bumps the version in CMakeLists.txt and moves the
# CHANGELOG's Unreleased section under the new version.
#
# Without --commit it only edits and prints the commands to run; with
# --commit it also makes the release commit and the annotated tag, as
# AGENTS.md describes (never tag from a dirty tree).
#
# Usage: prepare.sh X.Y.Z [--date YYYY-MM-DD] [--commit]
set -eu
. "$(dirname "$0")/common.sh"

[ "$#" -ge 1 ] || release_die "usage: prepare.sh X.Y.Z [--date YYYY-MM-DD] [--commit]"
new=$1
shift
date=$(date +%F)
commit=false
while [ "$#" -gt 0 ]; do
    case "$1" in
        --date)
            [ "$#" -ge 2 ] || release_die "--date needs a value"
            date=$2
            shift 2
            ;;
        --commit)
            commit=true
            shift
            ;;
        *)
            release_die "unknown option: $1"
            ;;
    esac
done

printf '%s' "$new" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' \
    || release_die "version must be MAJOR.MINOR.PATCH, got '$new'"

release_require_clean_tree
root=$(release_root)
old=$(release_version)
[ "$new" != "$old" ] || release_die "already at $old"
if [ "$(printf '%s\n%s\n' "$old" "$new" | sort -V | tail -1)" != "$new" ]; then
    release_die "$new is not newer than $old"
fi
if git -C "$root" rev-parse -q --verify "refs/tags/v$new" >/dev/null; then
    release_die "tag v$new already exists"
fi

# Validate the changelog before touching anything.
changelog="$root/CHANGELOG.md"
[ -f "$changelog" ] || release_die "CHANGELOG.md is missing"
grep -q '^## \[Unreleased\]$' "$changelog" \
    || release_die "CHANGELOG.md has no [Unreleased] section"

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

# 1. The version in CMakeLists.txt.
sed "s/^\([[:space:]]*VERSION[[:space:]]*\)$old\$/\1$new/" "$root/CMakeLists.txt" > "$tmp"
mv "$tmp" "$root/CMakeLists.txt"
[ "$(release_version)" = "$new" ] || release_die "could not update the version in CMakeLists.txt"

# 2. The Unreleased notes become this release's notes.
awk -v ver="$new" -v date="$date" '
    { print }
    /^## \[Unreleased\]$/ && !inserted {
        printf "\n## [%s] - %s\n", ver, date
        inserted = 1
    }
' "$changelog" > "$tmp"
mv "$tmp" "$changelog"

# 3. The compare links at the bottom.
awk -v old="$old" -v new="$new" '
    /^\[Unreleased\]: / {
        base = $0
        sub(/^\[Unreleased\]: /, "", base)   # the URL
        sub(/v[0-9][^ ]*$/, "", base)        # keep up to and including ".../compare/"
        print "[Unreleased]: " base "v" new "...HEAD"
        print "[" new "]: " base "v" old "...v" new
        next
    }
    { print }
' "$changelog" > "$tmp"
mv "$tmp" "$changelog"

if [ "$commit" = true ]; then
    git -C "$root" add -A
    git -C "$root" commit -m "Release v$new"
    git -C "$root" tag -a "v$new" -m "BeeXRef $new"
    echo "prepare: committed and tagged v$new"
else
    echo "prepare: CMakeLists.txt $old -> $new, CHANGELOG.md updated"
    echo "next:"
    echo "  git add -A && git commit -m \"Release v$new\" && git tag -a v$new -m \"BeeXRef $new\""
fi
