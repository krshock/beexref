# Changelog

All notable changes to BeeXRef are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the
project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Changed

- Internal: the extensibility pass (tool host, item-type traits,
  incremental scene sync, menu table, insertion handlers, export and
  LOD-method registries). No behaviour or board-format changes.

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
