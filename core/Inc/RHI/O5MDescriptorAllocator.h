#ifndef O5M_DESCRIPTOR_ALLOCATOR_H
#define O5M_DESCRIPTOR_ALLOCATOR_H

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <utility>
#include <vector>

#include "O5MDevice.h"

struct BindingDesc {
    uint32_t             binding;
    vk::DescriptorType   type;
    vk::ShaderStageFlags stages;
    uint32_t             count = 1;

    bool operator==(const BindingDesc&) const = default;
    bool operator<(const BindingDesc& other) const { return binding < other.binding; }
};

class DescriptorSetLayoutDesc {
public:
    explicit DescriptorSetLayoutDesc(
        std::initializer_list<BindingDesc> bindings,
        vk::DescriptorSetLayoutCreateFlags flags = {}
    ):m_bindings(bindings),
      m_flags(flags) {
        std::sort(m_bindings.begin(), m_bindings.end());
        const auto duplicate = std::adjacent_find(
            m_bindings.begin(), m_bindings.end(),
            [](const BindingDesc& a, const BindingDesc& b) { return a.binding == b.binding; });
        if (duplicate != m_bindings.end()) {
            throw std::invalid_argument("descriptor binding duplicated");
        }
    }

    bool operator==(const DescriptorSetLayoutDesc& other) const = default;

    const std::vector<BindingDesc>& getBindings(void) const { return m_bindings; }
    const vk::DescriptorSetLayoutCreateFlags& getFlags(void) const { return m_flags; }
private:
    std::vector<BindingDesc> m_bindings;
    vk::DescriptorSetLayoutCreateFlags m_flags;
};

using DescriptorSetLayoutHandle = uint32_t;

struct DescriptorSet {
    vk::DescriptorSet set = nullptr;
    DescriptorSetLayoutHandle layout{};
};

struct BufferBinding {
    vk::Buffer     buffer = nullptr;
    vk::DeviceSize offset = 0;
    vk::DeviceSize size   = VK_WHOLE_SIZE;
};

class O5MDescriptorAllocator {
public:
  O5MDescriptorAllocator(const O5MDescriptorAllocator &) = delete;
  O5MDescriptorAllocator(O5MDescriptorAllocator &&) = delete;
  O5MDescriptorAllocator &operator=(const O5MDescriptorAllocator &) = delete;
  O5MDescriptorAllocator &operator=(O5MDescriptorAllocator &&) = delete;
  O5MDescriptorAllocator(O5MDevice &device) : m_device(device) {}
  ~O5MDescriptorAllocator() = default;

  std::vector<DescriptorSet>
  allocate(DescriptorSetLayoutHandle,
           uint32_t count = 1);
  DescriptorSetLayoutHandle
  createLayout(const DescriptorSetLayoutDesc &layoutDesc);
  vk::DescriptorSetLayout getLayout(DescriptorSetLayoutHandle) const;

  void writeBuffer(DescriptorSet set, uint32_t binding, vk::DescriptorType type,
                   const BufferBinding &buffer, uint32_t arrayElement = 0);

  void
  writeImage(DescriptorSet set, uint32_t binding, vk::DescriptorType type,
             vk::ImageView view, vk::Sampler sampler,
             vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal,
             uint32_t arrayElement = 0);

  void writeBuffers(DescriptorSet set, uint32_t binding,
                    vk::DescriptorType type,
                    vk::ArrayProxy<const BufferBinding> buffers);

private:
    struct Entry {
        vk::raii::DescriptorSetLayout layout;
        std::vector<BindingDesc> bindings;
        std::vector<vk::DescriptorPoolSize> poolCosts;
    };

    struct PoolEntry {
        vk::raii::DescriptorPool pool;
        DescriptorSetLayoutHandle layout{};
        uint32_t remainingSets = 0;
    };

    const Entry& entryOf(DescriptorSetLayoutHandle h) const;
    void ensureCapacity(DescriptorSetLayoutHandle h, const Entry& e, uint32_t setCount);
    void createPool(DescriptorSetLayoutHandle h, const Entry& e, uint32_t maxSets);

    const BindingDesc& dscriptorOf(
        DescriptorSet set,
        uint32_t binding,
        vk::DescriptorType type,
        uint32_t arrayElement
    ) const;

    O5MDevice& m_device;
    std::vector<Entry> m_layouts;
    std::vector<std::pair<DescriptorSetLayoutDesc, DescriptorSetLayoutHandle>> m_layoutIndex;

    std::vector<PoolEntry> m_pools;
};

#endif // !O5M_DESCRIPTOR_ALLOCATOR_H
