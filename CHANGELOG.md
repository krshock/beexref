# Changelog

All notable changes to BeeXRef are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the
project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.8.0] - 2026-09-28

### Added

- **Panning has momentum**: a fast pan release lets the canvas glide to a
  stop instead of stopping dead; a slow release (or a pause before
  releasing) stops where the cursor left it, and any new press or wheel
  step ends the glide. A deliberate, subtle deviation from the reference.
- **Grayscale methods**: the grayscale toggle can render with Classic
  (the look BeeXRef always had), BT.601 luma, BT.709 luma, Average,
  Lightness, Max or Min — picked from `Images ▸ Grayscale Method` (also
  in the right-click menu). The choice applies instantly and is
  remembered.
- **Peek Scene** (`Shift`+middle-drag): a temporary look at the
  neighbourhood without losing the current view. The pointer's travel
  from where the drag started zooms the canvas out — at a constant
  proportional rate, down to 20% of the starting zoom by the time the
  pointer reaches the window edge — and leans it in the pointer's
  direction, so the canvas "looks at the horizon"; travel is normalized
  per viewport axis, so the window's size and aspect ratio do not change
  the feel. Releasing with Shift still held commits the look — the
  original zoom, centered on the canvas point under the pointer; letting
  Shift go first cancels and restores the exact view. Any mode cancel or
  wheel step cancels. Rebindable in Controls like the other mouse
  actions.
- Windows: `beexref.exe` carries the BeeXRef icon and its file version
  information, so Explorer, the taskbar and the Properties dialog show
  them.

### Changed

- **Decoded levels are written to the disk cache only when they are
  evicted from RAM** — previously every decoded level was written
  immediately, even if it never left memory. A session that keeps its
  levels in RAM now writes nothing to the cache; after an eviction the
  level is still served from disk instead of being decoded again. The
  decoded-level RAM cache also defaults to 512 MB instead of 150 MB
  (still tunable in Settings ▸ Performance), and a single cache entry is
  never larger than 16 MB — one huge level or payload is not worth the
  disk it would hold for the session.
- The session cache setting is labelled for what it does — Session disk
  cache, covering decoded levels and detached payloads — instead of
  mentioning only the undo history.
- Deleting or cutting a large image no longer stalls the window: its
  payload is encoded and moved to the session cache on a worker, and the
  item picks up the cache-backed source when the write lands. An undo
  that arrives before that still restores the image.

### Fixed

- Windows: a save that rewrites the whole file can replace a board the
  app has open (with `Save/incremental` off it used to fail and keep a
  `.tmp`), stale temporary and session files are swept again, and a
  failed save leaves no temporary file behind.

## [0.7.0] - 2026-09-27

### Added

- **Recovery**: a board that cannot be fully read still opens. Missing
  image data, invalid metadata, orphaned thumbnails, files written by a
  newer version and files whose item table is gone are reported in one
  summary (full list in the details and the log), and the status bar
  keeps a `recovered · N` badge until the broken items are deleted.
- **Recovered copies**: a board opened with problems is never written
  over its source. Save As writes a copy in which the imageless items are
  explicit placeholders, and that copy can be saved normally afterwards.
- **Incremental saves** (`Save/incremental`, on by default): saving into
  the board's own file writes only what changed, so Ctrl+S on a large
  board is fast. Save As, migrated or newer files, and turning the
  setting off keep writing a complete new file atomically. A save with
  nothing to write says so with a toast instead of silently doing
  nothing.

### Changed

- Board files stay compatible with the other ports; a recovered copy
  marks its imageless items so they are not mistaken for damage.
- Internal: the extensibility pass (tool host, item-type traits,
  incremental scene sync, menu table, insertion handlers, export and
  LOD-method registries).

### Fixed

- A save can no longer write less than it promises: an image row is never
  written without its image, the written file is verified before it
  replaces the target (and an in-place update before it commits), and a
  failed replace keeps the complete new file and reports its path.
- Saving refuses when the file changed on disk since it was opened
  (another instance, a sync client) instead of overwriting it; a failed
  post-save reopen is logged instead of swallowed.

## [0.6.0] - 2026-09-26

### Added

- A move handle in the canvas' top-left corner for windows whose title
  bar is hidden; it floats above the empty-board overlay.
- `Insert ▸ Text` and in-place text editing (double-click a note).
- A user-level README, the GPLv3 license text, and the free-software
  notice with the logo in the About box.
- A Windows build guide and a packaging script that bundles every
  non-system DLL and the Qt plugins.

### Fixed

- The multi-second startup stall on Windows.
- A Windows session-cache lock when the schema is recreated.
- The Windows portability issues found by the first native build
  (atomic save, RSS accounting, settings paths).

## [0.5.0] - 2026-09-25

The first tagged C++ release: the port of BeeRef's canvas (images, text,
crop, transforms, arrange, exports), the `.beex` board format with
per-image metadata and LOD levels, the session cache, and the Linux and
Windows builds.

[Unreleased]: https://github.com/krshock/beexref/compare/v0.8.0...HEAD
[0.8.0]: https://github.com/krshock/beexref/compare/v0.7.0...v0.8.0
[0.7.0]: https://github.com/krshock/beexref/compare/v0.6.0...v0.7.0
[0.6.0]: https://github.com/krshock/beexref/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/krshock/beexref/releases/tag/v0.5.0
