#ifndef O5M_MATERIAL_TABLE_H
#define O5M_MATERIAL_TABLE_H

#include <vector>
#include <unordered_map>

#include "Resources/O5MMaterialResource.h"
#include "RHI/O5MDescriptorAllocator.h"
#include "RHI/O5MDevice.h"

class O5MMaterialTable {
public:
    static constexpr uint32_t kCapacity = 64u;

    O5MMaterialTable(
        O5MDevice&,
        O5MDescriptorAllocator&,
        uint32_t frameInFlight,
        vk::DescriptorSetLayout layout1,
        vk::DescriptorSetLayout layout2
    );

    void add(O5MMaterialResource* resource);//not handle because this table is not part of user
    uint32_t indexOf(const O5MMaterialResource* handle) const;

    void upload(uint32_t frameSlot); // wtf is this
    vk::DescriptorSet paramSet(uint32_t frameSlot) const;
    DescriptorSet textureSet(uint32_t index) const;
private: 
    std::array<O5MMaterialResource::MaterialParamsGLSL, kCapacity>      m_params;
    std::vector<const O5MMaterialResource*>                             m_materials;
    std::unordered_map<uint64_t, uint32_t>                              m_idToIndex;
    std::vector<std::pair<vk::raii::Buffer, vk::raii::DeviceMemory>>    m_paramBuffers;
    std::vector<DescriptorSet>                                          m_paramSets;
    std::vector<DescriptorSet>                                          m_textureSets;

    vk::raii::Sampler m_sampler = nullptr;

    O5MDevice& m_device;
    O5MDescriptorAllocator& m_descAllocator;

    const uint32_t frameInFlight;
};

#endif  // !O5M_MATERIAL_TABLE_H
