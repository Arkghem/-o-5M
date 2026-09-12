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

# Vulkan 与 shaderc 由 core/CMakeLists.txt 自行查找（本引擎单 Vulkan 后端，
# 不存在"可选后端"开关；旧的 O5M_VULKAN option 是死代码，已删除）。

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
# Legacy OpenGL Executable（Phase 2 实时渲染目标验收后退役删除）
# ---------------------------------------------------------------------------
add_executable(o5m apps/legacy_gl.cpp)

# Link vendored glad (OpenGL loader) and system OpenGL.
target_link_libraries(o5m PRIVATE glad OpenGL::GL)

# Link GLFW (handle both find_package naming variants).
if(glfw3_FOUND)
    target_link_libraries(o5m PRIVATE glfw)
else()
    target_link_libraries(o5m PRIVATE glfw)
endif()

# ---------------------------------------------------------------------------
# RHI Verification Executable（离屏回归测试，永久保留）
# ---------------------------------------------------------------------------
add_executable(rhi_verify apps/rhi_verify.cpp)

# offscreen 验证程序：只依赖 o5m_core（Vulkan/glm 经 PUBLIC 传播），无窗口。
target_link_libraries(rhi_verify PRIVATE o5m_core)

# ---------------------------------------------------------------------------
# Texture Verification Executable（Phase 0 验收：TextureResource 修复的靶场）
# ---------------------------------------------------------------------------
# 依赖 O5MTextureResource 修复后的 getImageView() 接口，修复完成前不编译。
# add_executable(tex_verify apps/tex_verify.cpp)
# target_link_libraries(tex_verify PRIVATE o5m_core)

# ---------------------------------------------------------------------------
# Unit Tests（ctest）
# ---------------------------------------------------------------------------
enable_testing()
add_executable(resmgr_cycle_test test/resmgr_cycle_test.cpp)
target_link_libraries(resmgr_cycle_test PRIVATE o5m_core)
add_test(NAME resmgr_cycle COMMAND resmgr_cycle_test)

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
