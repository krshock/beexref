# AGENTS.md

BeeXRef in C++/Qt6: a port of the BeeRefX infinite-canvas reference-image
viewer. It must stay byte-compatible with the `.beex` format and share the
app's config/cache paths. Priorities, in order: low RAM, then low CPU, then
simplicity. Match the Python reference's behaviour including its quirks; call
out deliberate deviations.

## References (not in this repo)

- Python reference: `../beeref/beerefx/` (sibling checkout)
- Go/Fyne reference: `../beerefx-go/beexref/` (sibling checkout)
- When semantics are unclear, read these. `fileio/legacy.py`, `fileio/sql.py`,
  `fileio/export.py` define the on-disk and export shapes.

## Build & test

```sh
cmake --preset linux-debug && cmake --build --preset linux-debug
ctest --preset linux-debug
# same for linux-release
```

- Presets live in `CMakePresets.json`. `linux-debug` is **RelWithDebInfo**, not
  a debug build. Both use ccache and mold (LTO is on for release).
- Requires Qt >= 6.8. Point `CMAKE_PREFIX_PATH` at your Qt install: edit
  the `base` preset or pass `-DCMAKE_PREFIX_PATH=...`.
- Binaries land in `build/linux-{debug,release}/`: `beexref`,
  `beexref-boardcheck`, `beexref-ui-smoke`.
- Single test binary: `./build/linux-debug/tests/test_<name> <slotName>`
  (Qt Test), or `ctest --preset linux-debug -R <regex>`.
- Tests run offscreen; the test presets already set `QT_QPA_PLATFORM=offscreen`.
- Formatting: `.clang-format` (LLVM base, Allman braces, 4 spaces, 100 cols).
- Adding a test target means editing `tests/CMakeLists.txt` (one
  `qt_add_executable` + `add_test` per suite).
- Windows build requirements and steps: `docs/building-windows.md`.

## Hard rules

- **Never commit without explicit user approval.**
- **Tests must never touch the user's real `~/.config/BeeXRef` or
  `~/.cache/BeeXRef`.** Use `tests/test_env.h` (`testenv::isolate()`), or point
  `settings::setSettingsDir()` at a `QTemporaryDir`. The UI smoke harness
  copies settings into its output dir and honours `BEEXREF_SMOKE_SETTINGS`.
  A live interactive `beexref` session may legitimately write the real paths;
  never attribute those writes to a test run.
- **Never inject input into the user's desktop session.** UI verification is
  in-process/offscreen: `beexref-ui-smoke` drives the real `MainWindow` with
  Qt events, or add a Qt Test slot.

## Architecture

- `beexref_core` (static lib, `src/`) holds everything; the executables are thin
  entrypoints (`src/main.cpp`, `tools/`). Link new code into `beexref_core`
  and list its sources in the root `CMakeLists.txt`.
- Layers: `board/` (SQLite, schema, streaming writer), `doc/` (Document, Item,
  immutable `Source`, undo), `ui/` (Scene/SceneItem, View, MainWindow, LOD),
  `settings`/`logging`, `cache/`.
- Rendering is a custom `QGraphicsItem` painting a `QImage`; item-local
  coordinates are original-image pixels, so an LOD level can be coarser than
  the item without changing its geometry. `SceneItem` builds a **`QTransform`**
  (scale/flip/rotate), not `QGraphicsItem::scale()` — read the scale from
  `item()->scale`.
- `doc::Item::source` is the only member safe to touch off the UI thread; that
  immutability is what makes cross-thread decode and save safe.
- LOD: `ui/levels.*`, `ui/level_loader.*` (decode pool + shared decoded-level
  LRU), `ui/lod_manager.*` (byte accounting, budgets, hints). Settings split:
  the **LOD** tab holds only level-defining knobs (method, fractions, quality);
  cache/RAM/threads/settle live in the **Performance** tab.
- `constants::kFloorLevelSize` is 64 here (the ports use 128); keep it
  referenced through the constant.
- Order-sensitive LOD tests pin `decodeThreads = 1`; completion order with
  N > 1 is not deterministic.

## Versioning

The version is `MAJOR.MINOR.PATCH`, defined once in `CMakeLists.txt`
(`project(BeeXRef VERSION x.y.z)`); CMake passes it to the code as
`constants::Version`, which `--version`, the About dialog and the
startup log use. To release: bump the version there, commit, and tag the
release commit `vX.Y.Z` (annotated), matching the Python reference's
tags (`v0.3.3`). Do not tag from a dirty tree.

## Format & tools

- Native format is `.beex`; `write.cpp` streams it atomically (temp file +
  rename). The fork adds `items.meta`/`items.uuid` and a `lod` table.
- `.bee` is the legacy upstream interchange shape (no meta/uuid/lod, header
  `user_version 2` / `application_id 2060242126`, thumbnails never stored). It
  is export-only and never saved in place; `.bee` files are import-only.
- Thumbnails are always stored on save (there is no setting to disable it).
- `beexref-boardcheck [--write-back PATH] <board>...` migrates on a copy,
  decodes every blob, checks dimensions and diffs all tables; use it to verify
  writer/reader changes against the other ports.
- `QT_QPA_PLATFORM=offscreen ./build/linux-debug/beexref-ui-smoke [board] [outdir]`
  drives the UI and writes screenshots (default outdir `/tmp/opencode/ui-smoke`).
