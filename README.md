# BeeXRef

BeeXRef is a free, open-source moodboard for your desktop: an infinite
canvas that holds hundreds of reference images while you work. Keep your
references in one place, arrange and inspect them, and leave the board
floating over your art program.

This is the C++/Qt 6 build.

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

## Running BeeXRef

Building requires CMake, a C++ compiler and Qt 6.8 or newer:

    cmake --preset linux-release
    cmake --build --preset linux-release
    ./build/linux-release/beexref my-board.beex

On Windows, `docs/building-windows.md` covers the MSYS2 build and the
self-contained package.

## Board files

Boards are saved as `.beex` files: one file holds your images, their
positions and adjustments, and their metadata. Legacy `.bee` files from
the original BeeRef can be opened and imported.

## License

BeeXRef is free software under the GPL-3.0 license (see `LICENSE`).
It is part of the [BeeRefX fork](https://github.com/rbreu/beeref) of
BeeRef. Developers: see `AGENTS.md`.
