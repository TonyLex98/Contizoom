cmake_minimum_required(VERSION 3.16)

project(infinite_canvas
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Infinite Canvas - Fractal text-based design tool"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Source files
# ============================================================================
set(MODEL_SOURCES
    src/model/node.cpp
    src/model/connection.cpp
    src/model/canvas.cpp
)

set(STORAGE_SOURCES
    src/storage/json_storage.cpp
)

set(VIEW_SOURCES
    src/view/console_renderer.cpp
)

set(INPUT_SOURCES
    src/input/handler.cpp
    src/input/console_input.cpp
)

set(APP_SOURCES
    src/app/app.cpp
    src/app/project_manager.cpp
    src/app/project_menu.cpp
    src/main.cpp
)

set(ALL_SOURCES
    ${MODEL_SOURCES}
    ${STORAGE_SOURCES}
    ${VIEW_SOURCES}
    ${INPUT_SOURCES}
    ${APP_SOURCES}
)

# ============================================================================
# Include directories
# ============================================================================
include_directories(
    include
)

# ============================================================================
# Library (core)
# ============================================================================
add_library(infinite_canvas_core STATIC
    ${MODEL_SOURCES}
    ${STORAGE_SOURCES}
    ${VIEW_SOURCES}
    ${INPUT_SOURCES}
    src/app/app.cpp
    src/app/project_manager.cpp
    src/app/project_menu.cpp
)

target_include_directories(infinite_canvas_core
    PUBLIC include
    PRIVATE include
)

# Compiler warnings
target_compile_options(infinite_canvas_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# ============================================================================
# Executable
# ============================================================================
add_executable(infinite_canvas src/main.cpp)

target_link_libraries(infinite_canvas PRIVATE infinite_canvas_core)

target_compile_options(infinite_canvas PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# ============================================================================
# Install
# ============================================================================
install(TARGETS infinite_canvas infinite_canvas_core
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include)

# ============================================================================
# Version config (generated)
# ============================================================================
configure_file(
    "${CMAKE_SOURCE_DIR}/include/version/version.h"
    "${CMAKE_SOURCE_DIR}/include/version/version.h"
    COPYONLY
)
