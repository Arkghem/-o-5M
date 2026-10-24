#ifndef O5M_DRAW_ITEM_H
#define O5M_DRAW_ITEM_H

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <vector>

struct O5MDrawItem {
    vk::Buffer vertexBuffer;
    vk::Buffer indexBuffer;
    uint32_t indexCount = 0;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
    glm::mat4 model;
    glm::vec4 boundsSphere{0.f, 0.f, 0.f, 1.f};// local center.xyz + radius.w
};

using O5MDrawList = std::vector<O5MDrawItem>;
#endif //O5M_DRAW_ITEM_H
