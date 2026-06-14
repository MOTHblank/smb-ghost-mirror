################################################################################
# Ghost Mirror — Triple-Emulator Ghost Racing for SMB
#
# Reuses Nestopia from Static and SDL2 binaries already in the workspace.
################################################################################
cmake_minimum_required(VERSION 3.15)
project(ghost-mirror CXX)

set(CMAKE_CXX_STANDARD 20)

# ── Nestopia (from local 3rd party) ──────────────────────────────────────────
set(NESTOPIA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rd/nestopia")
add_subdirectory("${NESTOPIA_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/nestopia")

set(NESTOPIA_INCLUDE_DIRS
    "${NESTOPIA_DIR}/source/core/"
    "${NESTOPIA_DIR}/source/core/api"
)

# ── SDL2 (prebuilt binaries) ────────────────────────────────────────────────
set(SDL2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rd/SDL2-Binaries")
set(SDL2_INCLUDE_DIR "${SDL2_DIR}/include" "${SDL2_DIR}/include/SDL2")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(SDL2_LIB_DIR "${SDL2_DIR}/win32/lib64")
else()
    set(SDL2_LIB_DIR "${SDL2_DIR}/win32/lib")
endif()

add_library(SDL2_imported SHARED IMPORTED)
set_target_properties(SDL2_imported PROPERTIES
    IMPORTED_LOCATION "${SDL2_LIB_DIR}/SDL2.dll"
    IMPORTED_IMPLIB   "${SDL2_LIB_DIR}/SDL2.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}"
)

# ── Local static NES library sources ─────────────────────────────────────────
set(STATIC_NES_SOURCES
    "src/nes/nes.cpp"
    "src/nes/nestopiaimpl.cpp"
    "src/util/string.cpp"
)

# ── Ghost Mirror sources ───────────────────────────────────────────────────
set(GHOST_MIRROR_SOURCES
    src/main.cpp
    src/emu_trio.cpp
    src/recorder.cpp
    src/renderer.cpp
    src/input.cpp
    src/savestate_organizer.cpp
    src/splitter.cpp
)

add_executable(ghost-mirror
    ${GHOST_MIRROR_SOURCES}
    ${STATIC_NES_SOURCES}
)

target_include_directories(ghost-mirror PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${NESTOPIA_INCLUDE_DIRS}
    "${SDL2_INCLUDE_DIR}"
)

target_link_libraries(ghost-mirror PRIVATE
    3rdnestopia
    SDL2_imported
)

target_compile_definitions(ghost-mirror PRIVATE
    NST_NO_ZLIB
    _CRT_SECURE_NO_WARNINGS
    SDL_MAIN_HANDLED
)

# On MSVC, suppress some Nestopia-inherited warnings
if(MSVC)
    target_compile_options(ghost-mirror PRIVATE
        /W3 /wd4244 /wd4267 /wd4305 /wd4838 /utf-8
    )
endif()

# ── Copy final binary and SDL2.dll to root and output ───────────────────────
add_custom_command(TARGET ghost-mirror POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL2_LIB_DIR}/SDL2.dll"
        "$<TARGET_FILE_DIR:ghost-mirror>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:ghost-mirror>"
        "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL2_LIB_DIR}/SDL2.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
