// rhi_verify -- offscreen rendergraph verification (no window), Phase 1 API.
//
// Chain: render(UBO-driven, writes sceneColor/RGBA16F)
//     -> postprocess(samples sceneColor, Reinhard tonemap + gamma, writes finalColor/RGBA8)
//     -> readback(copyImageToBuffer -> HOST_VISIBLE buffer, imported)
//     -> CPU per-pixel verification (same math recomputed, tolerance 2/255)
//
// Verified:
//   1. O5MDevice bootstrap + dynamic rendering (Vulkan 1.2 + extensions)
//   2. RenderGraph Phase 1: ResourceInfo declaration, PassBuilder setup,
//      derived usage flags, topological sort, dead-pass culling via markOutput,
//      compile-time barrier planning, replay-style execute
//   3. Imported buffer resources (params UBO + readback own their memory)
//   4. Descriptor set (UBO + combined image sampler)
//   5. Fence sync + pixel validation, exit code = pass/fail
//
// Exit code: 0 = full chain passed; non-zero = failed (error to stderr).

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "RHI/O5MDevice.h"
#include "RHI/O5MPipeline.h"
#include "RenderGraph/O5MRendergraph.h"
#include "Resources/O5MShaderResource.h"

namespace {

#ifdef __APPLE__
// MoltenVK requires explicit portability enumeration, else no physical devices
const std::vector<const char*> kInstanceExtensions = {
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
};
// The SDK header does not define VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
const std::vector<const char*> kDeviceExtensions = {
    "VK_KHR_portability_subset",
    VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
};
#else
const std::vector<const char*> kInstanceExtensions = {};
const std::vector<const char*> kDeviceExtensions = {
    VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
};
#endif

uint32_t findGraphicsQueueFamily(const vk::PhysicalDevice& physicalDevice) {
    auto props = physicalDevice.getQueueFamilyProperties();
    for (uint32_t i = 0; i < props.size(); ++i) {
        if (props[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            return i;
        }
    }
    throw std::runtime_error("no graphics queue family found");
}

// ---- embedded GLSL, compiled at runtime through O5MShaderResource/shaderc ----

// fullscreen triangle: 3 vertices (-1,-1) (3,-1) (-1,3), no vertex buffer
const char* kFullscreenVert = R"GLSL(
#version 450
layout(location = 0) out vec2 vUV;
void main() {
    vec2 pos = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    vUV = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

// render pass: UV gradient * exposure from UBO (validates buffer -> shader path)
const char* kSceneFrag = R"GLSL(
#version 450
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform Params {
    vec4 data; // .x = exposure
} params;
void main() {
    outColor = vec4(vUV.x * params.data.x, vUV.y * params.data.x, 0.25, 1.0);
}
)GLSL";

// postprocess pass: sample sceneColor, Reinhard tonemap + 1/2.2 gamma
const char* kPostFrag = R"GLSL(
#version 450
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 1) uniform sampler2D sceneColor;
void main() {
    vec3 c = texture(sceneColor, vUV).rgb;
    c = c / (1.0 + c);
    c = pow(c, vec3(1.0 / 2.2));
    outColor = vec4(c, 1.0);
}
)GLSL";

// CPU-side recomputation of the shader math (double precision)
double shaderMath(double channel) {
    double c = channel / (1.0 + channel);
    return std::pow(c, 1.0 / 2.2);
}

} // namespace

int main() {
    try {
        // ---- 1. instance ----
        vk::raii::Context context;
        vk::ApplicationInfo appInfo("o5m-rhi-verify", 1, "o5m", 1, VK_API_VERSION_1_2);

        // sync validation via VK_LAYER_KHRONOS_validation + VK_EXT_validation_features;
        // silently degrades to plain run when the layer is not installed
        std::vector<const char*> validationLayers;
        for (const auto& layer : context.enumerateInstanceLayerProperties()) {
            if (std::string_view(layer.layerName) == "VK_LAYER_KHRONOS_validation") {
                validationLayers.push_back("VK_LAYER_KHRONOS_validation");
                std::cout << "[ok] validation layer found, sync validation enabled\n";
                break;
            }
        }
        const bool useValidation = !validationLayers.empty();

        vk::ValidationFeatureEnableEXT syncFeature =
            vk::ValidationFeatureEnableEXT::eSynchronizationValidation;
        vk::ValidationFeaturesEXT validationFeatures(1, &syncFeature, 0, nullptr);
        vk::StructureChain<vk::InstanceCreateInfo, vk::ValidationFeaturesEXT>
            validationChain;

        vk::InstanceCreateInfo instanceInfo;
#ifdef __APPLE__
        instanceInfo.setFlags(vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR);
#endif
        instanceInfo.setPApplicationInfo(&appInfo)
                    .setPEnabledLayerNames(validationLayers)
                    .setPEnabledExtensionNames(kInstanceExtensions);

        if (useValidation) {
            validationChain.get<vk::InstanceCreateInfo>() = instanceInfo;
            validationChain.get<vk::ValidationFeaturesEXT>() = validationFeatures;
        }
        vk::raii::Instance instance(
            context,
            useValidation ? validationChain.get<vk::InstanceCreateInfo>()
                          : instanceInfo);
        std::cout << "[ok] instance created\n";

        // ---- 2. physical device ----
        vk::raii::PhysicalDevices physicalDevices(instance);
        if (physicalDevices.empty()) {
            throw std::runtime_error("no physical device (portability enumeration missing?)");
        }
        const vk::raii::PhysicalDevice& physicalDevice = physicalDevices.front();
        std::cout << "[ok] physical device: "
                  << physicalDevice.getProperties().deviceName << "\n";

        // ---- 3. O5MDevice ----
        const uint32_t graphicsFamily = findGraphicsQueueFamily(*physicalDevice);
        float queuePriority = 1.0f;
        vk::DeviceQueueCreateInfo queueInfo({}, graphicsFamily, 1, &queuePriority);
        vk::DeviceCreateInfo deviceInfo;
        // features are opt-in in Vulkan
        vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures(true);
        deviceInfo.setQueueCreateInfos(queueInfo)
                  .setPEnabledExtensionNames(kDeviceExtensions)
                  .setPNext(&dynamicRenderingFeatures);
        O5MDevice o5mDevice(physicalDevice, vk::raii::Device(physicalDevice, deviceInfo),
                            graphicsFamily);
        std::cout << "[ok] device + graphics queue (family " << graphicsFamily << ")\n";

        // ---- 4. command pool + one command buffer ----
        vk::raii::CommandPool commandPool(o5mDevice.getDevice(), { {}, graphicsFamily });
        vk::raii::CommandBuffers commandBuffers(
            o5mDevice.getDevice(), { *commandPool, vk::CommandBufferLevel::ePrimary, 1 });

        // ---- 5. render chain via the Phase 1 graph ----
        constexpr uint32_t kSize = 64;
        const vk::Extent2D extent{ kSize, kSize };
        const float kExposure = 2.0f;

        // 5a. graph-declared (created) resources: usage flags are DERIVED at
        // compile() from the UseDecls below, never hand-written here.
        std::string sceneColorName = "sceneColor";
        std::string finalColorName = "finalColor";
        ResourceInfo sceneColorInfo(sceneColorName, extent,
                                     vk::Format::eR16G16B16A16Sfloat);
        ResourceInfo finalColorInfo(finalColorName, extent,
                                    vk::Format::eR8G8B8A8Unorm);

        // 5b. imported buffers: created outside the graph (we keep the memory
        // so we can write the UBO / read back the pixels ourselves).
        // HOST_VISIBLE|HOST_COHERENT: params needs CPU write, readback needs CPU read.
        auto [paramsBuffer, paramsMemory] = o5mDevice.createBuffer(
            16, vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible |
                vk::MemoryPropertyFlagBits::eHostCoherent);
        auto [readbackBuffer, readbackMemory] = o5mDevice.createBuffer(
            vk::DeviceSize(kSize) * kSize * 4, vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eHostVisible |
                vk::MemoryPropertyFlagBits::eHostCoherent);

        O5MRendergraph graph(o5mDevice);
        // imported info objects reuse the same interned handle as the import call
        std::string paramsName = "params";
        std::string readbackName = "readback";
        ResourceInfo paramsInfo(paramsName, vk::DeviceSize(16),
                                ResourceSource::Imported);
        ResourceInfo readbackInfo(readbackName,
                                  vk::DeviceSize(kSize) * kSize * 4,
                                  ResourceSource::Imported);
        graph.importBuffer(paramsName, *paramsBuffer, 16,
                           vk::BufferUsageFlagBits::eUniformBuffer);
        graph.importBuffer(readbackName, *readbackBuffer,
                           vk::DeviceSize(kSize) * kSize * 4,
                           vk::BufferUsageFlagBits::eTransferDst);
        graph.markOutput(readbackInfo.handle); // root for dead-pass culling

        // 5c. shaders: embedded GLSL, runtime shaderc compile
        // (direct construction: the manager's create() is still buggy, bypassed)
        O5MShaderResource vsRes("fullscreen.vert", vk::ShaderStageFlagBits::eVertex, o5mDevice);
        O5MShaderResource sceneFsRes("scene.frag", vk::ShaderStageFlagBits::eFragment, o5mDevice);
        O5MShaderResource postFsRes("post.frag", vk::ShaderStageFlagBits::eFragment, o5mDevice);
        vsRes.setData(const_cast<char*>(kFullscreenVert), std::strlen(kFullscreenVert) + 1);
        sceneFsRes.setData(const_cast<char*>(kSceneFrag), std::strlen(kSceneFrag) + 1);
        postFsRes.setData(const_cast<char*>(kPostFrag), std::strlen(kPostFrag) + 1);
        vsRes.load(); sceneFsRes.load(); postFsRes.load();
        std::cout << "[ok] shaders compiled (GLSL -> SPIR-V via shaderc)\n";

        // 5d. descriptor layout: b0 = UBO(params), b1 = sampler2D(sceneColor)
        // both pipelines share one layout/set; render ignores b1, post ignores b0
        const std::vector<vk::DescriptorSetLayoutBinding> bindings = {
            { 0, vk::DescriptorType::eUniformBuffer, 1,
              vk::ShaderStageFlagBits::eFragment, nullptr },
            { 1, vk::DescriptorType::eCombinedImageSampler, 1,
              vk::ShaderStageFlagBits::eFragment, nullptr },
        };
        vk::DescriptorSetLayoutCreateInfo layoutInfo({}, bindings);
        vk::raii::DescriptorSetLayout descLayout(o5mDevice.getDevice(), layoutInfo);

        const std::vector<vk::DescriptorPoolSize> poolSizes = {
            { vk::DescriptorType::eUniformBuffer, 2 },
            { vk::DescriptorType::eCombinedImageSampler, 1 },
        };
        vk::DescriptorPoolCreateInfo poolInfo(
            vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 2, poolSizes);
        vk::raii::DescriptorPool descPool(o5mDevice.getDevice(), poolInfo);
        std::vector<vk::DescriptorSetLayout> setLayouts{ *descLayout, *descLayout };
        vk::DescriptorSetAllocateInfo allocInfo(*descPool, setLayouts);
        vk::raii::DescriptorSets descSets(o5mDevice.getDevice(), allocInfo);
        // one set per pipeline: updating a set that an earlier pass already
        // bound would invalidate the command buffer
        vk::DescriptorSet renderDescSet = descSets[0];
        vk::DescriptorSet postDescSet = descSets[1];

        vk::SamplerCreateInfo samplerInfo;
        samplerInfo.setMagFilter(vk::Filter::eLinear)
            .setMinFilter(vk::Filter::eLinear)
            .setMipmapMode(vk::SamplerMipmapMode::eLinear)
            .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
            .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
            .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
            .setMinLod(0.f).setMaxLod(0.f);
        vk::raii::Sampler sampler(o5mDevice.getDevice(), samplerInfo);

        // 5e. pipelines: render -> RGBA16F, postprocess -> RGBA8
        O5MPipeline renderPipeline(o5mDevice);
        renderPipeline.createPipeline({ vk::Format::eR16G16B16A16Sfloat },
                                      vsRes.getShaderModule(), sceneFsRes.getShaderModule(),
                                      "main", "main", *descLayout);
        O5MPipeline postPipeline(o5mDevice);
        postPipeline.createPipeline({ vk::Format::eR8G8B8A8Unorm },
                                    vsRes.getShaderModule(), postFsRes.getShaderModule(),
                                    "main", "main", *descLayout);
        std::cout << "[ok] pipelines created\n";

        // 5f. passes: setup declares reads/writes, execute records commands.
        // The graph derives ordering + barriers from the declarations.
        graph.addGraphicPass("render",
            [&](O5MPassBuilder& b) {
                b.read(paramsInfo, BufRead::Uniform);
                b.write(sceneColorInfo, TexWrite::ColorStore);
            },
            [&](O5MRenderContext& ctx, vk::raii::CommandBuffer& cmd) {
                renderPipeline.begin(cmd, { ctx.getImageView(sceneColorInfo.handle) }, extent);
                renderPipeline.bindDescriptorSet(cmd, renderDescSet);
                cmd.draw(3, 1, 0, 0);
                renderPipeline.end(cmd);
            });

        graph.addGraphicPass("postprocess",
            [&](O5MPassBuilder& b) {
                b.read(sceneColorInfo, TexRead::Sampled);
                b.write(finalColorInfo, TexWrite::ColorStore);
            },
            [&](O5MRenderContext& ctx, vk::raii::CommandBuffer& cmd) {
                // the sampled view only exists after compile(), so the
                // descriptor write happens at record time via the context;
                // it targets a set no earlier pass has bound yet
                vk::DescriptorBufferInfo uboInfo(*paramsBuffer, 0, 16);
                vk::DescriptorImageInfo sceneInfo(
                    *sampler, ctx.getImageView(sceneColorInfo.handle),
                    vk::ImageLayout::eShaderReadOnlyOptimal);
                vk::WriteDescriptorSet writes[] = {
                    { postDescSet, 0, 0, vk::DescriptorType::eUniformBuffer, {}, uboInfo, {} },
                    { postDescSet, 1, 0, vk::DescriptorType::eCombinedImageSampler, sceneInfo, {}, {} },
                };
                o5mDevice.getDevice().updateDescriptorSets(writes, {});

                postPipeline.begin(cmd, { ctx.getImageView(finalColorInfo.handle) }, extent);
                postPipeline.bindDescriptorSet(cmd, postDescSet);
                cmd.draw(3, 1, 0, 0);
                postPipeline.end(cmd);
            });

        graph.addGraphicPass("readback",
            [&](O5MPassBuilder& b) {
                b.read(finalColorInfo, TexRead::TransferSrc);
                b.write(readbackInfo, BufWrite::TransferDst);
            },
            [&](O5MRenderContext& ctx, vk::raii::CommandBuffer& cmd) {
                vk::BufferImageCopy region(
                    0, 0, 0,
                    { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                    { 0, 0, 0 }, { kSize, kSize, 1 });
                // layout is guaranteed eTransferSrcOptimal by the compiled barrier
                cmd.copyImageToBuffer(ctx.getImage(finalColorInfo.handle),
                                      vk::ImageLayout::eTransferSrcOptimal,
                                      ctx.getBuffer(readbackInfo.handle), region);
            });

        graph.compile();
        graph.dump();
        std::cout << "[ok] graph compiled\n";

        // 5g. UBO content: host write into OUR memory (graph only tracks sync)
        {
            float params[4] = { kExposure, 0.f, 0.f, 0.f };
            void* mapped = paramsMemory.mapMemory(0, vk::WholeSize);
            std::memcpy(mapped, params, sizeof(params));
            paramsMemory.unmapMemory();
        }

        // render set's UBO binding is known before execute; write it up front
        {
            vk::DescriptorBufferInfo uboInfo(*paramsBuffer, 0, 16);
            vk::WriteDescriptorSet write {
                renderDescSet, 0, 0, vk::DescriptorType::eUniformBuffer, {}, uboInfo, {}
            };
            o5mDevice.getDevice().updateDescriptorSets(write, {});
        }

        // ---- 6. execute (fence sync) + verify ----
        vk::raii::Fence fence(o5mDevice.getDevice(), vk::FenceCreateInfo());
        graph.execute(commandBuffers[0], *o5mDevice.getQueue(), &fence);
        if (o5mDevice.getDevice().waitForFences(*fence, true, UINT64_MAX) !=
            vk::Result::eSuccess) {
            throw std::runtime_error("waitForFences failed");
        }
        std::cout << "[ok] submitted + fence waited\n";

        // CPU per-pixel check. No v-flip: Vulkan NDC is Y-down
        // ((-1,-1) = viewport top-left), the fullscreen triangle's vUV
        // interpolates to (x+0.5)/W, (y+0.5)/H directly.
        const uint8_t* pixels = static_cast<const uint8_t*>(
            readbackMemory.mapMemory(0, vk::WholeSize));
        struct Probe { uint32_t x, y; };
        const Probe probes[] = { { 32, 32 }, { 10, 20 }, { 40, 50 }, { 5, 5 } };
        bool ok = true;
        for (const auto& p : probes) {
            const double u = (p.x + 0.5) / kSize;
            const double v = (p.y + 0.5) / kSize;
            const double expected[3] = {
                shaderMath(u * kExposure), shaderMath(v * kExposure), shaderMath(0.25) };
            const size_t i = (p.y * kSize + p.x) * 4;
            for (int c = 0; c < 3; ++c) {
                const int got = pixels[i + c];
                const int want = static_cast<int>(std::lround(expected[c] * 255.0));
                if (std::abs(got - want) > 2) {
                    std::cerr << "[FAIL] pixel (" << p.x << "," << p.y << ") ch" << c
                              << " got " << got << " want " << want << "\n";
                    ok = false;
                }
            }
            if (pixels[i + 3] != 255) {
                std::cerr << "[FAIL] pixel (" << p.x << "," << p.y << ") alpha "
                          << pixels[i + 3] << " != 255\n";
                ok = false;
            }
        }
        readbackMemory.unmapMemory();

        if (!ok) {
            return 1;
        }
        std::cout << "[ok] " << std::size(probes) << " pixels verified (UBO + tonemap math)\n";
        std::cout << "[PASS] rhi_verify: render -> postprocess -> readback via Phase 1 graph\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << "\n";
        return 1;
    }
}
