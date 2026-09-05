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
add_library(o5m_core STATIC
    Src/O5MComponent.cpp
    Src/O5MEntity.cpp
    Src/O5MMeshComponent.cpp
    Src/O5MResource.cpp
    Src/O5MResourceManager.cpp
    Src/O5MTextureResource.cpp
    Src/O5MRendergraph.cpp
    # Add new core sources here as the ECS grows, e.g.:
    # Src/O5MEntity.cpp
    # Src/O5MEvent.cpp
    # Src/O5MCameraComponent.cpp
)

# PUBLIC = this include directory propagates to every target that links
# o5m_core (currently the `o5m` executable in main/).
target_include_directories(o5m_core PUBLIC Inc)

target_link_libraries(o5m_core PUBLIC glm::glm)

# Vulkan — O5MTextureResource.h stores a vk::Image handle in a public header,
# so the Vulkan loader is a PUBLIC dependency: any consumer of core/Inc must
# also resolve <vulkan/vulkan.hpp> and link libvulkan.
target_link_libraries(o5m_core PUBLIC Vulkan::Vulkan)
