#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include "O5MShaderResource.h"
#include "shaderc/shaderc.hpp"

bool O5MShaderResource::doLoad(void) {
    assert(getData() != nullptr);

    char* casted_data = static_cast<char*>(getData());
    std::string glslSource(casted_data);

    std::vector<uint32_t> code = compileGLSL(glslSource, getName());
    
    vk::ShaderModuleCreateInfo shaderModuleCreateInfo {
        .codeSize = code.size() * sizeof(uint32_t),
        .pCode = code.data(),
    };

    m_shaderModule = m_device.getDevice().createShaderModule(shaderModuleCreateInfo);

    return true;
}

void O5MShaderResource::doUnload(void) {
    m_shaderModule.clear();
}


std::vector<uint32_t> O5MShaderResource::compileGLSL(const std::string& glslSource,
                                  const std::string& debugName) {
    static shaderc::Compiler compiler; 

    shaderc_shader_kind kind = (m_stage & vk::ShaderStageFlagBits::eVertex)
                                   ? shaderc_glsl_vertex_shader
                                   : shaderc_glsl_fragment_shader;

    shaderc::CompileOptions options;
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    options.SetGenerateDebugInfo();

    shaderc::SpvCompilationResult result =
        compiler.CompileGlslToSpv(glslSource, kind, debugName.c_str(), options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        throw std::runtime_error("GLSL compile failed [" + debugName + "]: " +
                                 result.GetErrorMessage());
    }

    return { result.cbegin(), result.cend() };
}
