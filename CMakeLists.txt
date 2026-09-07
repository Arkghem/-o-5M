cmake_minimum_required(VERSION 3.21)
project(o5m LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# LSP Supply
# ---------------------------------------------------------------------------
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# C++ Standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Vendored Third-Party Dependencies
# ---------------------------------------------------------------------------
add_subdirectory(third_party)

# tinygltf — header-only glTF 2.0 loader（FetchContent 引入，vcpkg/brew 无现成包）。
# TINYGLTF_HEADER_ONLY=ON → 仅创建 INTERFACE 目标，不编译任何 .cc；
# TINYGLTF_IMPLEMENTATION 由 core/Src/O5MMeshResource.cpp 这一个 TU 提供，
# 若改用默认静态库模式会与之产生重复符号，必须保持 header-only。
include(FetchContent)
set(TINYGLTF_HEADER_ONLY ON CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.9.4
)
FetchContent_MakeAvailable(tinygltf)

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

    find_package(shaderc QUIET)
    if(NOT shaderc_FOUND)
        # Fallback: Vulkan SDK ships shaderc_combined. Try to locate it.
        find_library(SHADERC_LIBRARY NAMES shaderc_combined)
        if(SHADERC_LIBRARY)
            add_library(shaderc::shaderc_combined UNKNOWN IMPORTED)
            set_target_properties(shaderc::shaderc_combined PROPERTIES
                IMPORTED_LOCATION ${SHADERC_LIBRARY}
                INTERFACE_INCLUDE_DIRECTORIES "${Vulkan_INCLUDE_DIRS}")
            message(STATUS "shaderc found (fallback): ${SHADERC_LIBRARY}")
        else()
            message(FATAL_ERROR "shaderc not found. Install Vulkan SDK: https://vulkan.lunarg.com/")
        endif()
    else()
        message(STATUS "shaderc found via CMake config")
    endif()
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
# Core Library
# ---------------------------------------------------------------------------
# Added AFTER dependency discovery (OpenGL/Vulkan/glfw3/shaderc) so that the
# imported targets they create (Vulkan::Vulkan, shaderc::shaderc_combined,
# OpenGL::GL) are visible to the RHI backends defined in core/.
add_subdirectory(core)

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

# offscreen 验证程序：只依赖 o5m_core（Vulkan/glm 经 PUBLIC 传播），无窗口。
target_link_libraries(rhi_verify PRIVATE o5m_core)

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
