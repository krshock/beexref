# BeeXRef

BeeXRef is a free, open-source moodboard for your desktop: an infinite
canvas that holds hundreds of reference images while you work. Keep your
references in one place, arrange and inspect them, and leave the board
floating over your art program.

This is the C++/Qt 6 build, a multiplatform application that runs on
Linux and Windows (x64).

## What you can do

- **Collect references** — drag images from your file manager onto the
  board, or paste images and text from the clipboard.
- **Navigate an endless board** — pan and zoom freely, fit the whole
  board or just the selection to the window.
- **Arrange** — move, scale, rotate, flip and crop images; line them up
  in rows, columns or a square; normalize their widths, heights or
  sizes.
- **Adjust** — opacity and grayscale per image, without altering the
  original file.
- **Inspect** — per-image name, author, source URL and notes, a color
  gamut view, and a color picker that copies any pixel to the clipboard.
- **Keep it handy** — float the window over your other apps, turn the
  title bar off and move the window by the handle in the canvas'
  top-left corner, toggle the HUD, and drive the app from the keyboard.
- **Work safely** — full undo/redo, clipboard, and z-order controls.
- **Export** — save the board as an image (PNG, JPEG or SVG), write every
  image out to a folder, or export the legacy `.bee` format.

## Board files

Boards are saved as `.beex` files: one file holds your images, their
positions and adjustments, and their metadata. Legacy `.bee` files from
the original BeeRef can be opened and imported.

## License

BeeXRef is free software under the GPL-3.0 license (see `LICENSE`).

## About BeeXRef

BeeXRef is a fork of [BeeRef](https://github.com/rbreu/beeref) by Rebecca Breu
Its a port from the original python code to C++ plus some new features

Compared to BeeRef it adds:
- Big memory usage optimizations to allow hundreds if images in
the same scene
- You can edit name, author name or notes of any image
- a new `.beex` board format, with editable per-image metadata: name,
  author, origin link and notes
- Original beeref `.bee` file format is supported as import/export only
- Image smoothing settings
- board and image exports (PNG/JPEG/SVG, or every image to a folder);
- "spotlight" brings an image to the front without modifying its z-order (non-destructive)

## Development

The extension points — tools, item types, drop/paste handlers, export
formats, LOD methods, settings and commands — are documented in
[`docs/extending.md`](docs/extending.md).
