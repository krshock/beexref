#!/bin/sh
# Writes the release notes for a version to stdout (or --output FILE).
#
# The body comes from the CHANGELOG.md section for the version; when the
# section is missing it falls back to the commit log since the previous
# tag, so a release is never blocked on changelog bookkeeping.
#
# Usage: notes.sh [VERSION] [--output FILE]
set -eu
. "$(dirname "$0")/common.sh"

version=$(release_version)
output=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --output)
            [ "$#" -ge 2 ] || release_die "--output needs a file"
            output=$2
            shift 2
            ;;
        -*)
            release_die "unknown option: $1"
            ;;
        *)
            version=$1
            shift
            ;;
    esac
done

root=$(release_root)
body=$(awk -v ver="$version" '
    index($0, "## [" ver "]") == 1 { inside = 1; next }
    inside && index($0, "## [") == 1 { exit }
    inside { print }
' "$root/CHANGELOG.md")

if [ -z "$body" ]; then
    echo "notes: no CHANGELOG.md section for $version; falling back to git log" >&2
    tag="v$version"
    if git -C "$root" rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
        previous=$(git -C "$root" describe --tags --abbrev=0 "$tag^" 2>/dev/null || true)
        if [ -n "$previous" ]; then
            range="$previous..$tag"
        else
            range="$tag"
        fi
    else
        range="HEAD"
    fi
    body=$(git -C "$root" log --oneline --no-decorate -40 "$range" | sed 's/^/- /')
fi

notes=$(mktemp)
trap 'rm -f "$notes"' EXIT
{
    printf '# BeeXRef %s\n\n' "$version"
    printf '%s\n' "$body"
    printf '\n## Compatibility\n\n'
    printf -- '- `.beex` boards are unchanged: they open in BeeXRef, the Python and the Go ports.\n'
    printf -- '- Linux AppImage floor: glibc 2.31 (Ubuntu 20.04 or newer).\n'
    printf -- '- Wayland sessions run through XWayland; Always On Top is a no-op there.\n'
} > "$notes"

if [ -n "$output" ]; then
    mv "$notes" "$output"
    trap - EXIT
    echo "notes: wrote $output"
else
    cat "$notes"
fi
