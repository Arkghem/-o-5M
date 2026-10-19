#ifndef O5M_DESCRIPTOR_ALLOCATOR_H
#define O5M_DESCRIPTOR_ALLOCATOR_H

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <map>
#include <cassert>
#include <set>
#include <vector>
#include <cstdint>

#include "O5MDevice.h"

struct BindingDesc {
    uint32_t             binding;
    vk::DescriptorType   type;
    vk::ShaderStageFlags stages;
    uint32_t             count = 1;

    bool operator==(const BindingDesc&) const = default;
    bool operator<(const BindingDesc& other) const { return binding < other.binding; }

};

/* like shit
struct DescriptorSetLayoutInfo {
    vk::DescriptorSetLayout  layout;
    vector<BindingDesc> bindings;
};
*/

class DescriptorSetLayoutDesc {
public:
    explicit DescriptorSetLayoutDesc(
        std::initializer_list<BindingDesc> bindings,
        vk::DescriptorSetLayoutCreateFlags flags = {}
    ):m_bindings(bindings),
      m_flags(flags) {
        //verification
        bool sorted = std::is_sorted(m_bindings.begin(), m_bindings.end());
        bool unique = std::set<int>(m_bindings.begin(), m_bindings.end()).size() == m_bindings.size();

        assert(sorted && "binding number not sorted");
        assert(unique && "binding number duplicated");
    }
private:
    std::vector<BindingDesc> m_bindings;
    vk::DescriptorSetLayoutCreateFlags m_flags;
};

using DescriptorSetLayoutHandle = uint32_t;

struct DescriptorSet {
    vk::DescriptorSet set{};
    DescriptorSetLayoutHandle layout{};
};

class O5MDescriptorAllocator {
public:
public:
    O5MDescriptorAllocator(O5MDevice& device):m_device(device){}
    ~O5MDescriptorAllocator() = default;

    DescriptorSet allocate(DescriptorSetLayoutHandle, uint32_t count = 1); //count for batch allocate maybe

    DescriptorSetLayoutHandle create(const DescriptorSetLayoutDesc& layoutDesc);
    void writeBuffer(DescriptorSet set, uint32_t binding, vk::DescriptorType type, vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize, uint32_t arrayElement = 0);
    void writeImage(DescriptorSet set, uint32_t binding, vk::DescriptorType type, vk::ImageView view, vk::Sampler sampler, vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal, uint32_t arrayElement = 0);
    void writeBuffers(DescriptorSet, uint32_t binding, vk::DescriptorType, vk::ArrayProxy<const vk::DescriptorBufferInfo> buffers);
private:
    struct Entry {
        vk::raii::DescriptorSetLayout layout;
        std::vector<vk::DescriptorPoolSize> poolCost;  

    };

    struct PoolEntry {
        vk::raii::DescriptorPool pool;
        std::vector<vk::DescriptorPoolSize> remaining;//we really gotta push it this high??
        uint32_t remainingSets = 0;
    };
private:
    void writeSet(DescriptorSet set, std::vector<vk::WriteDescriptorSet> writes);

    O5MDevice& m_device;
    vk::raii::DescriptorPool m_pool = nullptr;
    vk::raii::DescriptorPool m_spare = nullptr; //no idea that this gonna write like this way

    std::vector<Entry> m_layouts; //wtf is this entry
    std::map<DescriptorSetLayoutDesc, DescriptorSetLayoutHandle> m_layoutIndex;
};

#endif // !O5M_DESCRIPTOR_ALLOCATOR_H
