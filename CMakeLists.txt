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
    src/board/error.h
    src/board/schema.cpp
    src/board/schema.h
    src/board/sqlite.cpp
    src/board/sqlite.h
    src/constants.h
    src/logging.cpp
    src/logging.h
    src/settings.cpp
    src/settings.h)
target_include_directories(beexref_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
target_link_libraries(beexref_core
    PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::Network Threads::Threads
    PRIVATE beexref_sqlite3)
beexref_set_warnings(beexref_core)

qt_add_executable(beexref src/main.cpp)
target_link_libraries(beexref PRIVATE beexref_core)
beexref_set_warnings(beexref)
set_target_properties(beexref PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE)

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
