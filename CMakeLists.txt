cmake_minimum_required(VERSION 3.16)

project(infinite_canvas
    VERSION 0.2.0
    DESCRIPTION "Infinite Canvas - zoomable nested-box outliner for the terminal"
    LANGUAGES CXX
)

option(IC_BUILD_TESTS "Build the unit tests" ON)
option(IC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Shared warning / encoding settings for every target.
add_library(ic_options INTERFACE)
if(MSVC)
    # Sources contain UTF-8 box-drawing characters.
    target_compile_options(ic_options INTERFACE /W4 /utf-8 /permissive-)
    target_compile_definitions(ic_options INTERFACE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    if(IC_WARNINGS_AS_ERRORS)
        target_compile_options(ic_options INTERFACE /WX)
    endif()
else()
    target_compile_options(ic_options INTERFACE -Wall -Wextra -Wpedantic -Wshadow)
    if(IC_WARNINGS_AS_ERRORS)
        target_compile_options(ic_options INTERFACE -Werror)
    endif()
endif()
if(MINGW)
    # Self-contained executables: no dependency on whichever MinGW runtime
    # DLLs happen to be first on PATH.
    target_link_options(ic_options INTERFACE -static)
endif()

# Small release binaries. Speed is irrelevant for a console UI, size is what
# people notice when downloading: optimize for size, drop unused functions
# and data, strip symbols. MSVC links its runtime statically for the same
# self-contained single .exe as MinGW.
option(IC_SMALL_RELEASE "Optimize release builds for size" ON)
if(IC_SMALL_RELEASE)
    if(MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(ic_options INTERFACE $<$<CONFIG:Release>:/O1 /Gy>)
        target_link_options(ic_options INTERFACE $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>)
    else()
        target_compile_options(ic_options INTERFACE
            $<$<CONFIG:Release>:-Os -ffunction-sections -fdata-sections>)
        if(APPLE)
            target_link_options(ic_options INTERFACE $<$<CONFIG:Release>:-Wl,-dead_strip>)
        else()
            target_link_options(ic_options INTERFACE $<$<CONFIG:Release>:-Wl,--gc-sections -s>)
        endif()
    endif()
endif()

# ----------------------------------------------------------------------------
# Core: model, storage, layout/rendering into a cell buffer, input logic.
# Platform independent, so it builds and is tested on any OS.
# ----------------------------------------------------------------------------
add_library(infinite_canvas_core STATIC
    src/i18n/i18n.cpp
    src/util/file.cpp
    src/util/json.cpp
    src/util/process.cpp
    src/model/node.cpp
    src/model/canvas.cpp
    src/storage/document_json.cpp
    src/storage/json_storage.cpp
    src/storage/markdown_export.cpp
    src/view/console_renderer.cpp
    src/input/handler.cpp
    src/input/history.cpp
    src/app/project_manager.cpp
    src/app/settings.cpp
    src/app/demo.cpp
)
target_include_directories(infinite_canvas_core PUBLIC include)
target_link_libraries(infinite_canvas_core PUBLIC ic_options)

# ----------------------------------------------------------------------------
# Executable. The interactive editor uses the Win32 console API; elsewhere
# the binary still supports --snapshot (headless rendering).
# ----------------------------------------------------------------------------
add_executable(infinite_canvas src/main.cpp)
target_link_libraries(infinite_canvas PRIVATE infinite_canvas_core)

if(WIN32)
    target_sources(infinite_canvas PRIVATE
        src/app/app.cpp
        src/platform/console_input.cpp
        src/platform/console_output.cpp
        src/platform/popup_menu.cpp
    )
endif()

install(TARGETS infinite_canvas RUNTIME DESTINATION bin)

# ----------------------------------------------------------------------------
# Tests
# ----------------------------------------------------------------------------
if(IC_BUILD_TESTS)
    enable_testing()
    add_executable(ic_tests
        tests/test_main.cpp
        tests/test_utf8.cpp
        tests/test_json.cpp
        tests/test_canvas.cpp
        tests/test_storage.cpp
        tests/test_renderer.cpp
        tests/test_input.cpp
        tests/test_history.cpp
        tests/test_project.cpp
        tests/test_i18n.cpp
    )
    target_link_libraries(ic_tests PRIVATE infinite_canvas_core)
    add_test(NAME unit_tests COMMAND ic_tests)
endif()
