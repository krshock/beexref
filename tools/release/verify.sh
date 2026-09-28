#!/bin/sh
# Checks the release artifacts in dist/ (or the files given):
#
#   * AppImage: extracts it, checks the library closure, the image
#     plugins boards need, and that it runs and reports the version;
#   * zip: lists the essentials (executable, platform plugin, image
#     formats) for a Windows machine to run;
#   * SHA256SUMS: verifies it when verifying all of dist/ (a build
#     script checking the artifact it just wrote names it explicitly, so
#     it never trips on a checksums file that still describes the
#     previous build).
#
# Usage: verify.sh [artifact...]
set -eu
. "$(dirname "$0")/common.sh"

dist=$(release_dist)
if [ "$#" -gt 0 ]; then
    # Resolve the named artifacts once: the AppImage check extracts from
    # a different directory, so a relative path would no longer resolve.
    artifacts=""
    for artifact in "$@"; do
        case "$artifact" in
            /*) ;;
            *) artifact="$PWD/$artifact" ;;
        esac
        artifacts="$artifacts $artifact"
    done
else
    artifacts=$(find "$dist" -maxdepth 1 -type f \( -name '*.AppImage' -o -name '*.zip' \) | sort)
fi
[ -n "$artifacts" ] || release_die "no artifacts to verify (build one first)"

version=$(release_version)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
failed=0

fail()
{
    echo "  FAIL: $*" >&2
    failed=1
}

check_appimage()
{
    image=$1
    echo "verify: $image"
    work="$tmp/appimage"
    rm -rf "$work"
    mkdir -p "$work"
    ( cd "$work" && "$image" --appimage-extract >/dev/null 2>&1 ) \
        || { fail "could not extract the AppImage"; return; }
    root="$work/squashfs-root"

    # Nothing the bundle loads may be missing.
    missing=$(find "$root" -type f \( -name '*.so*' -o -name beexref \) -print0 \
        | xargs -0 -r ldd 2>/dev/null | grep 'not found' || true)
    [ -z "$missing" ] || fail "missing libraries: $(printf '%s' "$missing" | head -3)"

    # The image plugins boards need (a package once shipped without them
    # and JPEG/WebP items stayed empty).
    find "$root" -name 'libqjpeg*' | grep -q . || fail "no JPEG image plugin"
    find "$root" -name 'libqwebp*' | grep -q . || fail "no WebP image plugin"

    # It must start and report the version.
    apprun="$root/AppRun"
    [ -x "$apprun" ] || apprun="$root/usr/bin/beexref"
    output=$(QT_QPA_PLATFORM=offscreen "$apprun" --version 2>/dev/null) || output=""
    if [ -z "$output" ] && command -v xvfb-run >/dev/null 2>&1; then
        output=$(xvfb-run -a "$apprun" --version 2>/dev/null) || output=""
    fi
    case "$output" in
        *"$version"*) echo "  runs and reports $version" ;;
        *) fail "could not run --version (got '${output:-nothing}')" ;;
    esac
}

check_zip()
{
    zip=$1
    echo "verify: $zip"
    before=$failed
    list=$(unzip -l "$zip" 2>/dev/null) || { fail "could not read the zip"; return; }
    printf '%s\n' "$list" | grep -q 'beexref\.exe' || fail "beexref.exe is missing"
    printf '%s\n' "$list" | grep -q 'platforms/qwindows\.dll' \
        || fail "platforms/qwindows.dll is missing"
    printf '%s\n' "$list" | grep -q 'imageformats/.*\.dll' \
        || fail "no image format plugins"
    if [ "$failed" = "$before" ]; then
        echo "  contains the executable, the platform plugin and image formats"
    fi
}

for artifact in $artifacts; do
    case "$artifact" in
        *.AppImage) check_appimage "$artifact" ;;
        *.zip) check_zip "$artifact" ;;
        *) echo "verify: skipping $artifact" ;;
    esac
done

if [ "$#" -eq 0 ] && [ -f "$dist/SHA256SUMS" ]; then
    echo "verify: $dist/SHA256SUMS"
    ( cd "$dist" && sha256sum -c SHA256SUMS ) || fail "checksum mismatch"
fi

[ "$failed" = 0 ] || release_die "verification failed"
echo "verify: all checks passed"
