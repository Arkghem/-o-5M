#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include "RHI/O5MDescriptorAllocator.h"

#include <algorithm>
#include <map>
#include <ranges>
#include <stdexcept>

std::vector<DescriptorSet> O5MDescriptorAllocator::allocate(DescriptorSetLayoutHandle handle,
                                                            uint32_t count) {
    if (count == 0) {
        return {};
    }

    const Entry& e = entryOf(handle);
    ensureCapacity(handle, e, count);

    PoolEntry* target = nullptr;
    for (PoolEntry& pool : m_pools) {
        if (pool.layout == handle && pool.remainingSets >= count) {
            target = &pool;
            break;
        }
    }
    if (!target) {
        throw std::runtime_error("descriptor pool allocation failed");
    }

    std::vector<vk::DescriptorSetLayout> setLayouts(count, *e.layout);
    vk::DescriptorSetAllocateInfo allocInfo{
        .descriptorPool = *target->pool,
        .descriptorSetCount = count,
        .pSetLayouts = setLayouts.data()
    };

    std::vector<vk::raii::DescriptorSet> descriptorSets =
        m_device.getDevice().allocateDescriptorSets(allocInfo);
    target->remainingSets -= count;

    std::vector<DescriptorSet> result;
    result.reserve(descriptorSets.size());
    for (vk::raii::DescriptorSet& d : descriptorSets) {
        result.emplace_back(d.release(), handle);
    }
    return result;
}

DescriptorSetLayoutHandle O5MDescriptorAllocator::createLayout(
    const DescriptorSetLayoutDesc& layoutDesc) {
    auto it = std::find_if(m_layoutIndex.begin(), m_layoutIndex.end(),
        [&](const std::pair<DescriptorSetLayoutDesc, DescriptorSetLayoutHandle>& p) {
            return p.first == layoutDesc;
        });
    if (it != m_layoutIndex.end()) {
        return it->second;
    }

    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    std::vector<vk::DescriptorPoolSize> poolCosts;
    for (const BindingDesc& b : layoutDesc.getBindings()) {
        bindings.emplace_back(b.binding, b.type, b.count, b.stages);
        poolCosts.emplace_back(b.type, b.count);
    }

    vk::DescriptorSetLayoutCreateInfo layoutInfo{
        .flags = layoutDesc.getFlags(),
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };

    Entry entry{
        m_device.getDevice().createDescriptorSetLayout(layoutInfo),
        layoutDesc.getBindings(),
        poolCosts
    };

    const DescriptorSetLayoutHandle index = static_cast<DescriptorSetLayoutHandle>(m_layouts.size() + 1);
    m_layoutIndex.emplace_back(layoutDesc, index);
    m_layouts.push_back(std::move(entry));
    return index;
}

vk::DescriptorSetLayout
O5MDescriptorAllocator::getLayout(DescriptorSetLayoutHandle handle) const {
    return *entryOf(handle).layout;
}

void O5MDescriptorAllocator::writeBuffer(
    DescriptorSet set,
    uint32_t binding,
    vk::DescriptorType type,
    const BufferBinding& buffer,
    uint32_t arrayElement
) {
    dscriptorOf(set, binding, type, arrayElement);

    vk::DescriptorBufferInfo bufferInfo{
        .buffer = buffer.buffer,
        .offset = buffer.offset,
        .range = buffer.size
    };

    vk::WriteDescriptorSet descriptorWrite{
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
    dscriptorOf(set, binding, type, arrayElement);

    vk::DescriptorImageInfo imageInfo{
        .sampler = sampler,
        .imageView = view,
        .imageLayout = layout,
    };
    vk::WriteDescriptorSet descriptorWrite{
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
    const BindingDesc& desc = dscriptorOf(set, binding, type, 0);
    if (buffers.size() > desc.count) {
        throw std::out_of_range("buffer array exceeds descriptor binding count");
    }

    std::vector<vk::DescriptorBufferInfo> bufferInfos;
    bufferInfos.reserve(buffers.size());
    for (const BufferBinding& b : buffers) {
        bufferInfos.emplace_back(b.buffer, b.offset, b.size);
    }

    vk::WriteDescriptorSet descriptorWrite{
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
    if (h == 0 || h > m_layouts.size()) {
        throw std::out_of_range("descriptor set layout handle out of range");
    }
    return m_layouts[h - 1];
}

void O5MDescriptorAllocator::ensureCapacity(DescriptorSetLayoutHandle handle,
                                            const Entry& e, uint32_t setCount) {
    for (const PoolEntry& pool : m_pools) {
        if (pool.layout == handle && pool.remainingSets >= setCount) {
            return;
        }
    }
    createPool(handle, e, std::max(setCount, 64u));
}

void O5MDescriptorAllocator::createPool(DescriptorSetLayoutHandle handle, const Entry& e,
                                        uint32_t maxSets) {
    std::map<vk::DescriptorType, uint32_t> counts;
    for (const vk::DescriptorPoolSize& cost : e.poolCosts) {
        counts[cost.type] += cost.descriptorCount;
    }

    std::vector<vk::DescriptorPoolSize> poolSizes;
    poolSizes.reserve(counts.size());
    for (const auto& [type, count] : counts) {
        poolSizes.emplace_back(type, count * maxSets);
    }

    vk::DescriptorPoolCreateInfo poolInfo{
        .maxSets = maxSets,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data()
    };

    m_pools.push_back(PoolEntry{
        vk::raii::DescriptorPool(m_device.getDevice(), poolInfo),
        handle,
        maxSets
    });
}

const BindingDesc& O5MDescriptorAllocator::dscriptorOf(
    DescriptorSet set,
    uint32_t binding,
    vk::DescriptorType type,
    uint32_t arrayElement
) const {
    if (!set.set) {
        throw std::invalid_argument("null descriptor set");
    }
    const Entry& e = entryOf(set.layout);
    for (const BindingDesc& desc : e.bindings) {
        if (desc.binding != binding) {
            continue;
        }
        if (desc.type != type) {
            throw std::invalid_argument("descriptor type does not match layout");
        }
        if (arrayElement >= desc.count) {
            throw std::out_of_range("descriptor array element out of range");
        }
        return desc;
    }
    throw std::out_of_range("descriptor binding not found in layout");
}
