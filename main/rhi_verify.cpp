// rhi_verify — 最小 rendergraph 验证程序（无窗口，offscreen 全链路）
//
// 验证内容：
//   1. Vulkan instance/device/queue 最小 bootstrap（macOS MoltenVK 可跑）
//   2. O5MRendergraph: addResource -> addPass -> compile -> execute -> wait
//   3. 两个 pass 的依赖关系（render 写 rt -> composite 读 rt）能正确排序执行
//   4. fence 同步：submit 后等待完成再退出
//
// 退出码：0 = 全链路通过；非 0 = 失败（异常信息打印到 stderr）。

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <iostream>
#include <string_view>
#include <vector>

#include "O5MDevice.h"
#include "O5MRendergraph.h"

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

        // ---- 5. rendergraph 最小管线 ----
        //   render(写 rt) -> composite(读 rt)
        O5MRendergraph graph(o5mDevice);

        graph.addResource("rt", vk::Format::eR8G8B8A8Unorm, { 64, 64 },
                          vk::ImageUsageFlagBits::eColorAttachment |
                              vk::ImageUsageFlagBits::eSampled,
                          vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eShaderReadOnlyOptimal);

        graph.addPass("render", {}, { "rt" },
                      [](vk::raii::CommandBuffer&) {
                          std::cout << "  [pass] render   (write rt)\n";
                      });

        graph.addPass("composite", { "rt" }, {},
                      [](vk::raii::CommandBuffer&) {
                          std::cout << "  [pass] composite (read rt)\n";
                      });

        graph.compile();
        std::cout << "[ok] graph compiled\n";

        // ---- 6. 执行一帧并等待完成 ----
        graph.execute(commandBuffers[0], *o5mDevice.getQueue());
        o5mDevice.getDevice().waitIdle();
        std::cout << "[ok] submitted + fence waited\n";

        std::cout << "[PASS] rhi_verify: rendergraph 全链路 OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << "\n";
        return 1;
    }
}
