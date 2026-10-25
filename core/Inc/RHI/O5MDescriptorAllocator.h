#ifndef O5M_DESCRIPTOR_ALLOCATOR_H
#define O5M_DESCRIPTOR_ALLOCATOR_H

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <unordered_map>
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

class DescriptorSetLayoutDesc {
public:
    explicit DescriptorSetLayoutDesc(
        std::initializer_list<BindingDesc> bindings,
        vk::DescriptorSetLayoutCreateFlags flags = {}
    ):m_bindings(bindings),
      m_flags(flags) {
        //verification
        bool sorted = std::is_sorted(m_bindings.begin(), m_bindings.end());
        bool unique = std::set<BindingDesc>(m_bindings.begin(), m_bindings.end()).size() == m_bindings.size();

        assert(sorted && "binding number not sorted");
        assert(unique && "binding number duplicated");
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

// Public, allocator-owned description of one buffer binding.
// Callers never have to touch vk::DescriptorBufferInfo.
struct BufferBinding {
    vk::Buffer     buffer = nullptr;
    vk::DeviceSize offset = 0;
    vk::DeviceSize size   = VK_WHOLE_SIZE;
};

//TODO: Decriptor management.
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
           uint32_t count = 1); // count for batch allocate maybe
  DescriptorSetLayoutHandle
  createLayout(const DescriptorSetLayoutDesc &layoutDesc);

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
        std::vector<vk::DescriptorPoolSize> poolCosts;  
    };

    struct PoolEntry {
        vk::raii::DescriptorPool pool;
        std::vector<vk::DescriptorPoolSize> remaining;//we really gotta push it this high??
        uint32_t remainingSets = 0;
    };

    const Entry& entryOf(DescriptorSetLayoutHandle h) const;
    void ensureCapasity(const Entry& e, uint32_t setCount);
    void createPool(uint32_t maxSets); // thought this should gowit O%MDevice

    const BindingDesc& dscriptorOf(
        DescriptorSet set,
        uint32_t binding,
        vk::DescriptorType type,
        uint32_t arrayElement
    ) const; //verification

    O5MDevice& m_device;
    vk::raii::DescriptorPool m_pool = nullptr;
    vk::raii::DescriptorPool m_spare = nullptr; //no idea that this gonna write like this way

    std::vector<Entry> m_layouts;
    std::vector<std::pair<const DescriptorSetLayoutDesc, DescriptorSetLayoutHandle>> m_layoutIndex;
};

#endif // !O5M_DESCRIPTOR_ALLOCATOR_H
