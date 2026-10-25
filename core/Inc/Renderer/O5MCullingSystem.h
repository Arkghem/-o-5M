#ifndef O5M_CULLING_SYSTEM_H
#define O5M_CULLING_SYSTEM_H

#include <array>

#include <glm/glm.hpp>

#include "Renderer/O5MDrawItem.h"

class O5MCullingSystem {
public:
    O5MDrawList cull(const glm::mat4& viewProj, const O5MDrawList& candidates) const;

private:
    static std::array<glm::vec4, 6> extractPlanes(const glm::mat4& viewProj);
};

#endif // O5M_CULLING_SYSTEM_H
