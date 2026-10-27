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
#include <glm/gtc/quaternion.hpp>

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

namespace {

struct CameraUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 camPos;
    glm::vec4 params;
};

struct OutlinePC {
    glm::mat4 model;
    float width;
};

enum class DebugMode {
    Off,
    Depth,
    Normal
};

class SpinComponent : public O5MComponent {
public:
    glm::vec3 axis = glm::vec3(0.0f, 1.0f, 0.0f);
    float speed = 1.0f;
    float angle = 0.0f;

private:
    void update(float deltaTime) override {
        angle += speed * deltaTime;
        O5MEntity* owner = getOwner();
        if (!owner) {
            return;
        }
        O5MTransformComponent* transform = owner->getComponent<O5MTransformComponent>();
        if (!transform) {
            return;
        }
        const glm::vec3 normalizedAxis = glm::dot(axis, axis) > 0.0f
            ? glm::normalize(axis)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        transform->setRotation(glm::angleAxis(angle, normalizedAxis));
    }
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

Attachment createAttachment(O5MDevice& device, vk::Format format, vk::Extent2D extent,
                            vk::ImageUsageFlags usage) {
    auto [image, memory] = device.createImage2D(
        format, extent, 1, vk::ImageTiling::eOptimal, usage,
        vk::MemoryPropertyFlagBits::eDeviceLocal);
    vk::raii::ImageView view = device.createImageView2D(*image, format);
    return Attachment{std::move(image), std::move(memory), std::move(view), format};
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

void dumpModel(const O5MModelAsset& model) {
    std::cout << "[model] meshes=" << model.meshes().size()
              << " materials=" << model.materials().size()
              << " textures=" << model.textures().size()
              << " parts=" << model.parts().size() << "\n";

    for (size_t i = 0; i < model.meshes().size(); ++i) {
        const O5MMeshResource* mesh = model.meshes()[i].get();
        if (!mesh) {
            std::cout << "  mesh[" << i << "] <empty>\n";
            continue;
        }
        std::cout << "  mesh[" << i << "] v=" << mesh->getVertexCount()
                  << " i=" << mesh->getIndexCount()
                  << " submeshes=" << mesh->getSubMeshes().size()
                  << " bounds=" << mesh->getBoundsSphere().x << ","
                  << mesh->getBoundsSphere().y << ","
                  << mesh->getBoundsSphere().z << " r="
                  << mesh->getBoundsSphere().w << "\n";
        for (size_t s = 0; s < mesh->getSubMeshes().size(); ++s) {
            const auto& sub = mesh->getSubMeshes()[s];
            std::cout << "    sub[" << s << "] first=" << sub.firstIndex
                      << " count=" << sub.indexCount
                      << " material=" << (sub.material.get() ? "valid" : "null")
                      << "\n";
        }
    }

    for (size_t i = 0; i < model.materials().size(); ++i) {
        const O5MMaterialResource* material = model.materials()[i].get();
        if (!material) {
            std::cout << "  material[" << i << "] <empty>\n";
            continue;
        }
        const auto& params = material->getMaterialParams();
        const auto& slots = material->getTextures();
        std::cout << "  material[" << i << "] " << material->getName()
                  << " albedo=(" << params.albedoFactor.r << ","
                  << params.albedoFactor.g << ","
                  << params.albedoFactor.b << ","
                  << params.albedoFactor.w << ") mr=("
                  << params.mrFactor.x << "," << params.mrFactor.y << ")\n";

        const auto logSlot = [](const char* name, const auto& handle) {
            const O5MTextureResource* texture = handle.get();
            if (!texture) {
                std::cout << "    " << name << "=none\n";
                return;
            }
            std::cout << "    " << name << "=" << texture->getWidth() << "x"
                      << texture->getHeight() << " "
                      << (texture->getColorSpace() == O5MTextureColorSpace::Srgb
                              ? "sRGB"
                              : "linear")
                      << "\n";
        };
        logSlot("baseColor", slots.baseColor);
        logSlot("metallicRoughness", slots.metallicRoughness);
        logSlot("normal", slots.normal);
        logSlot("occlusion", slots.occlusion);
        logSlot("emissive", slots.emissive);
    }

    for (size_t i = 0; i < model.parts().size(); ++i) {
        const glm::mat4& world = model.parts()[i].worldTransform;
        const glm::vec4 translation = world[3];
        const glm::vec3 xAxis = glm::normalize(glm::vec3(world[0]));
        const glm::vec3 yAxis = glm::normalize(glm::vec3(world[1]));
        const glm::vec3 zAxis = glm::normalize(glm::vec3(world[2]));
        std::cout << "  part[" << i << "] translation=(" << translation.x << ","
                  << translation.y << "," << translation.z << ") x=("
                  << xAxis.x << "," << xAxis.y << "," << xAxis.z << ") y=("
                  << yAxis.x << "," << yAxis.y << "," << yAxis.z << ") z=("
                  << zAxis.x << "," << zAxis.y << "," << zAxis.z << ")\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        uint32_t smokeFrames = UINT32_MAX;
        uint32_t rebuildEvery = 0;
        DebugMode debugMode = DebugMode::Off;
        bool dumpImportedModel = false;
        std::string modelPath =
            std::string(O5M_ASSET_DIR) + "/models/cube.glb";

        for (int i = 1; i < argc; ++i) {
            const std::string_view arg(argv[i]);
            if (arg == "--frames" && i + 1 < argc) {
                smokeFrames = static_cast<uint32_t>(std::atoi(argv[++i]));
            } else if (arg == "--rebuild-every" && i + 1 < argc) {
                rebuildEvery = static_cast<uint32_t>(std::atoi(argv[++i]));
            } else if (arg == "--model" && i + 1 < argc) {
                modelPath = argv[++i];
            } else if (arg == "--dump-model") {
                dumpImportedModel = true;
            } else if (arg == "--debug" && i + 1 < argc) {
                const std::string_view mode(argv[++i]);
                if (mode == "depth") {
                    debugMode = DebugMode::Depth;
                } else if (mode == "normal") {
                    debugMode = DebugMode::Normal;
                } else if (mode == "off") {
                    debugMode = DebugMode::Off;
                } else {
                    throw std::runtime_error("unknown debug mode: " + std::string(mode));
                }
            }
        }

        std::cout << std::unitbuf;

        O5MWindow window("o5m viewer (phase 3)", 1280, 720);

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
        vk::raii::Instance instance(
            context,
            useValidation ? chain.get<vk::InstanceCreateInfo>() : instanceInfo);

        vk::raii::PhysicalDevices physicalDevices(instance);
        if (physicalDevices.empty()) {
            throw std::runtime_error("no physical device");
        }
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
#endif
        deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        deviceInfo.setQueueCreateInfos(queueInfo)
                  .setPEnabledExtensionNames(deviceExtensions)
                  .setPNext(&dynamicRenderingFeatures);

        O5MDevice device(physicalDevice, vk::raii::Device(physicalDevice, deviceInfo),
                         graphicsFamily);
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
            { vk::CommandPoolCreateFlagBits::eResetCommandBuffer, graphicsFamily });
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
                vk::MemoryPropertyFlagBits::eHostVisible |
                    vk::MemoryPropertyFlagBits::eHostCoherent));
        }

        const std::string shaderDir = O5M_SHADER_DIR;
        const std::vector<std::string> includeDirs = { shaderDir };

        auto loadShader = [&](const std::string& path, vk::ShaderStageFlagBits stage) {
            std::vector<std::string> includeStack;
            std::string source = expandIncludes(readTextFile(path), dirName(path),
                                                includeDirs, includeStack, 0);
            auto shader = std::make_unique<O5MShaderResource>(path, stage, device);
            shader->setData(source.data(), source.size() + 1);
            if (!shader->load()) {
                throw std::runtime_error("shader load failed: " + path);
            }
            return shader;
        };

        auto objectVS = loadShader(shaderDir + "/geometry/object.vert",
                                   vk::ShaderStageFlagBits::eVertex);
        auto objectFS = loadShader(shaderDir + "/geometry/object.frag",
                                   vk::ShaderStageFlagBits::eFragment);
        auto outlineVS = loadShader(shaderDir + "/geometry/outline.vert",
                                    vk::ShaderStageFlagBits::eVertex);
        auto outlineFS = loadShader(shaderDir + "/geometry/outline.frag",
                                    vk::ShaderStageFlagBits::eFragment);
        auto quadVS = loadShader(shaderDir + "/fullscreen/quad.vert",
                                 vk::ShaderStageFlagBits::eVertex);
        auto debugFS = loadShader(shaderDir + "/fullscreen/debug.frag",
                                  vk::ShaderStageFlagBits::eFragment);
        std::cout << "[ok] shaders compiled\n";

        const vk::Format depthFormat = pickDepthFormat(physicalDevice);
        const vk::Format normalFormat = vk::Format::eR8G8B8A8Unorm;
        std::cout << "[ok] depth format " << vk::to_string(depthFormat) << "\n";

        O5MDescriptorAllocator descriptorAllocator(device);
        const DescriptorSetLayoutHandle cameraLayoutHandle =
            descriptorAllocator.createLayout(DescriptorSetLayoutDesc{
                { 0, vk::DescriptorType::eUniformBuffer,
                  vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment },
            });
        const vk::DescriptorSetLayout cameraLayout =
            descriptorAllocator.getLayout(cameraLayoutHandle);
        std::vector<DescriptorSet> cameraSets =
            descriptorAllocator.allocate(cameraLayoutHandle, kInFlight);

        for (uint32_t i = 0; i < kInFlight; ++i) {
            descriptorAllocator.writeBuffer(
                cameraSets[i], 0, vk::DescriptorType::eUniformBuffer,
                { *cameraUBO[i].first, 0, sizeof(CameraUBO) });
        }

        const DescriptorSetLayoutHandle debugLayoutHandle =
            descriptorAllocator.createLayout(DescriptorSetLayoutDesc{
                { 0, vk::DescriptorType::eCombinedImageSampler,
                  vk::ShaderStageFlagBits::eFragment },
            });
        const vk::DescriptorSetLayout debugLayout =
            descriptorAllocator.getLayout(debugLayoutHandle);
        const DescriptorSet debugSet =
            descriptorAllocator.allocate(debugLayoutHandle, 1).front();

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

        vk::VertexInputBindingDescription vertexBinding(
            0, sizeof(O5MMeshResource::Vertex), vk::VertexInputRate::eVertex);
        std::vector<vk::VertexInputAttributeDescription> vertexAttributes = {
            { 0, 0, vk::Format::eR32G32B32Sfloat,
              static_cast<uint32_t>(offsetof(O5MMeshResource::Vertex, m_pos)) },
            { 1, 0, vk::Format::eR32G32B32Sfloat,
              static_cast<uint32_t>(offsetof(O5MMeshResource::Vertex, m_color)) },
            { 2, 0, vk::Format::eR32G32B32Sfloat,
              static_cast<uint32_t>(offsetof(O5MMeshResource::Vertex, m_norm)) },
            { 3, 0, vk::Format::eR32G32B32Sfloat,
              static_cast<uint32_t>(offsetof(O5MMeshResource::Vertex, m_tangent)) },
            { 4, 0, vk::Format::eR32G32Sfloat,
              static_cast<uint32_t>(offsetof(O5MMeshResource::Vertex, m_texCoord)) },
        };

        const vk::StencilOpState baseStencil =
            makeStencilState(vk::CompareOp::eAlways, vk::StencilOp::eReplace, 0xFF, 1);
        const vk::StencilOpState outlineStencil =
            makeStencilState(vk::CompareOp::eNotEqual, vk::StencilOp::eKeep, 0x00, 1);

        O5MPipelineSpec objectSpec{};
        objectSpec.colorFormats = { swapchain.getFormat(), normalFormat };
        objectSpec.depthStencilFormat = depthFormat;
        objectSpec.vertexBindings = { vertexBinding };
        objectSpec.vertexAttributes = vertexAttributes;
        objectSpec.topology = vk::PrimitiveTopology::eTriangleList;
        objectSpec.cullMode = vk::CullModeFlagBits::eBack;
        objectSpec.frontFace = vk::FrontFace::eCounterClockwise;
        objectSpec.depthTestEnable = true;
        objectSpec.depthWriteEnable = true;
        objectSpec.depthCompareOp = vk::CompareOp::eLess;
        objectSpec.stencilTestEnable = true;
        objectSpec.frontStencil = baseStencil;
        objectSpec.backStencil = baseStencil;
        objectSpec.setLayouts = { cameraLayout };
        objectSpec.pushConstants = {
            { vk::ShaderStageFlagBits::eVertex, 0,
              static_cast<uint32_t>(sizeof(OutlinePC)) },
        };

        O5MPipelineSpec outlineSpec = objectSpec;
        outlineSpec.depthTestEnable = false;
        outlineSpec.depthWriteEnable = false;
        outlineSpec.cullMode = vk::CullModeFlagBits::eNone;
        outlineSpec.frontStencil = outlineStencil;
        outlineSpec.backStencil = outlineStencil;

        O5MPipelineSpec debugSpec{};
        debugSpec.colorFormats = { swapchain.getFormat() };
        debugSpec.topology = vk::PrimitiveTopology::eTriangleList;
        debugSpec.cullMode = vk::CullModeFlagBits::eNone;
        debugSpec.setLayouts = { debugLayout };
        debugSpec.pushConstants = {
            { vk::ShaderStageFlagBits::eFragment, 0, 16 },
        };

        O5MPipeline objectPipeline(device, objectSpec);
        objectPipeline.createPipeline(objectVS->getShaderModule(),
                                      objectFS->getShaderModule());
        O5MPipeline outlinePipeline(device, outlineSpec);
        outlinePipeline.createPipeline(outlineVS->getShaderModule(),
                                       outlineFS->getShaderModule());
        O5MPipeline debugPipeline(device, debugSpec);
        debugPipeline.createPipeline(quadVS->getShaderModule(),
                                     debugFS->getShaderModule());
        std::cout << "[ok] pipelines created\n";

        Attachment sceneColor = createAttachment(
            device, swapchain.getFormat(), swapchain.getExtent(),
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
        vk::raii::ImageView depthSampleView = device.createImageView2D(
            *depthBuffer.image, depthFormat, vk::ImageAspectFlagBits::eDepth);

        auto writeDebugDescriptor = [&]() {
            const vk::ImageView source = debugMode == DebugMode::Depth
                ? *depthSampleView
                : *normalBuffer.view;
            descriptorAllocator.writeImage(
                debugSet, 0, vk::DescriptorType::eCombinedImageSampler,
                source, *sampler);
        };
        writeDebugDescriptor();

        auto recreateAttachments = [&]() {
            const vk::Extent2D extent = swapchain.getExtent();
            sceneColor = createAttachment(
                device, swapchain.getFormat(), extent,
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
            depthSampleView = device.createImageView2D(
                *depthBuffer.image, depthFormat, vk::ImageAspectFlagBits::eDepth);
            writeDebugDescriptor();
        };

        std::cout << "[ok] loading model " << modelPath << "\n";
        std::vector<uint8_t> modelData = readBinaryFile(modelPath);
        O5MResourceManager& resourceManager = O5MResourceManager::getInstance();

        O5MModelAsset model = O5MModelAsset::importGLB(
            device, baseName(modelPath), modelData, dirName(modelPath));
        if (!model.isValid() || model.meshes().empty()) {
            throw std::runtime_error("model import produced no mesh: " + modelPath);
        }

        O5MResourceHandle<O5MMeshResource> meshHandle;
        for (const auto& candidate : model.meshes()) {
            if (candidate.get() != nullptr) {
                meshHandle = candidate;
                break;
            }
        }
        if (meshHandle.get() == nullptr) {
            throw std::runtime_error("model import produced no drawable mesh: " + modelPath);
        }

        const vk::Buffer meshVertexBuffer = meshHandle->getVertexBuffer();
        const vk::Buffer meshIndexBuffer = meshHandle->getIndexBuffer();
        std::cout << "[ok] imported " << model.meshes().size() << " meshes, "
                  << model.materials().size() << " materials, "
                  << model.textures().size() << " textures; mesh "
                  << meshHandle->getVertexCount() << " vertices, "
                  << meshHandle->getIndexCount() << " indices, "
                  << meshHandle->getSubMeshes().size() << " submeshes\n";
        if (dumpImportedModel) {
            dumpModel(model);
        }

        // Mix imported and runtime-authored materials. The latter have no
        // relationship to the GLB at all: same mesh, different material.
        struct DemoMaterialDesc {
            glm::vec4 albedo;
            float metallic;
            float roughness;
        };
        const std::array<DemoMaterialDesc, 4> demoDescs = {{
            { { 0.90f, 0.20f, 0.15f, 1.0f }, 0.0f, 0.15f },
            { { 0.20f, 0.80f, 0.35f, 1.0f }, 0.0f, 0.55f },
            { { 0.20f, 0.35f, 0.90f, 1.0f }, 1.0f, 0.20f },
            { { 0.95f, 0.75f, 0.20f, 1.0f }, 1.0f, 0.65f },
        }};

        std::vector<O5MResourceHandle<O5MMaterialResource>> demoMaterials;
        std::vector<uint64_t> demoMaterialIds;
        demoMaterials.reserve(demoDescs.size());
        demoMaterialIds.reserve(demoDescs.size());
        for (size_t i = 0; i < demoDescs.size(); ++i) {
            O5MMaterialResource::MaterialParamsGLSL params{};
            params.albedoFactor = demoDescs[i].albedo;
            params.mrFactor = glm::vec4(demoDescs[i].metallic,
                                        demoDescs[i].roughness, 0.0f, 0.0f);
            params.emissiveFactor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            params.flags = glm::vec4(0.0f, 0.5f, 1.0f, 1.0f);

            auto material = resourceManager.create<O5MMaterialResource>(
                "demo-material-" + std::to_string(i), params);
            demoMaterialIds.push_back(material->getResourceId());
            if (!material->load()) {
                throw std::runtime_error("demo material validation failed");
            }
            demoMaterials.push_back(material);
        }



        std::string meshVBName = "mesh-vertex";
        std::string meshIBName = "mesh-index";
        ResourceInfo meshVBInfo(
            meshVBName,
            static_cast<vk::DeviceSize>(meshHandle->getVertexCount() *
                                        sizeof(O5MMeshResource::Vertex)),
            ResourceSource::Imported);
        ResourceInfo meshIBInfo(
            meshIBName,
            static_cast<vk::DeviceSize>(meshHandle->getIndexCount() *
                                        sizeof(uint32_t)),
            ResourceSource::Imported);

        O5MWorld world;
        const O5MEntityHandle cameraEntity = world.createEntity("camera");
        O5MCameraComponent* camera =
            world.getEntity(cameraEntity)->addComponent<O5MCameraComponent>();

        const O5MEntityHandle rootEntity = world.createEntity("root");
        O5MTransformComponent* rootTransform =
            world.getEntity(rootEntity)->addComponent<O5MTransformComponent>();
        SpinComponent* rootSpin =
            world.getEntity(rootEntity)->addComponent<SpinComponent>();
        rootSpin->speed = 0.35f;

        struct CubeDesc {
            glm::vec3 position;
            glm::vec3 scale;
            glm::vec3 spinAxis;
            float spinSpeed;
        };
        const std::array<CubeDesc, 5> cubes = {{
            { { -2.2f, 0.0f, 0.0f }, { 0.8f, 0.8f, 0.8f },
              { 0.0f, 1.0f, 0.0f }, 1.2f },
            { { 0.0f, 0.0f, -0.8f }, { 1.0f, 1.0f, 1.0f },
              { 1.0f, 1.0f, 0.0f }, -0.8f },
            { { 2.2f, 0.0f, 0.0f }, { 0.7f, 1.2f, 0.7f },
              { 0.0f, 1.0f, 1.0f }, 1.5f },
            { { 4.5f, 0.5f, -12.0f }, { 2.5f, 2.5f, 2.5f },
              { 0.0f, 1.0f, 0.0f }, 0.2f },
            { { 0.0f, 500.0f, 0.0f }, { 1.0f, 1.0f, 1.0f },
              { 0.0f, 1.0f, 0.0f }, 1.0f },
        }};

        for (size_t i = 0; i < cubes.size(); ++i) {
            const O5MEntityHandle entity =
                world.createEntity("cube-" + std::to_string(i));
            O5MEntity* rawEntity = world.getEntity(entity);
            O5MTransformComponent* transform =
                rawEntity->addComponent<O5MTransformComponent>();
            transform->setPosition(cubes[i].position);
            transform->setScale(cubes[i].scale);
            transform->setParent(rootTransform);

            // No override on the first entity: it renders with the imported
            // SubMesh material(s). The rest use runtime-authored materials.
            O5MResourceHandle<O5MMaterialResource> material;
            if (i > 0 && (i - 1) < demoMaterials.size()) {
                material = demoMaterials[i - 1];
            }
            rawEntity->addComponent<O5MMeshComponent>(meshHandle, material);
            SpinComponent* spin = rawEntity->addComponent<SpinComponent>();
            spin->axis = cubes[i].spinAxis;
            spin->speed = cubes[i].spinSpeed;
        }

        world.init();

        O5MEventBus& eventBus = window.getEventBus();
        eventBus.addEvent<O5MKeyboardEvent>([&window](const O5MEvent& event) {
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

        auto buildGraph = [&](uint32_t imageIndex, uint32_t frame,
                              const O5MDrawList& visible) {
            auto graph = std::make_unique<O5MRendergraph>(device);
            const vk::Extent2D extent = swapchain.getExtent();
            const bool debug = debugMode != DebugMode::Off;

            std::string backbufferName = "backbuffer";
            ResourceInfo backbufferInfo(backbufferName, extent,
                                        swapchain.getFormat(),
                                        ResourceSource::Imported);
            graph->importTexture(
                backbufferName, swapchain.getImages()[imageIndex],
                *swapchain.getViews()[imageIndex], swapchain.getFormat(), extent,
                vk::ImageUsageFlagBits::eColorAttachment,
                vk::ImageLayout::eUndefined);

            std::string sceneColorName = "scene-color";
            ResourceInfo sceneColorInfo(sceneColorName, extent,
                                        sceneColor.format,
                                        ResourceSource::Imported);
            graph->importTexture(
                sceneColorName, *sceneColor.image, *sceneColor.view,
                sceneColor.format, extent,
                vk::ImageUsageFlagBits::eColorAttachment |
                    vk::ImageUsageFlagBits::eSampled,
                vk::ImageLayout::eUndefined);

            std::string normalName = "normal-buffer";
            ResourceInfo normalInfo(normalName, extent, normalBuffer.format,
                                    ResourceSource::Imported);
            graph->importTexture(
                normalName, *normalBuffer.image, *normalBuffer.view,
                normalBuffer.format, extent,
                vk::ImageUsageFlagBits::eColorAttachment |
                    vk::ImageUsageFlagBits::eSampled,
                vk::ImageLayout::eUndefined);

            std::string depthName = "depth-buffer";
            ResourceInfo depthInfo(depthName, extent, depthBuffer.format,
                                   ResourceSource::Imported);
            graph->importTexture(
                depthName, *depthBuffer.image, *depthBuffer.view,
                depthBuffer.format, extent,
                vk::ImageUsageFlagBits::eDepthStencilAttachment |
                    vk::ImageUsageFlagBits::eSampled,
                vk::ImageLayout::eUndefined);

            std::string cameraName = "camera";
            ResourceInfo cameraInfo(cameraName,
                                    static_cast<vk::DeviceSize>(sizeof(CameraUBO)),
                                    ResourceSource::Imported);
            graph->importBuffer(cameraName, *cameraUBO[frame].first,
                                sizeof(CameraUBO),
                                vk::BufferUsageFlagBits::eUniformBuffer);

            graph->importBuffer(
                meshVBName, meshVertexBuffer,
                std::get<BufferInfo>(meshVBInfo.info).size,
                vk::BufferUsageFlagBits::eVertexBuffer |
                    vk::BufferUsageFlagBits::eIndexBuffer);
            graph->importBuffer(
                meshIBName, meshIndexBuffer,
                std::get<BufferInfo>(meshIBInfo.info).size,
                vk::BufferUsageFlagBits::eVertexBuffer |
                    vk::BufferUsageFlagBits::eIndexBuffer);

            graph->markOutput(backbufferInfo.handle);

            const ResourceInfo* colorInfo = debug ? &sceneColorInfo : &backbufferInfo;

            graph->addGraphicPass("geometry",
                [colorTarget = *colorInfo, normalInfo, depthInfo, cameraInfo,
                 meshVBInfo, meshIBInfo, debug](O5MPassBuilder& builder) mutable {
                    builder.write(colorTarget,
                                  debug ? TexWrite::ColorStore : TexWrite::Present);
                    builder.write(normalInfo, TexWrite::ColorStore);
                    builder.write(depthInfo, TexWrite::Depth);
                    builder.read(cameraInfo, BufRead::Uniform);
                    builder.read(meshVBInfo, BufRead::VertexIndex);
                    builder.read(meshIBInfo, BufRead::VertexIndex);
                },
                [colorInfo = *colorInfo, normalInfo, depthInfo, extent,
                 visible, frame, cameraSets, &objectPipeline, &outlinePipeline]
                (O5MRenderContext& context, vk::raii::CommandBuffer& cmd) mutable {
                    O5MBeginInfo beginInfo{};
                    beginInfo.colorViews = {
                        context.getImageView(colorInfo.handle),
                        context.getImageView(normalInfo.handle),
                    };
                    beginInfo.depthView = context.getImageView(depthInfo.handle);
                    beginInfo.stencilView = beginInfo.depthView;
                    beginInfo.extent = extent;
                    O5MPipeline::begin(cmd, beginInfo);

                    objectPipeline.bindPipeline(cmd);
                    objectPipeline.bindDescriptorSet(cmd, cameraSets[frame].set);
                    for (const O5MDrawItem& item : visible) {
                        const std::array<vk::Buffer, 1> buffers = { item.vertexBuffer };
                        const std::array<vk::DeviceSize, 1> offsets = { 0 };
                        objectPipeline.pushConstant(cmd, vk::ShaderStageFlagBits::eVertex,
                                                    item.model);
                        cmd.bindVertexBuffers(0, buffers, offsets);
                        cmd.bindIndexBuffer(item.indexBuffer, 0,
                                            vk::IndexType::eUint32);
                        cmd.drawIndexed(item.indexCount, 1, item.firstIndex,
                                        item.vertexOffset, 0);
                    }

                    outlinePipeline.bindPipeline(cmd);
                    outlinePipeline.bindDescriptorSet(cmd, cameraSets[frame].set);
                    for (const O5MDrawItem& item : visible) {
                        const OutlinePC pushConstant{ item.model, 0.04f };
                        const std::array<vk::Buffer, 1> buffers = { item.vertexBuffer };
                        const std::array<vk::DeviceSize, 1> offsets = { 0 };
                        outlinePipeline.pushConstant(
                            cmd, vk::ShaderStageFlagBits::eVertex, pushConstant);
                        cmd.bindVertexBuffers(0, buffers, offsets);
                        cmd.bindIndexBuffer(item.indexBuffer, 0,
                                            vk::IndexType::eUint32);
                        cmd.drawIndexed(item.indexCount, 1, item.firstIndex,
                                        item.vertexOffset, 0);
                    }

                    O5MPipeline::end(cmd);
                });

            if (debug) {
                ResourceInfo debugSource =
                    debugMode == DebugMode::Depth ? depthInfo : normalInfo;
                const float mode = debugMode == DebugMode::Depth ? 0.0f : 1.0f;
                const float nearPlane = camera->getNearPlane();
                const float farPlane = camera->getFarPlane();

                graph->addGraphicPass("debug",
                    [debugSource, backbufferInfo](O5MPassBuilder& builder) mutable {
                        builder.read(debugSource, TexRead::Sampled);
                        builder.write(backbufferInfo, TexWrite::Present);
                    },
                    [backbufferInfo, debugSource, debugSet, extent, mode,
                     nearPlane, farPlane, &debugPipeline]
                    (O5MRenderContext& context, vk::raii::CommandBuffer& cmd) mutable {
                        O5MBeginInfo beginInfo{};
                        beginInfo.colorViews = {
                            context.getImageView(backbufferInfo.handle)
                        };
                        beginInfo.extent = extent;
                        O5MPipeline::begin(cmd, beginInfo);
                        debugPipeline.bindPipeline(cmd);
                        debugPipeline.bindDescriptorSet(cmd, debugSet.set);
                        const glm::vec4 params(mode, nearPlane, farPlane, 20.0f);
                        debugPipeline.pushConstant(cmd,
                                                   vk::ShaderStageFlagBits::eFragment,
                                                   params);
                        cmd.draw(3, 1, 0, 0);
                        O5MPipeline::end(cmd);
                    });
            }

            graph->compile();
            return graph;
        };

        uint32_t frameIndex = 0;
        uint32_t sweep = 0;
        float elapsedSeconds = 0.0f;
        auto lastTime = std::chrono::high_resolution_clock::now();
        auto fpsWindowStart = lastTime;
        uint32_t fpsCounter = 0;
        O5MCullingSystem cullingSystem;

        std::cout << "[ok] viewer running (frames-in-flight=" << kInFlight
                  << ") -- ESC to quit\n";

        while (!window.shouldClose() && frameIndex < smokeFrames) {
            window.pollEvents();

            auto now = std::chrono::high_resolution_clock::now();
            const float deltaTime = std::chrono::duration<float>(now - lastTime).count();
            lastTime = now;
            elapsedSeconds += deltaTime;

            const float aspect = static_cast<float>(swapchain.getExtent().width) /
                                 static_cast<float>(swapchain.getExtent().height);
            camera->setAspectRatio(aspect);
            world.update(deltaTime);

            O5MDrawList drawItems;
            world.forEachEntity([&](O5MEntity& entity) {
                O5MTransformComponent* transform =
                    entity.getComponent<O5MTransformComponent>();
                O5MMeshComponent* meshComponent =
                    entity.getComponent<O5MMeshComponent>();
                if (!transform || !meshComponent || !meshComponent->isVisible()) {
                    return;
                }
                O5MMeshResource* mesh = meshComponent->mesh().get();
                if (!mesh || !mesh->isloaded()) {
                    return;
                }

                O5MMaterialResource* overrideMaterial =
                    meshComponent->material().get();
                const auto subMeshes = mesh->getSubMeshes();
                if (subMeshes.empty()) {
                    O5MDrawItem item{};
                    item.vertexBuffer = mesh->getVertexBuffer();
                    item.indexBuffer = mesh->getIndexBuffer();
                    item.indexCount = mesh->getIndexCount();
                    item.firstIndex = 0;
                    item.vertexOffset = 0;
                    item.model = transform->getWorldMatrix();
                    item.boundsSphere = mesh->getBoundsSphere();
                    item.material = overrideMaterial;
                    drawItems.push_back(item);
                    return;
                }

                for (const O5MMeshResource::SubMesh& subMesh : subMeshes) {
                    if (subMesh.indexCount == 0) {
                        continue;
                    }
                    O5MDrawItem item{};
                    item.vertexBuffer = mesh->getVertexBuffer();
                    item.indexBuffer = mesh->getIndexBuffer();
                    item.indexCount = subMesh.indexCount;
                    item.firstIndex = subMesh.firstIndex;
                    item.vertexOffset = 0;
                    item.model = transform->getWorldMatrix();
                    item.boundsSphere = mesh->getBoundsSphere();
                    item.material = overrideMaterial
                        ? overrideMaterial
                        : subMesh.material.get();
                    drawItems.push_back(item);
                }
            });

            const glm::mat4 viewProj =
                camera->getProjectionMatrix() * camera->getViewMatrix();
            const O5MDrawList visible = cullingSystem.cull(viewProj, drawItems);
            if (frameIndex % 120 == 0) {
                std::cout << "[draw] " << visible.size() << "/" << drawItems.size()
                          << "\n";
            }

            const uint32_t frame = frameIndex % kInFlight;

            ++sweep;
            const bool forceRebuild =
                rebuildEvery != 0 && sweep % rebuildEvery == 0;
            if (window.takeResizeFlag() || forceRebuild) {
                rebuildSwapchain();
                continue;
            }

            device.getDevice().waitForFences(*frameFences[frame], true, UINT64_MAX);

            auto [imageIndex, needsRebuild] = swapchain.acquire(imageAvailable[frame]);
            if (needsRebuild) {
                rebuildSwapchain();
                continue;
            }

            device.getDevice().resetFences(*frameFences[frame]);

            {
                const glm::mat4 view = camera->getViewMatrix();
                const glm::mat4 projection = camera->getProjectionMatrix();
                const CameraUBO ubo{
                    .view = view,
                    .proj = projection,
                    .camPos = glm::vec4(glm::vec3(glm::inverse(view)[3]), 1.0f),
                    .params = { elapsedSeconds, aspect,
                                camera->getNearPlane(), camera->getFarPlane() },
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

            if (!swapchain.present(*device.getQueue(), imageIndex,
                                   renderFinishedPool[imageIndex])) {
                rebuildSwapchain();
            }

            ++frameIndex;
            ++fpsCounter;
            if (std::chrono::duration<float>(now - fpsWindowStart).count() >= 1.0f) {
                std::cout << "[fps] " << fpsCounter
                          << " (" << (1000.0f / std::max(1u, fpsCounter))
                          << " ms/frame)\n";
                fpsCounter = 0;
                fpsWindowStart = now;
            }
        }

        eventBus.removeEvent<O5MKeyboardEvent>(camKeyToken);
        eventBus.removeEvent<O5MMouseMovedEvent>(camMoveToken);
        eventBus.removeEvent<O5MMouseScrolledEvent>(camScrollToken);

        for (uint32_t i = 0; i < kInFlight; ++i) {
            device.getDevice().waitForFences(*frameFences[i], true, UINT64_MAX);
        }
        device.getQueue().waitIdle();
        for (uint64_t materialId : demoMaterialIds) {
            resourceManager.release(materialId);
        }
        model.release();
        std::cout << "[ok] " << frameIndex << " frames rendered, clean exit\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << "\n";
        return 1;
    }
}
