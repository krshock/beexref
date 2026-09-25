# Building BeeXRef on Windows

Target: Windows 10 (1809 or later) / Windows 11, x86_64. Qt 6.11's
supported floor is Windows 10 1809, so that is the port's baseline.

A native Windows build is the primary path. Cross-compiling from Linux
is possible and documented at the end, mainly for CI or machines without
Windows.

## Requirements

### Compiler

- **MSVC 2022** (v143 toolset, "Desktop development with C++"), or
- **MinGW-w64 13.1**, the version Qt 6.11 ships with. The Qt installer
  puts it in `C:/Qt/Tools/mingw1310_64`; use that compiler rather than an
  unrelated MinGW so the ABI matches Qt's binaries.

The project needs C++20.

### Qt

- **Qt 6.8 or newer**; the port is developed against **6.11.2**. Keep
  the Qt version in step with the one used for Linux builds.
- Modules: `Core`, `Gui`, `Widgets`, `Svg`, `Network` and `Test`
  (`Test` is only needed when building the tests).
- Install with the Qt Online Installer / Maintenance Tool, or with
  `aqtinstall` (`pip install aqtinstall`, then
  `aqt install-qt windows desktop 6.11.2 win64_mingw`). Qt 6.11 changed
  the download repository layout; if the download fails, update
  `aqtinstall` or fetch the `.7z` packages from `download.qt.io`.
  MSYS2 packages Qt too; see "MSYS2 (UCRT64)" below.

### Build tools

- **CMake 3.25 or newer** (the project itself needs 3.24; the preset
  format needs 3.25).
- **Ninja** (the generator the project's presets use; the Visual Studio
  generator also works).
- **Git for Windows**.
- **windeployqt** (ships with Qt) for packaging.

SQLite is vendored in `third_party/sqlite3`, so there is nothing to
install for it. `ccache` and `mold` are only used by the Linux presets
and are not needed on Windows.

## Native build

### 1. Open a compiler environment

- MSVC: use "x64 Native Tools Command Prompt for VS 2022" so `cl.exe`
  and the Windows SDK are on the environment.
- MinGW: put Qt's MinGW `bin` directory first on `PATH`.

### 2. Configure and build

```
cmake -B build -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/mingw_64 ^
    -DBUILD_TESTING=ON
cmake --build build
```

Use `C:/Qt/6.11.2/msvc2022_64` as `CMAKE_PREFIX_PATH` for the MSVC kit.
The committed CMake presets are Linux-only (Linux Qt path, ccache,
`-fuse-ld=mold`), so configure explicitly as above until a Windows
preset exists.

### 3. Run the tests

```
set QT_QPA_PLATFORM=offscreen
ctest --test-dir build --output-on-failure
```

(In PowerShell: `$env:QT_QPA_PLATFORM = "offscreen"`.)

The suite runs headless through Qt's offscreen platform plugin. A few
LOD suites are timing-sensitive; `ctest -R <regex>` narrows a run when a
machine is busy.

The UI smoke harness works too; pass an explicit output directory
because its default is a Unix path:

```
set QT_QPA_PLATFORM=offscreen
build\beexref-ui-smoke.exe "" %TEMP%\ui-smoke
```

### 4. Run and package

```
build\beexref.exe
```

`beexref.exe` is a GUI-subsystem binary (no console window);
`beexref-boardcheck.exe` and the test binaries are console programs.

In an MSYS2 shell, `tools/package_windows.sh` assembles a folder that
runs on a machine without MSYS2:

```
tools/package_windows.sh [build-dir] [out-dir] [exe...]
# defaults: build  dist/beexref-windows-x64  <build-dir>/beexref.exe
```

It copies the executables, runs `windeployqt` for the Qt DLLs, copies
the plugin directories whole (`platforms/`, `imageformats/`, `styles/`,
`tls/`, ...), then copies the `ldd` closure of everything in the
package — the executables *and* the plugins. Plugin dependencies never
show up in the executable's listing: `qjpeg` needs libjpeg, `qwebp`
needs libwebp, and without them the images silently stay empty in the
dist build. The script fails if a non-system dependency is missing.
Zip the output folder to distribute it.

With the Qt installer's MinGW kit, deploy manually instead:

```
C:\Qt\6.11.2\mingw_64\bin\windeployqt.exe --release build\beexref.exe
```

plus the compiler runtime DLLs (`libgcc_s_seh-1.dll`,
`libstdc++-6.dll`, `libwinpthread-1.dll`) from Qt's MinGW `bin` if
`windeployqt` did not copy them.

## MSYS2 (UCRT64)

MSYS2 is the quickest route to a native build: it packages both the
mingw-w64 GCC toolchain and Qt 6. Use its **UCRT64** environment
(native x86_64, UCRT C runtime, posix threads) — not `msys` (Cygwin
runtime) and not the legacy `mingw64` (MSVCRT) one. Its install prefix
is `/ucrt64`; packages are named `mingw-w64-ucrt-x86_64-*`.

From the "MSYS2 UCRT64" shell:

```
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,git,qt6-base,qt6-svg}
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

`qt6-base` carries Core/Gui/Widgets/Network/Test, `qt6-svg` carries Svg.
The shell's compiler and Qt are found automatically, so no
`CMAKE_PREFIX_PATH` is needed. Package with `tools/package_windows.sh`.

MSYS2 is a rolling release: `pacman -Syu` moves GCC and Qt, and its Qt
may differ from the version used for Linux builds. When the exact Qt
version matters, use the Qt installer route above.

Filesystem notes: Windows drives are mounted at `/c`, `/d`, …;
`cygpath -w /c/foo` prints `C:\foo`; `$HOME` is MSYS2's own home
(`C:\msys64\home\<user>`), while the Windows profile is `/c/Users/<user>`.

## Windows-specific behaviour

- Settings and logs live under `%LOCALAPPDATA%/BeeXRef`, the cache under
  `%LOCALAPPDATA%/cache/BeeXRef`, through `QStandardPaths`.
- The atomic save uses `MoveFileExW`; `<windows.h>` is included with
  `NOMINMAX` defined because the file also uses `std::max`.
- RSS accounting uses `GetProcessMemoryInfo` (psapi); the allocator trim
  and process-liveness helpers are no-ops on Windows.
- `.beex` is platform-neutral, so a Windows build reads and writes the
  same boards as the Linux, Python and Go ports. `beexref-boardcheck`
  is the cross-port check:

```
build\beexref-boardcheck.exe board.beex
```

## Verification checklist

- All targets build: `beexref`, `beexref-boardcheck`, `beexref-ui-smoke`
  and the tests.
- `ctest` passes with `QT_QPA_PLATFORM=offscreen`.
- `beexref-boardcheck` reports `roundtrip=identical` on a sample board.
- The app opens a `.beex`, and Save/Export produce files that the Linux
  build opens.

## Cross-compiling from Linux

Useful when no Windows machine is available. The pieces:

- MinGW-w64 **13.2 posix** from the distribution
  (`g++-mingw-w64-x86-64-posix`), matching Qt's MinGW 13.1 major.
- Qt for Windows `win64_mingw` 6.11.2 from `aqtinstall` (or the `.7z`
  packages on `download.qt.io`).
- The Linux Qt 6.11.2 (`~/Qt/6.11.2/gcc_64`) as `QT_HOST_PATH` for
  `moc`, `rcc` and `uic`.
- A toolchain file setting `CMAKE_SYSTEM_NAME Windows`, the `-posix`
  compilers, `windres` and the find-root modes; clear the presets'
  `-fuse-ld=mold` flags, which cannot link PE binaries.
- Tests run through Wine with `CMAKE_CROSSCOMPILING_EMULATOR=wine` and
  `WINEPATH` pointing at the target Qt `bin` and the MinGW runtime;
  `windeployqt.exe` also runs under Wine.

A committed toolchain file and preset for this route are not in the tree
yet; until then use the native build above.
