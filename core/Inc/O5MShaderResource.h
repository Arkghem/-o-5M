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
public:
    O5MShaderResource(const std::string &name, vk::ShaderStageFlagBits stage) : 
        O5MResource(name),
        m_stage(stage) {}
    ~O5MShaderResource() override { unload(); };
private:
    std::unique_ptr<ShaderData> m_data;
public:
    vk::ShaderModule getShaderModule(void) const { return *m_data->m_shaderModule; }
    vk::ShaderStageFlagBits getStage(void) const { return m_stage; }
    vk::Device getDevice(void);
private:
    bool doLoad(void) override;
    void doUnload(void) override;

    bool readFile(const std::string& fileName);
    void createShaderModule(const std::vector<char>& code);
};

#endif // __O5MSHADERRESOURCE__H
