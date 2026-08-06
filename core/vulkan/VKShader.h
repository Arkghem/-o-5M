#ifndef __VKSHADER_H__
#define __VKSHADER_H__

#include "IShader.h"
#include <vulkan/vulkan.h>

// ─────────────────────────────────────────────────────────────────────────────
// VKShader — Vulkan IShader implementation using shaderc for GLSL→SPIR-V
// ─────────────────────────────────────────────────────────────────────────────
// Key differences from GLShader:
//   1. compile() uses shaderc to transpile GLSL source → SPIR-V bytecode,
//      then creates a VkShaderModule from the result. GL compiles per-driver;
//      Vulkan pre-compiles to SPIR-V at engine init.
//   2. link() is a no-op. In Vulkan, "linking" happens at pipeline creation
//      (VkPipelineShaderStageCreateInfo bundles multiple VkShaderModules into
//      a VkGraphicsPipelineCreateInfo). There's no separate link step.
//   3. Reflection (uniformBlocks/textureBindings) returns empty vectors.
//      C10 scope: no SPIRV-Cross. Pipeline reflection will be done later
//      via VkShaderModule introspection or SPIRV-Cross.
//   4. COMPUTE shaders are NOT supported (C9). compile() returns false with
//      a log message.
// ─────────────────────────────────────────────────────────────────────────────

class VKShader : public IShader {
public:
    /// @param type   E_SHADER_TYPE::VERTEX / FRAGMENT / GEOMETRY (COMPUTE unsupported)
    /// @param source Raw GLSL source (will be auto-upgraded to #version 450 if needed)
    /// @param device VkDevice handle for vkCreateShaderModule
    VKShader(E_SHADER_TYPE type, const char* source, VkDevice device);

    /// Destroys the VkShaderModule. The shaderc compiler (if initialized) is
    /// released at compile() scope — we don't keep it alive per instance.
    ~VKShader() override;

    /// Compile GLSL → SPIR-V via shaderc, then create a VkShaderModule.
    /// Auto-upgrades #version directives below 450 to 450 (Vulkan requires
    /// GLSL 450 semantics for the SPIR-V target).
    /// @return true on success, false with compileLog() populated on failure
    bool compile() override;

    /// No-op in Vulkan: "linking" happens at VkGraphicsPipeline creation.
    /// @return always true
    bool link() override { return true; }

    E_SHADER_TYPE type() const override { return m_type; }
    const std::string& compileLog() const override { return m_compileLog; }

    /// Reflection vectors — empty in C10 (no SPIRV-Cross).
    /// Future: populate via SPIRV-Cross or VK_KHR_shader_reflection.
    const std::vector<UniformBlock>& uniformBlocks() const override { return m_uniformBlocks; }
    const std::vector<TextureBinding>& textureBindings() const override { return m_textureBindings; }

    /// @return The compiled VkShaderModule (VK_NULL_HANDLE if compile() failed)
    VkShaderModule module() const { return m_module; }

    /// Maps E_SHADER_TYPE to the corresponding VkShaderStageFlagBits.
    /// Used when building VkPipelineShaderStageCreateInfo.
    VkShaderStageFlagBits stageFlag() const;

private:
    E_SHADER_TYPE m_type;
    std::string   m_source;
    std::string   m_compileLog;
    VkShaderModule m_module = VK_NULL_HANDLE;
    VkDevice       m_device = VK_NULL_HANDLE;
    std::vector<UniformBlock>   m_uniformBlocks;
    std::vector<TextureBinding> m_textureBindings;
};

#endif // __VKSHADER_H__
