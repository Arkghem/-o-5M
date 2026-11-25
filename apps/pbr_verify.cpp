#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>


#include <glm/glm.hpp>

#include "Events/O5MKeyboardEvent.h"
#include "Events/O5MMouseEvent.h"
#include "Platform/O5MWindow.h"
#include "RHI/O5MDescriptorAllocator.h"
#include "RHI/O5MDevice.h"
#include "RHI/O5MPipeline.h"
#include "RHI/O5MSwapchain.h"
#include "RenderGraph/O5MRendergraph.h"
#include "Renderer/O5MCullingSystem.h"
#include "Renderer/O5MDrawItem.h"
#include "Resources/O5MMaterialResource.h"
#include "Resources/O5MMeshResource.h"
#include "Resources/O5MModelAsset.h"
#include "Resources/O5MResourceManager.h"
#include "Resources/O5MShaderResource.h"
#include "Scene/O5MCameraComponent.h"
#include "Scene/O5MMeshComponent.h"
#include "Scene/O5MTransformComponent.h"
#include "Scene/O5MWorld.h"


#ifndef O5M_SHADER_DIR
#define O5M_SHADER_DIR "shaders"
#endif

#ifndef O5M_ASSET_DIR
#define O5M_ASSET_DIR "assets"
#endif

struct CameraUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 camPos;
    glm::vec4 params;
};

struct Attachment {
    vk::raii::Image image = nullptr;
    vk::raii::DeviceMemory memory = nullptr;
    vk::raii::ImageView view = nullptr;
    vk::Format format = vk::Format::eUndefined;
};


bool tryReadFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

bool fileExists(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return static_cast<bool>(in);
}

std::string readTextFile(const std::string& path) {
    std::string out;
    if (!tryReadFile(path, out)) {
        throw std::runtime_error("cannot open shader file: " + path);
    }
    return out;
}

Attachment createAttachment(O5MDevice& device, vk::Format format, vk::Extent2D extent,
                            vk::ImageUsageFlags usage) {
    auto [image, memory] = device.createImage2D(
        format, extent, 1, vk::ImageTiling::eOptimal, usage,
        vk::MemoryPropertyFlagBits::eDeviceLocal);
    vk::raii::ImageView view = device.createImageView2D(*image, format);
    return Attachment{std::move(image), std::move(memory), std::move(view), format};
}

// IBL baking outputs are cube maps with an optional mip chain (prefiltered).
Attachment createCubeAttachment(O5MDevice& device, vk::Format format, uint32_t extent,
                                uint32_t mipLevels, vk::ImageUsageFlags usage) {
    auto [image, memory] = device.createImageCube(
        format, extent, mipLevels, vk::ImageTiling::eOptimal, usage,
        vk::MemoryPropertyFlagBits::eDeviceLocal);
    vk::raii::ImageView view = device.createImageViewCube(*image, format, mipLevels);
    return Attachment{std::move(image), std::move(memory), std::move(view), format};
}

vk::Format pickDepthFormat(const vk::raii::PhysicalDevice& physicalDevice) {
    const vk::FormatFeatureFlags required =
        vk::FormatFeatureFlagBits::eDepthStencilAttachment |
        vk::FormatFeatureFlagBits::eSampledImage;
    const std::array<vk::Format, 2> candidates = {
        vk::Format::eD32SfloatS8Uint,
        vk::Format::eD24UnormS8Uint,
    };
    for (vk::Format format : candidates) {
        const vk::FormatProperties properties = physicalDevice.getFormatProperties(format);
        if ((properties.optimalTilingFeatures & required) == required) {
            return format;
        }
    }
    throw std::runtime_error("no sampled depth-stencil format available");
}

vk::StencilOpState makeStencilState(vk::CompareOp compareOp, vk::StencilOp passOp,
                                    uint32_t writeMask, uint32_t reference) {
    vk::StencilOpState state{};
    state.failOp = vk::StencilOp::eKeep;
    state.passOp = passOp;
    state.depthFailOp = vk::StencilOp::eKeep;
    state.compareOp = compareOp;
    state.compareMask = 0xFF;
    state.writeMask = writeMask;
    state.reference = reference;
    return state;
}

std::vector<uint8_t> readBinaryFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open file: " + path);
    }
    in.seekg(0, std::ios::end);
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!in.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error("cannot read file: " + path);
    }
    return data;
}

std::string dirName(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

std::string baseName(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    std::string name = slash == std::string::npos
        ? path
        : path.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) {
        name.resize(dot);
    }
    return name.empty() ? std::string("model") : name;
}

std::string expandIncludes(const std::string& source,
                           const std::string& sourceDir,
                           const std::vector<std::string>& includeDirs,
                           std::vector<std::string>& includeStack,
                           uint32_t depth) {
    constexpr uint32_t kMaxIncludeDepth = 16;
    if (depth > kMaxIncludeDepth) {
        throw std::runtime_error("shader #include nested too deeply");
    }

    std::string out;
    size_t pos = 0;
    uint32_t lineNo = 0;

    while (pos < source.size()) {
        size_t eol = source.find('\n', pos);
        if (eol == std::string::npos) {
            eol = source.size();
        }
        const std::string line = source.substr(pos, eol - pos);
        pos = eol + 1;
        ++lineNo;

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
                    if (close == std::string::npos) {
                        throw std::runtime_error("malformed #include: " + line);
                    }
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

        std::string resolved;
        bool found = false;
        if (!angled) {
            const std::string candidate = sourceDir + "/" + name;
            if (fileExists(candidate)) {
                resolved = candidate;
                found = true;
            }
        }
        if (!found) {
            for (const std::string& dir : includeDirs) {
                const std::string candidate = dir + "/" + name;
                if (fileExists(candidate)) {
                    resolved = candidate;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            throw std::runtime_error("shader #include not found: \"" + name +
                                     "\" (from " + sourceDir + ", line " +
                                     std::to_string(lineNo) + ")");
        }

        if (std::find(includeStack.begin(), includeStack.end(), resolved) !=
            includeStack.end()) {
            throw std::runtime_error("circular shader #include: " + resolved);
        }

        includeStack.push_back(resolved);
        out += "#line 1\n";
        out += expandIncludes(readTextFile(resolved), dirName(resolved),
                              includeDirs, includeStack, depth + 1);
        includeStack.pop_back();
        out += "#line " + std::to_string(lineNo + 1) + "\n";
    }

    return out;
}

uint32_t findGraphicsQueueFamily(const vk::raii::PhysicalDevice& physicalDevice) {
    const auto properties = physicalDevice.getQueueFamilyProperties();
    for (uint32_t i = 0; i < properties.size(); ++i) {
        if (properties[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            return i;
        }
    }
    throw std::runtime_error("no graphics queue family found");
}


int main(int argc, char** argv) {
    O5MWindow window("pbr verify", 1280, 720);
    vk::raii::Context context;
    vk::ApplicationInfo appInfo("pbr", 1, "O5M", 1, VK_API_VERSION_1_2);

    std::vector<const char*> validationLayers;
    for (const auto& layer : context.enumerateInstanceLayerProperties()) {
        if (std::string_view(layer.layerName) == "VK_LAYER_KHRONOS_validation") {
            validationLayers.push_back("VK_LAYER_KHRONOS_validation");
            std::cout << "[ok] validation layer enabled\n";
            break;
        }
    }

    const bool useValidation = !validationLayers.empty();
    vk::ValidationFeatureEnableEXT syncFeature = vk::ValidationFeatureEnableEXT::eSynchronizationValidation;
    vk::ValidationFeaturesEXT validationFeatures(1, &syncFeature, 0, nullptr);
    vk::StructureChain<vk::InstanceCreateInfo, vk::ValidationFeaturesEXT> chain;

    auto glfwExtensions = O5MWindow::getRequiredInstanceExtensions();
    vk::InstanceCreateInfo instanceInfo;

#ifdef __APPLE__
    instanceInfo.setFlags(vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR);
    std::vector<const char*> extensions = glfwExtensions;
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    instanceInfo.setPEnabledExtensionNames(extensions);
#else
    instanceInfo.setPEnabledExtensionNames(glfwExtensions);
#endif
    instanceInfo.setPApplicationInfo(&appInfo)
                .setPEnabledLayerNames(validationLayers);

    if (useValidation) {
        chain.get<vk::InstanceCreateInfo>() = instanceInfo;
        chain.get<vk::ValidationFeaturesEXT>() = validationFeatures;
    }
    vk::raii::Instance instance(context, useValidation ? chain.get<vk::InstanceCreateInfo>() : instanceInfo);

    vk::raii::PhysicalDevices physicalDevices(instance);
    const vk::raii::PhysicalDevice& physicalDevice = physicalDevices.front();

    const uint32_t graphicFamily = findGraphicsQueueFamily(physicalDevice);
    float queuePriority = 1.0f;
    vk::DeviceQueueCreateInfo queueInfo{{}, graphicFamily, 1, &queuePriority};
    vk::DeviceCreateInfo deviceInfo;
    vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures(true);

    
    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
#ifdef __APPLE__
    deviceExtensions.push_back("VK_KHR_portability_subset");
#endif
    deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    deviceInfo.setQueueCreateInfos(queueInfo)
              .setPEnabledExtensionNames(deviceExtensions)
              .setPNext(&dynamicRenderingFeatures);

    O5MDevice device(physicalDevice, vk::raii::Device(physicalDevice, deviceInfo),
                     graphicFamily);
    std::cout << "[ok] device: " << physicalDevice.getProperties().deviceName << "\n";

    auto [framebufferWidth, framebufferHeight] = window.getFramebufferSize();
    vk::raii::SurfaceKHR surface = window.createSurface(instance);
    O5MSwapchain swapchain(device, surface,
                           vk::Extent2D{framebufferWidth, framebufferHeight});
    std::cout << "[ok] swapchain " << swapchain.getExtent().width << "x"
              << swapchain.getExtent().height << " "
              << vk::to_string(swapchain.getFormat()) << "\n";

    vk::raii::CommandPool commandPool(
        device.getDevice(),
        { vk::CommandPoolCreateFlagBits::eResetCommandBuffer, graphicFamily });
    constexpr uint32_t kInFlight = 2;

    vk::raii::CommandBuffers commandBuffers(
        device.getDevice(),
        { *commandPool, vk::CommandBufferLevel::ePrimary, kInFlight });

    std::array<vk::raii::Fence, kInFlight> frameFences = {
        vk::raii::Fence(device.getDevice(),
            vk::FenceCreateInfo{ vk::FenceCreateFlagBits::eSignaled }),
        vk::raii::Fence(device.getDevice(),
            vk::FenceCreateInfo{ vk::FenceCreateFlagBits::eSignaled }),
    };

    const vk::SemaphoreCreateInfo semaphoreInfo;
    std::array<vk::raii::Semaphore, kInFlight> imageAvailable = {
        vk::raii::Semaphore(device.getDevice(), semaphoreInfo),
        vk::raii::Semaphore(device.getDevice(), semaphoreInfo),
    };
    auto makeRenderFinished = [&]() {
        std::vector<vk::raii::Semaphore> pool;
        pool.reserve(swapchain.getImageCount());
        for (uint32_t i = 0; i < swapchain.getImageCount(); ++i) {
            pool.emplace_back(device.getDevice(), semaphoreInfo);
        }
        return pool;
    };
    auto renderFinishedPool = makeRenderFinished();

    std::vector<std::pair<vk::raii::Buffer, vk::raii::DeviceMemory>> cameraUBO;
    cameraUBO.reserve(kInFlight);
    for (uint32_t i = 0; i < kInFlight; ++i) {
        cameraUBO.push_back(device.createBuffer(
                    sizeof(CameraUBO), vk::BufferUsageFlagBits::eUniformBuffer,
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
    }

    const vk::Format colorFormat = vk::Format::eR16G16B16A16Sfloat; // HDR
    const vk::Format depthFormat = pickDepthFormat(physicalDevice);
    const vk::Format normalFormat = vk::Format::eR8G8B8A8Unorm;
    vk::Extent2D renderExtent = swapchain.getExtent();

    O5MDescriptorAllocator descriptorAllocator(device);
    const DescriptorSetLayoutHandle cameraLayoutHandle = 
        descriptorAllocator.createLayout(DescriptorSetLayoutDesc{
            {0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
        });

    const vk::DescriptorSetLayout cameralayout = 
        descriptorAllocator.getLayout(cameraLayoutHandle); 
    std::vector<DescriptorSet> cameraSets = descriptorAllocator.allocate(cameraLayoutHandle, kInFlight);

    for (uint32_t i = 0; i < kInFlight; ++i) {
        descriptorAllocator.writeBuffer(
            cameraSets[i], 0, vk::DescriptorType::eUniformBuffer,
                {*cameraUBO[i].first, 0, sizeof(CameraUBO)});
    }

    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMinLod(0.0f)
        .setMaxLod(0.0f);
    vk::raii::Sampler sampler(device.getDevice(), samplerInfo);

    vk::VertexInputBindingDescription vertexBinding(0, sizeof(O5MMeshResource::Vertex), vk::VertexInputRate::eVertex);
    std::vector<vk::VertexInputAttributeDescription> vertexAttributes = {
        {0, 0, vk::Format::eR32G32B32A32Sfloat, static_cast<uint32_t>(offsetof(O5MMeshResource::Vertex, m_pos))},
        {1, 0, vk::Format::eR32G32B32A32Sfloat, static_cast<uint32_t>(offsetof(O5MMeshResource::Vertex, m_norm))},
        {2, 0, vk::Format::eR32G32B32A32Sfloat, static_cast<uint32_t>(offsetof(O5MMeshResource::Vertex, m_tangent))},
        {3, 0, vk::Format::eR32G32Sfloat, static_cast<uint32_t>(offsetof(O5MMeshResource::Vertex, m_texCoord))}
    };


    const vk::StencilOpState baseStencil =
        makeStencilState(vk::CompareOp::eAlways, vk::StencilOp::eReplace, 0xFF, 1);
    const vk::StencilOpState outlineStencil =
        makeStencilState(vk::CompareOp::eNotEqual, vk::StencilOp::eKeep, 0x00, 1);

    // TODO(M1d): object pipeline creation lives here. It is deliberately
    // absent while the object shaders are being rewritten and the gbuffer pass
    // body is still empty -- the graph skeleton must stay runnable. Restore it
    // with: colorFormats {colorFormat, normalFormat}, depth/stencil =
    // depthFormat, camera set = set0, material table = set1, textures = set2,
    // push constant { model, materialIndex }.

    Attachment sceneColor = createAttachment(
        device, colorFormat, swapchain.getExtent(),
        vk::ImageUsageFlagBits::eColorAttachment |
            vk::ImageUsageFlagBits::eSampled);
    Attachment normalBuffer = createAttachment(
        device, normalFormat, swapchain.getExtent(),
        vk::ImageUsageFlagBits::eColorAttachment |
            vk::ImageUsageFlagBits::eSampled);
    Attachment depthBuffer = createAttachment(
        device, depthFormat, swapchain.getExtent(),
        vk::ImageUsageFlagBits::eDepthStencilAttachment |
            vk::ImageUsageFlagBits::eSampled);
    // Lighting renders into HDR, tonemap is the only pass that touches the
    // backbuffer: that keeps tonemap out of the G-buffer pass and gives M5's
    // debug views an HDR scene to read.
    Attachment hdrColor = createAttachment(
        device, colorFormat, swapchain.getExtent(),
        vk::ImageUsageFlagBits::eColorAttachment |
            vk::ImageUsageFlagBits::eSampled);
    vk::raii::ImageView depthSampleView = device.createImageView2D(
        *depthBuffer.image, depthFormat, vk::ImageAspectFlagBits::eDepth);
    // Layout is per-image state that outlives a single graph. The runtime graph
    // is rebuilt every frame, so it must import each persistent image with the
    // layout the previous frame left it in -- otherwise the very first barrier
    // starts from a lie (validation will say "oldLayout != current layout").
    // eUndefined means "never touched yet" (frame 0 and right after a resize).
    vk::ImageLayout sceneColorLayout = vk::ImageLayout::eUndefined;
    vk::ImageLayout normalLayout = vk::ImageLayout::eUndefined;
    vk::ImageLayout depthLayout = vk::ImageLayout::eUndefined;
    vk::ImageLayout hdrColorLayout = vk::ImageLayout::eUndefined;
    std::vector<vk::ImageLayout> backbufferLayouts(
        swapchain.getImageCount(), vk::ImageLayout::eUndefined);

    auto recreateAttachments = [&]() {
        const vk::Extent2D extent = swapchain.getExtent();
        sceneColor = createAttachment(
            device, colorFormat, extent,
            vk::ImageUsageFlagBits::eColorAttachment |
                vk::ImageUsageFlagBits::eSampled);
        normalBuffer = createAttachment(
            device, normalFormat, extent,
            vk::ImageUsageFlagBits::eColorAttachment |
                vk::ImageUsageFlagBits::eSampled);
        depthBuffer = createAttachment(
            device, depthFormat, extent,
            vk::ImageUsageFlagBits::eDepthStencilAttachment |
                vk::ImageUsageFlagBits::eSampled);
        hdrColor = createAttachment(
            device, colorFormat, extent,
            vk::ImageUsageFlagBits::eColorAttachment |
                vk::ImageUsageFlagBits::eSampled);
        depthSampleView = device.createImageView2D(
            *depthBuffer.image, depthFormat, vk::ImageAspectFlagBits::eDepth);

        sceneColorLayout = vk::ImageLayout::eUndefined;
        normalLayout = vk::ImageLayout::eUndefined;
        depthLayout = vk::ImageLayout::eUndefined;
        hdrColorLayout = vk::ImageLayout::eUndefined;
        backbufferLayouts.assign(swapchain.getImageCount(),
                                 vk::ImageLayout::eUndefined);
    };

    //confusing

    O5MWorld world;
    const O5MEntityHandle cameraEntity = world.createEntity("camera");
    O5MCameraComponent* camera = 
        world.getEntity(cameraEntity)->addComponent<O5MCameraComponent>();
    // TODO: import model assets and initiliaize here

    world.init();

    O5MEventBus& eventBus = window.getEventBus();
    eventBus.addEvent<O5MKeyboardEvent>([&window](const O5MEvent& event){
        const auto& key = static_cast<const O5MKeyboardEvent&>(event);
        if (key.getKey() == O5MKeyboardEvent::Key_Escape && 
            key.getEventType() == O5MKeyboardEvent::Pressed) {
            window.requestClose();
        }
    });

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

    //I remeber I write something to support this but I simply forget where
    auto rebuildSwapchain = [&]() {
        auto [width, height] = window.getFramebufferSize();
        const vk::Extent2D extent = (width == 0 || height == 0)
            ? swapchain.getExtent()
            : vk::Extent2D{ width, height };
        swapchain.rebuild(extent);
        renderFinishedPool = makeRenderFinished();
        for (auto& semaphore : imageAvailable) {
            semaphore = vk::raii::Semaphore(device.getDevice(), semaphoreInfo);
        }
        recreateAttachments();
        std::cout << "[ok] swapchain rebuilt " << extent.width << "x"
                  << extent.height << "\n";
    };

    // ------------------------------------------------------------------------
    //  In pbr renderer we will use two rendergraph.
    //  There will be a baking graph and a runtime graph.
    //  Baking graph will only be sumbit once during the loading period. 
    //  And It will not related to swapchian.
    //  The output of backing graph will be stored permently for the runtime graph to use, 
    //  thus the runtime graph will only need to sample it from a texture.
    //  markOuput: skyCUbe / irradianceCube / prefilteredCube/ brdLUT
    //  holycrap my comment looks like a pro~~~
    // ------------------------------------------------------------------------
    
    // A graph is a FRAME graph: its Created resources are transient and die
    // with the object. So everything that outlives one frame (the four IBL
    // outputs, the G-buffer, the HDR target) is allocated here by the app and
    // Imported into whichever graph needs it. Only the equirect sky is
    // Created -- it is consumed inside the bake and then forgotten.
    const vk::Format iblFormat = vk::Format::eR16G16B16A16Sfloat;
    const vk::Format lutFormat = vk::Format::eR16G16Sfloat;
    const vk::Extent2D skyCubeExtent{ 2048, 2048 };
    const vk::Extent2D irradianceCubeExtent{ 64, 64 };
    const vk::Extent2D prefilteredCubeExtent{ 128, 128 };
    const vk::Extent2D brdfLutExtent{ 512, 512 };
    constexpr uint32_t kPrefilteredMipLevels = 5;

    struct BakedEnvironment {
        Attachment skyCube;
        Attachment irradianceCube;
        Attachment prefilteredCube;
        Attachment brdfLUT;
    };
    BakedEnvironment bakedEnvironment;
    bakedEnvironment.skyCube = createCubeAttachment(
        device, iblFormat, skyCubeExtent.width, 1,
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage);
    bakedEnvironment.irradianceCube = createCubeAttachment(
        device, iblFormat, irradianceCubeExtent.width, 1,
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage);
    bakedEnvironment.prefilteredCube = createCubeAttachment(
        device, iblFormat, prefilteredCubeExtent.width, kPrefilteredMipLevels,
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage);
    bakedEnvironment.brdfLUT = createAttachment(
        device, lutFormat, brdfLutExtent,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled);


    // --- baking graph: built once, executed once, swapchain-free ------------
    auto buildBakingGraph = [&]() {
        auto graph = std::make_unique<O5MRendergraph>(device);

        // The only graph-owned resource. It is read by the conversion pass and
        // never needed again, so its lifetime is the graph's lifetime.
        std::string equirectName = "skyEquirect";

        const vk::Extent2D equirectExtent{ skyCubeExtent.width,
                                           skyCubeExtent.width / 2 };
        Attachment equirect = createAttachment(
            device,
            iblFormat,
            equirectExtent,
            vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled
        );

        ResourceInfo equirectInfo(equirectName, equirectExtent, iblFormat,
                                  ResourceSource::Created);
        O5MShaderResource equirectShader("equirect_sky_generator", vk::ShaderStageFlagBits::eCompute, device);
        auto shaderData = readTextFile(O5M_SHADER_DIR + std::string("/") + "equirect_sky.glsl");
        equirectShader.setData(shaderData.data(), shaderData.size());
        equirectShader.load();

        struct SkyParamsUBO {
            glm::vec4 sunColorIntensity;
            glm::vec4 sunDirection;
            glm::vec4 zenithColor;
            glm::vec4 horizonColor;
            glm::vec4 groundColor;
        };

        SkyParamsUBO skyParamsData = {};

        const auto skyLayout = descriptorAllocator.createLayout(
            DescriptorSetLayoutDesc{
                {0, vk::DescriptorType::eStorageImage, vk::ShaderStageFlagBits::eCompute},
            });
        auto skySets = descriptorAllocator.allocate(skyLayout, 1);

        descriptorAllocator.writeImage(skySets[0], 0, vk::DescriptorType::eStorageImage, *equirect.view, nullptr, vk::ImageLayout::eGeneral);
        O5MPipelineSpec skySpec{
            .setLayouts = {descriptorAllocator.getLayout(skyLayout)},
            .pushConstants = {{{vk::ShaderStageFlagBits::eCompute}, 0, sizeof(SkyParamsUBO)}}
        };
        O5MPipeline skyPipeline(device, skySpec);

        // irradiance cube resources declared below.

        const auto cubeLayout = descriptorAllocator.createLayout(
                DescriptorSetLayoutDesc{
                    {0, vk::DescriptorType::eStorageImage, vk::ShaderStageFlagBits::eCompute},
                    {1, vk::DescriptorType::eStorageImage, vk::ShaderStageFlagBits::eCompute},
                });
        auto cubeSets = descriptorAllocator.allocate(cubeLayout, 1);

        descriptorAllocator.writeImage(cubeSets[0], 0, vk::DescriptorType::eStorageImage, *bakedEnvironment.skyCube.view, nullptr, vk::ImageLayout::eGeneral);
        O5MPipelineSpec cubeSpec{
            .setLayouts = {descriptorAllocator.getLayout(cubeLayout)}
        };
        
        O5MPipeline cubePipeline(device, cubeSpec);

        // Persisted outputs: allocated above, imported so the graph writes
        // through a handle it does not own. `compiled` still tracks layouts
        // for them; only the memory ownership stays outside.
        std::string skyCubeName = "skyCube";
        ResourceInfo skyCubeInfo(skyCubeName, skyCubeExtent, iblFormat,
                                 ResourceSource::Imported);
        graph->importTexture(
            skyCubeName, *bakedEnvironment.skyCube.image,
            *bakedEnvironment.skyCube.view, iblFormat, skyCubeExtent,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage,
            vk::ImageLayout::eUndefined);

        std::string irradianceName = "irradianceCube";
        ResourceInfo irradianceInfo(irradianceName, irradianceCubeExtent,
                                    iblFormat, ResourceSource::Imported);
        graph->importTexture(
            irradianceName, *bakedEnvironment.irradianceCube.image,
            *bakedEnvironment.irradianceCube.view, iblFormat,
            irradianceCubeExtent,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage,
            vk::ImageLayout::eUndefined);

        std::string prefilteredName = "prefilteredCube";
        ResourceInfo prefilteredInfo(prefilteredName, prefilteredCubeExtent,
                                     iblFormat, ResourceSource::Imported);
        graph->importTexture(
            prefilteredName, *bakedEnvironment.prefilteredCube.image,
            *bakedEnvironment.prefilteredCube.view, iblFormat,
            prefilteredCubeExtent,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage,
            vk::ImageLayout::eUndefined);

        std::string brdfLutName = "brdfLUT";
        ResourceInfo brdfLutInfo(brdfLutName, brdfLutExtent, lutFormat,
                                 ResourceSource::Imported);
        graph->importTexture(
            brdfLutName, *bakedEnvironment.brdfLUT.image,
            *bakedEnvironment.brdfLUT.view, lutFormat, brdfLutExtent,
            vk::ImageUsageFlagBits::eColorAttachment |
                vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout::eUndefined);

        graph->addComputePass("equirect-sky",
            [equirectInfo](O5MPassBuilder& builder) mutable {
                builder.write(equirectInfo, TexWrite::Storage);
            },
            [&skyPipeline, skySet = skySets[0], skyParamsData](O5MRenderContext& ctx, vk::raii::CommandBuffer& cmd) {
                skyPipeline.bindPipeline(cmd);
                skyPipeline.bindDescriptorSet(cmd, skySet.set);
                skyPipeline.pushConstant(cmd, vk::ShaderStageFlagBits::eCompute, skyParamsData, 0);
                cmd.dispatch(2048/32, 1024/32, 1);
            });

        graph->addComputePass("equirect-to-cube",
            [equirectInfo, skyCubeInfo](O5MPassBuilder& builder) mutable {
                builder.read(equirectInfo, TexRead::Sampled);
                builder.write(skyCubeInfo, TexWrite::Storage);
            },
            [&cubePipeline, cubeSet = cubeSets[0]](O5MRenderContext& ctx, vk::raii::CommandBuffer& cmd) {
                cubePipeline.bindPipeline(cmd);
                cubePipeline.bindDescriptorSet(cmd, cubeSet.set);
                cmd.dispatch(1024/32, 1024/32, 6);
            });

        graph->addComputePass("irradiance-convolve",
            [skyCubeInfo, irradianceInfo](O5MPassBuilder& builder) mutable {
                builder.read(skyCubeInfo, TexRead::Sampled);
                builder.write(irradianceInfo, TexWrite::Storage);
            },
            [](O5MRenderContext&, vk::raii::CommandBuffer&) {
            
            });

        // 4. specular prefilter: GGX importance sampling, mip = roughness.
        graph->addComputePass("prefilter-convolve",
            [skyCubeInfo, prefilteredInfo](O5MPassBuilder& builder) mutable {
                builder.read(skyCubeInfo, TexRead::Sampled);
                builder.write(prefilteredInfo, TexWrite::Storage);
            },
            [](O5MRenderContext&, vk::raii::CommandBuffer&) {
                // TODO(M4): one dispatch per mip (per-mip cube views are still
                //   missing) or a subresource-aware barrier plan; the graph's
                //   image-granularity barriers vs per-mip dependencies is the
                //   known friction to solve here.
            });

        // 5. BRDF LUT: RG16F, x = scale, y = bias (split-sum second term).
        graph->addGraphicPass("brdf-lut",
            [brdfLutInfo](O5MPassBuilder& builder) mutable {
                builder.write(brdfLutInfo, TexWrite::ColorStore);
            },
            [](O5MRenderContext&, vk::raii::CommandBuffer&) {
                // TODO(M4): fullscreen triangle integrating the BRDF over
                //   NdotV x roughness.
            });

        graph->markOutput(skyCubeInfo.handle);
        graph->markOutput(irradianceInfo.handle);
        graph->markOutput(prefilteredInfo.handle);
        graph->markOutput(brdfLutInfo.handle);
        graph->compile();
        return graph;
    };

    {
        auto bakingGraph = buildBakingGraph();
        bakingGraph->dump();  // print bake order + barriers once at startup
        vk::raii::CommandBuffers bakingCommands(
            device.getDevice(),
            { *commandPool, vk::CommandBufferLevel::ePrimary, 1 });
        bakingGraph->execute(bakingCommands.front(), *device.getQueue(),
                             nullptr, 0, nullptr, nullptr);
        // One-shot load: block until the GPU is done, then let the graph (and
        // its Created equirect image) die. The baked outputs are imports and
        // therefore survive this scope.
        device.getQueue().waitIdle();
        std::cout << "[ok] IBL bake submitted (stub bodies: layouts were "
                     "transitioned, content is still TODO)\n";
    }

    // Layout each baked image is left in by the bake graph -- the last
    // declared use wins. The runtime graph imports with exactly these so its
    // first barrier starts from the truth. Keep in sync with the pass list
    // above (skyCube is re-read by the prefilter pass, hence ShaderReadOnly).
    //
    // These are NOT constants: after the first runtime frame the lighting pass
    // has sampled all four, so the next frame must import them as
    // ShaderReadOnly. Forgetting this line is exactly the kind of cross-graph
    // layout lie sync validation reports on frame 2.
    vk::ImageLayout skyCubeLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    vk::ImageLayout irradianceCubeLayout = vk::ImageLayout::eGeneral;
    vk::ImageLayout prefilteredCubeLayout = vk::ImageLayout::eGeneral;
    vk::ImageLayout brdfLutLayout = vk::ImageLayout::eColorAttachmentOptimal;

    bool runtimeGraphDumped = false;

    auto buildGraph = [&](uint32_t imageIndex, uint32_t frame, const O5MDrawList& visible) {
        auto graph = std::make_unique<O5MRendergraph>(device);
        const vk::Extent2D extent = swapchain.getExtent();

        std::string backbufferName = "backbuffer";
        ResourceInfo backbufferInfo(
            backbufferName,
            extent,
            swapchain.getFormat(),
            ResourceSource::Imported
        );

        graph->importTexture(
            backbufferName,
            swapchain.getImages()[imageIndex],
            *swapchain.getViews()[imageIndex],
            swapchain.getFormat(),
            extent,
            vk::ImageUsageFlagBits::eColorAttachment,
            backbufferLayouts[imageIndex]
        );

        std::string sceneColorName = "sceneColor";
        ResourceInfo sceneColorInfo(
            sceneColorName,
            extent,
            sceneColor.format,
            ResourceSource::Imported
        );

        graph->importTexture(
            sceneColorName,
            *sceneColor.image,
            *sceneColor.view,
            sceneColor.format,
            extent,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            sceneColorLayout
        );

        std::string normalName = "normalBuffer";
        ResourceInfo normalInfo(
            normalName,
            extent,
            normalFormat,
            ResourceSource::Imported
        );

        graph->importTexture(
            normalName,
            *normalBuffer.image,
            *normalBuffer.view,
            normalFormat,
            extent,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            normalLayout
        );

        std::string depthName = "depthBuffer";
        ResourceInfo depthInfo(
            depthName,
            extent,
            depthFormat,
            ResourceSource::Imported
        );

        graph->importTexture(
            depthName,
            *depthBuffer.image,
            *depthBuffer.view,
            depthFormat,
            extent,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
            depthLayout
        );

        std::string cameraName = "camera";
        ResourceInfo cameraInfo(
            cameraName, 
            static_cast<vk::DeviceSize>(sizeof(CameraUBO)),
            ResourceSource::Imported
        );

        graph->importBuffer(
            cameraName,
            *cameraUBO[frame].first,
            sizeof(CameraUBO),
            vk::BufferUsageFlagBits::eUniformBuffer
        );

        std::string hdrColorName = "hdrColor";
        ResourceInfo hdrColorInfo(hdrColorName, extent, hdrColor.format,
                                  ResourceSource::Imported);
        graph->importTexture(
            hdrColorName,
            *hdrColor.image,
            *hdrColor.view,
            hdrColor.format,
            extent,
            vk::ImageUsageFlagBits::eColorAttachment |
                vk::ImageUsageFlagBits::eSampled,
            hdrColorLayout
        );

        // Baked IBL inputs. They are imported, not owned: the runtime graph
        // only samples them, the bake graph is already gone.
        std::string skyCubeName = "skyCube";
        ResourceInfo skyCubeInfo(skyCubeName, skyCubeExtent, iblFormat,
                                 ResourceSource::Imported);
        graph->importTexture(
            skyCubeName, *bakedEnvironment.skyCube.image,
            *bakedEnvironment.skyCube.view, iblFormat, skyCubeExtent,
            vk::ImageUsageFlagBits::eSampled, skyCubeLayout);

        std::string irradianceName = "irradianceCube";
        ResourceInfo irradianceInfo(irradianceName, irradianceCubeExtent,
                                    iblFormat, ResourceSource::Imported);
        graph->importTexture(
            irradianceName, *bakedEnvironment.irradianceCube.image,
            *bakedEnvironment.irradianceCube.view, iblFormat,
            irradianceCubeExtent,
            vk::ImageUsageFlagBits::eSampled, irradianceCubeLayout);

        std::string prefilteredName = "prefilteredCube";
        ResourceInfo prefilteredInfo(prefilteredName, prefilteredCubeExtent,
                                     iblFormat, ResourceSource::Imported);
        graph->importTexture(
            prefilteredName, *bakedEnvironment.prefilteredCube.image,
            *bakedEnvironment.prefilteredCube.view, iblFormat,
            prefilteredCubeExtent,
            vk::ImageUsageFlagBits::eSampled, prefilteredCubeLayout);

        std::string brdfLutName = "brdfLUT";
        ResourceInfo brdfLutInfo(brdfLutName, brdfLutExtent, lutFormat,
                                 ResourceSource::Imported);
        graph->importTexture(
            brdfLutName, *bakedEnvironment.brdfLUT.image,
            *bakedEnvironment.brdfLUT.view, lutFormat, brdfLutExtent,
            vk::ImageUsageFlagBits::eSampled, brdfLutLayout);

        (void)visible;  // TODO(M1c): fill the collection stage + VB/IB imports

        graph->markOutput(backbufferInfo.handle);

        // 1. G-buffer: HDR base color + world normal + depth (+ stencil once
        //    the outline pass is ported here).
        graph->addGraphicPass("gbuffer",
            [sceneColorInfo, normalInfo, depthInfo, cameraInfo](
                O5MPassBuilder& builder) mutable {
                builder.write(sceneColorInfo, TexWrite::ColorClear);
                builder.write(normalInfo, TexWrite::ColorClear);
                builder.write(depthInfo, TexWrite::Depth);
                builder.read(cameraInfo, BufRead::Uniform);
            },
            [sceneColorInfo, normalInfo, depthInfo, extent](
                O5MRenderContext& context, vk::raii::CommandBuffer& cmd) {
                O5MBeginInfo beginInfo{};
                beginInfo.colorViews = {
                    context.getImageView(sceneColorInfo.handle),
                    context.getImageView(normalInfo.handle),
                };
                beginInfo.depthView = context.getImageView(depthInfo.handle);
                beginInfo.stencilView = beginInfo.depthView;
                beginInfo.extent = extent;
                O5MPipeline::begin(cmd, beginInfo);

                // TODO(M1d/M2): restore pipeline creation (see the note
                //   above), bind objectPipeline + camera set (later the
                //   material table), then draw every O5MDrawItem in `visible`.

                O5MPipeline::end(cmd);
            });

        // 2. PBR lighting: reconstruct view-space position from depth, shade
        //    direct + IBL, write HDR. The skybox is folded in here on purpose:
        //    the graph is single-writer, so a separate skybox pass could not
        //    also write hdrColor without blend support.
        graph->addGraphicPass("pbr-lighting",
            [sceneColorInfo, normalInfo, depthInfo, hdrColorInfo, cameraInfo,
             skyCubeInfo, irradianceInfo, prefilteredInfo, brdfLutInfo](
                O5MPassBuilder& builder) mutable {
                builder.read(sceneColorInfo, TexRead::Sampled);
                builder.read(normalInfo, TexRead::Sampled);
                builder.read(depthInfo, TexRead::Sampled);
                builder.read(cameraInfo, BufRead::Uniform);
                builder.read(skyCubeInfo, TexRead::Sampled);
                builder.read(irradianceInfo, TexRead::Sampled);
                builder.read(prefilteredInfo, TexRead::Sampled);
                builder.read(brdfLutInfo, TexRead::Sampled);
                builder.write(hdrColorInfo, TexWrite::ColorClear);
            },
            [hdrColorInfo, extent](O5MRenderContext& context,
                                   vk::raii::CommandBuffer& cmd) {
                O5MBeginInfo beginInfo{};
                beginInfo.colorViews = {
                    context.getImageView(hdrColorInfo.handle)
                };
                beginInfo.extent = extent;
                O5MPipeline::begin(cmd, beginInfo);

                // TODO(M2/M3/M4): fullscreen triangle; direct light + shadow
                //   PCF, ambient from irradiance, specular from prefiltered +
                //   BRDF LUT, sky for depth == far.

                O5MPipeline::end(cmd);
            });

        // 3. Tonemap: HDR -> swapchain. This is also where M5's debug views
        //    are picked (data views before, scene views after tonemap).
        graph->addGraphicPass("tonemap",
            [hdrColorInfo, backbufferInfo](O5MPassBuilder& builder) mutable {
                builder.read(hdrColorInfo, TexRead::Sampled);
                builder.write(backbufferInfo, TexWrite::Present);
            },
            [backbufferInfo, extent](O5MRenderContext& context,
                                     vk::raii::CommandBuffer& cmd) {
                O5MBeginInfo beginInfo{};
                beginInfo.colorViews = {
                    context.getImageView(backbufferInfo.handle)
                };
                beginInfo.extent = extent;
                // Placeholder clear so the skeleton is visibly alive; replace
                // with the fullscreen triangle in M2.
                beginInfo.colorClear = vk::ClearColorValue(
                    std::array<float, 4>{ 0.05f, 0.07f, 0.10f, 1.0f });
                O5MPipeline::begin(cmd, beginInfo);
                O5MPipeline::end(cmd);
            });

        graph->compile();
        if (!runtimeGraphDumped) {
            graph->dump();
            runtimeGraphDumped = true;
        }
        return graph;
    };

    O5MDrawList visible;  // TODO(M1c): collection stage (see o5m_viewer)
    uint64_t frameCounter = 0;
    float elapsedSeconds = 0.0f;
    auto lastTime = std::chrono::high_resolution_clock::now();

    std::cout << "[ok] pbr_verify running (frames-in-flight=" << kInFlight
              << ") -- ESC to quit\n";

    while (!window.shouldClose()) {
        window.pollEvents();

        const auto now = std::chrono::high_resolution_clock::now();
        const float deltaTime =
            std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        elapsedSeconds += deltaTime;

        if (window.takeResizeFlag()) {
            rebuildSwapchain();
            continue;
        }

        const uint32_t frame = static_cast<uint32_t>(frameCounter % kInFlight);
        (void)device.getDevice().waitForFences(*frameFences[frame], true,
                                              UINT64_MAX);

        auto [imageIndex, needsRebuild] = swapchain.acquire(imageAvailable[frame]);
        if (needsRebuild) {
            rebuildSwapchain();
            continue;
        }
        device.getDevice().resetFences(*frameFences[frame]);

        const float aspect = static_cast<float>(swapchain.getExtent().width) /
                             static_cast<float>(swapchain.getExtent().height);
        camera->setAspectRatio(aspect);
        world.update(deltaTime);

        {
            const glm::mat4 view = camera->getViewMatrix();
            const glm::mat4 projection = camera->getProjectionMatrix();
            const CameraUBO ubo{
                .view = view,
                .proj = projection,
                .camPos = glm::vec4(glm::vec3(glm::inverse(view)[3]), 1.0f),
                .params = { elapsedSeconds, aspect, camera->getNearPlane(),
                            camera->getFarPlane() },
            };
            void* mapped = cameraUBO[frame].second.mapMemory(0, vk::WholeSize);
            std::memcpy(mapped, &ubo, sizeof(ubo));
            cameraUBO[frame].second.unmapMemory();
        }

        auto graph = buildGraph(imageIndex, frame, visible);
        graph->execute(commandBuffers[frame], *device.getQueue(),
                       &frameFences[frame], 0,
                       &imageAvailable[frame],
                       &renderFinishedPool[imageIndex]);

        // Last use of each persistent image in the runtime graph (mirror of
        // the pass declarations above). The next frame imports from here.
        sceneColorLayout = vk::ImageLayout::eShaderReadOnlyOptimal;   // lighting
        normalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;       // lighting
        depthLayout = vk::ImageLayout::eShaderReadOnlyOptimal;        // lighting
        hdrColorLayout = vk::ImageLayout::eShaderReadOnlyOptimal;     // tonemap
        backbufferLayouts[imageIndex] = vk::ImageLayout::ePresentSrcKHR;  // exit

        // Baked inputs were only "General"/"ColorAttachment" right after the
        // bake; from the first runtime frame on the lighting pass samples
        // them, so they stay ShaderReadOnly.
        skyCubeLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        irradianceCubeLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        prefilteredCubeLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        brdfLutLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        if (!swapchain.present(*device.getQueue(), imageIndex,
                               renderFinishedPool[imageIndex])) {
            rebuildSwapchain();
        }

        ++frameCounter;
    }

    for (uint32_t i = 0; i < kInFlight; ++i) {
        (void)device.getDevice().waitForFences(*frameFences[i], true,
                                              UINT64_MAX);
    }
    device.getQueue().waitIdle();

    eventBus.removeEvent<O5MKeyboardEvent>(camKeyToken);
    eventBus.removeEvent<O5MMouseMovedEvent>(camMoveToken);
    eventBus.removeEvent<O5MMouseScrolledEvent>(camScrollToken);

    std::cout << "[ok] " << frameCounter << " frames rendered, clean exit\n";
    return 0;
}
