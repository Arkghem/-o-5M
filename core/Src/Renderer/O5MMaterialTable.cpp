#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include "Renderer/O5MMaterialTable.h"

O5MMaterialTable::O5MMaterialTable(
    O5MDevice& device,
    O5MDescriptorAllocator& descAllocator,
    uint32_t frameInFlight,
    vk::DescriptorSetLayout layout1,
    vk::DescriptorSetLayout layout2
): m_device(device), m_descAllocator(descAllocator), frameInFlight(frameInFlight) {
    auto paramsLayout = m_descAllocator.createLayout(
        DescriptorSetLayoutDesc {
        { {0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment}, }
    });

    auto textureLayout = m_descAllocator.createLayout(
        DescriptorSetLayoutDesc {
            {
                {0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment},
                {1, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment},
                {2, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment},
                {3, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment},
                {4, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment}
            }
        }
    );

    const vk::DeviceSize paramBytes = 
        static_cast<vk::DeviceSize>(kCapacity) * 
        sizeof(O5MMaterialResource::MaterialParamsGLSL);

    m_paramBuffers.reserve(frameInFlight);
    for (uint32_t i = 0; i< frameInFlight; ++i) {
        m_paramBuffers.push_back(m_device.createBuffer(
            paramBytes,
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        )); 
    }

    m_paramSets = m_descAllocator.allocate(paramsLayout, frameInFlight);
    for (uint32_t i = 0; i < frameInFlight; ++i) {
        m_descAllocator.writeBuffer(
            m_paramSets[i],
            0,
            vk::DescriptorType::eUniformBuffer,
            { m_paramBuffers[i].first }
        );
    }

    m_textureSets = m_descAllocator.allocate(textureLayout, kCapacity);

     vk::SamplerCreateInfo samplerInfo{
         .magFilter = vk::Filter::eLinear,
         .minFilter = vk::Filter::eLinear,
         .mipmapMode = vk::SamplerMipmapMode::eLinear,
         .addressModeU = vk::SamplerAddressMode::eRepeat,
         .addressModeV = vk::SamplerAddressMode::eRepeat,
         .addressModeW = vk::SamplerAddressMode::eRepeat,
         .maxLod = vk::LodClampNone,
     };

     m_sampler = std::move(vk::raii::Sampler(m_device.getDevice(), samplerInfo));
}

void O5MMaterialTable::add(O5MMaterialResource *resource) {
    assert(m_materials.size() < kCapacity && "Material table exceeds capacity");

    const uint32_t index = static_cast<uint32_t>(m_materials.size());

    m_materials.push_back(resource);
    m_idToIndex[resource->getResourceId()] = index;
    m_params[index] = resource->getMaterialParams();

    auto& textures = resource->getTextures();
    auto baseColorView = textures.baseColor ? textures.baseColor->getImageView() : nullptr;
    auto metallicRoughnessView = textures.metallicRoughness ? textures.metallicRoughness->getImageView() : nullptr;
    auto normalView = textures.normal ? textures.normal->getImageView() : nullptr;
    auto occlusionView = textures.occlusion ? textures.occlusion->getImageView() : nullptr;
    auto emissiveView = textures.emissive ? textures.emissive->getImageView() : nullptr;

    auto writeTex = [&](uint32_t binding, vk::ImageView& view) {
        if (view != nullptr) {
            m_descAllocator.writeImage(
                m_textureSets[index],
                binding,
                vk::DescriptorType::eCombinedImageSampler,
                view,
                m_sampler
            );
        } else {
            m_descAllocator.writeImage(
                m_textureSets[index],
                binding,
                vk::DescriptorType::eCombinedImageSampler,
                nullptr,
                nullptr
            );
        }
    };

    writeTex(0, baseColorView);
    writeTex(1, metallicRoughnessView);
    writeTex(2, normalView);
    writeTex(3, occlusionView);
    writeTex(4, emissiveView);
}

uint32_t O5MMaterialTable::indexOf(const O5MMaterialResource *handle) const {
    auto it = m_idToIndex.find(handle->getResourceId());
    if (it != m_idToIndex.end()) {
        return it->second;
    }
    throw std::out_of_range("Material not found in table");
}

void O5MMaterialTable::upload(uint32_t frameSlot) {
    void* data = m_paramBuffers[frameSlot].second.mapMemory(0, vk::WholeSize);
    memcpy(data, m_params.data(), sizeof(O5MMaterialResource::MaterialParamsGLSL) * kCapacity);
    m_paramBuffers[frameSlot].second.unmapMemory();
}

vk::DescriptorSet O5MMaterialTable::paramSet(uint32_t frameSlot) const {
    return m_paramSets[frameSlot].set;
}

DescriptorSet O5MMaterialTable::textureSet(uint32_t index) const {
    return m_textureSets[index];
}
