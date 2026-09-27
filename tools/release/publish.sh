#!/bin/sh
# Creates the GitHub release for the version in CMakeLists.txt (draft by
# default; --publish makes it public) and uploads the dist/ artifacts
# plus SHA256SUMS. The tag must exist and point at HEAD.
#
# Usage: publish.sh [--publish]
set -eu
. "$(dirname "$0")/common.sh"

publish=false
while [ "$#" -gt 0 ]; do
    case "$1" in
        --publish)
            publish=true
            shift
            ;;
        *)
            release_die "unknown option: $1"
            ;;
    esac
done

command -v gh >/dev/null 2>&1 || release_die "gh (GitHub CLI) is not installed"
gh auth status >/dev/null 2>&1 || release_die "gh is not authenticated (run: gh auth login)"
release_require_clean_tree
release_require_tag

version=$(release_version)
dist=$(release_dist)
release_checksums

notes="$dist/notes.md"
"$(dirname "$0")/notes.sh" "$version" --output "$notes"
{
    printf '\n## Checksums\n\n```\n'
    cat "$dist/SHA256SUMS"
    printf '```\n'
} >> "$notes"

# The artifacts to attach: everything but the notes.
set -- $(find "$dist" -maxdepth 1 -type f \
    \( -name '*.AppImage' -o -name '*.zip' -o -name 'SHA256SUMS' \) | LC_ALL=C sort)
[ "$#" -gt 0 ] || release_die "no artifacts in $dist"

tag="v$version"
if gh release view "$tag" >/dev/null 2>&1; then
    gh release upload "$tag" --clobber "$@"
    echo "publish: uploaded artifacts to the existing release $tag"
elif [ "$publish" = true ]; then
    gh release create "$tag" --title "BeeXRef $version" --notes-file "$notes" "$@"
else
    gh release create "$tag" --draft --title "BeeXRef $version" --notes-file "$notes" "$@"
    echo "publish: draft created; review it on GitHub and press Publish"
fi
