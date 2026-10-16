#ifndef __O5MPIPELINE__H
#define __O5MPIPELINE__H

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <vector>

#include "RHI/O5MDevice.h"

struct O5MPipelineSpec {
    std::vector<vk::Format>  colorFormats; //MRT
    std::optional<vk::Format> depthStencilFormat; //enable depth/stencil
    std::vector<vk::VertexInputBindingDescription> vertexBindings;
    std::vector<vk::VertexInputAttributeDescription> vertexAttributes;

    vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
    vk::CullModeFlags cullMode     = vk::CullModeFlagBits::eNone;
    vk::FrontFace frontFace        = vk::FrontFace::eCounterClockwise;
    bool depthTestEnable           = false;
    bool depthWriteEnable          = false;
    vk::CompareOp depthCompareOp   = vk::CompareOp::eLess;
    bool stencilTestEnable         = false;

    vk::StencilOpState frontStencil{}, backStencil{};
    std::vector<vk::DescriptorSetLayout> setLayouts;
    std::vector<vk::PushConstantRange> pushConstants;
};

struct O5MBeginInfo {
    std::vector<vk::ImageView> colorViews;
    vk::ImageView depthView = nullptr;
    vk::Extent2D extent;
    vk::ClearValue colorClear = vk::ClearColorValue(std::array<float, 4>{0.f,0.f,0.f,1.f}), depthClear{vk::ClearDepthStencilValue{1.0f, 0u}};
    vk::AttachmentLoadOp depthLoadOp = vk::AttachmentLoadOp::eClear;
    vk::AttachmentStoreOp depthStoreOp = vk::AttachmentStoreOp::eStore;
};

class O5MPipeline {
public:
    O5MPipeline(void) = delete;
    O5MPipeline(const O5MPipeline&) = delete;
    O5MPipeline& operator=(const O5MPipeline&) = delete;

    O5MPipeline(O5MDevice& device, const O5MPipelineSpec& spec) : 
        m_device(device), m_spec(spec) {}

    void createPipeline(vk::ShaderModule vs, vk::ShaderModule fs,
                        const char* vsEntry = "main",
                        const char* fsEntry = "main");

    void bindDescriptorSet(vk::raii::CommandBuffer& cmd, vk::DescriptorSet set);
    void bindPipeline(vk::raii::CommandBuffer& cmd);

    void pushConstant(
        vk::raii::CommandBuffer& cmd,
        vk::ShaderStageFlags stages,
        uint32_t offset,
        uint32_t size,
        const void* data
    ) const;

    template <typename T>
    void pushConstant(
        vk::raii::CommandBuffer& cmd,
        vk::ShaderStageFlags stages,
        const T& value,
        uint32_t offset = 0
    ) const {
        static_assert(std::is_trivially_copyable_v<T>, "push constant payload must be trivially copyable");
        pushConstant(cmd, stages, offset, static_cast<uint32_t>(sizeof(T)), &value);
    }

    static void begin(vk::raii::CommandBuffer& cmd, const O5MBeginInfo& beginInfo);
    static void end(vk::raii::CommandBuffer& cmd);

    const O5MPipelineSpec& getSpec(void) const { return m_spec; }
private:
    O5MDevice& m_device;
    O5MPipelineSpec m_spec;

    vk::raii::PipelineLayout m_pipelineLayout = nullptr;
    vk::raii::Pipeline m_pipeline = nullptr;
};

#endif //__O5MPIPELINE__H
