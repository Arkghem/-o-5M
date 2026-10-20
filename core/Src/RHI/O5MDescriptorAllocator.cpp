#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include "RHI/O5MDescriptorAllocator.h"

std::vector<DescriptorSet> O5MDescriptorAllocator::allocate(DescriptorSetLayoutHandle handle, uint32_t count) {
    const Entry& e = m_layouts[handle-1];
    std::vector<vk::DescriptorSetLayout> setLayouts(count, *e.layout);

    vk::DescriptorSetAllocateInfo allocInfo {
        .descriptorPool = m_pool,
        .descriptorSetCount = count,
        .pSetLayouts = setLayouts.data()
    };

    std::vector<vk::raii::DescriptorSet> descriptorSets = m_device.getDevice().allocateDescriptorSets(allocInfo);
    std::vector<DescriptorSet> result;

    std::ranges::for_each(descriptorSets, [&](vk::raii::DescriptorSet& d)->void{
        result.emplace_back(d.release(), handle);
    });

    //resigter this to entry count

    return result;
}

DescriptorSetLayoutHandle O5MDescriptorAllocator::createLayout(const DescriptorSetLayoutDesc& layoutDesc) {
    auto it = std::find_if(m_layoutIndex.begin(), m_layoutIndex.end(), [&](std::pair<const DescriptorSetLayoutDesc, DescriptorSetLayoutHandle>& p)->bool {
        return p.first == layoutDesc;
    });

    if (it != m_layoutIndex.end()) 
        return it->second;

    std::vector<vk::DescriptorSetLayoutBinding> bindings; //omg, how many this "bindings" i used.
    std::vector<vk::DescriptorPoolSize> poolCosts;
    std::ranges::for_each(layoutDesc.getBindings(), [&](const BindingDesc& b)->void{
        bindings.emplace_back(b.binding, b.type, b.count, b.stages);
        poolCosts.emplace_back(b.type, b.count);
    });

    //build the DescriptorSetLayout
    vk::DescriptorSetLayoutCreateInfo layoutInfo {
        .flags = layoutDesc.getFlags(),
        .bindingCount = static_cast<uint32_t>(layoutDesc.getBindings().size()),
        .pBindings = bindings.data(),
    };

    Entry entry {
        .layout = m_device.getDevice().createDescriptorSetLayout(layoutInfo),
        .poolCosts = poolCosts
    };

    DescriptorSetLayoutHandle index = m_layouts.size() + 1;
    m_layoutIndex.push_back(std::pair<const DescriptorSetLayoutDesc, DescriptorSetLayoutHandle>(layoutDesc, index));

    m_layouts.push_back(std::move(entry));
}

void O5MDescriptorAllocator::writeBuffer(
    DescriptorSet set,
    uint32_t binding,
    vk::DescriptorType type,
    const BufferBinding& buffer,
    uint32_t arrayElement
) {
    vk::DescriptorBufferInfo bufferInfo {
        .buffer = buffer.buffer,
        .offset = buffer.offset,
        .range = buffer.size
    };

    vk::WriteDescriptorSet descriptorWrite {
        .dstSet = set.set,
        .dstBinding = binding,
        .dstArrayElement = arrayElement,
        .descriptorCount = 1,
        .descriptorType = type,
        .pBufferInfo = &bufferInfo
    };

    m_device.getDevice().updateDescriptorSets(descriptorWrite, {});
}

void O5MDescriptorAllocator::writeImage(
    DescriptorSet set,
    uint32_t binding,
    vk::DescriptorType type,
    vk::ImageView view,
    vk::Sampler sampler,
    vk::ImageLayout layout,
    uint32_t arrayElement
) {
    vk::DescriptorImageInfo imageInfo {
        .sampler = sampler,
        .imageView = view,
        .imageLayout = layout,
    };
    vk::WriteDescriptorSet descriptorWrite {
        .dstSet = set.set,
        .dstBinding = binding,
        .dstArrayElement = arrayElement,
        .descriptorCount = 1,
        .descriptorType = type,
        .pImageInfo = &imageInfo
    };
 
    m_device.getDevice().updateDescriptorSets(descriptorWrite, {});
}

void O5MDescriptorAllocator::writeBuffers(
    DescriptorSet set,
    uint32_t binding,
    vk::DescriptorType type,
    vk::ArrayProxy<const BufferBinding> buffers
) {
    // Translate the public description into Vulkan's own struct right before the call.
    std::vector<vk::DescriptorBufferInfo> bufferInfos;
    bufferInfos.reserve(buffers.size());
    for (const BufferBinding& b : buffers) {
        bufferInfos.emplace_back(b.buffer, b.offset, b.size);
    }

    vk::WriteDescriptorSet descriptorWrite {
        .dstSet = set.set,
        .dstBinding = binding,
        .dstArrayElement = 0,
        .descriptorCount = static_cast<uint32_t>(bufferInfos.size()),
        .descriptorType = type,
        .pBufferInfo = bufferInfos.data()
    };

    m_device.getDevice().updateDescriptorSets(descriptorWrite, {});
}

const O5MDescriptorAllocator::Entry&
    O5MDescriptorAllocator::entryOf(DescriptorSetLayoutHandle h) const {
    assert(h >= 1 && m_layouts.size() >= h && "layout request out of bound");

    return m_layouts[h - 1];
}

