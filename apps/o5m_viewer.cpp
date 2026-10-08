// o5m_viewer — Phase 2 主程序骨架（AI 脚手架，标注 TODO(user) 的部分是你的学习点）
//
// 已搭好的（脚手架区）：
//   GLFW 窗口 + surface + swapchain + resize 重建 + 事件队列
//   RenderGraph 接线：当帧 acquire 的 swapchain image 以 importTexture 进图
//   （初始布局 eUndefined），TexWrite::Present 出图（exit barrier 转 present）
//   帧循环 v1：frames-in-flight = 2（双 slot fence/semaphore/cmd/UBO + per-image
//   renderFinished），CPU 与 GPU 重叠一帧
//   --frames N 冒烟参数（无人值守跑 N 帧退出）
//
// 设计取舍（对应 phase2 文档 决策点 4 的延伸）：swapchain image 每帧一换，
// 不做成常驻图资源——否则屏障计划会转换"present 引擎还持有"的未 acquire 图像。
// 每帧 buildGraph(imageIndex) 只 import 当帧那张，compile 是纯 CPU 小开销。
//
// 学习方式（分工重定，见 docs/phase2-exercises.md）：
//   本文件的同步编排实现（v1 frames-in-flight）已完成 —— 学习点改为
//   「读代码 + 做推导练习」：swapchain 参数取舍、三信号物语义、双 slot
//   时序、三颗已踩的雷，全部整理在 docs/phase2-exercises.md 的清单里。
//   相机由你从零写：新建 core/Inc/Scene/O5MFpsCamera.h/.cpp，接口约定见
//   exercises 文档末尾「相机接入点」，写好后替换本文件的 O5MFpsCamera 占位。
//   shader（viewProj 应用）仍归你：kViewerVert 里的 TODO(user)。
//
// 退出码：0 = 正常退出（关窗或 --frames 到数）。

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "RHI/O5MDevice.h"
#include "RHI/O5MPipeline.h"
#include "RHI/O5MSwapchain.h"
#include "RenderGraph/O5MRendergraph.h"
#include "Platform/O5MWindow.h"
#include "Resources/O5MShaderResource.h"

namespace {

// P2 定稿的相机 UBO 布局（P4 前保持稳定，写 shader 时对齐）
struct CameraUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 camPos;
    glm::vec4 params;   // x: time(s), y: aspect, zw: reserved
};

// TODO(user) [3]：FPS 相机。骨架只提供身份矩阵占位，消费事件 + 更新数学是你的部分：
//   - O5MMouseEvent 的 x/y 是相对位移（cursor disabled 模式）→ yaw/pitch
//   - O5MKeyboardEvent Pressed（W/A/S/D/Q/E）→ 移动，速度乘 dt
//   - aspect 从 swapchain extent 来，resize 后必须重算 proj
struct O5MFpsCamera {
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 proj = glm::mat4(1.0f);
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 2.0f);

    void onEvent(const O5MEvent& event) {
        (void)event; // TODO(user): dispatch Keyboard/Mouse, integrate movement
    }
    void onUpdate(float dt, float aspect) {
        (void)dt; (void)aspect;
        // TODO(user): rebuild view (lookAt) & proj (perspective, aspect)
    }
};

// ---- GLSL ----
// TODO(user) [4]：vertex 目前是 NDC fullscreen triangle passthrough。
// 把它换成世界空间三角形并应用 proj * view，让相机运动在画面上可见。
const char* kViewerVert = R"GLSL(
#version 450
layout(location = 0) out vec2 vUV;
layout(set = 0, binding = 0) uniform Camera {
    mat4 view;
    mat4 proj;
    vec4 camPos;
    vec4 params;
} camera;
void main() {
    vec2 pos = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    vUV = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

const char* kViewerFrag = R"GLSL(
#version 450
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vec4(vUV, 0.25, 1.0);
}
)GLSL";

uint32_t findGraphicsQueueFamily(const vk::raii::PhysicalDevice& physicalDevice) {
    auto props = physicalDevice.getQueueFamilyProperties();
    for (uint32_t i = 0; i < props.size(); ++i) {
        if (props[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            return i;
        }
    }
    throw std::runtime_error("no graphics queue family found");
}

} // namespace

int main(int argc, char** argv) {
    try {
        // --frames N: unattended smoke test
        uint32_t smokeFrames = UINT32_MAX;
        for (int i = 1; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--frames" && i + 1 < argc)
                smokeFrames = static_cast<uint32_t>(std::atoi(argv[++i]));
        }

        // unbuffered stdout so piped smoke-test logs stay live
        std::cout << std::unitbuf;

        // ---- 1. window first: instance extensions come from GLFW ----
        O5MWindow window("o5m viewer (phase 2)", 1280, 720);

        // ---- 2. instance (+ sync validation when available) ----
        vk::raii::Context context;
        vk::ApplicationInfo appInfo("o5m-viewer", 1, "o5m", 1, VK_API_VERSION_1_2);

        std::vector<const char*> validationLayers;
        for (const auto& layer : context.enumerateInstanceLayerProperties()) {
            if (std::string_view(layer.layerName) == "VK_LAYER_KHRONOS_validation") {
                validationLayers.push_back("VK_LAYER_KHRONOS_validation");
                std::cout << "[ok] validation layer enabled\n";
                break;
            }
        }
        const bool useValidation = !validationLayers.empty();

        vk::ValidationFeatureEnableEXT syncFeature =
            vk::ValidationFeatureEnableEXT::eSynchronizationValidation;
        vk::ValidationFeaturesEXT validationFeatures(1, &syncFeature, 0, nullptr);
        vk::StructureChain<vk::InstanceCreateInfo, vk::ValidationFeaturesEXT> chain;

        auto glfwExtensions = O5MWindow::getRequiredInstanceExtensions();
        vk::InstanceCreateInfo instanceInfo;
#ifdef __APPLE__
        instanceInfo.setFlags(vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR);
        std::vector<const char*> exts = glfwExtensions;
        exts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        instanceInfo.setPEnabledExtensionNames(exts);
#else
        instanceInfo.setPEnabledExtensionNames(glfwExtensions);
#endif
        instanceInfo.setPApplicationInfo(&appInfo)
                    .setPEnabledLayerNames(validationLayers);

        if (useValidation) {
            chain.get<vk::InstanceCreateInfo>() = instanceInfo;
            chain.get<vk::ValidationFeaturesEXT>() = validationFeatures;
        }
        vk::raii::Instance instance(
            context,
            useValidation ? chain.get<vk::InstanceCreateInfo>() : instanceInfo);

        // ---- 3. device (+ swapchain extension) ----
        vk::raii::PhysicalDevices physicalDevices(instance);
        if (physicalDevices.empty())
            throw std::runtime_error("no physical device");
        const vk::raii::PhysicalDevice& physicalDevice = physicalDevices.front();

        const uint32_t graphicsFamily = findGraphicsQueueFamily(physicalDevice);
        float queuePriority = 1.0f;
        vk::DeviceQueueCreateInfo queueInfo({}, graphicsFamily, 1, &queuePriority);
        vk::DeviceCreateInfo deviceInfo;
        vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures(true);

        std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };
#ifdef __APPLE__
        deviceExtensions.push_back("VK_KHR_portability_subset");
        deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
#else
        deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
#endif
        deviceInfo.setQueueCreateInfos(queueInfo)
                  .setPEnabledExtensionNames(deviceExtensions)
                  .setPNext(&dynamicRenderingFeatures);

        O5MDevice device(physicalDevice, vk::raii::Device(physicalDevice, deviceInfo),
                         graphicsFamily);
        std::cout << "[ok] device: " << physicalDevice.getProperties().deviceName << "\n";

        // ---- 4. surface + swapchain ----
        auto [fbw, fbh] = window.getFramebufferSize();
        vk::raii::SurfaceKHR surface = window.createSurface(instance);
        O5MSwapchain swapchain(device, surface, vk::Extent2D{ fbw, fbh });
        std::cout << "[ok] swapchain " << swapchain.getExtent().width << "x"
                  << swapchain.getExtent().height << " "
                  << vk::to_string(swapchain.getFormat()) << "\n";

        // ---- 5. per-frame sync objects: frames-in-flight = 2 ----
        // 资源分两个池，别混：
        //   per FRAME SLOT（frameIndex % 2）：cmd / fence / imageAvailable /
        //     camera UBO —— CPU 复用它们前必须等 fence（slot 空闲）
        //   per SWAPCHAIN IMAGE（acquire 下标）：renderFinished —— present
        //     引擎在 image 重新 acquire 前仍持有它，按帧分会在轮换中冲突
        constexpr uint32_t kInFlight = 2;

        vk::raii::CommandPool commandPool(
            device.getDevice(),
            { vk::CommandPoolCreateFlagBits::eResetCommandBuffer, graphicsFamily });
        vk::raii::CommandBuffers commandBuffers(
            device.getDevice(),
            { *commandPool, vk::CommandBufferLevel::ePrimary, kInFlight });
        std::array<vk::raii::Fence, kInFlight> frameFences = {
            vk::raii::Fence(device.getDevice(),
                vk::FenceCreateInfo{ vk::FenceCreateFlagBits::eSignaled }),
            vk::raii::Fence(device.getDevice(),
                vk::FenceCreateInfo{ vk::FenceCreateFlagBits::eSignaled }),
        };
        // eSignaled：首圈每个 slot 的第一次 waitForFences 必须立刻通过
        // （fence 语义是"本 slot 上一次提交是否完成"，没提交过 = 完成）
        vk::SemaphoreCreateInfo semInfo;
        std::array<vk::raii::Semaphore, kInFlight> imageAvailable = {
            vk::raii::Semaphore(device.getDevice(), semInfo),
            vk::raii::Semaphore(device.getDevice(), semInfo),
        };
        auto makeRenderFinished = [&]() {
            std::vector<vk::raii::Semaphore> pool;
            pool.reserve(swapchain.getImageCount());
            for (uint32_t i = 0; i < swapchain.getImageCount(); ++i)
                pool.emplace_back(device.getDevice(), semInfo);
            return pool;
        };
        auto renderFinishedPool = makeRenderFinished();

        // camera UBO 每 slot 一份：CPU 写 frame N 的 UBO 时，GPU 可能还在读
        // frame N-1 的 —— 同一份内存就成数据竞争，这正是 in-flight 的意义
        std::array<vk::raii::Buffer, kInFlight> cameraBuffers = {
            vk::raii::Buffer(nullptr), vk::raii::Buffer(nullptr) };
        std::array<vk::raii::DeviceMemory, kInFlight> cameraMemories = {
            vk::raii::DeviceMemory(nullptr), vk::raii::DeviceMemory(nullptr) };
        for (uint32_t i = 0; i < kInFlight; ++i) {
            auto [buf, mem] = device.createBuffer(
                sizeof(CameraUBO), vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible |
                    vk::MemoryPropertyFlagBits::eHostCoherent);
            cameraBuffers[i] = std::move(buf);
            cameraMemories[i] = std::move(mem);
        }

        // ---- 6. 与 image 无关的长寿对象：shader / pipeline / descriptor ----
        O5MShaderResource vsRes("viewer.vert", vk::ShaderStageFlagBits::eVertex, device);
        O5MShaderResource fsRes("viewer.frag", vk::ShaderStageFlagBits::eFragment, device);
        vsRes.setData(const_cast<char*>(kViewerVert), std::strlen(kViewerVert) + 1);
        fsRes.setData(const_cast<char*>(kViewerFrag), std::strlen(kViewerFrag) + 1);
        vsRes.load(); fsRes.load();

        const std::vector<vk::DescriptorSetLayoutBinding> bindings = {
            { 0, vk::DescriptorType::eUniformBuffer, 1,
              vk::ShaderStageFlagBits::eVertex, nullptr },
        };
        vk::DescriptorSetLayoutCreateInfo layoutInfo({}, bindings);
        vk::raii::DescriptorSetLayout descLayout(device.getDevice(), layoutInfo);

        O5MPipeline pipeline(device);
        pipeline.createPipeline({ swapchain.getFormat() },
                                vsRes.getShaderModule(), fsRes.getShaderModule(),
                                "main", "main", *descLayout);

        const std::vector<vk::DescriptorPoolSize> poolSizes = {
            { vk::DescriptorType::eUniformBuffer, kInFlight },
        };
        vk::raii::DescriptorPool descPool(
            device.getDevice(),
            vk::DescriptorPoolCreateInfo(
                vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                kInFlight, poolSizes));
        // 每 slot 一个 set：分别绑到各自的 camera UBO 上
        std::vector<vk::DescriptorSetLayout> layouts;
        layouts.reserve(kInFlight);
        for (uint32_t i = 0; i < kInFlight; ++i)
            layouts.push_back(*descLayout);
        vk::DescriptorSetAllocateInfo allocInfo(*descPool, layouts);
        vk::raii::DescriptorSets descSets(device.getDevice(), allocInfo);
        std::array<vk::DescriptorSet, kInFlight> camDescSets = {
            descSets[0], descSets[1]
        };
        for (uint32_t i = 0; i < kInFlight; ++i) {
            vk::DescriptorBufferInfo uboInfo(*cameraBuffers[i], 0, sizeof(CameraUBO));
            vk::WriteDescriptorSet write {
                camDescSets[i], 0, 0, vk::DescriptorType::eUniformBuffer, {}, uboInfo, {}
            };
            device.getDevice().updateDescriptorSets(write, {});
        }
        std::cout << "[ok] pipeline + descriptors ready (2 slots)\n";

        // ---- 每帧一图：import 当帧 acquire 的 image + 当帧 slot 的相机 UBO ----
        auto buildGraph = [&](uint32_t imageIndex, uint32_t frame) -> std::unique_ptr<O5MRendergraph> {
            auto graph = std::make_unique<O5MRendergraph>(device);
            const vk::Extent2D extent = swapchain.getExtent();

            std::string bbName = "backbuffer";
            ResourceInfo bbInfo(bbName, extent, swapchain.getFormat(),
                                ResourceSource::Imported);
            // eUndefined 而非 ePresentSrcKHR：swapchain 里尚未被 present 过的
            // image 实际处于 UNDEFINED。每帧全屏重画，丢弃旧内容无害；用
            // UNDEFINED 作 oldLayout 永远与实际状态匹配（presentSrc 只在
            // "已知被 present 过"时才安全，per-image 状态追踪是后续优化）。
            graph->importTexture(bbName, swapchain.getImages()[imageIndex],
                                 *swapchain.getViews()[imageIndex],
                                 swapchain.getFormat(), extent,
                                 vk::ImageUsageFlagBits::eColorAttachment,
                                 vk::ImageLayout::eUndefined);
            graph->markOutput(bbInfo.handle);

            std::string camName = "camera";
            ResourceInfo camInfo(camName, static_cast<vk::DeviceSize>(sizeof(CameraUBO)),
                                 ResourceSource::Imported);
            graph->importBuffer(camName, *cameraBuffers[frame], sizeof(CameraUBO),
                                vk::BufferUsageFlagBits::eUniformBuffer);

            // ResourceInfo 按值捕获进 lambda（图的生命周期长于本函数栈帧）
            graph->addGraphicPass("presentPass",
                [bbInfo, camInfo](O5MPassBuilder& b) mutable {
                    b.write(bbInfo, TexWrite::Present);
                    b.read(camInfo, BufRead::Uniform);
                },
                [bbInfo, extent, &pipeline, camDescSets, frame]
                (O5MRenderContext& ctx, vk::raii::CommandBuffer& cmd) {
                    pipeline.begin(cmd, { ctx.getImageView(bbInfo.handle) }, extent);
                    pipeline.bindDescriptorSet(cmd, camDescSets[frame]);
                    cmd.draw(3, 1, 0, 0);
                    pipeline.end(cmd);
                });

            graph->compile();
            return graph;
        };

        // ---- 7. frame loop (v0 sync: wait-each-frame, NO pipelining) ----
        O5MFpsCamera camera;
        uint32_t frameIndex = 0;
        auto lastTime = std::chrono::high_resolution_clock::now();
        auto fpsWindowStart = lastTime;
        uint32_t fpsCounter = 0;

        std::cout << "[ok] viewer running (v0 single-slot sync) -- ESC to quit\n";

        while (!window.shouldClose() && frameIndex < smokeFrames) {
            window.pollEvents();

            auto now = std::chrono::high_resolution_clock::now();
            const float dt = std::chrono::duration<float>(now - lastTime).count();
            lastTime = now;

            // 事件 → 相机（TODO(user) [3] 消费）
            for (auto& event : window.drainEvents())
                camera.onEvent(*event);
            const float aspect =
                static_cast<float>(swapchain.getExtent().width) /
                static_cast<float>(swapchain.getExtent().height);
            camera.onUpdate(dt, aspect);

            // ---- slot 同步：等本 slot 上一次提交完成（而非"上一帧"！）----
            // kInFlight=2 时 CPU 最多超前 GPU 一帧；slot N 的 fence 只在
            // frameIndex 与 frameIndex-2 之间复用时被等。必须在 acquire 前：
            // 复用 imageAvailable[slot] 前要保证它的信号/等待都已结算。
            const uint32_t frame = frameIndex % kInFlight;
            device.getDevice().waitForFences(*frameFences[frame], true, UINT64_MAX);
            device.getDevice().resetFences(*frameFences[frame]);

            // ---- acquire（可能带回重建请求） ----
            const bool resizeFlag = window.takeResizeFlag();
            auto [acqIndex, needsRebuild] = swapchain.acquire(imageAvailable[frame]);
            if (resizeFlag || needsRebuild) {
                auto [w, h] = window.getFramebufferSize();
                swapchain.rebuild({ w, h });   // views/images 全部换新
                renderFinishedPool = makeRenderFinished();
                std::cout << "[ok] swapchain rebuilt " << w << "x" << h << "\n";
                continue;
            }

            // camera UBO host write（只写本 slot 的那份，另一份 GPU 可能还在读）
            {
                CameraUBO ubo {
                    .view = camera.view,
                    .proj = camera.proj,
                    .camPos = glm::vec4(camera.position, 1.0f),
                    .params = { 0.f, aspect, 0.f, 0.f }
                };
                void* mapped = cameraMemories[frame].mapMemory(0, vk::WholeSize);
                std::memcpy(mapped, &ubo, sizeof(ubo));
                cameraMemories[frame].unmapMemory();
            }

            auto graph = buildGraph(acqIndex, frame);
            if (frameIndex == 0)
                graph->dump();

            // 单发图永远走冷启动计划（UNDEFINED -> colorAttachment ->
            // present）；稳态计划的前提是"同一张 image 连续复用"，不成立。
            graph->execute(commandBuffers[frame], *device.getQueue(),
                           &frameFences[frame],
                           /*frameIndex=*/0, &imageAvailable[frame],
                           &renderFinishedPool[acqIndex]);

            if (!swapchain.present(*device.getQueue(), acqIndex,
                                    renderFinishedPool[acqIndex])) {
                auto [w, h] = window.getFramebufferSize();
                swapchain.rebuild({ w, h });
                renderFinishedPool = makeRenderFinished();
                std::cout << "[ok] swapchain rebuilt (present) " << w << "x" << h << "\n";
            }

            ++frameIndex;
            ++fpsCounter;
            if (std::chrono::duration<float>(now - fpsWindowStart).count() >= 1.0f) {
                std::cout << "[fps] " << fpsCounter
                          << " (" << (1000.0f / std::max(1u, fpsCounter)) << " ms/frame)\n";
                fpsCounter = 0;
                fpsWindowStart = now;
            }
        }

        // ---- 8. clean shutdown: all in-flight work must retire ----
        // waitIdle 已保证两份 fence 全部 signaled；显式再等一遍是防御式写法
        for (uint32_t i = 0; i < kInFlight; ++i)
            device.getDevice().waitForFences(*frameFences[i], true, UINT64_MAX);
        device.getQueue().waitIdle();
        std::cout << "[ok] " << frameIndex << " frames rendered, clean exit\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << "\n";
        return 1;
    }
}
