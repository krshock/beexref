#pragma once

namespace constants {

inline constexpr char AppName[] = "BeeXRef";
inline constexpr char AppNameFull[] = "BeeXRef Reference Image Viewer";
inline constexpr char Version[] = "0.1.0";

inline constexpr char BeeFileExtension[] = ".bee";
inline constexpr char BeexFileExtension[] = ".beex";

// Browser user agent for web image drops, so sites that block default
// download clients work (same string as the Python and Go ports).
inline constexpr char UserAgent[] =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

// Floor level size: the longest side of the coarsest level, used both
// for the runtime minimum level size and for the loading thumbnail
// stored on save. The Python and Go ports use 128 for both; this port
// starts at 64. Expected to become a setting; keep it referenced
// through this constant.
inline constexpr int kFloorLevelSize = 64;

} // namespace constants
