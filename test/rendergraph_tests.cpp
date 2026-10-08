// rendergraph_tests — Phase 1 验收单元测试（无渲染，但需真设备做物化）
//
// 覆盖（对应 docs/phase1-rendergraph-design.md §7 验收清单）：
//   1. 环检测：构造 RAW 环 → compile() throw，且错误信息报出环上 pass 名单
//   2. 死 pass 裁剪：markOutput 反向可达之外的 pass 被剔除（调度计数正确）
//   3. 生命周期 + usage 推导：dump() 输出 firstUse/lastUse 与 usage 并集
//   4. 导入资源 usage 校验：越界用法 → throw 且带资源名与 pass 名
//
// 退出码：0 = 全部通过；非 0 = 失败（失败项打印到 stderr，带行号）。
// 运行：cmake --build build --target rendergraph_tests && ./build/rendergraph_tests

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "RHI/O5MDevice.h"
#include "RenderGraph/O5MRendergraph.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool cond, const char* msg, int line) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::cerr << "[FAIL] line " << line << ": " << msg << "\n";
    }
}
#define CHECK(cond, msg) check((cond), (msg), __LINE__)

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

// Minimal device bootstrap (no validation layers -- these are CPU-side tests;
// the device is only needed because compile() materializes created resources)

// Capture graph.dump() output into a string (restores std::cout afterwards)
std::string dumpToString(O5MRendergraph& graph) {
    std::ostringstream capture;
    auto* oldBuf = std::cout.rdbuf(capture.rdbuf());
    graph.dump();
    std::cout.rdbuf(oldBuf);
    return capture.str();
}

} // namespace

int main() {
    try {
        vk::raii::Context context;
        vk::ApplicationInfo appInfo("o5m-rendergraph-tests", 1, "o5m", 1, VK_API_VERSION_1_2);
        vk::InstanceCreateInfo instanceInfo;
#ifdef __APPLE__
        instanceInfo.setFlags(vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR);
#endif
        instanceInfo.setPApplicationInfo(&appInfo)
                    .setPEnabledExtensionNames(kInstanceExtensions);
        vk::raii::Instance instance(context, instanceInfo);

        vk::raii::PhysicalDevices physicalDevices(instance);
        if (physicalDevices.empty())
            throw std::runtime_error("no physical device");
        const vk::raii::PhysicalDevice& physicalDevice = physicalDevices.front();

        const uint32_t graphicsFamily = findGraphicsQueueFamily(*physicalDevice);
        float queuePriority = 1.0f;
        vk::DeviceQueueCreateInfo queueInfo({}, graphicsFamily, 1, &queuePriority);
        vk::DeviceCreateInfo deviceInfo;
        vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures(true);
        deviceInfo.setQueueCreateInfos(queueInfo)
                  .setPEnabledExtensionNames(kDeviceExtensions)
                  .setPNext(&dynamicRenderingFeatures);
        O5MDevice device(physicalDevice, vk::raii::Device(physicalDevice, deviceInfo),
                         graphicsFamily);

        // ---- 1. cycle detection: two passes reading each other's output ----
        // loopA reads final and writes scene; loopB reads scene and writes
        // final -> RAW cycle. innocent writes an unconsumed resource and must
        // NOT be reported. (Single-writer construction: relying on the
        // not-yet-enforced single-writer rule with a third writer would
        // corrupt the WAR bookkeeping -- see design doc step 2.)
        {
            O5MRendergraph graph(device);
            std::string sceneName = "scene";
            std::string finalName = "final";
            std::string innocentName = "innocentTex";
            ResourceInfo sceneInfo(sceneName, vk::Extent2D{ 8, 8 },
                                   vk::Format::eR8G8B8A8Unorm);
            ResourceInfo finalInfo(finalName, vk::Extent2D{ 8, 8 },
                                   vk::Format::eR8G8B8A8Unorm);
            ResourceInfo innocentInfo(innocentName, vk::Extent2D{ 8, 8 },
                                      vk::Format::eR8G8B8A8Unorm);

            graph.addGraphicPass("loopA",
                [&](O5MPassBuilder& b) {
                    b.read(finalInfo, TexRead::Sampled);
                    b.write(sceneInfo, TexWrite::ColorStore);
                },
                [](O5MRenderContext&, vk::raii::CommandBuffer&) {});
            graph.addGraphicPass("loopB",
                [&](O5MPassBuilder& b) {
                    b.read(sceneInfo, TexRead::Sampled);
                    b.write(finalInfo, TexWrite::ColorStore);
                },
                [](O5MRenderContext&, vk::raii::CommandBuffer&) {});
            graph.addGraphicPass("innocent",
                [&](O5MPassBuilder& b) { b.write(innocentInfo, TexWrite::ColorStore); },
                [](O5MRenderContext&, vk::raii::CommandBuffer&) {});

            bool threw = false;
            std::string what;
            try {
                graph.compile();
            } catch (const std::exception& e) {
                threw = true;
                what = e.what();
            }
            CHECK(threw, "cycle graph must throw on compile()");
            CHECK(what.find("loopA") != std::string::npos,
                  "cycle error must name 'loopA'");
            CHECK(what.find("loopB") != std::string::npos,
                  "cycle error must name 'loopB'");
            CHECK(what.find("innocent") == std::string::npos,
                  "cycle error must not name unrelated pass 'innocent'");
            std::cout << "[ok] cycle detection reports: " << what << "\n";
        }

        // ---- 2 + 3. dead-pass culling + lifecycle/usage dump ----
        // render(scene) -> post(scene->final), finalColor is markOutput;
        // orphanPass writes orphanTex which nothing consumes -> culled.
        {
            O5MRendergraph graph(device);
            std::string sceneName = "sceneColor";
            std::string finalName = "finalColor";
            std::string orphanName = "orphanTex";
            ResourceInfo sceneInfo(sceneName, vk::Extent2D{ 8, 8 },
                                   vk::Format::eR8G8B8A8Unorm);
            ResourceInfo finalInfo(finalName, vk::Extent2D{ 8, 8 },
                                   vk::Format::eR8G8B8A8Unorm);
            ResourceInfo orphanInfo(orphanName, vk::Extent2D{ 8, 8 },
                                    vk::Format::eR8G8B8A8Unorm);

            graph.addGraphicPass("renderPass",
                [&](O5MPassBuilder& b) { b.write(sceneInfo, TexWrite::ColorStore); },
                [](O5MRenderContext&, vk::raii::CommandBuffer&) {});
            graph.addGraphicPass("postPass",
                [&](O5MPassBuilder& b) {
                    b.read(sceneInfo, TexRead::Sampled);
                    b.write(finalInfo, TexWrite::ColorStore);
                },
                [](O5MRenderContext&, vk::raii::CommandBuffer&) {});
            graph.addGraphicPass("orphanPass",
                [&](O5MPassBuilder& b) { b.write(orphanInfo, TexWrite::ColorStore); },
                [](O5MRenderContext&, vk::raii::CommandBuffer&) {});
            graph.markOutput(finalInfo.handle);

            graph.compile();
            const std::string dump = dumpToString(graph);

            CHECK(dump.find("scheduled: 2") != std::string::npos,
                  "orphan pass must be culled (2 of 3 scheduled)");

            // dead pass must not appear in the execution order section, but
            // its resource must show up in the lifecycle table as never used
            const auto execEnd = dump.find("-- resource lifecycle --");
            CHECK(execEnd != std::string::npos, "dump has a lifecycle section");
            const std::string execSection = dump.substr(0, execEnd);
            const std::string lifeSection = dump.substr(execEnd);
            CHECK(execSection.find("orphanPass") == std::string::npos,
                  "culled pass must not be scheduled");
            CHECK(lifeSection.find("never used (dead resource)") != std::string::npos,
                  "dead resource must be reported in the lifecycle table");
            CHECK(lifeSection.find("orphanTex") != std::string::npos,
                  "dead resource name must appear in the lifecycle table");

            // usage derivation: sceneColor is written as color attachment in
            // renderPass and sampled in postPass -> union must contain both
            CHECK(dump.find("ColorAttachment") != std::string::npos,
                  "derived usage must contain ColorAttachment");
            CHECK(dump.find("Sampled") != std::string::npos,
                  "derived usage must contain Sampled");
            CHECK(dump.find("firstUse") != std::string::npos,
                  "lifecycle table must print firstUse/lastUse");

            std::cout << "[ok] dead-pass culling + lifecycle/usage dump\n";
        }

        // ---- 4. imported resource usage validation ----
        // params imported as Uniform, but a pass declares BufRead::Storage:
        // compile() must throw naming the resource AND the pass.
        {
            O5MRendergraph graph(device);
            auto [paramsBuffer, paramsMemory] = device.createBuffer(
                16, vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible);
            auto [sinkBuffer, sinkMemory] = device.createBuffer(
                16, vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eHostVisible);

            std::string paramsName = "params";
            std::string sinkName = "sink";
            ResourceInfo paramsInfo(paramsName, vk::DeviceSize(16),
                                    ResourceSource::Imported);
            ResourceInfo sinkInfo(sinkName, vk::DeviceSize(16),
                                  ResourceSource::Imported);
            graph.importBuffer(paramsName, *paramsBuffer, 16,
                               vk::BufferUsageFlagBits::eUniformBuffer);
            graph.importBuffer(sinkName, *sinkBuffer, 16,
                               vk::BufferUsageFlagBits::eTransferDst);
            graph.markOutput(sinkInfo.handle);

            graph.addGraphicPass("badUsagePass",
                [&](O5MPassBuilder& b) {
                    b.read(paramsInfo, BufRead::Storage); // unsupported!
                    b.write(sinkInfo, BufWrite::TransferDst);
                },
                [](O5MRenderContext&, vk::raii::CommandBuffer&) {});

            bool threw = false;
            std::string what;
            try {
                graph.compile();
            } catch (const std::exception& e) {
                threw = true;
                what = e.what();
            }
            CHECK(threw, "unsupported imported usage must throw");
            CHECK(what.find("params") != std::string::npos,
                  "usage error must name the resource");
            CHECK(what.find("badUsagePass") != std::string::npos,
                  "usage error must name the pass");
            std::cout << "[ok] imported usage validation reports: " << what << "\n";
        }

        if (g_failures == 0) {
            std::cout << "[PASS] rendergraph_tests: " << g_checks << " checks\n";
            return 0;
        }
        std::cerr << "[FAIL] " << g_failures << "/" << g_checks << " checks failed\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] unexpected exception: " << e.what() << "\n";
        return 1;
    }
}
