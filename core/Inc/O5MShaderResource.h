#ifndef __O5MSHADERRESOURCE__H
#define __O5MSHADERRESOURCE__H

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "O5MResource.h"

class O5MShaderResource : public O5MResource {
private:
    struct ShaderData {
        vk::raii::ShaderModule m_shaderModule;
    };
    vk::ShaderStageFlagBits m_stage;
    vk::raii::Device& m_device;
public:
    O5MShaderResource(void) = delete;
    [[nodiscard]] O5MShaderResource(const std::string& filePath, vk::ShaderStageFlagBits stage, vk::raii::Device& device):
        O5MResource(filePath),
        m_stage(stage),
        m_device(device){}
    ~O5MShaderResource() override { unload(); };
private:
    std::unique_ptr<ShaderData> m_data;
public:
    vk::ShaderModule getShaderModule(void) const { return *m_data->m_shaderModule; }
    vk::ShaderStageFlags getStage(void) const { return m_stage; }
    vk::raii::Device& getDevice(void) const { return m_device; };
private:
    bool doLoad(void) override;
    void doUnload(void) override;

    void createShaderModule(const std::vector<uint32_t>& code);

    std::vector<uint32_t> compileGLSL(const std::string& glslSource,
                                      const std::string& debugName = "shader");
};

#endif // __O5MSHADERRESOURCE__H
