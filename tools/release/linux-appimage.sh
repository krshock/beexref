#!/bin/sh
# Builds dist/BeeXRef-<version>-x86_64.AppImage.
#
# Default: in the pinned Ubuntu 20.04 container (docker or podman), so
# the artifact keeps the glibc 2.31 floor documented in
# docs/linux-appimage.md. --host runs the same steps on a machine that
# already has Qt 6.8.3 and the AppImage tools (the VM recipe);
# --with-wayland adds the qtwayland module to the image. CC/CXX override
# the 20.04 toolchain (gcc-10/g++-10); --fetch-tools downloads the pinned
# AppImage tools into APPIMAGE_TOOLS first.
#
# Usage: linux-appimage.sh [--container|--host] [--with-wayland] [--fetch-tools]
set -eu
. "$(dirname "$0")/common.sh"

mode=container
inner=false
with_wayland=false
fetch_tools=false
while [ "$#" -gt 0 ]; do
    case "$1" in
        --container) mode=container; shift ;;
        --host) mode=host; shift ;;
        --inner) inner=true; shift ;; # internal: the container re-invokes this
        --with-wayland) with_wayland=true; shift ;;
        --fetch-tools) fetch_tools=true; shift ;;
        *) release_die "unknown option: $1" ;;
    esac
done

root=$(release_root)
version=$(release_version)
dist=$(release_dist)

if [ "$mode" = container ] && [ "$inner" = false ]; then
    if command -v docker >/dev/null 2>&1; then
        runtime=docker
    elif command -v podman >/dev/null 2>&1; then
        runtime=podman
    else
        release_die "needs docker or podman; use --host on a prepared machine"
    fi

    with_wayland_arg=0
    [ "$with_wayland" = true ] && with_wayland_arg=1
    image="beexref-appimage:qt6.8.3-wayland-$with_wayland_arg"
    "$runtime" build --build-arg "WITH_WAYLAND=$with_wayland_arg" \
        -t "$image" -f "$root/tools/release/Dockerfile.appimage" "$root/tools/release"
    exec "$runtime" run --rm \
        --user "$(id -u):$(id -g)" \
        -e HOME=/tmp \
        -v "$root:/src" -w /src \
        "$image" tools/release/linux-appimage.sh --inner
fi

# Inside the container (--inner) or on a prepared host.
QT_DIR=${QT_DIR:-$HOME/Qt/6.8.3/gcc_64}
TOOLS_DIR=${APPIMAGE_TOOLS:-$HOME/appimage-tools}
[ -d "$QT_DIR" ] || release_die "no Qt at $QT_DIR (set QT_DIR)"

# The tool versions live in the Dockerfile, so the container and the host
# recipe always fetch the same ones.
dockerfile="$root/tools/release/Dockerfile.appimage"
tool_version()
{
    sed -n "s/^ARG $1=//p" "$dockerfile"
}

if [ "$fetch_tools" = true ]; then
    mkdir -p "$TOOLS_DIR"
    ld_version=$(tool_version LINUXDEPLOY_VERSION)
    ld_sha=$(tool_version LINUXDEPLOY_SHA256)
    qt_plugin_version=$(tool_version LINUXDEPLOY_PLUGIN_QT_VERSION)
    appimagetool_version=$(tool_version APPIMAGETOOL_VERSION)
    wget -q -O "$TOOLS_DIR/linuxdeploy-x86_64.AppImage" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/$ld_version/linuxdeploy-x86_64.AppImage"
    echo "$ld_sha  $TOOLS_DIR/linuxdeploy-x86_64.AppImage" | sha256sum -c -
    wget -q -O "$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage" \
        "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/$qt_plugin_version/linuxdeploy-plugin-qt-x86_64.AppImage"
    wget -q -O "$TOOLS_DIR/appimagetool-x86_64.AppImage" \
        "https://github.com/AppImage/appimagetool/releases/download/$appimagetool_version/appimagetool-x86_64.AppImage"
    chmod +x "$TOOLS_DIR"/*.AppImage
    echo "linux-appimage: tools in $TOOLS_DIR"
    exit 0
fi

[ -x "$TOOLS_DIR/linuxdeploy-x86_64.AppImage" ] \
    || release_die "no linuxdeploy at $TOOLS_DIR (set APPIMAGE_TOOLS, or --fetch-tools)"
[ -x "$TOOLS_DIR/appimagetool-x86_64.AppImage" ] \
    || release_die "no appimagetool at $TOOLS_DIR (set APPIMAGE_TOOLS, or --fetch-tools)"

cc=${CC:-gcc-10}
cxx=${CXX:-g++-10}

build="$root/build/appimage"
echo "linux-appimage: configuring in $build"
cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$cc" \
    -DCMAKE_CXX_COMPILER="$cxx" \
    -DCMAKE_PREFIX_PATH="$QT_DIR" \
    -DBUILD_TESTING=ON
cmake --build "$build"
( cd "$build" && QT_QPA_PLATFORM=offscreen ctest --output-on-failure )

stage="$build/stage"
appdir="$build/AppDir"
icon="$build/beexref.png"
rm -rf "$stage" "$appdir" "$build"/*.AppImage
cmake --install "$build" --prefix "$stage"
cp "$root/assets/logo.png" "$icon"

# linuxdeploy wants qmake on PATH (or QMAKE) and the architecture. The
# build-tree binary is deployed on purpose: its RUNPATH points at the
# pinned Qt, so linuxdeploy resolves Qt 6.8.3; the installed binary's
# $ORIGIN RUNPATH would resolve the host's Qt instead.
export PATH="$QT_DIR/bin:$PATH"
export QMAKE="$QT_DIR/bin/qmake"
export ARCH=x86_64
export APPIMAGE_EXTRACT_AND_RUN=1

echo "linux-appimage: deploying into $appdir"
"$TOOLS_DIR/linuxdeploy-x86_64.AppImage" \
    --appdir "$appdir" --plugin qt \
    --executable "$build/beexref" \
    --desktop-file "$root/packaging/beexref.desktop" \
    --icon-file "$icon"

# The Qt plugin deploys the xcb platform plugin only. Add the offscreen
# plugin (so the AppImage can run headless, e.g. --version in scripts and
# CI) and, with --with-wayland, the Wayland plugins; their RUNPATHs
# ($ORIGIN/../../lib) resolve inside the AppDir, and a second linuxdeploy
# pass bundles their non-Qt dependencies.
platforms="$appdir/usr/plugins/platforms"
mkdir -p "$platforms"
cp "$QT_DIR/plugins/platforms/libqoffscreen.so" "$platforms/"
if [ "$with_wayland" = true ]; then
    cp "$QT_DIR"/plugins/platforms/libqwayland-*.so "$platforms/"
fi
"$TOOLS_DIR/linuxdeploy-x86_64.AppImage" --appdir "$appdir"

target="$dist/BeeXRef-$version-x86_64.AppImage"
echo "linux-appimage: building $target"
"$TOOLS_DIR/appimagetool-x86_64.AppImage" "$appdir" "$target"
echo "linux-appimage: wrote $target"

"$(dirname "$0")/verify.sh" "$target"
