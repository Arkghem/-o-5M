# ===========================================================================
# Core Library — RHI Abstraction + OpenGL Backend
# ===========================================================================
#
# Architecture:
#   core_rhi      — header-only interface (IBuffer, IShader, ITexture, ...)
#   opengl_rhi    — shared library implementing IRhi via OpenGL 4.6 + DSA
#
# Upper layers link against opengl_rhi and never include <glad/glad.h>.
# When a Vulkan backend arrives, it ships as vulkan_rhi with the same IRhi
# surface — no code above core/ changes.
# ===========================================================================

# ---------------------------------------------------------------------------
# RHI Interface (header-only)
# ---------------------------------------------------------------------------
# An INTERFACE library: no compiled code, just headers. Consumers that link
# against core_rhi get the rhi/ include path transitively.
add_library(core_rhi INTERFACE)
target_include_directories(core_rhi INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/rhi)

# ---------------------------------------------------------------------------
# OpenGL RHI Backend (shared library)
# ---------------------------------------------------------------------------
# Gather every .cpp and .h under opengl/ so that CONFIGURE_DEPENDS correctly
# triggers a re-run when headers are added or removed.
file(GLOB_RECURSE OPENGL_RHI_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/opengl/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/opengl/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/opengl/*.hpp
)

add_library(opengl_rhi SHARED ${OPENGL_RHI_SOURCES})

# Let opengl/ files find their own headers (e.g. GLRhi.h) and the RHI interface.
target_include_directories(opengl_rhi
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/opengl   # GLRhi.h etc.
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/rhi       # IBuffer.h etc.
)

# The OpenGL backend calls gl* functions through glad and needs the system GL.
# glad is built in third_party/; OpenGL::GL comes from FindOpenGL (root CMakeLists).
# core_rhi is PUBLIC so anything linking opengl_rhi also gets the interface headers.

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

target_link_libraries(opengl_rhi
    PUBLIC  core_rhi
    PRIVATE glad
    PRIVATE glfw
)

# ---------------------------------------------------------------------------
# Vulkan RHI Backend (shared library) — Phase 10+
# ---------------------------------------------------------------------------
# Same IRhi interface as opengl_rhi, different GPU API underneath.
# Source files live in core/vulkan/; target is only created when files exist
# AND O5M_VULKAN is enabled at the CMake level.
if(O5M_VULKAN)
    file(GLOB_RECURSE VULKAN_RHI_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/vulkan/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/vulkan/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/vulkan/*.hpp
    )

    if(VULKAN_RHI_SOURCES)
        find_package(spdlog REQUIRED)

        add_library(vulkan_rhi SHARED ${VULKAN_RHI_SOURCES})

        target_include_directories(vulkan_rhi
            PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/vulkan
            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/rhi
        )

        target_link_libraries(vulkan_rhi
            PUBLIC  core_rhi
            PRIVATE Vulkan::Vulkan
            PRIVATE shaderc::shaderc_combined
            PRIVATE glfw
            PRIVATE spdlog::spdlog
        )

        if(WIN32)
            target_compile_definitions(vulkan_rhi PRIVATE VULKAN_RHI_EXPORTS)
        endif()
    else()
        message(WARNING "O5M_VULKAN=ON but no source files found in core/vulkan/")
    endif()
endif()

# ---------------------------------------------------------------------------
# Platform-specific: Windows DLL export
# ---------------------------------------------------------------------------
# Shared libraries on Windows require __declspec(dllexport) on public symbols.
# Define OPENGL_RHI_EXPORTS during the build of opengl_rhi itself so that a
# macro like OPENGL_RHI_API can switch between dllexport (when building) and
# dllimport (when consuming).
if(WIN32)
    target_compile_definitions(opengl_rhi PRIVATE OPENGL_RHI_EXPORTS)
endif()
