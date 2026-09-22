cmake_minimum_required(VERSION 3.24)

project(BeeXRef
    VERSION 0.1.0
    DESCRIPTION "BeeXRef reference image viewer"
    LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Svg Network Test)
find_package(Threads REQUIRED)
qt_standard_project_setup()

# Vendored SQLite amalgamation. The flags match the behavior of the
# sqlite3 builds the Python and Go ports use.
add_library(beexref_sqlite3 STATIC third_party/sqlite3/sqlite3.c)
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
    src/constants.h
    src/doc/document.cpp
    src/doc/document.h
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
    src/ui/downloader.cpp
    src/ui/downloader.h
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
    src/ui/scene.cpp
    src/ui/scene.h
    src/ui/scene_item.cpp
    src/ui/scene_item.h
    src/ui/theme.h
    src/ui/view.cpp
    src/ui/view.h
    src/util/format.cpp
    src/util/format.h
    src/util/memory.cpp
    src/util/memory.h)
target_include_directories(beexref_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
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

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
