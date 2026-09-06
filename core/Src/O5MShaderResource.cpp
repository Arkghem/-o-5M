#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include "O5MShaderResource.h"
#include "shaderc/shaderc.hpp"

bool O5MShaderResource::doLoad(void) {
    std::string glslSource = readFile(getfilePath());
    if (glslSource.empty()) {
        return false;
    }

    std::vector<uint32_t> code = compileGLSL(glslSource, getfilePath());

    createShaderModule(code);

    return true;
}

void O5MShaderResource::doUnload(void) {
    if (isloaded()) {
        m_data.reset();
    }
}

void O5MShaderResource::createShaderModule(const std::vector<uint32_t>& code) {
    vk::ShaderModuleCreateInfo shaderModuleCreateInfo {
        .codeSize = code.size() * sizeof(uint32_t),
        .pCode = code.data(),
    };

    m_data = std::make_unique<ShaderData>(m_device.createShaderModule(shaderModuleCreateInfo));
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
