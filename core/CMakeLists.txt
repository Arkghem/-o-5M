# core — O5M engine core (ECS: entity / component / event systems)
#
# This target owns the `Inc/` public headers. Any consumer that links
# `o5m_core` (currently `main/`'s `o5m` executable) automatically gets
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

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
file(GLOB SRC "Src/*.cpp")
add_library(o5m_core STATIC
    ${SRC}
)

# PUBLIC = this include directory propagates to every target that links
# o5m_core (currently the `o5m` executable in main/).
target_include_directories(o5m_core PUBLIC Inc)
target_include_directories(o5m_core PUBLIC ../test/)
target_link_libraries(o5m_core PUBLIC glm::glm)

# tinygltf — 只在 Src/O5MMeshResource.cpp（#include <tiny_gltf.h>）中使用，
# 公共头 O5MMeshResource.h 不暴露 tinygltf 类型，所以 PRIVATE。
target_link_libraries(o5m_core PRIVATE tinygltf)

# Vulkan — O5MTextureResource.h stores a vk::Image handle in a public header,
# so the Vulkan loader is a PUBLIC dependency: any consumer of core/Inc must
# also resolve <vulkan/vulkan.hpp> and link libvulkan.
find_package(Vulkan REQUIRED)
target_link_libraries(o5m_core PUBLIC Vulkan::Vulkan)

# shaderc — 运行时 GLSL -> SPIR-V。只被 Src/O5MShaderResource.cpp 使用，
# 头文件不暴露 shaderc 类型，所以 PRIVATE。
# 根 CMakeLists 的 shaderc 查找在 if(O5M_VULKAN) 里，core 不依赖该开关，自己找。
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
