# Changelog

All notable changes to BeeXRef are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the
project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

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
  setting off keep writing a complete new file atomically.

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

[Unreleased]: https://github.com/krshock/beexref/compare/v0.6.0...HEAD
[0.6.0]: https://github.com/krshock/beexref/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/krshock/beexref/releases/tag/v0.5.0
