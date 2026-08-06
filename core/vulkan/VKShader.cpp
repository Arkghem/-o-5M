#include "VKShader.h"

#include <shaderc/shaderc.hpp>
#include <cstring>       // strlen
#include <regex>         // std::regex for #version replacement

VKShader::VKShader(E_SHADER_TYPE type, const char* source, VkDevice device)
    : m_type(type), m_source(source), m_device(device) {}

VKShader::~VKShader() {
    if (m_module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(m_device, m_module, nullptr);
        m_module = VK_NULL_HANDLE;
    }
}

bool VKShader::compile() {
    // ── C9: compute shaders not yet supported ──────────────────────────────
    if (m_type == E_SHADER_TYPE::COMPUTE) {
        m_compileLog = "compute shaders not supported";
        return false;
    }

    // ── Map E_SHADER_TYPE → shaderc_shader_kind ───────────────────────────
    shaderc_shader_kind kind;
    switch (m_type) {
        case E_SHADER_TYPE::VERTEX:   kind = shaderc_vertex_shader;   break;
        case E_SHADER_TYPE::FRAGMENT: kind = shaderc_fragment_shader; break;
        case E_SHADER_TYPE::GEOMETRY: kind = shaderc_geometry_shader; break;
        default:
            m_compileLog = "unknown shader type";
            return false;
    }

    // ── Auto-upgrade GLSL #version to 450 (Vulkan SPIR-V requirement) ─────
    // Vulkan's GLSL→SPIR-V compiler (glslang, invoked by shaderc) targets
    // Vulkan semantics which require #version 450. shader sources written for
    // OpenGL often use #version 410 core. If the directive is below 450,
    // replace it in-place so the same source works for both backends.
    std::string source = m_source;
    {
        std::regex versionRe(R"(#version\s+(\d+))");
        std::smatch match;
        if (std::regex_search(source, match, versionRe)) {
            int ver = std::stoi(match[1].str());
            if (ver < 450) {
                source = std::regex_replace(source, versionRe, "#version 450");
            }
        }
    }

    // ── Compile GLSL → SPIR-V via shaderc ─────────────────────────────────
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                 shaderc_env_version_vulkan_1_2);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);

    shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
        source.c_str(), source.size(), kind,
        "main",                // entry point name
        options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        m_compileLog  = "shaderc compilation failed:\n";
        m_compileLog += result.GetErrorMessage();
        return false;
    }

    // ── Create VkShaderModule from SPIR-V bytecode ────────────────────────
    std::vector<uint32_t> spirv(result.cbegin(), result.cend());

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = spirv.size() * sizeof(uint32_t);
    moduleInfo.pCode    = spirv.data();

    if (vkCreateShaderModule(m_device, &moduleInfo, nullptr, &m_module)
        != VK_SUCCESS) {
        m_compileLog = "vkCreateShaderModule failed";
        return false;
    }

    return true;
}

VkShaderStageFlagBits VKShader::stageFlag() const {
    switch (m_type) {
        case E_SHADER_TYPE::VERTEX:   return VK_SHADER_STAGE_VERTEX_BIT;
        case E_SHADER_TYPE::FRAGMENT: return VK_SHADER_STAGE_FRAGMENT_BIT;
        case E_SHADER_TYPE::GEOMETRY: return VK_SHADER_STAGE_GEOMETRY_BIT;
        default:                      return VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
    }
}
