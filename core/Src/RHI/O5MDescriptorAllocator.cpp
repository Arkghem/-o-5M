#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include "RHI/O5MDescriptorAllocator.h"

void O5MDescriptorAllocator::create(void) {
    //2 is just the temporay value for frame-in-flight
    std::array<vk::DescriptorPoolSize, 2> poolSizes {{
        {.type = vk::DescriptorType::eUniformBuffer, .descriptorCount = 2},
        {.type = vk::DescriptorType::eSampledImage, .descriptorCount = 2}
    }};

    vk::DescriptorPoolCreateInfo pooInfo {
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = 2,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };

    m_pool = m_device.getDevice().createDescriptorPool(pooInfo);
}
