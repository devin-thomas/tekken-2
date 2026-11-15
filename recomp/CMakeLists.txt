cmake_minimum_required(VERSION 3.20)
# The Mac player release (T2_RELEASE=ON, scripts/release.sh) runs on macOS 11 and
# later, not only on the build machine's macOS: set before project().
if(T2_RELEASE AND CMAKE_HOST_APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS for the player release" FORCE)
endif()
project(Tekken_2_Recomp LANGUAGES C CXX)

include(CTest)

# C++17, the framework's own requirement (runtime.cmake: cxx_std_17). With
# C++20, Apple Clang 21 rejects the bundled fmt 9.1.1 header-only build
# (consteval FMT_STRING in format-inl.h).
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(PSXRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp" CACHE PATH "Path to psxrecomp")
set(GAME_CONFIG "game.toml" CACHE STRING "Path to the USA Rev 1 game config")

find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/project_config.py"
        --config "${GAME_CONFIG}"
    RESULT_VARIABLE _config_result
    ERROR_VARIABLE _config_error)
if(NOT _config_result EQUAL 0)
    message(FATAL_ERROR "${_config_error}")
endif()
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sync_symbols.py" --check
    RESULT_VARIABLE _symbols_result)
if(NOT _symbols_result EQUAL 0)
    message(FATAL_ERROR "Symbol exports are stale; run scripts/sync_symbols.py")
endif()

if(BUILD_TESTING)
    add_test(NAME tekken2_symbol_exports
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
            -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p "test_*.py")
    set_tests_properties(tekken2_symbol_exports
        PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# First pass: no recomp-ui launcher and no netplay. Both are opt-in in the
# framework; this title leaves them off.
set(PSX_RECOMP_UI OFF CACHE BOOL "Build the shared recomp-ui launcher" FORCE)
# Fail configure rather than silently building the setup host without game C.
set(PSXRECOMP_REQUIRE_GAME_C ON CACHE BOOL "Fail configure if generated game C is missing")

# T2_RELEASE=ON: the player release (scripts/release.sh, release.ps1). Besides the
# settings panel below, it maps the source and build folders out of every
# __FILE__ and debug path, so no build machine's folders reach the binary.
option(T2_RELEASE "Player release: minimal settings panel, no build paths" OFF)
# It is also psxrecomp's "public" release channel, so the framework's Playtest
# Diagnostics (input recording and the on-screen display) ship off by default.
# psxrecomp stamps crash reports with `git describe` of its own checkout; a
# player release says its version instead (PSX_BUILD_GIT is the program the
# framework runs for that, so a stand-in prints the version).
if(T2_RELEASE)
    set(PSX_RELEASE_CHANNEL "public" CACHE STRING
        "Release package channel: public or playtest (empty = dev build, not packageable)" FORCE)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _t2_rev LIMIT_COUNT 1)
    string(STRIP "${_t2_rev}" _t2_rev)
    if(CMAKE_HOST_WIN32)
        set(_t2_rev_tool "${CMAKE_CURRENT_BINARY_DIR}/t2_build_rev.cmd")
        file(WRITE "${_t2_rev_tool}" "@echo tekken2-recompiled-${_t2_rev}\r\n")
    else()
        set(_t2_rev_tool "${CMAKE_CURRENT_BINARY_DIR}/t2_build_rev.sh")
        file(WRITE "${_t2_rev_tool}" "#!/bin/sh\necho tekken2-recompiled-${_t2_rev}\n")
        file(CHMOD "${_t2_rev_tool}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    endif()
    set(PSX_BUILD_GIT "${_t2_rev_tool}" CACHE FILEPATH "Build revision program (release: prints the version)" FORCE)
endif()
# On the Mac the release builds SDL3 from psxrecomp's pinned source (static, for
# macOS 11) instead of Homebrew's (built for the build machine's macOS), and leaves
# OpenSSL out: psxrecomp then hashes files with its own portable SHA-256.
if(T2_RELEASE AND APPLE)
    set(CMAKE_DISABLE_FIND_PACKAGE_SDL3 ON)
    set(CMAKE_DISABLE_FIND_PACKAGE_OpenSSL ON)
    set(PSX_SDL3_FETCH ON CACHE BOOL "Fetch the pinned SDL3 release" FORCE)
    set(PSX_STATIC_RUNTIME ON CACHE BOOL "Statically link SDL" FORCE)
endif()
if(T2_RELEASE AND NOT MSVC)
    add_compile_options(
        "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=."
        "-ffile-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=build")
endif()

if(EXISTS "${PSXRECOMP_ROOT}/runtime/runtime.cmake")
    include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

    psxrecomp_add_game_runtime(psx-runtime
        WINDOW_TITLE "Tekken 2 Recompiled"
        DEFAULT_GAME_CONFIG_PATH "${GAME_CONFIG}"
        MAX_PLAYERS 2
        GEN_MARKER "generated/SLUS_002.13_dispatch.c"
        # Before GEN_FULL_GLOB: that multi-value argument would swallow it.
        # Generate writes the file from aot/overlays.json (static_output).
        GAME_OVERLAY_STATIC_C "${CMAKE_CURRENT_SOURCE_DIR}/generated/overlays_static.c"
        GEN_FULL_GLOB "generated/SLUS_002.13_full_*.c"
    )

    if(IOS)
        # iOS app shell (docs/IOS.md): psx_ios_prepare() seeds the app home
        # and imports the disc through the Files document picker. Only iOS
        # builds compile it; scripts/package_ios.sh makes the .app.
        enable_language(OBJC)
        # T2TouchPad.m: the on-screen controller (the input layer's, src/t2_host.c).
        target_sources(psx-runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ios/T2Launch.m"
            "${CMAKE_CURRENT_SOURCE_DIR}/ios/T2TouchPad.m")
        set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/ios/T2Launch.m"
            "${CMAKE_CURRENT_SOURCE_DIR}/ios/T2TouchPad.m"
            PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_link_libraries(psx-runtime PRIVATE
            "-framework UIKit" "-framework UniformTypeIdentifiers" "-framework GameController")
    endif()

    # Controls, settings panel and fullscreen glue, adapted from the GGX+ port
    # (docs/GGX-PORT-REUSE.md): src/input (every pad and the keyboard at once,
    # by position), src/ui (the settings panel, its font and file),
    # src/t2_host.c (patches 0012-0014's hooks). The font is embedded, so no
    # package script ships it.
    set(T2_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    set(T2_FONT "${T2_SRC}/fonts/NotoSans-Regular.ttf")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${T2_FONT}")
    file(READ "${T2_FONT}" _t2_font_hex HEX)
    string(LENGTH "${_t2_font_hex}" _t2_font_len)
    math(EXPR _t2_font_bytes "${_t2_font_len} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _t2_font_hex "${_t2_font_hex}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/t2_font_data.c.in"
        "/* Generated by CMake from src/fonts/NotoSans-Regular.ttf (SIL OFL 1.1; see src/fonts). */\n"
        "const unsigned char t2_font_ttf[] = {${_t2_font_hex}};\n"
        "const unsigned int t2_font_ttf_size = ${_t2_font_bytes};\n")
    configure_file("${CMAKE_CURRENT_BINARY_DIR}/t2_font_data.c.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/t2_font_data.c" COPYONLY)
    add_library(t2_ui STATIC
        "${T2_SRC}/input/t2_input.c"
        "${T2_SRC}/ui/t2_config.c"
        "${T2_SRC}/ui/t2_text.c"
        "${T2_SRC}/ui/t2_settings.c"
        "${CMAKE_CURRENT_BINARY_DIR}/t2_font_data.c")
    target_include_directories(t2_ui PUBLIC
        "${T2_SRC}" "${T2_SRC}/input" "${T2_SRC}/ui" "${T2_SRC}/third_party/stb")
    target_link_libraries(t2_ui PUBLIC ${PSX_SDL_LIBRARIES})
    if(NOT MSVC)
        target_link_libraries(t2_ui PUBLIC m)
    endif()
    set_target_properties(t2_ui PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_sources(psx-runtime PRIVATE "${T2_SRC}/t2_host.c")
    target_link_libraries(psx-runtime PRIVATE t2_ui)

    # The version in VERSION reaches the binary (the settings panel's title) and
    # the package scripts (Info.plist, the Windows launcher's version resource).
    # T2_RELEASE=ON is the player release: the settings panel keeps only what a
    # player sets on first run (no controller mode, renderer, frame skip or
    # display refresh rows). scripts/release.sh and release.ps1 turn it on.
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" T2_VERSION LIMIT_COUNT 1)
    string(STRIP "${T2_VERSION}" T2_VERSION)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
    target_compile_definitions(psx-runtime PRIVATE "T2_VERSION=\"${T2_VERSION}\"")
    if(T2_RELEASE)
        target_compile_definitions(psx-runtime PRIVATE T2_RELEASE=1)
    endif()
    message(STATUS "Tekken 2 Recompiled ${T2_VERSION}, release panel: ${T2_RELEASE}")

    if(BUILD_TESTING)
        # The input layer with SDL's virtual controllers (DualSense, DualShock 4,
        # Xbox, an arcade stick), the settings panel driven by pad presses, and
        # the fullscreen keys and double-click with synthetic SDL events.
        foreach(_t t2_input_test t2_settings_test t2_fullscreen_test)
            if(_t STREQUAL "t2_fullscreen_test")
                add_executable(${_t} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native/${_t}.cpp")
            else()
                add_executable(${_t} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native/${_t}.c")
            endif()
            target_link_libraries(${_t} PRIVATE t2_ui)
            target_include_directories(${_t} PRIVATE "${PSXRECOMP_ROOT}/runtime/include")
            add_test(NAME ${_t} COMMAND ${_t} "${CMAKE_CURRENT_BINARY_DIR}")
            set_tests_properties(${_t} PROPERTIES
                ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
        endforeach()
        target_sources(t2_fullscreen_test PRIVATE "${PSXRECOMP_ROOT}/runtime/src/host_keymap.c")
        target_compile_definitions(t2_fullscreen_test PRIVATE PSX_SDL3=1)
    endif()

    add_custom_command(TARGET psx-runtime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/game.toml"
            "$<TARGET_FILE_DIR:psx-runtime>/game.toml"
        COMMENT "Staging game.toml"
        VERBATIM)
else()
    message(FATAL_ERROR
        "psxrecomp not found at ${PSXRECOMP_ROOT}. "
        "Run: git submodule update --init --recursive recomp/psxrecomp")
endif()
