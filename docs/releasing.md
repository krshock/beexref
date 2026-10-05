# Releasing BeeXRef

The release scripts live in `tools/release/`. They are POSIX sh, find the
repository through their own location and use `CMakeLists.txt` as the
single version source, so the same commands work locally and, later, in
CI (the workflows will just call these scripts).

The version is `MAJOR.MINOR.PATCH` in `CMakeLists.txt`; the release
commit is tagged `vX.Y.Z` (annotated). Never tag from a dirty tree —
every script checks that.

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
QT_DIR=$HOME/Qt/6.8.3/gcc_64 tools/release/linux-appimage.sh --host
tools/release/linux-appimage.sh --with-wayland   # bundle the Wayland plugins
QT_DIR=... tools/release/linux-appimage.sh --host --fetch-tools  # pinned tools
```

The container path needs `docker` or `podman` and carries its own pinned
Qt and tools; nothing to set. `--host` needs `QT_DIR` pointing at the
release Qt (the aqt install directory, e.g. `$HOME/Qt/6.8.3/gcc_64`) —
the script refuses another Qt series, so a dev `QT_DIR` (6.11.x) cannot
silently produce a release artifact — and the AppImage tools at
`$APPIMAGE_TOOLS` (default `<repo>/.tools`, filled by `--fetch-tools`).
It honours `CC`/`CXX` (default `gcc-10`/`g++-10`). Tool and Qt versions
and their checksums live in `tools/release/Dockerfile.appimage`, so both
paths use the same ones. The script builds, runs the test suite
offscreen, deploys with `linuxdeploy --plugin qt` (plus the offscreen
plugin, and the Wayland plugins with `--with-wayland`), writes
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
JPEG/WebP plugins and `--version`; lists the zip's essentials; and, when
it verifies all of `dist/` rather than named artifacts, checks
`SHA256SUMS` too (a build script checks only what it just wrote, so a
checksums file from a previous build never trips it).

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

## Test builds

A labeled build of the current tree reports a decorated version and
never touches the release artifacts:

```
sg docker -c 'tools/release/linux-appimage.sh --suffix newwin'
QT_DIR=... tools/release/linux-appimage.sh --host --suffix newwin
```

`--suffix LABEL` appends `-LABEL` to the version the app reports
(`--version`, the About dialog, the startup log) and writes the AppImage
to `dist/dev/BeeXRef-<version>-<label>-x86_64.AppImage`, which stays out
of `dist/SHA256SUMS` and out of `verify.sh`'s all-of-dist pass. The
project version in `CMakeLists.txt` is unchanged.

## Decisions and limits

- Release artifacts are pinned to **Qt 6.8.3 LTS**; the development
  presets use 6.11.2. The AppImage floor is glibc 2.31 (Ubuntu 20.04),
  as documented in `docs/linux-appimage.md`.
- Windows artifacts are built from MSYS2 UCRT64 today. When CI lands at
  the stable phase, the same `windows-zip.sh` runs on `windows-latest`
  with the Qt-installer toolchain.
- Checksums are not signed yet; artifacts have no auto-updater.
- Labeled builds (`--suffix`) are tests, not releases: they have no tag,
  no checksums and no GitHub release. Pre-release labels (`-alpha.1`,
  `-beta.2`, `-rc.1`) for an upcoming version will come with the release
  scripts' `--pre` support in the 1.0 cycle.
- `dist/` is gitignored; never commit artifacts.
