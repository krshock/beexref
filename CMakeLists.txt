cmake_minimum_required(VERSION 3.24)

project(BeeXRef
    VERSION 0.7.0
    DESCRIPTION "BeeXRef reference image viewer"
    LANGUAGES C CXX)

# The version is the single source of truth: it reaches the app as
# constants::Version (--version, the About dialog, the startup log).
# Bump MAJOR.MINOR.PATCH here and tag the release commit vX.Y.Z
# (annotated), like the Python reference's tags.
if(NOT PROJECT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR
        "project VERSION must be MAJOR.MINOR.PATCH, got '${PROJECT_VERSION}'")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Svg Network Test)
find_package(Threads REQUIRED)
qt_standard_project_setup()

# Qt comes from QT_DIR (the aqt install directory), which the presets
# pass as CMAKE_PREFIX_PATH; nothing machine-specific lives in the
# presets, so a fresh checkout builds with QT_DIR set or
# -DCMAKE_PREFIX_PATH=<prefix> on the command line.

# mold is a link-time speedup, not a requirement: use it when it is
# installed, so a machine without it still configures. -DBEEXREF_USE_MOLD=OFF
# disables the detection.
option(BEEXREF_USE_MOLD "Link with mold when it is installed" ON)
if(BEEXREF_USE_MOLD AND UNIX AND NOT APPLE)
    find_program(BEEXREF_MOLD_EXECUTABLE mold)
    if(BEEXREF_MOLD_EXECUTABLE)
        add_link_options(-fuse-ld=mold)
    endif()
endif()

# Vendored SQLite amalgamation. The flags match the behavior of the
# sqlite3 builds the Python and Go ports use.
add_library(beexref_sqlite3 STATIC third_party/sqlite3/sqlite3.c)
# The directory is on the include path, so it must never contain a file
# named "version": Windows' case-insensitive filesystem would let it
# shadow C++'s <version> header. The provenance lives in VERSION.txt.
target_include_directories(beexref_sqlite3 SYSTEM PUBLIC third_party/sqlite3)
target_compile_definitions(beexref_sqlite3 PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_OMIT_DEPRECATED
    SQLITE_OMIT_LOAD_EXTENSION)
set_target_properties(beexref_sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON)

qt_add_library(beexref_core STATIC
    src/board/board.cpp
    src/board/board.h
    src/board/error.h
    src/board/schema.cpp
    src/board/schema.h
    src/board/sqlite.cpp
    src/board/sqlite.h
    src/board/write.cpp
    src/board/write.h
    src/cache/session_cache.cpp
    src/cache/session_cache.h
    src/constants.h
    src/doc/document.cpp
    src/doc/document.h
    src/doc/image_export.cpp
    src/doc/image_export.h
    src/doc/image_io.cpp
    src/doc/image_io.h
    src/doc/item.cpp
    src/doc/item.h
    src/doc/source.cpp
    src/doc/source.h
    src/doc/undo.cpp
    src/doc/undo.h
    src/logging.cpp
    src/logging.h
    src/settings.cpp
    src/settings.h
    src/ui/actions.cpp
    src/ui/actions.h
    src/ui/color_gamut.cpp
    src/ui/controls.cpp
    src/ui/controls.h
    src/ui/controls_dialog.cpp
    src/ui/controls_dialog.h
    src/ui/color_gamut.h
    src/ui/color_sampler_tool.cpp
    src/ui/color_sampler_tool.h
    src/ui/color_swatch.cpp
    src/ui/color_swatch.h
    src/ui/color_tools.cpp
    src/ui/color_tools.h
    src/ui/crop_tools.cpp
    src/ui/export_conflict_dialog.cpp
    src/ui/export_conflict_dialog.h
    src/ui/export_formats.cpp
    src/ui/export_formats.h
    src/ui/crop_tools.h
    src/ui/crop_tool.cpp
    src/ui/crop_tool.h
    src/ui/downloader.cpp
    src/ui/downloader.h
    src/ui/grayscale.cpp
    src/ui/grayscale.h
    src/ui/hud.cpp
    src/ui/info_dialogs.cpp
    src/ui/info_dialogs.h
    src/ui/item_types.cpp
    src/ui/item_types.h
    src/ui/hud.h
    src/ui/hud_preview.cpp
    src/ui/hud_preview.h
    src/ui/layout_ops.cpp
    src/ui/metadata_panel.cpp
    src/ui/metadata_panel.h
    src/ui/move_handle.cpp
    src/ui/move_handle.h
    src/ui/move_window_tool.cpp
    src/ui/move_window_tool.h
    src/ui/layout_ops.h
    src/ui/opacity_dialog.cpp
    src/ui/opacity_dialog.h
    src/ui/pan_tool.cpp
    src/ui/pan_tool.h
    src/ui/cursors.cpp
    src/ui/cursors.h
    src/ui/selection_ops.cpp
    src/ui/settings_dialog.cpp
    src/ui/settings_dialog.h
    src/ui/selection_ops.h
    src/ui/selection_tools.cpp
    src/ui/selection_tools.h
    src/ui/downloader.h
    src/ui/drag_zoom_tool.cpp
    src/ui/drag_zoom_tool.h
    src/ui/drop.cpp
    src/ui/drop.h
    src/ui/input_controller.cpp
    src/ui/input_controller.h
    src/ui/level_loader.cpp
    src/ui/level_loader.h
    src/ui/levels.cpp
    src/ui/levels.h
    src/ui/lod_manager.cpp
    src/ui/lod_manager.h
    src/ui/main_window.cpp
    src/ui/main_window.h
    src/ui/menu_layout.cpp
    src/ui/menu_layout.h
    src/ui/scene.cpp
    src/ui/scene.h
    src/ui/scene_export.cpp
    src/ui/scene_export.h
    src/ui/scene_export_dialog.cpp
    src/ui/scene_export_dialog.h
    src/ui/scene_item.cpp
    src/ui/scene_item.h
    src/ui/rendering.cpp
    src/ui/rendering.h
    src/ui/theme.h
    src/ui/text_edit_tool.cpp
    src/ui/text_edit_tool.h
    src/ui/tool.cpp
    src/ui/tool.h
    src/ui/welcome_overlay.cpp
    src/ui/welcome_overlay.h
    src/ui/view.cpp
    src/ui/view.h
    src/util/format.cpp
    src/util/format.h
    src/util/memory.cpp
    src/util/memory.h
    src/util/process.cpp
    src/util/process.h)
target_include_directories(beexref_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
target_compile_definitions(beexref_core PUBLIC BEEXREF_VERSION="${PROJECT_VERSION}")
qt_add_resources(beexref_core "beexref_assets"
    PREFIX "/"
    FILES
        assets/cursors/cursor_flip_h.png
        assets/cursors/cursor_flip_v.png
        assets/cursors/cursor_rotate.png
        assets/documentation/controls.html
        assets/logo.png)
target_link_libraries(beexref_core
    PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::Network Threads::Threads
    PRIVATE beexref_sqlite3)
if(WIN32)
    target_link_libraries(beexref_core PRIVATE psapi)
endif()
beexref_set_warnings(beexref_core)

qt_add_executable(beexref src/main.cpp)
target_link_libraries(beexref PRIVATE beexref_core)
beexref_set_warnings(beexref)
set_target_properties(beexref PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE)

qt_add_executable(beexref-boardcheck tools/boardcheck/main.cpp)
target_link_libraries(beexref-boardcheck PRIVATE beexref_core)
beexref_set_warnings(beexref-boardcheck)

# Offscreen UI smoke harness: drives the real window with in-process
# events and writes screenshots; never touches the window system.
qt_add_executable(beexref-ui-smoke tools/ui_smoke/main.cpp)
target_link_libraries(beexref-ui-smoke PRIVATE beexref_core Qt6::Test)
beexref_set_warnings(beexref-ui-smoke)

include(CheckIPOSupported)
check_ipo_supported(RESULT beexref_ipo_supported OUTPUT beexref_ipo_error)
if(beexref_ipo_supported)
    set_property(TARGET beexref_core beexref
        PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

include(GNUInstallDirs)
install(TARGETS beexref
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Desktop integration for distro packages and the AppImage build (the
# icon keeps the desktop entry's Icon= name). The AppImage script stages
# the same two files; see docs/linux-appimage.md.
if(UNIX AND NOT APPLE)
    install(FILES packaging/beexref.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
    install(FILES assets/logo.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps
        RENAME beexref.png)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
