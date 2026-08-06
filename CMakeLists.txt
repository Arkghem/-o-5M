cmake_minimum_required(VERSION 3.20)
project(o5m LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# LSP Supply
# ---------------------------------------------------------------------------
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# C++ Standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Vendored Third-Party Dependencies
# ---------------------------------------------------------------------------
add_subdirectory(third_party)
# ---------------------------------------------------------------------------
# Core Library
# ---------------------------------------------------------------------------
add_subdirectory(core)

# ---------------------------------------------------------------------------
# System / Package Dependencies
# ---------------------------------------------------------------------------
# OpenGL is provided by the system graphics driver.
find_package(OpenGL REQUIRED)

# Vulkan (Optional — Vulkan RHI backend, Phase 10+)
# Enable with: cmake -DO5M_VULKAN=ON ..
# Requires Vulkan SDK: https://vulkan.lunarg.com/
option(O5M_VULKAN "Build with Vulkan RHI backend support" OFF)

if(O5M_VULKAN)
    find_package(Vulkan REQUIRED)
    message(STATUS "Vulkan SDK found: ${Vulkan_INCLUDE_DIRS}")
    add_compile_definitions(O5M_HAS_VULKAN)
endif()

# GLFW3 — window and input management. Install via vcpkg or system package.
#   vcpkg: ./vcpkg install glfw3
#   Homebrew (macOS): brew install glfw
#   apt (Linux): sudo apt install libglfw3-dev
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    # Some distributions package GLFW as "glfw" (lowercase).
    find_package(glfw QUIET)
endif()
if(NOT glfw3_FOUND AND NOT glfw_FOUND)
    message(FATAL_ERROR
        "GLFW3 not found.\n"
        "Install via vcpkg:  ./vcpkg install glfw3\n"
        "Or via system package manager (brew install glfw / apt install libglfw3-dev)"
    )
endif()

# ---------------------------------------------------------------------------
# Main Executable
# ---------------------------------------------------------------------------
add_executable(o5m main/main.cpp)

# Link vendored glad (OpenGL loader) and system OpenGL.
target_link_libraries(o5m PRIVATE glad OpenGL::GL)

# Link GLFW (handle both find_package naming variants).
if(glfw3_FOUND)
    target_link_libraries(o5m PRIVATE glfw)
else()
    target_link_libraries(o5m PRIVATE glfw)
endif()

# ---------------------------------------------------------------------------
# RHI Verification Executable
# ---------------------------------------------------------------------------
add_executable(rhi_verify main/rhi_verify.cpp)

target_link_libraries(rhi_verify PRIVATE opengl_rhi glad OpenGL::GL glfw)

# ---------------------------------------------------------------------------
# Platform-Specific Settings
# ---------------------------------------------------------------------------
if(APPLE)
    # macOS requires the OpenGL framework linkage (handled by FindOpenGL).
    # No extra flags needed — FindOpenGL sets up the framework path.
elseif(WIN32)
    # Windows: ensure we link against opengl32.
    # Handled by FindOpenGL::GL.
elseif(UNIX)
    # Linux: link against libGL and libdl (dlopen for GLAD runtime loading).
    find_package(Threads REQUIRED)
    target_link_libraries(o5m PRIVATE Threads::Threads)
endif()
