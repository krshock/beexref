# AGENTS.md

BeeXRef in C++/Qt6: publicly a C++ port of the BeeRef infinite-canvas
reference-image viewer (https://github.com/rbreu/beeref), which saves
boards as `.bee` and has none of the features this port adds. Keep
`.beex` byte-compatible and share the app's config/cache paths.
Priorities, in order: low RAM, then low CPU, then simplicity. Call out
deliberate deviations.

## History

The `.beex` format and the extended behaviour (LOD, the metadata editor,
spotlight, the exports) were first implemented in `beerefx`, a Python
implementation that came before this port. The C++ port is complete and
stands on its own: docs and comments describe this codebase's rules
directly, without leaning on that origin.

## References (not in this repo)

- Upstream BeeRef — the public original, `.bee` boards only:
  https://github.com/rbreu/beeref. Never attribute this port's features
  to it in public text.
- `beerefx` — the Python implementation this port grew out of (private,
  not user-facing): `../beeref/beerefx/` (sibling checkout)
- Go/Fyne sibling port: `../beerefx-go/beexref/` (sibling checkout)
- Board-format questions: `fileio/legacy.py`, `fileio/sql.py` and
  `fileio/export.py` in that checkout define the on-disk and export
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
release commit `vX.Y.Z` (annotated). Do not tag from a dirty tree. The
scripts in `tools/release/` automate the whole flow (version bump,
changelog, both artifacts, checksums, GitHub release); see
`docs/releasing.md`.

## Format & tools

- Native format is `.beex` (upstream BeeRef only has `.bee`);
  `write.cpp` streams it atomically (temp file + rename). It adds
  `items.meta`/`items.uuid` and a `lod` table on top of upstream's
  tables.
- `.bee` is upstream BeeRef's native shape (no meta/uuid/lod, header
  `user_version 2` / `application_id 2060242126`, thumbnails never
  stored). This port imports and exports it, but never saves in place.
- Thumbnails are always stored on save (there is no setting to disable it).
- Saves are atomic and verified: the temp file must match the records
  (item rows, one blob per image, no orphaned floors, header) before it
  replaces the target. A pixmap that cannot produce its bytes fails the
  save instead of writing an image-less row, and a failed rename keeps
  the complete temp file and reports its path. On Windows the replace is
  retried for about 1.5 s on sharing/lock/access errors first, for
  antivirus, the indexer and sync clients. An in-place save refuses when
  the file changed on disk since it was opened (`Board::hasChangedOnDisk`
  checks size, mtime and SQLite's `data_version`), so another instance or
  a sync client is never clobbered: the caller writes a copy instead.
- A board with item-level problems still opens: `Document::damage()` lists
  them (missing blobs, non-JSON metadata, non-finite geometry, orphaned
  floors), the open dialog summarises by kind with the full list in the
  scrollable details area, and the status bar keeps a `recovered · N`
  badge counting the entries whose item is still in the scene.
- A newer-version file opens read-only and is marked (`newer board
  version`), never migrated or downgraded in place. A file whose item
  table is gone but whose blob store survives is salvaged: the images
  come back as items with their filename and no position (`recovered
  image` entries), and Save As writes them into a current-format copy.
- A damaged board is never saved in place: `Document::save` refuses
  (createNew false) and Save As writes a recovered copy whose imageless
  items are explicit placeholders (`data["placeholder"]`, no blob). The
  copy reopens clean -- the mark means "known gone", not damage -- and
  can be saved in place from then on. The `.bee` export stays strict and
  refuses placeholder rows.
- A save into the document's own file applies only its change set in
  place (`Document::changes()` -> `board::update`) when the file is
  native, current, healthy, writable and unchanged on disk, and
  `Save/incremental` is on (the default); everything else -- Save As, a
  migrated or newer file, the setting off -- writes a complete new file
  atomically. New row ids start above the highest id ever seen
  (`Document::maxSeenId_`), so a freed id is never reused while undo
  could restore an item that carries it. `board::update` verifies its own
  result before committing -- item and blob counts against the change
  set, no orphaned floors, header intact -- and a mismatch rolls the
  transaction back, leaving the file as it was.
- `beexref-boardcheck [--write-back PATH] <board>...` migrates on a copy,
  decodes every blob, checks dimensions and diffs all tables; use it to verify
  writer/reader changes against the other ports.
- `QT_QPA_PLATFORM=offscreen ./build/linux-debug/beexref-ui-smoke [board] [outdir]`
  drives the UI and writes screenshots (default outdir `/tmp/opencode/ui-smoke`).
