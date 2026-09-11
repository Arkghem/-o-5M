// rhi_verify — rendergraph 离屏渲染链路验证（无窗口）
//
// 链路：render(UBO 控制, 写 sceneColor/RGBA16F)
//     -> postprocess(采样 sceneColor, Reinhard tonemap + gamma, 写 finalColor/RGBA8)
//     -> readback(copyImageToBuffer -> HOST_VISIBLE buffer)
//     -> CPU 逐像素校验（期望值用同一套数学在 CPU 复算，容差 ±2/255）
//
// 验证内容：
//   1. O5MDevice bootstrap + dynamic rendering（Vulkan 1.2 + 扩展）
//   2. rendergraph: image/buffer 资源、三 pass 依赖排序、自动 barrier
//   3. descriptor set（UBO + combined image sampler）
//   4. buffer 资源链路（UBO host 写入 -> shader 读；TransferDst 回读）
//   5. fence 同步（替代 waitIdle）+ 回读校验退出码
//
// 退出码：0 = 全链路通过；非 0 = 失败（异常信息打印到 stderr）。

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "O5MDevice.h"
#include "O5MPipeline.h"
#include "O5MRendergraph.h"
#include "O5MShaderResource.h"

namespace {

#ifdef __APPLE__
// MoltenVK 要求显式开启 portability 枚举，否则枚举不到任何物理设备
const std::vector<const char*> kInstanceExtensions = {
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
};
// 该 SDK 头文件未定义 VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME 宏，用字符串字面量
const std::vector<const char*> kDeviceExtensions = {
    "VK_KHR_portability_subset",
    VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME // dynamic rendering（Vulkan 1.3 转正，1.2 走扩展）
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

// ---- GLSL 源（内嵌字符串，O5MShaderResource 运行时经 shaderc 编译成 SPIR-V）----
// 入口名必须是 main：GLSL 经 shaderc 编译不支持自定义入口名。

// fullscreen triangle：3 个顶点 (-1,-1) (3,-1) (-1,3)，无顶点缓冲
const char* kFullscreenVert = R"GLSL(
#version 450
layout(location = 0) out vec2 vUV;
void main() {
    vec2 pos = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    vUV = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

// render pass：UV 渐变 * UBO 里的 exposure（验证 buffer 资源 host->shader 链路）
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

// postprocess pass：采样 sceneColor，Reinhard tonemap + 1/2.2 gamma
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

// CPU 侧复算 shader 数学（double 精度），回读后逐像素比对
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

        // sync validation：走 VK_LAYER_KHRONOS_validation，通过
        // VK_EXT_validation_features 的 pNext 开启（该扩展无需显式 enable）。
        // 没装 SDK validation layer 时静默降级为普通运行。
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

        // ---- 3. O5MDevice：设备层接管 queue / 复制命令池 / 分配账本 ----
        const uint32_t graphicsFamily = findGraphicsQueueFamily(*physicalDevice);
        float queuePriority = 1.0f;
        vk::DeviceQueueCreateInfo queueInfo({}, graphicsFamily, 1, &queuePriority);
        vk::DeviceCreateInfo deviceInfo;
        // feature 也要显式开（Vulkan 的 feature 都默认关）
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

        // ---- 5. 渲染链路 ----
        //   render(params UBO -> sceneColor) -> postprocess(sceneColor -> finalColor)
        //   -> readback(finalColor -> HOST_VISIBLE buffer)
        constexpr uint32_t kSize = 64;
        const vk::Extent2D extent{ kSize, kSize };
        const float kExposure = 2.0f;

        O5MRendergraph graph(o5mDevice);
        graph.addResource("sceneColor", vk::Format::eR16G16B16A16Sfloat, extent,
                          vk::ImageUsageFlagBits::eColorAttachment |
                              vk::ImageUsageFlagBits::eSampled,
                          vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eShaderReadOnlyOptimal);
        graph.addResource("finalColor", vk::Format::eR8G8B8A8Unorm, extent,
                          vk::ImageUsageFlagBits::eColorAttachment |
                              vk::ImageUsageFlagBits::eTransferSrc,
                          vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eTransferSrcOptimal);
        graph.addBufferResource("params", 16, vk::BufferUsageFlagBits::eUniformBuffer,
                                vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent);
        graph.addBufferResource("readback", kSize * kSize * 4,
                                vk::BufferUsageFlagBits::eTransferDst,
                                vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent);

        // 注意：compile() 之前必须 addPass 完毕——拓扑排序发生在当时的 pass 列表上
        // shader：直接构造资源（manager 的 create() 还有 bug，绕开），data 内嵌 GLSL
        O5MShaderResource vsRes("fullscreen.vert", vk::ShaderStageFlagBits::eVertex, o5mDevice);
        O5MShaderResource sceneFsRes("scene.frag", vk::ShaderStageFlagBits::eFragment, o5mDevice);
        O5MShaderResource postFsRes("post.frag", vk::ShaderStageFlagBits::eFragment, o5mDevice);
        vsRes.setData(const_cast<char*>(kFullscreenVert), std::strlen(kFullscreenVert) + 1);
        sceneFsRes.setData(const_cast<char*>(kSceneFrag), std::strlen(kSceneFrag) + 1);
        postFsRes.setData(const_cast<char*>(kPostFrag), std::strlen(kPostFrag) + 1);
        vsRes.load(); sceneFsRes.load(); postFsRes.load();
        std::cout << "[ok] shaders compiled (GLSL -> SPIR-V via shaderc)\n";

        // descriptor：b0 = UBO(params)，b1 = combined image sampler(sceneColor)
        // 两个 pipeline 共用同一 layout/set；render 不用 b1、post 不用 b0，合法
        const std::vector<vk::DescriptorSetLayoutBinding> bindings = {
            { 0, vk::DescriptorType::eUniformBuffer, 1,
              vk::ShaderStageFlagBits::eFragment, nullptr },
            { 1, vk::DescriptorType::eCombinedImageSampler, 1,
              vk::ShaderStageFlagBits::eFragment, nullptr },
        };
        vk::DescriptorSetLayoutCreateInfo layoutInfo({}, bindings);
        vk::raii::DescriptorSetLayout descLayout(o5mDevice.getDevice(), layoutInfo);

        const std::vector<vk::DescriptorPoolSize> poolSizes = {
            { vk::DescriptorType::eUniformBuffer, 1 },
            { vk::DescriptorType::eCombinedImageSampler, 1 },
        };

        // descSet 先声明后赋值：pass 回调按引用捕获，execute 时取值
        vk::DescriptorSet descSet;

        vk::SamplerCreateInfo samplerInfo;
        samplerInfo.setMagFilter(vk::Filter::eLinear)
            .setMinFilter(vk::Filter::eLinear)
            .setMipmapMode(vk::SamplerMipmapMode::eLinear)
            .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
            .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
            .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
            .setMinLod(0.f).setMaxLod(0.f);
        vk::raii::Sampler sampler(o5mDevice.getDevice(), samplerInfo);


        // 两个 pipeline：render -> RGBA16F，postprocess -> RGBA8
        O5MPipeline renderPipeline(o5mDevice);
        renderPipeline.createPipeline({ vk::Format::eR16G16B16A16Sfloat },
                                      vsRes.getShaderModule(), sceneFsRes.getShaderModule(),
                                      "main", "main", *descLayout);
        O5MPipeline postPipeline(o5mDevice);
        postPipeline.createPipeline({ vk::Format::eR8G8B8A8Unorm },
                                    vsRes.getShaderModule(), postFsRes.getShaderModule(),
                                    "main", "main", *descLayout);
        std::cout << "[ok] pipelines created\n";

        graph.addPass("render", { "params" }, { "sceneColor" },
            [&](vk::raii::CommandBuffer& cmd) {
                renderPipeline.begin(cmd, { *graph.getResource("sceneColor")->imageView }, extent);
                renderPipeline.bindDescriptorSet(cmd, descSet);
                cmd.draw(3, 1, 0, 0);
                renderPipeline.end(cmd);
            });

        graph.addPass("postprocess", { "sceneColor" }, { "finalColor" },
            [&](vk::raii::CommandBuffer& cmd) {
                postPipeline.begin(cmd, { *graph.getResource("finalColor")->imageView }, extent);
                postPipeline.bindDescriptorSet(cmd, descSet);
                cmd.draw(3, 1, 0, 0);
                postPipeline.end(cmd);
            });

        graph.addPass("readback", { "finalColor" }, { "readback" },
            [&](vk::raii::CommandBuffer& cmd) {
                vk::BufferImageCopy region(
                    0, 0, 0,
                    { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                    { 0, 0, 0 }, { kSize, kSize, 1 });
                cmd.copyImageToBuffer(*graph.getResource("finalColor")->image,
                                      vk::ImageLayout::eTransferSrcOptimal,
                                      *graph.getResource("readback")->buffer, region);
            });

        graph.compile();
        std::cout << "[ok] graph compiled\n";

        // UBO 内容：host 写入（map/memcpy/unmap）——graph 只管分配和同步，内容归使用方
        {
            float params[4] = { kExposure, 0.f, 0.f, 0.f };
            void* mapped = graph.getResource("params")->memory.mapMemory(0, vk::WholeSize);
            std::memcpy(mapped, params, sizeof(params));
            graph.getResource("params")->memory.unmapMemory();
        }

        // descriptor set 现在才能填：write 引用的 imageView/buffer 在 compile() 才分配
        vk::DescriptorPoolCreateInfo poolInfoFull(
            vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, poolSizes);
        vk::raii::DescriptorPool descPool(o5mDevice.getDevice(), poolInfoFull);
        vk::DescriptorSetAllocateInfo allocInfo(*descPool, { *descLayout });
        vk::raii::DescriptorSets descSets(o5mDevice.getDevice(), allocInfo);
        descSet = descSets.front();

        vk::DescriptorBufferInfo uboInfo(*graph.getResource("params")->buffer, 0, 16);
        vk::DescriptorImageInfo sceneInfo(
            *sampler, *graph.getResource("sceneColor")->imageView,
            vk::ImageLayout::eShaderReadOnlyOptimal);
        vk::WriteDescriptorSet writes[] = {
            { descSet, 0, 0, vk::DescriptorType::eUniformBuffer, {}, uboInfo, {} },
            { descSet, 1, 0, vk::DescriptorType::eCombinedImageSampler, sceneInfo, {}, {} },
        };
        o5mDevice.getDevice().updateDescriptorSets(writes, {});

        // ---- 6. 执行（fence 同步）并校验 ----
        vk::raii::Fence fence(o5mDevice.getDevice(), vk::FenceCreateInfo());
        graph.execute(commandBuffers[0], *o5mDevice.getQueue(), &fence);
        if (o5mDevice.getDevice().waitForFences(*fence, true, UINT64_MAX) !=
            vk::Result::eSuccess) {
            throw std::runtime_error("waitForFences failed");
        }
        std::cout << "[ok] submitted + fence waited\n";

        // CPU 逐像素校验：期望值 = 同一套数学（uv*exposure -> Reinhard -> gamma）
        // 注意不翻转 v：Vulkan NDC 是 Y 朝下的（(-1,-1)=视口左上，与 OpenGL 相反），
        // fullscreen triangle 的 vUV 插值结果就是 (x+0.5)/W, (y+0.5)/H
        const uint8_t* pixels = static_cast<const uint8_t*>(
            graph.getResource("readback")->memory.mapMemory(0, vk::WholeSize));
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
        graph.getResource("readback")->memory.unmapMemory();

        if (!ok) {
            return 1;
        }
        std::cout << "[ok] " << std::size(probes) << " pixels verified (UBO + tonemap math)\n";
        std::cout << "[PASS] rhi_verify: render -> postprocess -> readback 链路 OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << "\n";
        return 1;
    }
}
