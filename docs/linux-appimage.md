# Building BeeXRef as a Linux AppImage

Target: x86_64 AppImage built on **Ubuntu 20.04 LTS**, which gives the
broadest practical runtime floor (glibc 2.31). The AppImage runs on
Ubuntu 20.04 and newer; 18.04 and older (glibc 2.27) are out of reach.

The reason to build here instead of a current distribution is the glibc
floor: a binary built on 22.04 (glibc 2.35) refuses to start on 20.04,
while one built on 20.04 runs from 20.04 up.

`tools/release/linux-appimage.sh` automates this recipe: it builds in a
pinned 20.04 container by default, or on this VM with `--host`, and
writes `dist/BeeXRef-<version>-x86_64.AppImage`. The manual steps below
are what the script runs; `docs/releasing.md` covers the whole release
flow.

**Qt version caveat.** The project is developed against Qt 6.11, but
Qt 6.11's official binaries are built on Ubuntu 24.04 and need
`GLIBC_2.34` and `GLIBCXX_3.4.29`, which 20.04 does not have. Use
**Qt 6.8.x** on this VM — the project's minimum. Qt 6.8's binaries are
built on RHEL 8.10 (glibc 2.28, GCC 10 toolset) and run on 20.04. If you
want 6.11, use an Ubuntu 22.04 VM and accept the newer runtime floor.
`aqtinstall` only downloads Qt's prebuilt binaries — it cannot make 6.11
run here; only building Qt from source could, which is not worth it.

### Why the base distribution and Qt version matter

An AppImage bundles the app, the Qt libraries and Qt's plugins, plus
other non-system libraries — but **not glibc and, normally, not
libstdc++**: those are on the AppImage exclusion list because they
belong to the base system.

- **Building**: the Qt you install has to run on this VM. Qt 6.11's
  binaries need `GLIBC_2.34` and `GLIBCXX_3.4.29`; 20.04 has glibc 2.31
  and libstdc++ 3.4.28, so its `qmake`, `moc`, `rcc` and libraries do
  not start here at all. Qt 6.8's binaries (RHEL 8.10 base) do.
- **Running**: the AppImage starts against the *target* machine's glibc
  and libstdc++, so the build machine's floor becomes the AppImage's
  floor (built on 20.04 → runs on 20.04 and newer). The compiler must
  not require a newer libstdc++ than a clean 20.04 provides, which is
  why the build below uses `g++-10` rather than a newer GCC from a PPA.

## Runtime support surface

Because glibc and libstdc++ come from the target machine, the AppImage
built here requires:

| Requirement | Floor | First releases that satisfy it |
| --- | --- | --- |
| glibc | 2.31 | Ubuntu 20.04, Debian 11, openSUSE Leap 15.3, Fedora 32, RHEL 9 |
| libstdc++ | GLIBCXX_3.4.28 (GCC 10) | the same releases |
| architecture | x86_64 | arm64 needs its own build |

Runs on Ubuntu 20.04+ (and Mint 20+, Pop!\_OS 20.04+, Zorin 16+,
elementary 6+), Debian 11+, Fedora 32+, RHEL 9+ (Rocky/Alma 9+),
openSUSE Leap 15.3+, SLES 15 SP3+, current Arch/Manjaro/Kali.

Does not run on Ubuntu 18.04 and older, Debian 10, RHEL/CentOS/Rocky/
Alma 8, Fedora 31 and older, openSUSE Leap 15.2 and older, Alpine and
other musl distributions (AppImages are glibc-based), or NixOS without
`appimage-run`.

Going lower needs an older build base (for example a RHEL 8 or Debian
10 container) *and* a bundled `libstdc++`, because Qt 6.8's own binaries
already need glibc 2.28 / GLIBCXX 3.4.28. Ubuntu 20.04 is the sweet
spot: old enough for a wide surface, new enough for Qt 6.8's binaries
and a stock `g++-10`.

Building on a newer base does **not** give the older targets back:
glibc and libstdc++ are forward-compatible only, so a 22.04-built
AppImage stops on 20.04 with `GLIBC_2.35 not found`. To build on a
newer host and keep the 20.04 floor, build inside a 20.04 container
(Docker or `chroot`) with the same Qt 6.8 and `g++-10` recipe; the
artifact then has the 20.04 floor. Bundling glibc is not an option — the
AppImage tooling excludes it for good reason.

## Requirements

### System packages

```
sudo apt update
sudo apt install -y \
    build-essential g++-10 ninja-build git wget pkg-config ccache \
    python3-pip python3-venv \
    libgl1 libxkbcommon0 libxkbcommon-x11-0 libfontconfig1 libfreetype6 \
    libdbus-1-3 libglib2.0-0 \
    libx11-xcb1 libxcb1 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
    libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-sync1 \
    libxcb-xfixes0 libxcb-xkb1 \
    libfuse2 patchelf file desktop-file-utils xvfb
```

- `g++-10` is needed for C++20 (`std::numbers`); 20.04's default GCC 9
  is too old. It also keeps the app on the stock 20.04 `libstdc++`
  (GLIBCXX_3.4.28), so the AppImage runs on a clean 20.04 without a
  compiler upgrade. Do not build with GCC 11+ from a PPA unless you also
  bundle `libstdc++`.
- `libfuse2` runs the AppImage tooling and the produced AppImage.
- `xvfb` is only for manual windowed testing in the VM; the test suite
  is headless through Qt's offscreen platform.

Qt's `libqxcb` platform plugin needs **`libxcb-cursor0`**, which is not
in 20.04's repositories. The quickest fix is the Ubuntu 22.04 package —
its dependencies are all in focal:

```
cd /tmp
wget http://archive.ubuntu.com/ubuntu/pool/universe/x/xcb-util-cursor/libxcb-cursor0_0.1.1-4ubuntu1_amd64.deb
sudo apt install -y ./libxcb-cursor0_0.1.1-4ubuntu1_amd64.deb
sudo ldconfig
```

Building it from source works too:

```
sudo apt install -y autoconf automake libtool xutils-dev \
    libxcb1-dev libxcb-render0-dev libxcb-render-util0-dev libxcb-image0-dev
git clone --depth 1 https://gitlab.freedesktop.org/xorg/lib/libxcb-cursor.git
cd libxcb-cursor
./autogen.sh --prefix=/usr
make -j"$(nproc)"
sudo make install
sudo ldconfig
```

### CMake

20.04 ships CMake 3.16; the project needs 3.24. Use Kitware's APT
repository:

```
wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null \
    | gpg --dearmor - | sudo tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null
echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ focal main' \
    | sudo tee /etc/apt/sources.list.d/kitware.list >/dev/null
sudo apt update
sudo apt install -y cmake
cmake --version   # 3.28 or newer
```

### Qt 6.8

Install with `aqtinstall`:

```
python3 -m pip install --user aqtinstall
~/.local/bin/aqt install-qt --outputdir "$HOME/Qt" \
    linux desktop 6.8.3 linux_gcc_64 -m qtimageformats
```

`qtbase` carries Core/Gui/Widgets/Network/Test, the base install already
includes Svg, and `qtimageformats` carries the WebP/TIFF image plugins
that boards need. Qt lands in `<outputdir>/6.8.3/gcc_64` (aqt's default
output directory is `~/Qt`); export `QT_DIR` as that directory for the
build below.

**aqtinstall on 20.04.** Ubuntu 20.04 has Python 3.8, and aqtinstall
dropped 3.8 after 3.1.18 — a version that cannot finish Qt 6.8.3: it
extracts the archives to the wrong place and then fails with
`Updater caused an IO error: ... mkspecs/qconfig.pri`. Either

- install Qt on a machine with Python 3.9+ (aqtinstall 3.3.0) and copy
  the tree into the VM, preserving symlinks:

  ```
  # newer machine
  aqt install-qt --outputdir "$HOME/Qt" \
      linux desktop 6.8.3 linux_gcc_64 -m qtimageformats
  tar -C ~/Qt -czf /tmp/Qt-6.8.3.tar.gz 6.8.3/gcc_64
  # VM
  tar -C ~/Qt -xzf /tmp/Qt-6.8.3.tar.gz
  ```

- or give the VM a Python 3.9+ (for example a miniforge install) and run
  `aqtinstall` there.

A Qt tree copied from another machine works as-is: it is just files, and
the VM only needs the runtime dependencies above.

Check that the platform plugin's dependencies resolve:

```
ldd "$QT_DIR/plugins/platforms/libqxcb.so" | grep "not found"
```

No output is the expected result; `libxcb-cursor` from above is the
usual missing one.

### AppImage tools

`tools/release/linux-appimage.sh --host --fetch-tools` downloads the
pinned tools into `.tools/`; the versions and checksums live in
`tools/release/Dockerfile.appimage`. To fetch them by hand:

```
mkdir -p ~/appimage-tools && cd ~/appimage-tools
wget https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage
wget https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage
wget https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
chmod +x *.AppImage
```

## Build

The presets take Qt from `QT_DIR` (they set `CMAKE_PREFIX_PATH` from
it), and `mold` — which is not in 20.04 — is only used when it is
installed, so configure with the 20.04 toolchain and the release Qt
exported:

```
cd beexrefcpp
export QT_DIR="$HOME/Qt/6.8.3/gcc_64"
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DCMAKE_C_COMPILER=gcc-10 \
    -DCMAKE_CXX_COMPILER=g++-10
cmake --build build -j"$(nproc)"
```

(`-DCMAKE_PREFIX_PATH="$QT_DIR"` on the command line does the same when
you would rather not export anything.)

Keep the build directory on the VM's own disk, not on a VirtualBox
shared folder: compiling is much faster there, and CMake's compiler
probes behave. A `build/` copied from another path also fails with
`CMakeCache.txt directory ... is different`; delete it and configure
fresh.

Add `-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache`
for rebuilds, and `-DBUILD_TESTING=ON` if you also want the tests:

```
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

SQLite is vendored in `third_party/sqlite3`; nothing to install.

## AppImage

Run the commands from the repository root. Three things are involved:

- `appimage/` — the staged desktop entry and icon, copied from
  `packaging/beexref.desktop` and `assets/logo.png` in step 1.
- `AppDir/` — the staging tree. **linuxdeploy creates it** (`--appdir
  AppDir`); you never fill it by hand. After step 2 it looks like:

  ```
  AppDir/
    AppRun
    usr/bin/beexref                            the executable
    usr/lib/                                   Qt libs and non-system deps (libxcb-cursor, ...)
    usr/plugins/                               platforms/, imageformats/, iconengines/, tls/, ...
    usr/share/applications/beexref.desktop
    usr/share/icons/hicolor/256x256/apps/beexref.png
  ```

- `BeeXRef-0.6.0-x86_64.AppImage` — the final file, written to the
  current directory. Move it wherever you like (for example `dist/`);
  `AppDir/` is disposable once the image is built.

### 1. Desktop entry and icon

The entry ships in `packaging/beexref.desktop` and the icon is
`assets/logo.png` (both are also installed by `cmake --install`, for
distro packages). linuxdeploy wants the icon file named after the
desktop `Icon=` key, so stage both under `appimage/`:

```
mkdir -p appimage
cp packaging/beexref.desktop appimage/
cp assets/logo.png appimage/beexref.png
```

The icon file name must match the desktop `Icon=` key (`beexref`).

### 2. Deploy into AppDir

`linuxdeploy` creates `AppDir/` (relative to the current directory) and
fills it: it copies the executable and its non-system libraries, while
`--plugin qt` adds the Qt libraries and plugins. The plugin finds Qt
through `qmake`, so put the VM's Qt on `PATH` (or set `QMAKE`):

```
export PATH="$HOME/appimage-tools:$QT_DIR/bin:$PATH"
export QMAKE="$QT_DIR/bin/qmake"
export ARCH=x86_64

linuxdeploy-x86_64.AppImage --appdir AppDir --plugin qt \
    --executable build/beexref \
    --desktop-file appimage/beexref.desktop \
    --icon-file appimage/beexref.png
```

### 3. Build the AppImage

```
appimagetool-x86_64.AppImage AppDir BeeXRef-0.6.0-x86_64.AppImage
```

(or add `--output appimage` to the `linuxdeploy` command above).

### 4. Verify

```
# Everything the app loads must be inside the AppDir or a system lib.
ldd AppDir/usr/bin/beexref | grep "not found"
find AppDir/usr/plugins -name "*.so" -exec ldd {} \; 2>/dev/null | grep "not found"

# The image plugins the boards need (the Windows package once shipped
# without them and JPEG/WebP items stayed empty):
ls AppDir/usr/plugins/imageformats/

# Run it; in a VM without FUSE use --appimage-extract-and-run.
./BeeXRef-0.6.0-x86_64.AppImage
```

Then open a board with PNG, JPEG and WebP items, check that they render,
and that Save/Export produce files the other ports open.

## Troubleshooting

- **`Could not load the Qt platform plugin "xcb"`** — `libxcb-cursor0`
  is missing. Install it as shown above, then check with
  `ldd "$QT_DIR/plugins/platforms/libqxcb.so" | grep "not found"`.

- **`The install of the beexref target requires changing an RPATH ...
  ELF-based or XCOFF-based platform`** — CMake did not detect the
  platform as ELF (it reads the format of the compiler-id test binary).
  Configure in a directory on the VM's own disk, deleting any stale
  `build/` first. If it persists, add `-DCMAKE_EXECUTABLE_FORMAT=ELF`
  (Linux is ELF) or `-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON`.

- **`CMakeCache.txt directory ... is different than the directory ...`** —
  the build directory was copied from another checkout; remove it and
  configure fresh.

- **`Detected locale ... not UTF-8`** — harmless; Qt falls back to
  `en_US.UTF-8`. To silence it, `sudo locale-gen en_US.UTF-8` and set
  `LANG=en_US.UTF-8`.

- **aqtinstall `Updater caused an IO error: ... qconfig.pri`** — the
  Python 3.8 cap described in the Qt section: install Qt elsewhere and
  copy the tree in, or use a newer Python.

## Dependency notes

- **glibc floor**: the AppImage needs glibc >= 2.31 (Ubuntu 20.04).
  It will not start on 18.04 and older.
- **FUSE**: running AppImages needs FUSE 2 (`libfuse2`). In containers
  or VMs without it, use `--appimage-extract-and-run` or set
  `APPIMAGE_EXTRACT_AND_RUN=1`.
- **libxcb-cursor**: Qt 6.5+ links it. Installing it on the build VM
  makes linuxdeploy bundle it, so the AppImage also runs on
  distributions that lack the package.
- **Image formats**: `qjpeg`/`qgif`/`qico` come from `qtbase`;
  `qwebp`/`qtiff` from the `qtimageformats` module. Keep the module
  installed, or those items stay empty in the AppImage.
- **TLS**: URL downloads go through Qt's OpenSSL backend plugin, which
  loads the system `libssl`/`libcrypto`. The AppImage does not bundle
  OpenSSL; on a machine without it URLs cannot be downloaded (boards and
  files still work).
- **Wayland**: only the xcb platform plugin is bundled; Wayland
  sessions run the app through XWayland. Install the `qtwayland` module
  and re-deploy for a native Wayland plugin.

## Verification checklist

- `cmake --build build` succeeds with Qt 6.8 and `g++-10`.
- `ctest` passes with `QT_QPA_PLATFORM=offscreen` (when tests are on).
- `AppDir/usr/plugins/imageformats/` contains `libqjpeg.so` and
  `libqwebp.so`.
- `ldd` reports nothing missing inside `AppDir`.
- The AppImage starts on a clean 20.04+ VM, opens a `.beex`, renders
  PNG/JPEG/WebP, saves and exports.
