#!/bin/sh
# Packages a Windows build with every non-system DLL it needs, so the
# result runs on a machine without MSYS2.
#
# Run from the MSYS2 UCRT64 shell after building:
#
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
#   cmake --build build
#   tools/package_windows.sh
#
# Usage: tools/package_windows.sh [build-dir] [out-dir] [exe...]
# Defaults: build, dist/beexref-windows-x64, <build-dir>/beexref.exe.
# Extra executables (e.g. build/beexref-boardcheck.exe) are packaged too.
set -eu

build=${1:-build}
out=${2:-dist/beexref-windows-x64}
if [ "$#" -gt 2 ]; then
    shift 2
    exes=$*
else
    exes="$build/beexref.exe"
fi

case "${MSYSTEM:-}" in
    UCRT64 | MINGW64 | CLANG64) ;;
    *)
        echo "Run this from an MSYS2 UCRT64 (or MINGW64/CLANG64) shell." >&2
        exit 1
        ;;
esac
: "${MINGW_PREFIX:?MINGW_PREFIX is not set}"

for exe in $exes; do
    if [ ! -f "$exe" ]; then
        echo "No such executable: $exe" >&2
        exit 1
    fi
done

case "$out" in
    "" | "/" | "." | "..")
        echo "Refusing to use '$out' as the output directory." >&2
        exit 1
        ;;
esac
rm -rf "$out"
mkdir -p "$out"
for exe in $exes; do
    cp "$exe" "$out/"
done

# The paths ldd reports for one binary, one per line.
dll_paths()
{
    ldd "$1" 2>/dev/null | awk '
        {
            path = ""
            if ($2 == "=>")
                path = $3
            else if ($1 ~ /^\//)
                path = $1
            if (path ~ /[Dd][Ll][Ll]$/)
                print path
        }
    '
}

# Qt's own DLLs and the runtime-loaded plugins (platform, styles,
# imageformats, tls, ...). windeployqt ships with the MSYS2 Qt; without
# it, copy the plugin directories whole.
windeployqt=$(command -v windeployqt.exe 2>/dev/null || command -v windeployqt 2>/dev/null || true)
if [ -n "$windeployqt" ]; then
    for exe in $exes; do
        "$windeployqt" --release --no-translations "$out/$(basename "$exe")" >/dev/null
    done
else
    echo "windeployqt not found; copying the Qt plugin directories." >&2
    plugins="$MINGW_PREFIX/share/qt6/plugins"
    for dir in platforms styles imageformats iconengines tls networkinformation; do
        [ -d "$plugins/$dir" ] && cp -r "$plugins/$dir" "$out/"
    done
fi

# The MSYS2 DLL closure. ldd is recursive for an executable, but the
# plugins windeployqt copies can pull extra DLLs, so repeat until the
# set stops growing.
while :; do
    before=$(ls "$out"/*.dll 2>/dev/null | wc -l)
    for file in "$out"/*.exe "$out"/*.dll; do
        [ -e "$file" ] || continue
        dll_paths "$file" | while read -r dep; do
            case "$dep" in
                "$MINGW_PREFIX"/*)
                    base=${dep##*/}
                    [ -e "$out/$base" ] || cp "$dep" "$out/"
                    ;;
                /[Cc]/[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/* | [A-Za-z]:/[Ww]indows/*) ;;
                *)
                    # Non-system and outside the MSYS2 prefix: still
                    # needed, so take it.
                    base=${dep##*/}
                    if [ ! -e "$out/$base" ]; then
                        echo "Copying extra dependency: $dep" >&2
                        cp "$dep" "$out/"
                    fi
                    ;;
            esac
        done
    done
    after=$(ls "$out"/*.dll 2>/dev/null | wc -l)
    [ "$before" = "$after" ] && break
done

# Every non-system dependency must now sit next to the executable.
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
missing="$tmp/missing"
: > "$missing"
for exe in $exes; do
    dll_paths "$out/$(basename "$exe")" | while read -r dep; do
        case "$dep" in
            "$MINGW_PREFIX"/*)
                base=${dep##*/}
                [ -e "$out/$base" ] || echo "$base" >> "$missing"
                ;;
        esac
    done
done
if [ -s "$missing" ]; then
    echo "Missing dependencies:" >&2
    sed 's/^/  /' "$missing" >&2
    exit 1
fi

# The platform plugin is loaded at runtime and never shows up in ldd.
if [ ! -e "$out/platforms/qwindows.dll" ]; then
    echo "Warning: platforms/qwindows.dll is missing; the app will not start." >&2
fi

echo "Packaged $(du -sh "$out" | cut -f1) into $out"
echo "Zip it (or copy the directory) to a machine without MSYS2."
