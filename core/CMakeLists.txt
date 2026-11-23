# core — O5M engine core（RHI / RenderGraph / Resources / Scene / Events / Platform / Renderer）
#
# This target owns the `Inc/` public headers. Any consumer that links
# `o5m_core`（apps/ 下的可执行目标）automatically gets
# `core/Inc` on its include path through the PUBLIC interface below — no
# need to repeat include_directories() at the top level.

# ---------------------------------------------------------------------------
# Header-only math library (glm)
# ---------------------------------------------------------------------------
# Camera / Transform components include <glm/glm.hpp> and
# <glm/gtc/quaternion.hpp>, so glm is a PUBLIC dependency: anyone who reads
# our headers must also resolve glm.
#   vcpkg: ./vcpkg install glm
#   Homebrew (macOS): brew install glm
find_package(glm REQUIRED)

# GLM_FORCE_DEPTH_ZERO_TO_ONE is defined globally in the ROOT CMakeLists so
# that core/ and apps/ TUs share one glm configuration (ODR consistency).

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SRC CONFIGURE_DEPENDS "Src/*.cpp")
add_library(o5m_core STATIC
    ${SRC}
)

# PUBLIC = this include directory propagates to every target that links
target_include_directories(o5m_core PUBLIC Inc)
target_link_libraries(o5m_core PUBLIC glm::glm)

target_link_libraries(o5m_core PRIVATE tinygltf)

target_link_libraries(o5m_core PRIVATE stb)

# GLFW — only Src/Platform/O5MWindow.cpp includes <GLFW/glfw3.h>; the public
# header keeps GLFW types opaque (forward declaration), so PRIVATE.
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    find_package(glfw QUIET)
endif()
if(NOT glfw3_FOUND AND NOT glfw_FOUND)
    message(FATAL_ERROR "GLFW3 not found (brew install glfw / vcpkg install glfw3)")
endif()
# Both package names create the same imported target `glfw`.
target_link_libraries(o5m_core PRIVATE glfw)

# Vulkan — O5MTextureResource.h stores a vk::Image handle in a public header,
# so the Vulkan loader is a PUBLIC dependency: any consumer of core/Inc must
# also resolve <vulkan/vulkan.hpp> and link libvulkan.
find_package(Vulkan REQUIRED)
target_link_libraries(o5m_core PUBLIC Vulkan::Vulkan)

find_package(shaderc QUIET)
if(shaderc_FOUND)
    target_link_libraries(o5m_core PRIVATE shaderc::shaderc)
else()
    # Fallback: Vulkan SDK ships shaderc_combined
    find_library(O5M_SHADERC_LIBRARY NAMES shaderc_combined REQUIRED)
    add_library(o5m_shaderc STATIC IMPORTED)
    set_target_properties(o5m_shaderc PROPERTIES
        IMPORTED_LOCATION ${O5M_SHADERC_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES "${Vulkan_INCLUDE_DIRS}")
    target_link_libraries(o5m_core PRIVATE o5m_shaderc)
endif()
