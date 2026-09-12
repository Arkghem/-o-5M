#ifndef __O5MSHADERRESOURCE__H
#define __O5MSHADERRESOURCE__H

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "Resources/O5MResource.h"
#include "RHI/O5MDevice.h"

class O5MShaderResource : public O5MResource {
private:
    vk::raii::ShaderModule m_shaderModule = nullptr;
    vk::ShaderStageFlagBits m_stage;

    const O5MDevice& m_device;
public:
    O5MShaderResource(void) = delete;
    [[nodiscard]] O5MShaderResource(const std::string& name, vk::ShaderStageFlagBits stage, const O5MDevice& device):
        O5MResource(name),
        m_stage(stage),
        m_device(device){}
    ~O5MShaderResource() override { unload(); };
public:
    vk::ShaderModule getShaderModule(void) const { return m_shaderModule; }
    vk::ShaderStageFlags getStage(void) const { return m_stage; }
private:
    bool doLoad(void) override; //create vulkan resource
    void doUnload(void) override;

    std::vector<uint32_t> compileGLSL(const std::string& glslSource,
                                      const std::string& debugName = "shader");
};

#endif // __O5MSHADERRESOURCE__H
