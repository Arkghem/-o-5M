#include "O5MShaderCompiler.h"

#include <shaderc/shaderc.hpp>

#include <stdexcept>


std::vector<uint32_t> compileGLSL(ShaderStage stage, const std::string& glslSource,
                                  const std::string& debugName) {
    static shaderc::Compiler compiler; 

    shaderc_shader_kind kind = (stage == ShaderStage::Vertex)
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

