#!/bin/sh
# Shared helpers for the release scripts in this directory. Source it:
#
#   . "$(dirname "$0")/common.sh"
#
# The scripts are POSIX sh (like tools/package_windows.sh) and find the
# repository through their own location, so they work from anywhere.

set -eu

release_die()
{
    echo "release: $*" >&2
    exit 1
}

# The repository root. $0 is the calling script when sourced, so this
# resolves through tools/release/.
release_root()
{
    git -C "$(dirname -- "$0")/../.." rev-parse --show-toplevel 2>/dev/null \
        || release_die "tools/release must be run from a git checkout"
}

# The version from CMakeLists.txt, the single source of truth
# (constants::Version is compiled from the same value).
release_version()
{
    version=$(awk '/^project\(BeeXRef/ { seen = 1; next }
                   seen && /^[[:space:]]+VERSION[[:space:]]/ { print $2; exit }' \
        "$(release_root)/CMakeLists.txt")
    [ -n "$version" ] || release_die "cannot read the version from CMakeLists.txt"
    printf '%s\n' "$version"
}

# The artifact directory, created on demand.
release_dist()
{
    dist="$(release_root)/dist"
    [ -d "$dist" ] || mkdir -p "$dist"
    printf '%s\n' "$dist"
}

release_require_clean_tree()
{
    [ -z "$(git -C "$(release_root)" status --porcelain)" ] \
        || release_die "working tree is dirty; commit or stash first"
}

# HEAD must carry the annotated tag v<version>.
release_require_tag()
{
    version=$(release_version)
    tag=$(git -C "$(release_root)" describe --tags --exact-match HEAD 2>/dev/null) \
        || release_die "HEAD is not tagged; expected v$version"
    [ "$tag" = "v$version" ] \
        || release_die "HEAD is tagged $tag but CMakeLists.txt says $version"
}

release_sha256()
{
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1"
    else
        shasum -a 256 "$1"
    fi
}

# (Re)writes dist/SHA256SUMS over every artifact there, so the file can
# be verified with `sha256sum -c` from dist/.
release_checksums()
{
    dist=$(release_dist)
    (
        cd "$dist"
        find . -maxdepth 1 -type f \
            \( -name '*.AppImage' -o -name '*.zip' -o -name '*.tar.gz' \) \
            | sed 's|^\./||' | LC_ALL=C sort \
            | while IFS= read -r file; do
                release_sha256 "$file"
            done
    ) > "$dist/SHA256SUMS"
    [ -s "$dist/SHA256SUMS" ] || release_die "no artifacts in $dist"
}
