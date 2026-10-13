// o5m_viewer — Phase 2 主程序（app/胶水层，AI 工作范围）
//
// 分工：apps/ 全部归 AI；core/（引擎）归用户。本文件的同步编排是
// **AI 实现参考版**，学习方式是「读代码 + 做推导练习」——swapchain 参数取舍、
// 三信号物语义、双 slot 时序、三颗已踩的雷，全部整理在
// docs/phase2-exercises.md，读完后在那份文档里作答，AI 做 file:line 批改。
//
// 已实现：
//   GLFW 窗口 + surface + swapchain + resize 重建 + 事件队列
//   RenderGraph 接线：当帧 acquire 的 swapchain image 以 importTexture 进图
//   （初始布局 eUndefined），TexWrite::Present 出图（exit barrier 转 present）
//   帧循环 v1：frames-in-flight = 2，CPU 与 GPU 重叠一帧。资源分两池：
//     per FRAME SLOT（frameIndex % 2）：cmd / fence / imageAvailable / camera UBO
//       —— CPU 复用前必须等 fence（slot 空闲）
//     per SWAPCHAIN IMAGE（acquire 下标）：renderFinished
//       —— present 引擎在 image 被重新 acquire 前仍持有它，按 slot 分会在轮换中
//          冲突（VUID-vkQueueSubmit-pSignalSemaphores-00067）
//   --frames N 冒烟参数（无人值守跑 N 帧退出）
//
// 设计取舍（对应 phase2 文档 决策点 4 的延伸）：swapchain image 每帧一换，
// 不做成常驻图资源——否则屏障计划会转换"present 引擎还持有"的未 acquire 图像。
// 每帧 buildGraph(imageIndex, frame) 只 import 当帧那张，compile 是纯 CPU 小开销。
//
// resize 路径的两处修正（参考实现 eecfedb 有 bug，此处已修，推导见 exercises 3.4）：
//   1) resetFences 不能紧跟 waitForFences——若随后因 resize 而 continue，
//      fence 会停在 unsignaled 且无 pending submit，下一轮同 slot 的
//      waitForFences 永久阻塞。改为"确定要提交时才 reset"。
//   2) acquire 返回 eSuboptimalKHR 时**图已 acquire、imageAvailable 已 signal**，
//      但没人 wait 它；直接复用会违反 VUID-vkAcquireNextImageKHR-semaphore-01779
//      （该 semaphore 必须 unsignaled）。rebuild 后整批重建 imageAvailable。
//
// 仍未实现（用户学习点）：shader 里的 viewProj 应用 + Vulkan 投影适配
//   （Y 翻转、深度 0..1）——见 shaders/fullscreen/viewer.vert 的 TODO(user) [4]。
//
// 退出码：0 = 正常退出（关窗或 --frames 到数）。

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
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
#include "Scene/O5MCameraComponent.h"
#include "Scene/O5MWorld.h"

namespace {

// P2 定稿的相机 UBO 布局（P4 前保持稳定，写 shader 时对齐）
struct CameraUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 camPos;
    glm::vec4 params;   // x: time(s), y: aspect, zw: reserved
};

// 相机：[3] 已完成 —— 不新建类，直接用 core/Inc/Scene/O5MCameraComponent.h
// （完整 FPS 相机），接线在下面的订阅区 + world.update(dt) 驱动其 update()。

// ---- GLSL：已经落盘到 shaders/，运行时读入 + shaderc 编译 ----
// TODO(user) [4] 现在在 shaders/fullscreen/viewer.vert 里，不在本文件。
#ifndef O5M_SHADER_DIR
#define O5M_SHADER_DIR "shaders"   // 兜底：CMake 未注入时按相对路径找
#endif

// Shader 源文件读成 NUL 结尾的字符串。O5MResource::setData 是非持有的
// （只存裸指针），而 O5MShaderResource::doLoad 用 std::string(casted) 当
// C 串读——所以返回的 string 必须活过 load()，调用点要留意作用域。
bool tryReadFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::ostringstream buf;
    buf << in.rdbuf();
    out = buf.str();
    return true;
}

bool fileExists(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return static_cast<bool>(in);
}

std::string readTextFile(const std::string& path) {
    std::string out;
    if (!tryReadFile(path, out))
        throw std::runtime_error("cannot open shader file: " + path);
    return out;
}

std::string dirName(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

// ---------------------------------------------------------------------------
// 极简 #include 展开器 —— **临时方案，P4 换成 shaderc SetIncluder**
// ---------------------------------------------------------------------------
// 已实测的两条局限（见 docs/phase2-exercises.md）：
//   ① 不参与条件编译：被 #if / #ifdef 排除掉的分支里的 #include 照样会被展开。
//      glslang 是"先算条件、再解析 include"（实测：包在 #ifdef 未定义宏里的
//      #include 不会报错，宏一旦定义就在对应行报错），文本拼接做不到这点。
//   ② 报错的文件名恒为主 shader：整份拼接结果只有一个 shaderc 源串，而 GLSL 的
//      #line 只能改行号或源串序号，无法指定文件名。
// 缓解②：记账时用**单参数** `#line N`（实测报错形如 "<主shader路径>:100"，
// 文件名与行号都在）。于是主 shader 自身的报错行号完全准确，只有被 include
// 进来的片段会把行号显示成主 shader 的名字。
//
// 换成 includer 时只需替换本函数，调用点不动。
std::string expandIncludes(const std::string& source,
                           const std::string& sourceDir,
                           const std::vector<std::string>& includeDirs,
                           std::vector<std::string>& includeStack,
                           uint32_t depth) {
    constexpr uint32_t kMaxIncludeDepth = 16;
    if (depth > kMaxIncludeDepth)
        throw std::runtime_error("shader #include nested too deeply");

    std::string out;
    size_t pos = 0;
    uint32_t lineNo = 0;

    while (pos < source.size()) {
        size_t eol = source.find('\n', pos);
        if (eol == std::string::npos)
            eol = source.size();
        const std::string line = source.substr(pos, eol - pos);
        pos = eol + 1;
        ++lineNo;

        // 形如: [空白] # [空白] include [空白] "name" | <name>
        bool isInclude = false;
        bool angled = false;
        std::string name;
        size_t p = line.find_first_not_of(" \t");
        if (p != std::string::npos && line[p] == '#') {
            p = line.find_first_not_of(" \t", p + 1);
            if (p != std::string::npos && line.compare(p, 7, "include") == 0) {
                p = line.find_first_not_of(" \t", p + 7);
                if (p != std::string::npos && (line[p] == '"' || line[p] == '<')) {
                    angled = (line[p] == '<');
                    const size_t close = line.find(angled ? '>' : '"', p + 1);
                    if (close == std::string::npos)
                        throw std::runtime_error("malformed #include: " + line);
                    name = line.substr(p + 1, close - p - 1);
                    isInclude = true;
                }
            }
        }

        if (!isInclude) {
            out += line;
            out += '\n';
            continue;
        }

        // 解析目标：引号形式先找 including 文件所在目录，再回退 includeDirs；
        // 尖括号形式只找 includeDirs。
        std::string resolved;
        bool found = false;
        if (!angled) {
            std::string candidate = sourceDir + "/" + name;
            if (fileExists(candidate)) {
                resolved = candidate;
                found = true;
            }
        }
        if (!found) {
            for (const std::string& dir : includeDirs) {
                std::string candidate = dir + "/" + name;
                if (fileExists(candidate)) {
                    resolved = candidate;
                    found = true;
                    break;
                }
            }
        }
        if (!found)
            throw std::runtime_error("shader #include not found: \"" + name +
                                     "\" (from " + sourceDir + ", line " +
                                     std::to_string(lineNo) + ")");

        if (std::find(includeStack.begin(), includeStack.end(), resolved) !=
            includeStack.end())
            throw std::runtime_error("circular shader #include: " + resolved);

        includeStack.push_back(resolved);
        // 单参数 #line：让被包含文件自己的行号从 1 开始
        out += "#line 1\n";
        out += expandIncludes(readTextFile(resolved), dirName(resolved),
                              includeDirs, includeStack, depth + 1);
        includeStack.pop_back();
        // 恢复主文件的记账：下一行就是原文件的 lineNo + 1
        out += "#line " + std::to_string(lineNo + 1) + "\n";
    }

    return out;
}

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
        // --rebuild-every N: 每 N 帧强制走一次 swapchain 重建路径。真实拖拽
        //   resize 无法无人值守触发（要 GLFW handle，属 core/），而这条路径
        //   在修掉 views 崩溃前从未被执行过——当年两个 bug 就是这么漏的。
        uint32_t smokeFrames = UINT32_MAX;
        uint32_t rebuildEvery = 0;
        for (int i = 1; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--frames" && i + 1 < argc)
                smokeFrames = static_cast<uint32_t>(std::atoi(argv[++i]));
            else if (std::string_view(argv[i]) == "--rebuild-every" && i + 1 < argc)
                rebuildEvery = static_cast<uint32_t>(std::atoi(argv[++i]));
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
        // 两个池别混（划分理由 = 练习 3.2）：
        //   per FRAME SLOT（frameIndex % kInFlight）：cmd / fence /
        //     imageAvailable / camera UBO —— CPU 复用它们前必须等 fence
        //   per SWAPCHAIN IMAGE（acquire 下标）：renderFinished
        //     —— present 引擎在 image 被重新 acquire 前仍持有它
        vk::raii::CommandPool commandPool(
            device.getDevice(),
            { vk::CommandPoolCreateFlagBits::eResetCommandBuffer, graphicsFamily });
        constexpr uint32_t kInFlight = 2;

        vk::raii::CommandBuffers commandBuffers(
            device.getDevice(),
            { *commandPool, vk::CommandBufferLevel::ePrimary, kInFlight });

        // eSignaled：首圈每个 slot 的第一次 waitForFences 必须立刻通过。
        // fence 语义是"本 slot 上一次提交是否完成"，从没提交过 = 已完成；
        // 不加这个 flag，第一帧就永久阻塞（练习 4.1 / 3.5）。
        std::array<vk::raii::Fence, kInFlight> frameFences = {
            vk::raii::Fence(device.getDevice(),
                vk::FenceCreateInfo{ vk::FenceCreateFlagBits::eSignaled }),
            vk::raii::Fence(device.getDevice(),
                vk::FenceCreateInfo{ vk::FenceCreateFlagBits::eSignaled }),
        };

        const vk::SemaphoreCreateInfo semInfo;
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

        // camera UBO 每 slot 一份（练习 3.3）：CPU 写 frame N 的 UBO 时，
        // GPU 可能还在读 frame N-1 的——同一份内存就是数据竞争。
        // 用 vector 而非 array：vk::raii 句柄没有默认构造函数，array 起不来。
        std::vector<std::pair<vk::raii::Buffer, vk::raii::DeviceMemory>> cameraUBO;
        cameraUBO.reserve(kInFlight);
        for (uint32_t i = 0; i < kInFlight; ++i) {
            cameraUBO.push_back(device.createBuffer(
                sizeof(CameraUBO), vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible |
                    vk::MemoryPropertyFlagBits::eHostCoherent));
        }
        // ---- 6. 与 image 无关的长寿对象：shader / pipeline / descriptor ----
        // Shader 从磁盘读。资源名用**绝对路径**：它经 doLoad →
        // compileGLSL(debugName) 变成 shaderc 的 input_file_name。用上
        // includer 之后它会作为 requesting_source 传给 include 回调，所以
        // 引擎侧不需要任何"shader 根目录"新 API。
        const std::string shaderDir = O5M_SHADER_DIR;
        // 尖括号 / 回退查找的根目录：让 #include "common/camera.glsl" 这类
        // 从 shaders/ 起算的写法在任何子目录里都成立
        const std::vector<std::string> includeDirs = { shaderDir };
        const std::string vertPath = shaderDir + "/fullscreen/viewer.vert";
        const std::string fragPath = shaderDir + "/fullscreen/viewer.frag";

        // 下面两个 string 必须活到 load() 之后（setData 非持有），故放在此处。
        // expandIncludes 是临时方案，换成 shaderc includer 时只替换它。
        std::vector<std::string> includeStack;
        std::string vertSrc = expandIncludes(readTextFile(vertPath),
                                             dirName(vertPath), includeDirs,
                                             includeStack, 0);
        includeStack.clear();
        std::string fragSrc = expandIncludes(readTextFile(fragPath),
                                             dirName(fragPath), includeDirs,
                                             includeStack, 0);

        O5MShaderResource vsRes(vertPath, vk::ShaderStageFlagBits::eVertex, device);
        O5MShaderResource fsRes(fragPath, vk::ShaderStageFlagBits::eFragment, device);
        vsRes.setData(vertSrc.data(), vertSrc.size() + 1);
        fsRes.setData(fragSrc.data(), fragSrc.size() + 1);
        vsRes.load(); fsRes.load();
        std::cout << "[ok] shaders compiled (" << shaderDir << ")\n";

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

        // 每 slot 一个 set，分别绑到各自的 camera UBO（跟 UBO 一起翻倍）
        const std::vector<vk::DescriptorPoolSize> poolSizes = {
            { vk::DescriptorType::eUniformBuffer, kInFlight },
        };
        vk::raii::DescriptorPool descPool(
            device.getDevice(),
            vk::DescriptorPoolCreateInfo(
                vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                kInFlight, poolSizes));
        std::vector<vk::DescriptorSetLayout> layouts(kInFlight, *descLayout);
        vk::DescriptorSetAllocateInfo allocInfo(*descPool, layouts);
        vk::raii::DescriptorSets descSets(device.getDevice(), allocInfo);
        std::array<vk::DescriptorSet, kInFlight> camDescSets = {
            descSets[0], descSets[1]
        };
        for (uint32_t i = 0; i < kInFlight; ++i) {
            vk::DescriptorBufferInfo uboInfo(*cameraUBO[i].first, 0, sizeof(CameraUBO));
            vk::WriteDescriptorSet write {
                camDescSets[i], 0, 0, vk::DescriptorType::eUniformBuffer, {}, uboInfo, {}
            };
            device.getDevice().updateDescriptorSets(write, {});
        }
        std::cout << "[ok] pipeline + descriptors ready (" << kInFlight << " slots)\n";

        // ---- 每帧一图：import 当帧 acquire 的 image + 当帧 slot 的相机 UBO ----
        auto buildGraph = [&](uint32_t imageIndex,
                              uint32_t frame) -> std::unique_ptr<O5MRendergraph> {
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
            graph->importBuffer(camName, *cameraUBO[frame].first, sizeof(CameraUBO),
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
        O5MWorld world;
        const O5MEntityHandle cameraEntity = world.createEntity("camera");
        O5MCameraComponent* camera =
            world.getEntity(cameraEntity)->addComponent<O5MCameraComponent>();
        world.init();

        uint32_t frameIndex = 0;
        uint32_t sweep = 0;   // 循环迭代计数（frameIndex 在 continue 路径上不前进）
        float elapsedSeconds = 0.0f;
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

        // 相机订阅：总线按类型分桶，「类型 → 具体回调」的转换只在订阅处写一次。
        // 键位只用来更新相机的电平状态，移动积分在 O5MCameraComponent::update 里做。
        const O5MEventBus::Token camKeyToken = eventBus.addEvent<O5MKeyboardEvent>(
            [camera](const O5MEvent& event) {
                const auto& key = static_cast<const O5MKeyboardEvent&>(event);
                camera->processKey(key.getKey(),
                                   key.getEventType() == O5MKeyboardEvent::Pressed);
            });
        const O5MEventBus::Token camMoveToken = eventBus.addEvent<O5MMouseMovedEvent>(
            [camera](const O5MEvent& event) {
                const auto& moved = static_cast<const O5MMouseMovedEvent&>(event);
                camera->processMouseMovement(moved.getX(), moved.getY());
            });
        const O5MEventBus::Token camScrollToken = eventBus.addEvent<O5MMouseScrolledEvent>(
            [camera](const O5MEvent& event) {
                const auto& scrolled = static_cast<const O5MMouseScrolledEvent&>(event);
                camera->processMouseScroll(scrolled.getYOffset());
            });

        // rebuild 时两池都要跟着换：swapchain 的 images/views 全新，
        // renderFinished 按新 imageCount 重建；imageAvailable 必须换新——
        // acquire 走 suboptimal 分支时它已被 signal 且无人 wait，复用会违反
        // VUID-vkAcquireNextImageKHR-semaphore-01779（该 semaphore 必须 unsignaled）。
        // rebuild() 内部已 waitIdle，此刻销毁/重建是安全的。
        auto rebuildSwapchain = [&]() {
            auto [w, h] = window.getFramebufferSize();
            // 最小化时 framebuffer 可能是 0 尺寸：退回旧 extent，绝不让
            // vkCreateSwapchainKHR 拿到 0（VUID-VkSwapchainCreateInfoKHR-imageExtent-01689）。
            // 注意这里不能"跳过重建"——三条调用路径都依赖两池被换新。
            const vk::Extent2D extent = (w == 0 || h == 0)
                ? swapchain.getExtent() : vk::Extent2D{ w, h };
            swapchain.rebuild(extent);
            renderFinishedPool = makeRenderFinished();
            for (auto& sem : imageAvailable)
                sem = vk::raii::Semaphore(device.getDevice(), semInfo);
            std::cout << "[ok] swapchain rebuilt " << extent.width << "x"
                      << extent.height << "\n";
        };

        std::cout << "[ok] viewer running (frames-in-flight=" << kInFlight
                  << ") -- ESC to quit\n";

        while (!window.shouldClose() && frameIndex < smokeFrames) {
            window.pollEvents();

            auto now = std::chrono::high_resolution_clock::now();
            const float dt = std::chrono::duration<float>(now - lastTime).count();
            lastTime = now;
            elapsedSeconds += dt;

            const float aspect =
                static_cast<float>(swapchain.getExtent().width) /
                static_cast<float>(swapchain.getExtent().height);
            // 相机：aspect 每帧同步（resize 后 proj 才会跟着变），
            // 移动积分由 world.update → O5MComponent::update 驱动。
            camera->setAspectRatio(aspect);
            world.update(dt);

            const uint32_t frame = frameIndex % kInFlight;

            // ---- (a) 窗口 resize 优先拦掉：不 acquire、不碰任何信号量 ----
            // forceRebuild 必须用 sweep（每轮循环都自增），不能用 frameIndex——
            // continue 路径上 frameIndex 不前进，电平触发会无限重建。
            ++sweep;
            const bool forceRebuild =
                rebuildEvery != 0 && sweep % rebuildEvery == 0;
            if (window.takeResizeFlag() || forceRebuild) {
                rebuildSwapchain();
                continue;   // fence 未 reset，仍 signaled → 下轮 wait 立即通过
            }

            // ---- (b) slot 同步：等本 slot 上一次提交完成（不是"上一帧"！）----
            // kInFlight=2 时 CPU 最多超前 GPU 一帧，slot 的 fence 只在
            // frameIndex 与 frameIndex-2 之间复用（练习 3.1）。必须在 acquire
            // 之前：imageAvailable[frame] 上一轮的 wait 要先结算（练习 3.4）。
            device.getDevice().waitForFences(*frameFences[frame], true, UINT64_MAX);

            // ---- (c) acquire ----
            auto [acqIndex, needsRebuild] = swapchain.acquire(imageAvailable[frame]);
            if (needsRebuild) {
                rebuildSwapchain();
                continue;   // 同上：fence 保持 signaled，别留下无提交的 reset
            }

            // ---- (d) 确定要提交了，此刻才 reset ----
            // reset 与 submit 之间不能有任何 continue 出口：否则 fence 停在
            // unsignaled 且无 pending submit，本 slot 的 waitForFences 永久阻塞。
            device.getDevice().resetFences(*frameFences[frame]);

            // ---- (e) camera UBO：只写本 slot 那份，另一份 GPU 可能还在读 ----
            {
                const glm::mat4 view = camera->getViewMatrix();
                const glm::mat4 proj = camera->getProjectionMatrix();
                const CameraUBO ubo {
                    .view = view,
                    .proj = proj,
                    // view 的逆矩阵平移列即相机世界坐标（组件未暴露 position）
                    .camPos = glm::vec4(glm::vec3(glm::inverse(view)[3]), 1.0f),
                    .params = { elapsedSeconds, aspect, 0.0f, 0.0f },
                };
                void* mapped = cameraUBO[frame].second.mapMemory(0, vk::WholeSize);
                std::memcpy(mapped, &ubo, sizeof(ubo));
                cameraUBO[frame].second.unmapMemory();
            }

            // ---- (f) 录制 + 提交 ----
            auto graph = buildGraph(acqIndex, frame);
            // 单发图恒走冷启动计划（frameIndex 实参 0，练习 4.3）：稳态计划假设
            // "同一张 image 连续复用、上帧以 present 布局收尾"，而这里每帧 import
            // 的是不同 image 且用 eUndefined 作 oldLayout，前提不成立。
            graph->execute(commandBuffers[frame], *device.getQueue(),
                           &frameFences[frame],
                           /*frameIndex=*/0,
                           &imageAvailable[frame],
                           &renderFinishedPool[acqIndex]);

            // ---- (g) present：等 renderFinishedPool[acqIndex] ----
            if (!swapchain.present(*device.getQueue(), acqIndex,
                                   renderFinishedPool[acqIndex])) {
                rebuildSwapchain();
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

        // ---- 8. clean shutdown ----
        // 退订：world 先于 window 析构，不退订总线里会留下指向已死组件的
        // 悬垂指针（只在收尾阶段再 pollEvents 才会炸，但别留这个窗口）。
        eventBus.removeEvent<O5MKeyboardEvent>(camKeyToken);
        eventBus.removeEvent<O5MMouseMovedEvent>(camMoveToken);
        eventBus.removeEvent<O5MMouseScrolledEvent>(camScrollToken);

        // 防御式收尾：waitIdle 覆盖队列上所有已提交工作，但显式把每份 in-flight
        // fence 等到 signaled，才能保证 slot 资源析构时没有 pending submit。
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
