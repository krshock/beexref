#pragma once

namespace constants {

inline constexpr char AppName[] = "BeeXRef";
inline constexpr char AppNameFull[] = "BeeXRef Reference Image Viewer";
// The application version, from the CMake project version (the single
// source of truth in CMakeLists.txt): bump it there and tag the release
// commit vX.Y.Z. Shown by --version, the About dialog and the startup
// log.
inline constexpr char Version[] = BEEXREF_VERSION;
// The reference's about box text.
inline constexpr char Copyright[] = "Copyright \u00A9 2021-2024 Rebecca Breu";
// The fork's own copyright: this port's changes.
inline constexpr char ForkCopyright[] = "Copyright \u00A9 2026 krshock";
// The About box's free-software notice links the licence name to its
// full text.
inline constexpr char LicenseName[] = "GNU GPL v3";
inline constexpr char LicenseUrl[] = "https://www.gnu.org/licenses/gpl-3.0.html";

inline constexpr char BeeFileExtension[] = ".bee";
inline constexpr char BeexFileExtension[] = ".beex";

// Browser user agent for web image drops, so sites that block default
// download clients work (same string as the Python and Go ports).
inline constexpr char UserAgent[] =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

// Appended to a settings group's title while its value differs from the
// default, as in the reference.
inline constexpr char kChangedSymbol[] = "\u270E";

// Z-order step: raising an item puts it above the current maximum by
// this much, as in the reference ports.
inline constexpr double kZStep = 0.001;

// Floor level size: the longest side of the coarsest level, used both
// for the runtime minimum level size and for the loading thumbnail
// stored on save. The Python and Go ports use 128 for both; this port
// starts at 64. Expected to become a setting; keep it referenced
// through this constant.
inline constexpr int kFloorLevelSize = 64;

} // namespace constants
