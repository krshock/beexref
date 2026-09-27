# Releasing BeeXRef

The release scripts live in `tools/release/`. They are POSIX sh, find the
repository through their own location and use `CMakeLists.txt` as the
single version source, so the same commands work locally and, later, in
CI (the workflows will just call these scripts).

The version is `MAJOR.MINOR.PATCH` in `CMakeLists.txt`; the release
commit is tagged `vX.Y.Z` (annotated), matching the Python reference's
tags. Never tag from a dirty tree — every script checks that.

## 1. Start the release

```
tools/release/prepare.sh 0.7.0
```

This bumps `CMakeLists.txt`, moves the CHANGELOG's `Unreleased` section
under `## [0.7.0] - <date>` and updates the compare links. It refuses a
version that is not newer, an existing tag, or a dirty tree. Add
`--commit` to also make the `Release v0.7.0` commit and the annotated
tag; without it, the script prints the commands to run.

Write the user-visible notes under `## [Unreleased]` in `CHANGELOG.md`
before this step; `tools/release/notes.sh` turns that section into the
GitHub release body (and falls back to the commit log when the section
is missing, so a release is never blocked on bookkeeping).

## 2. Build the artifacts

Linux AppImage (glibc 2.31 floor, Ubuntu 20.04 base):

```
tools/release/linux-appimage.sh            # in a pinned Ubuntu 20.04 container
tools/release/linux-appimage.sh --host     # on a prepared 20.04 machine
tools/release/linux-appimage.sh --with-wayland   # bundle the Wayland plugin
tools/release/linux-appimage.sh --host --fetch-tools  # download the pinned tools
```

The container path needs `docker` or `podman`; `--host` expects Qt 6.8.3
at `$QT_DIR` (default `~/Qt/6.8.3/gcc_64`) and the AppImage tools at
`$APPIMAGE_TOOLS` (default `~/appimage-tools`), and honours `CC`/`CXX`
(default `gcc-10`/`g++-10`). Tool versions and their checksums live in
`tools/release/Dockerfile.appimage`, so both paths use the same ones.
The script builds, runs the test suite offscreen, stages through
`cmake --install`, runs `linuxdeploy --plugin qt`, writes
`dist/BeeXRef-<version>-x86_64.AppImage` and verifies it.

Windows zip (from an MSYS2 UCRT64 shell, see `docs/building-windows.md`):

```
tools/release/windows-zip.sh [build-dir] [--skip-tests]
```

It builds, tests, wraps `tools/package_windows.sh` (Qt libraries, every
plugin category, the whole non-system DLL closure), and writes
`dist/BeeXRef-<version>-win64.zip`.

## 3. Checksums and verification

```
tools/release/checksums.sh                 # dist/SHA256SUMS
tools/release/verify.sh [artifact...]      # all dist artifacts by default
```

`verify.sh` extracts the AppImage and checks its library closure, the
JPEG/WebP plugins and `--version`; lists the zip's essentials; and
verifies `SHA256SUMS` when it is present.

## 4. Publish

```
tools/release/publish.sh            # draft release, artifacts attached
tools/release/publish.sh --publish  # straight to public
```

`publish.sh` needs the GitHub CLI (`gh auth login`), requires the tag to
point at HEAD, writes `dist/notes.md` (changelog section, compatibility
notes, checksums) and creates the release as a **draft** so a human can
review it before pressing Publish. Running it again uploads to the
existing release instead.

## Decisions and limits

- Release artifacts are pinned to **Qt 6.8.3 LTS**; the development
  presets use 6.11.2. The AppImage floor is glibc 2.31 (Ubuntu 20.04),
  as documented in `docs/linux-appimage.md`.
- Windows artifacts are built from MSYS2 UCRT64 today. When CI lands at
  the stable phase, the same `windows-zip.sh` runs on `windows-latest`
  with the Qt-installer toolchain.
- Checksums are not signed yet; artifacts have no auto-updater.
- `dist/` is gitignored; never commit artifacts.
