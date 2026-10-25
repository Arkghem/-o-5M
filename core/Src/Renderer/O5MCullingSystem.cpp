#include "Renderer/O5MCullingSystem.h"

#include <algorithm>
#include <cmath>

std::array<glm::vec4, 6> O5MCullingSystem::extractPlanes(const glm::mat4& viewProj) {
    const auto row = [&](int index) {
        return glm::vec4(viewProj[0][index], viewProj[1][index],
                         viewProj[2][index], viewProj[3][index]);
    };

    const glm::vec4 r0 = row(0);
    const glm::vec4 r1 = row(1);
    const glm::vec4 r2 = row(2);
    const glm::vec4 r3 = row(3);

    std::array<glm::vec4, 6> planes = {
        r2,
        r3 - r2,
        r3 + r0,
        r3 - r0,
        r3 + r1,
        r3 - r1,
    };

    for (glm::vec4& plane : planes) {
        const float length = glm::length(glm::vec3(plane));
        if (length > 0.0f) {
            plane /= length;
        }
    }
    return planes;
}

O5MDrawList O5MCullingSystem::cull(const glm::mat4& viewProj,
                                   const O5MDrawList& candidates) const {
    const std::array<glm::vec4, 6> planes = extractPlanes(viewProj);

    O5MDrawList visible;
    visible.reserve(candidates.size());

    for (const O5MDrawItem& item : candidates) {
        const glm::vec3 center =
            glm::vec3(item.model * glm::vec4(glm::vec3(item.boundsSphere), 1.0f));
        const float scaleX = glm::length(glm::vec3(item.model[0]));
        const float scaleY = glm::length(glm::vec3(item.model[1]));
        const float scaleZ = glm::length(glm::vec3(item.model[2]));
        const float radius =
            item.boundsSphere.w * std::max({scaleX, scaleY, scaleZ});

        bool inside = true;
        for (const glm::vec4& plane : planes) {
            if (glm::dot(glm::vec3(plane), center) + plane.w < -radius) {
                inside = false;
                break;
            }
        }

        if (inside) {
            visible.push_back(item);
        }
    }

    return visible;
}
