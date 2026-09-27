# AGENTS.md

BeeXRef in C++/Qt6: publicly a C++ port of the BeeRef infinite-canvas
reference-image viewer (https://github.com/rbreu/beeref), which saves
boards as `.bee` and has none of the features this port adds. The
development reference is a private Python fork of BeeRef that introduced
those features — the `.beex` format, the LOD system, the metadata editor,
spotlight, the exports — and this port follows that fork's behaviour,
including its quirks. In comments and docs, "the reference" means that
fork, never upstream BeeRef. Keep `.beex` byte-compatible with the fork
and share the app's config/cache paths. Priorities, in order: low RAM,
then low CPU, then simplicity. Call out deliberate deviations.

## References (not in this repo)

- Upstream BeeRef — the public original, `.bee` boards only, no `.beex`,
  LOD, metadata editor or spotlight: https://github.com/rbreu/beeref.
  Never attribute this port's features to it in public text.
- Python fork — the port's behaviour and `.beex` source; private, not
  user-facing: `../beeref/beerefx/` (sibling checkout)
- Go/Fyne sibling port of the same fork: `../beerefx-go/beexref/`
  (sibling checkout)
- When semantics are unclear, read the fork. `fileio/legacy.py`,
  `fileio/sql.py`, `fileio/export.py` define the on-disk and export
  shapes.

## Build & test

```sh
cmake --preset linux-debug && cmake --build --preset linux-debug
ctest --preset linux-debug
# same for linux-release
```

- Presets live in `CMakePresets.json`. `linux-debug` is **RelWithDebInfo**, not
  a debug build. Both use ccache; mold is used automatically when it is
  installed (`-DBEEXREF_USE_MOLD=OFF` disables it; LTO is on for release).
- Requires Qt >= 6.8. Point the presets at your Qt by exporting `QT_DIR`
  (the aqt install directory, e.g. `$HOME/Qt/6.11.2/gcc_64`), or pass
  `-DCMAKE_PREFIX_PATH=...` for a one-off. The presets carry no machine
  paths.
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
- Extension points (tools, item types, insertion handlers, export formats,
  LOD methods, settings, commands) are documented in `docs/extending.md`.

## Versioning

The version is `MAJOR.MINOR.PATCH`, defined once in `CMakeLists.txt`
(`project(BeeXRef VERSION x.y.z)`); CMake passes it to the code as
`constants::Version`, which `--version`, the About dialog and the
startup log use. To release: bump the version there, commit, and tag the
release commit `vX.Y.Z` (annotated), matching the Python fork's tags
(`v0.3.3`). Do not tag from a dirty tree. The scripts in
`tools/release/` automate the whole flow (version bump, changelog, both
artifacts, checksums, GitHub release); see `docs/releasing.md`.

## Format & tools

- Native format is `.beex`, the fork's extended board format (upstream
  BeeRef only has `.bee`); `write.cpp` streams it atomically (temp file +
  rename). The fork adds `items.meta`/`items.uuid` and a `lod` table on
  top of upstream's tables.
- `.bee` is upstream BeeRef's native shape (no meta/uuid/lod, header
  `user_version 2` / `application_id 2060242126`, thumbnails never
  stored). This port imports and exports it, but never saves in place.
- Thumbnails are always stored on save (there is no setting to disable it).
- `beexref-boardcheck [--write-back PATH] <board>...` migrates on a copy,
  decodes every blob, checks dimensions and diffs all tables; use it to verify
  writer/reader changes against the other ports.
- `QT_QPA_PLATFORM=offscreen ./build/linux-debug/beexref-ui-smoke [board] [outdir]`
  drives the UI and writes screenshots (default outdir `/tmp/opencode/ui-smoke`).
