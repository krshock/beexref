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

# Qt's own DLLs. windeployqt ships with the MSYS2 Qt and is worth
# running for the Qt DLL set, but its plugin handling is incomplete for
# MSYS2's layout, so the plugin directories are always copied below.
windeployqt=$(command -v windeployqt.exe 2>/dev/null || command -v windeployqt 2>/dev/null || true)
if [ -n "$windeployqt" ]; then
    for exe in $exes; do
        "$windeployqt" --release --no-translations "$out/$(basename "$exe")" >/dev/null
    done
fi

# The plugins are dlopen'd at runtime: ldd on the executable never lists
# them, and a plugin's own dependencies (qjpeg -> libjpeg, qwebp ->
# libwebp, ...) are invisible to the executable's closure too. Copy
# every plugin category whole; the closure below then resolves what they
# need.
plugins="$MINGW_PREFIX/share/qt6/plugins"
plugin_count=0
for dir in platforms styles imageformats iconengines tls networkinformation; do
    [ -d "$plugins/$dir" ] || continue
    mkdir -p "$out/$dir"
    for file in "$plugins/$dir"/*.dll; do
        [ -e "$file" ] || continue
        if [ ! -e "$out/$dir/${file##*/}" ]; then
            cp "$file" "$out/$dir/"
            plugin_count=$((plugin_count + 1))
        fi
    done
done

# Every executable and DLL in the package, plugins included.
package_files()
{
    find "$out" -type f \( -iname '*.exe' -o -iname '*.dll' \) | LC_ALL=C sort
}

# The MSYS2 DLL closure over the whole package. ldd is recursive for an
# executable, but plugins are loaded later, so repeat until the set
# stops growing.
while :; do
    before=$(package_files | wc -l)
    package_files | while IFS= read -r file; do
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
    after=$(package_files | wc -l)
    [ "$before" = "$after" ] && break
done

# Every non-system dependency, of the executables and of the plugins,
# must now sit next to the executable.
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
missing="$tmp/missing"
: > "$missing"
package_files | while IFS= read -r file; do
    dll_paths "$file" | while read -r dep; do
        case "$dep" in
            "$MINGW_PREFIX"/*)
                base=${dep##*/}
                [ -e "$out/$base" ] \
                    || echo "$base (needed by ${file#"$out"/})" >> "$missing"
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

image_plugins=$(ls "$out/imageformats"/*.dll 2>/dev/null | wc -l)
echo "Packaged $(du -sh "$out" | cut -f1) into $out"
echo "  plugins: $plugin_count copied, $image_plugins image formats"
echo "Zip it (or copy the directory) to a machine without MSYS2."
