// tex_verify — TextureResource 上传/采样链路验证（无窗口，Phase 0 验收靶场）
//
// 链路：checkerboard PNG（内嵌 187 字节，双色 8x8 格）
//     -> O5MTextureResource::load()（stbi 解码 -> staging -> device image）
//     -> blit pass（NEAREST 采样纹理，写 finalColor/R8G8B8A8_SRGB）
//     -> readback（copyImageToBuffer -> HOST_VISIBLE buffer）
//     -> CPU 逐像素校验（期望值 = 棋盘格原始字节，容差 ±1）
//
// !!! 本文件依赖 TextureResource 修复后的接口，修复完成前不参与编译 !!!
// 修复完成后：在根 CMakeLists.txt 取消 tex_verify 目标的两行注释即可。
//
// 对修复后 O5MTextureResource 的契约（tex_verify 按此调用）：
//   1. load() 成功后 getImageView() 返回可用的 vk::ImageView
//   2. image usage 含 eSampled（否则无法采样，validation 会报）
//   3. load() 返回时 image 处于 eShaderReadOnlyOptimal 布局
//      （当前 O5MDevice::copyBufferToImage 不做任何布局转换，需要修）
//   4. stbi 像素只释放一次；staging 写入对 GPU 可见（coherent 或 flush）
//
// 退出码：0 = 通过；非 0 = 失败。

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "RHI/O5MDevice.h"
#include "RHI/O5MPipeline.h"
#include "RenderGraph/O5MRendergraph.h"
#include "Resources/O5MShaderResource.h"
#include "Resources/O5MTextureResource.h"

namespace {

#ifdef __APPLE__
const std::vector<const char*> kInstanceExtensions = {
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
};
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

// ---- 输入纹理：64x64 RGBA PNG，8px 棋盘格，双色 ----
// A = (200, 60, 40, 255)，B = (30, 90, 220, 255)；cell(x,y) = (x/8 + y/8) % 2
static const unsigned char kCheckerPng[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x40, 0x08, 0x06, 0x00, 0x00, 0x00, 0xaa, 0x69, 0x71,
    0xde, 0x00, 0x00, 0x00, 0x82, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0xed, 0xd8, 0xb1, 0x15, 0x00,
    0x10, 0x10, 0x44, 0xc1, 0xab, 0x44, 0xac, 0x08, 0x95, 0xa8, 0x58, 0x11, 0x7a, 0xa1, 0x03, 0x62,
    0x6e, 0x02, 0xe1, 0x05, 0x26, 0xfa, 0x6f, 0x63, 0xb4, 0xba, 0x4e, 0xaf, 0xf4, 0x79, 0x7c, 0xaf,
    0xdf, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x18, 0xe0, 0xf7, 0x0f, 0xde, 0xee, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xcc, 0x00, 0x4a, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xb0, 0x07, 0x28, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x1e, 0xa0, 0x04, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x7b, 0x80, 0x12, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xec,
    0x01, 0x4a, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb0, 0x07, 0x28, 0x41, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xe0, 0x3b, 0x80, 0x0d, 0x10, 0xbd, 0xf2, 0x0e, 0x57, 0x5e, 0x4a, 0x98, 0x00,
    0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
};
static constexpr size_t kCheckerPngSize = sizeof(kCheckerPng);

constexpr uint32_t kTexSize = 64;      // 纹理与渲染目标同尺寸：NEAREST 1:1 直采
constexpr uint32_t kCell = 8;

struct RGB8 { uint8_t r, g, b; };
constexpr RGB8 kCellA = { 200, 60, 40 };
constexpr RGB8 kCellB = { 30, 90, 220 };

// 期望颜色：cell(x,y) = (x/8 + y/8) % 2，0 -> A，1 -> B
const RGB8& expectedColor(uint32_t x, uint32_t y) {
    return ((x / kCell + y / kCell) % 2 == 0) ? kCellA : kCellB;
}

// ---- GLSL：fullscreen triangle + NEAREST 直采 passthrough ----
const char* kFullscreenVert = R"GLSL(
#version 450
layout(location = 0) out vec2 vUV;
void main() {
    vec2 pos = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    vUV = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

// 纹理采样 -> linear（sRGB 附件自动解码）-> 写 sRGB 附件（自动编码回）
// 字节级 roundtrip 应还原原始 sRGB 字节（±1 量化误差）
const char* kBlitFrag = R"GLSL(
#version 450
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D kTexture;
void main() {
    outColor = texture(kTexture, vUV);
}
)GLSL";

} // namespace

int main() {
    try {
        // ---- 1. instance（与 rhi_verify 相同的 bootstrap）----
        vk::raii::Context context;
        vk::ApplicationInfo appInfo("o5m-tex-verify", 1, "o5m", 1, VK_API_VERSION_1_2);

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
        vk::StructureChain<vk::InstanceCreateInfo, vk::ValidationFeaturesEXT> validationChain;

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

        // ---- 2. physical device + O5MDevice ----
        vk::raii::PhysicalDevices physicalDevices(instance);
        if (physicalDevices.empty()) {
            throw std::runtime_error("no physical device (portability enumeration missing?)");
        }
        const vk::raii::PhysicalDevice& physicalDevice = physicalDevices.front();

        const uint32_t graphicsFamily = findGraphicsQueueFamily(*physicalDevice);
        float queuePriority = 1.0f;
        vk::DeviceQueueCreateInfo queueInfo({}, graphicsFamily, 1, &queuePriority);
        vk::DeviceCreateInfo deviceInfo;
        vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures(true);
        deviceInfo.setQueueCreateInfos(queueInfo)
                  .setPEnabledExtensionNames(kDeviceExtensions)
                  .setPNext(&dynamicRenderingFeatures);
        O5MDevice o5mDevice(physicalDevice, vk::raii::Device(physicalDevice, deviceInfo),
                            graphicsFamily);
        std::cout << "[ok] device + graphics queue (family " << graphicsFamily << ")\n";

        vk::raii::CommandPool commandPool(o5mDevice.getDevice(), { {}, graphicsFamily });
        vk::raii::CommandBuffers commandBuffers(
            o5mDevice.getDevice(), { *commandPool, vk::CommandBufferLevel::ePrimary, 1 });

        // ---- 3. 纹理资源：PNG -> stbi -> device image（被测对象）----
        O5MTextureResource texRes("checker.png", o5mDevice);
        texRes.setData(const_cast<unsigned char*>(kCheckerPng), kCheckerPngSize);
        if (!texRes.load()) {
            throw std::runtime_error("O5MTextureResource::load() failed (stbi decode?)");
        }
        if (texRes.getWidth() != int(kTexSize) || texRes.getHeight() != int(kTexSize)) {
            std::cerr << "[FAIL] decoded size " << texRes.getWidth() << "x" << texRes.getHeight()
                      << " != 64x64\n";
            return 1;
        }
        std::cout << "[ok] texture loaded (stbi -> staging -> device image, "
                  << kCheckerPngSize << "B PNG -> " << kTexSize << "x" << kTexSize << " RGBA)\n";

        // ---- 4. graph 资源：sRGB 输出图 + 回读 buffer ----
        const vk::Extent2D extent{ kTexSize, kTexSize };

        O5MRendergraph graph(o5mDevice);
        graph.addResource("finalColor", vk::Format::eR8G8B8A8Srgb, extent,
                          vk::ImageUsageFlagBits::eColorAttachment |
                              vk::ImageUsageFlagBits::eTransferSrc,
                          vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eTransferSrcOptimal);
        graph.addBufferResource("readback", kTexSize * kTexSize * 4,
                                vk::BufferUsageFlagBits::eTransferDst,
                                vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent);

        O5MShaderResource vsRes("fullscreen.vert", vk::ShaderStageFlagBits::eVertex, o5mDevice);
        O5MShaderResource blitFsRes("blit.frag", vk::ShaderStageFlagBits::eFragment, o5mDevice);
        vsRes.setData(const_cast<char*>(kFullscreenVert), std::strlen(kFullscreenVert) + 1);
        blitFsRes.setData(const_cast<char*>(kBlitFrag), std::strlen(kBlitFrag) + 1);
        vsRes.load(); blitFsRes.load();
        std::cout << "[ok] shaders compiled (GLSL -> SPIR-V via shaderc)\n";

        // NEAREST：无插值，1:1 尺寸下输出像素 (x,y) == 纹素 (x,y)，期望值可精确计算
        vk::SamplerCreateInfo samplerInfo;
        samplerInfo.setMagFilter(vk::Filter::eNearest)
            .setMinFilter(vk::Filter::eNearest)
            .setMipmapMode(vk::SamplerMipmapMode::eNearest)
            .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
            .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
            .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
            .setMinLod(0.f).setMaxLod(0.f);
        vk::raii::Sampler sampler(o5mDevice.getDevice(), samplerInfo);

        // descriptor：b0 = combined image sampler(纹理)
        const std::vector<vk::DescriptorSetLayoutBinding> bindings = {
            { 0, vk::DescriptorType::eCombinedImageSampler, 1,
              vk::ShaderStageFlagBits::eFragment, nullptr },
        };
        vk::DescriptorSetLayoutCreateInfo layoutInfo({}, bindings);
        vk::raii::DescriptorSetLayout descLayout(o5mDevice.getDevice(), layoutInfo);

        O5MPipeline blitPipeline(o5mDevice);
        blitPipeline.createPipeline({ vk::Format::eR8G8B8A8Srgb },
                                    vsRes.getShaderModule(), blitFsRes.getShaderModule(),
                                    "main", "main", *descLayout);
        std::cout << "[ok] pipeline created\n";

        // descSet 先声明后赋值：pass 回调按引用捕获，execute 时取值
        vk::DescriptorSet descSet;

        graph.addPass("blit", {}, { "finalColor" },
            [&](vk::raii::CommandBuffer& cmd) {
                blitPipeline.begin(cmd, { *graph.getResource("finalColor")->imageView }, extent);
                blitPipeline.bindDescriptorSet(cmd, descSet);
                cmd.draw(3, 1, 0, 0);
                blitPipeline.end(cmd);
            });

        graph.addPass("readback", { "finalColor" }, { "readback" },
            [&](vk::raii::CommandBuffer& cmd) {
                vk::BufferImageCopy region(
                    0, 0, 0,
                    { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                    { 0, 0, 0 }, { kTexSize, kTexSize, 1 });
                cmd.copyImageToBuffer(*graph.getResource("finalColor")->image,
                                      vk::ImageLayout::eTransferSrcOptimal,
                                      *graph.getResource("readback")->buffer, region);
            });

        graph.compile();
        std::cout << "[ok] graph compiled\n";

        // descriptor write：契约 —— load() 后纹理处于 eShaderReadOnlyOptimal
        const std::vector<vk::DescriptorPoolSize> poolSizes = {
            { vk::DescriptorType::eCombinedImageSampler, 1 },
        };
        vk::DescriptorPoolCreateInfo poolInfoFull(
            vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, poolSizes);
        vk::raii::DescriptorPool descPool(o5mDevice.getDevice(), poolInfoFull);
        vk::DescriptorSetAllocateInfo allocInfo(*descPool, { *descLayout });
        vk::raii::DescriptorSets descSets(o5mDevice.getDevice(), allocInfo);
        descSet = descSets.front();

        vk::DescriptorImageInfo texInfo(
            *sampler, texRes.getImageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
        vk::WriteDescriptorSet writes[] = {
            { descSet, 0, 0, vk::DescriptorType::eCombinedImageSampler, texInfo, {}, {} },
        };
        o5mDevice.getDevice().updateDescriptorSets(writes, {});
        std::cout << "[ok] descriptor written (view + nearest sampler)\n";

        // ---- 5. 执行 + 校验 ----
        vk::raii::Fence fence(o5mDevice.getDevice(), vk::FenceCreateInfo());
        graph.execute(commandBuffers[0], *o5mDevice.getQueue(), &fence);
        if (o5mDevice.getDevice().waitForFences(*fence, true, UINT64_MAX) !=
            vk::Result::eSuccess) {
            throw std::runtime_error("waitForFences failed");
        }
        std::cout << "[ok] submitted + fence waited\n";

        // 探针覆盖两种颜色 + 四角附近；Vulkan Y 朝下，vUV=(x+0.5)/W 与纹素一一对应
        const uint8_t* pixels = static_cast<const uint8_t*>(
            graph.getResource("readback")->memory.mapMemory(0, vk::WholeSize));
        struct Probe { uint32_t x, y; };
        const Probe probes[] = {
            { 4, 4 },     // cell(0,0)=A
            { 30, 10 },   // cell(3,1)=A
            { 12, 20 },   // cell(1,2)=B
            { 40, 50 },   // cell(5,6)=B
            { 63, 63 },   // cell(7,7)=A（右下角）
            { 63, 0 },    // cell(7,0)=B（右上角）
        };
        bool ok = true;
        for (const auto& p : probes) {
            const RGB8& want = expectedColor(p.x, p.y);
            const size_t i = (p.y * kTexSize + p.x) * 4;
            const int got[4] = { pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3] };
            const int exp[4] = { want.r, want.g, want.b, 255 };
            for (int c = 0; c < 4; ++c) {
                if (std::abs(got[c] - exp[c]) > 1) {   // sRGB roundtrip 允许 ±1
                    std::cerr << "[FAIL] pixel (" << p.x << "," << p.y << ") ch" << c
                              << " got " << got[c] << " want " << exp[c] << "\n";
                    ok = false;
                }
            }
        }
        graph.getResource("readback")->memory.unmapMemory();

        if (!ok) {
            return 1;
        }
        std::cout << "[ok] " << std::size(probes)
                  << " probes verified (PNG -> upload -> NEAREST sample -> sRGB roundtrip)\n";
        std::cout << "[PASS] tex_verify: TextureResource 链路 OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << "\n";
        return 1;
    }
}
