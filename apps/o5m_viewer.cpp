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
#include "Events/O5MKeyboardEvent.h"
#include "Events/O5MMouseEvent.h"
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
//   - onMouseMoved 的 x/y 是相对位移（cursor disabled 模式）→ yaw/pitch
//   - onKey 的 Pressed/Released 是**边沿**：自己在相机里记键位状态，
//     在 onUpdate 里按状态算移动（不要用 Pressed 直接算位移——那是帧率/重复率相关）
//   - aspect 从 swapchain extent 来，resize 后必须重算 proj
struct O5MFpsCamera {
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 proj = glm::mat4(1.0f);
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 2.0f);

    void onKey(const O5MKeyboardEvent& event) {
        (void)event; // TODO(user): 边沿 → 自己的键位状态
    }
    void onMouseMoved(const O5MMouseMovedEvent& event) {
        (void)event; // TODO(user): 相对位移 → 累积 yaw/pitch
    }
    void onMouseScrolled(const O5MMouseScrolledEvent& event) {
        (void)event; // TODO(user): 滚轮调 fov（可选）
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

        // ---- 5. TODO(user): 帧同步对象编排（练习 3 的"物"半边） ----
        // 清单（两池划分的理由 = 练习 3.2）：
        //   per FRAME SLOT（kInFlight=2，frameIndex % 2 取用）：
        //     - command buffers x2（pool 带 eResetCommandBuffer，为什么？）
        //     - fence x2（创建 flag 的推导：练习 3.5）
        //     - imageAvailable semaphore x2
        //   per SWAPCHAIN IMAGE（acquire 下标索引）：
        //     - renderFinished semaphore x getImageCount()
        // 脚手架只保留了 commandPool；其余对象你来声明。
        vk::raii::CommandPool commandPool(
            device.getDevice(),
            { vk::CommandPoolCreateFlagBits::eResetCommandBuffer, graphicsFamily });
        constexpr uint32_t kInFlight = 2;
        // TODO(user): 在此声明两池对象

        // camera UBO 基线：单份（升级 per-slot 是练习 3.3，流水跑通后再做）
        auto [cameraBuffer, cameraMemory] = device.createBuffer(
            sizeof(CameraUBO), vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible |
                vk::MemoryPropertyFlagBits::eHostCoherent);
        (void)kInFlight;
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

        // 基线：单 set 绑单 UBO（做练习 3.3 时随 UBO 一起翻倍）
        const std::vector<vk::DescriptorPoolSize> poolSizes = {
            { vk::DescriptorType::eUniformBuffer, 1 },
        };
        vk::raii::DescriptorPool descPool(
            device.getDevice(),
            vk::DescriptorPoolCreateInfo(
                vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, poolSizes));
        vk::DescriptorSetAllocateInfo allocInfo(*descPool, { *descLayout });
        vk::raii::DescriptorSets descSets(device.getDevice(), allocInfo);
        vk::DescriptorSet descSet = descSets.front();
        vk::DescriptorBufferInfo uboInfo(*cameraBuffer, 0, sizeof(CameraUBO));
        vk::WriteDescriptorSet write {
            descSet, 0, 0, vk::DescriptorType::eUniformBuffer, {}, uboInfo, {}
        };
        device.getDevice().updateDescriptorSets(write, {});
        std::cout << "[ok] pipeline + descriptor ready\n";

        // ---- 每帧一图（脚手架）：只 import 当帧 acquire 的那张 image ----
        auto buildGraph = [&](uint32_t imageIndex) -> std::unique_ptr<O5MRendergraph> {
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
            graph->importBuffer(camName, *cameraBuffer, sizeof(CameraUBO),
                                vk::BufferUsageFlagBits::eUniformBuffer);

            // ResourceInfo 按值捕获进 lambda（图的生命周期长于本函数栈帧）
            graph->addGraphicPass("presentPass",
                [bbInfo, camInfo](O5MPassBuilder& b) mutable {
                    b.write(bbInfo, TexWrite::Present);
                    b.read(camInfo, BufRead::Uniform);
                },
                [bbInfo, extent, &pipeline, descSet]
                (O5MRenderContext& ctx, vk::raii::CommandBuffer& cmd) {
                    pipeline.begin(cmd, { ctx.getImageView(bbInfo.handle) }, extent);
                    pipeline.bindDescriptorSet(cmd, descSet);
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

        // 订阅在进入循环前一次性注册；事件在 pollEvents() 内同步派发。
        // 退出决策留在应用层：ESC 不再是平台层的特权。
        O5MEventBus& eventBus = window.getEventBus();
        eventBus.addEvent<O5MKeyboardEvent>([&window](const O5MEvent& event) {
            const auto& key = static_cast<const O5MKeyboardEvent&>(event);
            if (key.getKey() == O5MKeyboardEvent::Key_Escape &&
                key.getEventType() == O5MKeyboardEvent::Pressed)
                window.requestClose();
        });

        // 相机订阅：总线按类型分桶，所以「类型 → 具体回调」的转换只需在订阅处写一次，
        // 相机内部因此不需要任何类型判断。TODO(user) [3] 消费即这四行。
        eventBus.addEvent<O5MKeyboardEvent>([&camera](const O5MEvent& event) {
            camera.onKey(static_cast<const O5MKeyboardEvent&>(event));
        });
        eventBus.addEvent<O5MMouseMovedEvent>([&camera](const O5MEvent& event) {
            camera.onMouseMoved(static_cast<const O5MMouseMovedEvent&>(event));
        });
        eventBus.addEvent<O5MMouseScrolledEvent>([&camera](const O5MEvent& event) {
            camera.onMouseScrolled(static_cast<const O5MMouseScrolledEvent&>(event));
        });

        std::cout << "[ok] viewer running (v0 single-slot sync) -- ESC to quit\n";

        while (!window.shouldClose() && frameIndex < smokeFrames) {
            window.pollEvents();

            auto now = std::chrono::high_resolution_clock::now();
            const float dt = std::chrono::duration<float>(now - lastTime).count();
            lastTime = now;

            const float aspect =
                static_cast<float>(swapchain.getExtent().width) /
                static_cast<float>(swapchain.getExtent().height);
            camera.onUpdate(dt, aspect);

            // ----------------------------------------------------------
            // TODO(user): 帧循环同步编排（练习 3 的"序"半边，本阶段核心）
            // 按这个顺序实现，每一步的"为什么"对应一道小题：
            //   a) frame = frameIndex % kInFlight；等并 reset 这个 slot 的 fence
            //      ——为什么等的是"两帧前"而不是上一帧？（3.1）
            //      ——为什么必须在 acquire 之前？（3.4）
            //   b) swapchain.acquire(imageAvailable[frame])；resize flag 或
            //      needsRebuild 时 rebuild + continue（重建后 renderFinished
            //      池要跟着 imageCount 重建）
            //   c) 写 camera UBO（基线版只写单份 cameraMemory）
            //   d) buildGraph(acqIndex) + execute：
            //      cmd 用 frame slot 的、fence 用 slot 的、wait 信号量用
            //      imageAvailable[frame]、signal 用 renderFinishedPool[acqIndex]
            //      ——单发图 execute 的 frameIndex 实参为什么恒为 0？（4.3）
            //   e) present：等 renderFinishedPool[acqIndex]；返回 false 同 b) 重建
            // 提示：先跑通再优化；validation layer 是你的老师，三颗雷
            // （见练习 4）一颗都别跳——撞上了自己推根因，再对照参考实现。
            // ----------------------------------------------------------
            throw std::runtime_error(
                "TODO(user): frame loop sync -- docs/phase2-exercises.md 练习 3");

            ++frameIndex;
            ++fpsCounter;
            if (std::chrono::duration<float>(now - fpsWindowStart).count() >= 1.0f) {
                std::cout << "[fps] " << fpsCounter
                          << " (" << (1000.0f / std::max(1u, fpsCounter)) << " ms/frame)\n";
                fpsCounter = 0;
                fpsWindowStart = now;
            }
        }

        // ---- 8. clean shutdown ----
        // TODO(user): waitIdle 之外，想想还应该显式等谁？（防御式收尾）
        device.getQueue().waitIdle();
        std::cout << "[ok] " << frameIndex << " frames rendered, clean exit\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << "\n";
        return 1;
    }
}
