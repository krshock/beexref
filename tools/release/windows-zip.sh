#!/bin/sh
# Packages a Windows build into dist/BeeXRef-<version>-win64.zip.
#
# Run from an MSYS2 UCRT64 (or MINGW64/CLANG64) shell after configuring
# the build (see docs/building-windows.md):
#
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
#   tools/release/windows-zip.sh
#
# The script builds the tree, runs the tests (--skip-tests skips them),
# wraps tools/package_windows.sh (Qt libraries, every plugin category and
# the whole non-system DLL closure, failing on a missing dependency),
# zips the directory and verifies the result.
#
# Usage: windows-zip.sh [build-dir] [--skip-tests]
set -eu
. "$(dirname "$0")/common.sh"

build=build
skip_tests=false
while [ "$#" -gt 0 ]; do
    case "$1" in
        --skip-tests)
            skip_tests=true
            shift
            ;;
        -*)
            release_die "unknown option: $1"
            ;;
        *)
            build=$1
            shift
            ;;
    esac
done

case "${MSYSTEM:-}" in
    UCRT64 | MINGW64 | CLANG64) ;;
    *)
        echo "windows-zip: warning: not an MSYS2 shell; package_windows.sh will refuse" >&2
        ;;
esac
command -v zip >/dev/null 2>&1 || release_die "zip is missing (pacman -S zip)"

root=$(release_root)
version=$(release_version)
dist=$(release_dist)
[ -f "$build/beexref.exe" ] || release_die "no $build/beexref.exe; configure and build first"

cmake --build "$build"
if [ "$skip_tests" = false ]; then
    ( cd "$build" && QT_QPA_PLATFORM=offscreen ctest --output-on-failure )
fi

exes="$build/beexref.exe"
if [ -f "$build/beexref-boardcheck.exe" ]; then
    exes="$exes $build/beexref-boardcheck.exe"
fi
if [ -f "$build/beexref-ui-smoke.exe" ]; then
    exes="$exes $build/beexref-ui-smoke.exe"
fi

outdir="$dist/BeeXRef-$version-win64"
rm -rf "$outdir"
"$root/tools/package_windows.sh" "$build" "$outdir" $exes

( cd "$dist" && zip -qr "BeeXRef-$version-win64.zip" "BeeXRef-$version-win64" )
echo "windows-zip: wrote $dist/BeeXRef-$version-win64.zip"

"$(dirname "$0")/verify.sh" "$dist/BeeXRef-$version-win64.zip"
