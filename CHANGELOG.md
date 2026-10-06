# Changelog

All notable changes to BeeXRef are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the
project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- **Peek Scene preview**: while peeking, a dashed rectangle shows the
  view the release will land on — the commit destination under the
  pointer with `Shift` held, or the view you return to when `Shift` is
  let go — so you can see how much of the zoomed-out canvas each choice
  will cover.
- **Selection name plate**: selecting an image shows its name (the first
  30 characters) in the bottom-left corner, with the author in bold and
  the year in parentheses next to it above, and the resolution below. It
  fades in, holds three seconds and fades out; a new selection, a
  double-click on the image or a metadata edit that changes what it shows
  brings it back, and clicks and the wheel pass through it to the canvas.
  Without a name it still appears when there is an author or a year to
  show.
- **Author suggestions**: the Author field completes from the authors
  already used in the board — case and accents don't matter (`jose`
  finds `José`), duplicate spellings collapse, and blank or invisible
  names are left out.
- **Year and Collection**: two new metadata fields. Year takes a number
  from -999999 to 999999 (leave it empty for none); Collection names the
  series, franchise or film an image belongs to, with the same
  suggestions as the Author field.
- **Open the source link**: with a web address in the URL field, the URL
  caption underlines itself and gains a `↗` (keeping your theme's label
  colour), and double-clicking it opens the address in your browser. Only
  `http` and `https` links are handed over — a file path or a script
  never launches anything.
- **Compact Board**: File ▸ Compact Board writes a smaller copy of the
  board by re-encoding its lossless images — losslessly (WebP, pixel for
  pixel) or imperceptibly (quality 95 for photographs, lossless where
  artifacts would show). The original file is left untouched, and a
  `.bee` export converts those images back to png/jpg so upstream BeeRef
  still opens it.
- **Image Storage setting**: how images entering the board are encoded —
  keep the originals (default), compact lossless, or compact
  imperceptibly. Pasted images are no longer stored as oversized PNG:
  they become lossless WebP.
- **A new window**: File ▸ New Window (`Ctrl+Shift+N`) opens a second
  BeeXRef window with its own board, undo history and memory, so a heavy
  board never holds up the other window. From an AppImage the new window
  starts from the `.AppImage` file itself, and closing the window you
  started from never affects it.
- **The window follows your desktop's light or dark mode**: the window,
  menus and dialogs take the desktop setting — the AppImage included,
  which used to ignore it. Settings ▸ Miscellaneous ▸ Theme picks
  between Follow the system, Dark and Light; the canvas and the HUD keep
  their dark look in every mode.
- **Edit several images at once**: with more than one image selected,
  the metadata panel (`I`) shows the fields that can be shared — Author,
  Collection and Year — and writes the ones you edit to every selected
  image in a single step, undo included. A field with different values
  shows `(multiple)`, and the fields you leave alone are left alone.

### Fixed

- **Unsaved metadata edits are no longer dropped by Save**: `Ctrl+S` (and
  Save As, the `.bee` export, New Scene, Open and quitting) commits what
  is still open in the metadata panel first, so it lands in the saved
  file — and the unsaved-changes prompt now sees it too.
- **Open asks before discarding unsaved changes**: opening another board
  used to replace the current one without a word; it now offers the same
  confirmation as New Scene and quitting.

### Changed

- **Metadata fields tidy themselves up when saved**: surrounding spaces
  are trimmed and repeated spaces collapse, while the notes keep their
  line breaks. The fields stop taking input past name 255, author 128,
  collection 128, URL 2048 and notes 4096 characters, and a longer value
  a board already holds is left untouched.

## [0.9.0] - 2026-09-28

### Added

- **Window memory**: the main window comes back the size, position and
  state it was left in; with nothing saved yet it opens at 600x450.

### Fixed

- The About box opens at a readable size; its wrapped text used to
  collapse it into a narrow column.

## [0.8.0] - 2026-09-28

### Added

- **Panning has momentum**: a fast pan release lets the canvas glide to a
  stop instead of stopping dead; a slow release (or a pause before
  releasing) stops where the cursor left it, and any new press or wheel
  step ends the glide. A deliberate, subtle deviation from BeeRef, which
  stops dead.
- **Grayscale methods**: the grayscale toggle can render with Classic
  (the look BeeXRef always had), BT.601 luma, BT.709 luma, Average,
  Lightness, Max or Min — picked from `Images ▸ Grayscale Method` (also
  in the right-click menu). The choice applies instantly and is
  remembered.
- **Peek Scene** (`Shift`+middle-drag): a temporary look at the
  neighbourhood without losing the current view. Travel from where the
  drag started zooms the canvas out (down to 20% by the time the pointer
  reaches the window edge) and leans it toward the pointer, so the canvas
  looks at the horizon; the feel is the same in any window size.
  Releasing with `Shift` still held keeps the look — your usual zoom,
  centered on the point under the pointer; letting `Shift` go first
  cancels it. Rebindable in Controls like the other mouse actions.
- **Windows icon and file details**: `beexref.exe` carries the BeeXRef
  icon and its version, so Explorer, the taskbar and the file's
  Properties show them.

### Changed

- **The session disk cache writes less**: decoded levels go to disk only
  once they leave memory, so a session that keeps its levels in RAM
  writes nothing, and a level that left memory still loads from disk
  instead of being decoded again. The in-RAM level cache now holds up to
  512 MB (Settings ▸ Performance), and single entries above 16 MB are not
  written.
- **Session disk cache setting**: the option is labelled for what it
  covers — decoded levels and images kept for undo — instead of only the
  undo history.
- **Deleting or cutting a large image no longer freezes the window**: its
  bytes move to the session cache in the background, and an undo that
  arrives before the move finishes still restores the image.

### Fixed

- **Windows saves**: a full save (with `Save/incremental` off) can now
  replace a board the app has open instead of failing and leaving a
  `.tmp` behind; stale temporary and session files are cleaned up again;
  and a failed save leaves no temporary file behind.

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

### Fixed

- **Saves are verified**: an image row is never written without its
  image, the written file is checked before it replaces the target (and
  an update before it commits), and a failed replace keeps the complete
  new file and reports its path.
- Saving refuses to overwrite a file that changed on disk since it was
  opened (another instance, a sync client) and reports a failed reopen
  instead of ignoring it.

## [0.6.0] - 2026-09-26

### Added

- A move handle in the canvas' top-left corner for windows whose title
  bar is hidden; it floats above the empty-board overlay.
- `Insert ▸ Text` and in-place text editing (double-click a note).
- A user-level README, the GPLv3 license text, and the free-software
  notice with the logo in the About box.
- A Windows build guide and a packaging script that bundles everything
  the app needs to run without a development environment.

### Fixed

- The multi-second startup stall on Windows.
- A Windows startup failure that could lock the session cache.
- The Windows issues found by the first native build (saving, memory
  accounting, settings paths).

## [0.5.0] - 2026-09-25

The first tagged C++ release: the port of BeeRef's canvas (images, text,
crop, transforms, arrange, exports), the `.beex` board format with
per-image metadata and LOD levels, the session cache, and the Linux and
Windows builds.

[Unreleased]: https://github.com/krshock/beexref/compare/v0.9.0...HEAD
[0.9.0]: https://github.com/krshock/beexref/compare/v0.8.0...v0.9.0
[0.8.0]: https://github.com/krshock/beexref/compare/v0.7.0...v0.8.0
[0.7.0]: https://github.com/krshock/beexref/compare/v0.6.0...v0.7.0
[0.6.0]: https://github.com/krshock/beexref/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/krshock/beexref/releases/tag/v0.5.0
