#include "VulkanDevice.h"

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include <cassert>
#include <cstring>
#include <algorithm>

// ===========================================================================
// VK_CHECK — local assertion macro for Vulkan result codes
// ===========================================================================
// Teaching note: unlike OpenGL where errors are polled per-call via
// glGetError(), Vulkan returns VkResult from every vkCreate*/vkAllocate*
// function. You MUST check every return value — a missed VK_ERROR_OUT_OF_HOST_MEMORY
// leads to a crash many frames later with no clear root cause.
// ===========================================================================
#define VK_CHECK(result, msg)                                               \
    do {                                                                    \
        VkResult _res = (result);                                          \
        if (_res != VK_SUCCESS) {                                          \
            spdlog::error("Vulkan error: {} — VkResult={}", msg,            \
                          static_cast<int>(_res));                         \
            assert(false && msg);                                          \
        }                                                                  \
    } while (0)

// ===========================================================================
// Debug messenger callback (VK_EXT_debug_utils / Vulkan 1.1+)
// ===========================================================================
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* /*pUserData*/)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        spdlog::warn("[Vulkan] {}", pCallbackData->pMessage);
    }
    return VK_FALSE; // don't abort
}

// ===========================================================================
// Dynamically load vkCreateDebugUtilsMessengerEXT (extension function)
// ===========================================================================
static VkResult createDebugMessenger(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    VkDebugUtilsMessengerEXT* pMessenger)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func) {
        return func(instance, pCreateInfo, nullptr, pMessenger);
    }
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void destroyDebugMessenger(
    VkInstance instance,
    VkDebugUtilsMessengerEXT messenger)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func) {
        func(instance, messenger, nullptr);
    }
}

// ===========================================================================
// init() — orchestrate full Vulkan bootstrap
// ===========================================================================
bool VulkanDevice::init(GLFWwindow* window)
{
    createInstance();
    pickPhysicalDevice();
    createLogicalDevice();
    createSurface(window);
    createSwapchain();
    createDefaultSampler();
    return true;
}

// ===========================================================================
// createInstance() — VkInstance + validation layers + debug messenger
// ===========================================================================
void VulkanDevice::createInstance()
{
    // --- Application info ---
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "o5m Vulkan RHI";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "o5m";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    // --- Get required GLFW extensions ---
    uint32_t glfwExtCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtCount);

    // --- macOS: MoltenVK requires portability enumeration ---
    // Teaching note: MoltenVK is a Vulkan-over-Metal translation layer.
    // Metal's graphics API surface differs from Vulkan's, so VK_KHR_portability_subset
    // extensions bridge the gap. Without VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
    // vkEnumeratePhysicalDevices returns 0 devices on macOS.
#ifdef __APPLE__
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#endif

    // --- Validation layers ---
    // Teaching note: validation layers are Vulkan's equivalent of GL_DEBUG_OUTPUT /
    // glDebugMessageCallback. They catch misused API calls, resource leaks, and
    // synchronization errors BEFORE the GPU sees them — far more thorough than
    // GL's error model. On macOS with MoltenVK, validation layers may not ship
    // with the SDK; we warn and continue rather than abort.
    std::vector<const char*> layers;
    const char* validationLayer = "VK_LAYER_KHRONOS_validation";
    {
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        bool found = false;
        for (const auto& layer : availableLayers) {
            if (strcmp(layer.layerName, validationLayer) == 0) {
                found = true;
                break;
            }
        }
        if (found) {
            layers.push_back(validationLayer);
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            m_validationEnabled = true;
            spdlog::info("[Vulkan] Validation layers enabled: {}", validationLayer);
        } else {
            spdlog::warn("[Vulkan] Validation layer '{}' not available — skipping", validationLayer);
        }
    }

    // --- Create VkInstance ---
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();

#ifdef __APPLE__
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_instance), "vkCreateInstance");

    // --- Debug messenger (only if validation layers are enabled) ---
    if (m_validationEnabled) {
        VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
        debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugInfo.pfnUserCallback = debugCallback;

        VkResult res = createDebugMessenger(m_instance, &debugInfo, &m_debugMessenger);
        if (res == VK_SUCCESS) {
            spdlog::info("[Vulkan] Debug messenger attached");
        } else {
            spdlog::warn("[Vulkan] Debug messenger creation failed (VkResult={})",
                         static_cast<int>(res));
        }
    }
}

// ===========================================================================
// pickPhysicalDevice() — select a GPU
// ===========================================================================
// Teaching note: Vulkan enumerates ALL GPUs in the system explicitly.
// OpenGL hides this behind the current context — you get whatever GPU the
// window system assigned. Vulkan lets you query every GPU's properties,
// features, queue families, and memory heaps before choosing. This is why
// multi-GPU setups (eGPU, heterogeneous rendering) are natural in Vulkan.
// ===========================================================================
void VulkanDevice::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr),
             "vkEnumeratePhysicalDevices count");
    if (deviceCount == 0) {
        spdlog::error("[Vulkan] No GPU with Vulkan support found");
        assert(false);
        return;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data()),
             "vkEnumeratePhysicalDevices");

    // Score: discrete GPU > integrated > others
    int bestScore = -1;
    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;

    for (const auto& device : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);

        int score = 0;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score = 100;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score = 50;

        // On macOS, check for portability_subset support
#ifdef __APPLE__
        {
            uint32_t extCount = 0;
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
            std::vector<VkExtensionProperties> availableExts(extCount);
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, availableExts.data());

            bool hasPortability = false;
            for (const auto& ext : availableExts) {
                if (strcmp(ext.extensionName, "VK_KHR_portability_subset") == 0) {
                    hasPortability = true;
                    break;
                }
            }
            if (!hasPortability) {
                spdlog::warn("[Vulkan] Device '{}' lacks VK_KHR_portability_subset — skipping",
                             props.deviceName);
                continue;
            }
        }
#endif

        spdlog::info("[Vulkan] GPU candidate: {} (type={}, score={})",
                     props.deviceName, static_cast<int>(props.deviceType), score);

        if (score > bestScore) {
            bestScore = score;
            bestDevice = device;
        }
    }

    if (bestDevice == VK_NULL_HANDLE) {
        // Fallback: take the first device
        bestDevice = devices[0];
        spdlog::warn("[Vulkan] No suitable GPU found via scoring — using first available");
    }

    m_physicalDevice = bestDevice;

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    spdlog::info("[Vulkan] Selected GPU: {}", props.deviceName);
}

// ===========================================================================
// createLogicalDevice() — VkDevice + graphics queue
// ===========================================================================
// Teaching note: VkPhysicalDevice is a handle to a physical GPU. VkDevice is
// a "logical device" — a software abstraction that owns queues, memory, and
// resources. You can create multiple VkDevices from the same physical GPU
// (e.g., one per thread). The logical device is where you specify which
// features and extensions you actually want to use.
//
// Queue families: GPUs expose different queue types (graphics, compute,
// transfer, sparse binding). We find the first family that supports graphics
// and use it for everything. A more advanced setup would use separate
// transfer/compute queues for async work.
// ===========================================================================
void VulkanDevice::createLogicalDevice()
{
    // --- Find a queue family that supports graphics ---
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

    m_queueFamilyIndex = 0;
    bool found = false;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            m_queueFamilyIndex = i;
            found = true;
            break;
        }
    }
    assert(found && "No queue family with graphics support found");

    // --- Device extensions ---
    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
#ifdef __APPLE__
    deviceExtensions.push_back("VK_KHR_portability_subset");
#endif

    // --- Queue create info ---
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = m_queueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    // --- Device features (none needed for now) ---
    VkPhysicalDeviceFeatures deviceFeatures{};

    // --- Create VkDevice ---
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    createInfo.pEnabledFeatures = &deviceFeatures;

    VK_CHECK(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device),
             "vkCreateDevice");

    // --- Retrieve the queue handle ---
    vkGetDeviceQueue(m_device, m_queueFamilyIndex, 0, &m_graphicsQueue);

    spdlog::info("[Vulkan] Logical device created (queue family={})", m_queueFamilyIndex);
}

// ===========================================================================
// createSurface() — VkSurfaceKHR via GLFW
// ===========================================================================
void VulkanDevice::createSurface(GLFWwindow* window)
{
    VK_CHECK(glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface),
             "glfwCreateWindowSurface");
}

// ===========================================================================
// createSwapchain() — VkSwapchainKHR + image views
// ===========================================================================
// Teaching note: the swapchain is Vulkan's presentation mechanism — a ring
// buffer of images that the GPU draws into and the display reads from.
// OpenGL hides this behind glfwSwapBuffers + the default framebuffer (0).
// Vulkan makes it explicit: you choose format, color space, present mode,
// and image count. This gives you control over vsync, HDR, and latency
// that GL's wgl/glX swap interval doesn't provide.
//
// Fixed extent {800, 600} — no resize handling per C2 scope lock.
// ===========================================================================
void VulkanDevice::createSwapchain()
{
    // --- Surface capabilities ---
    VkSurfaceCapabilitiesKHR capabilities;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &capabilities),
             "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    // --- Surface format ---
    // Prefer sRGB for correct color space handling (gamma-aware rendering).
    // Fall back to UNORM if sRGB is not available.
    uint32_t formatCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr),
             "vkGetPhysicalDeviceSurfaceFormatsKHR count");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data()),
             "vkGetPhysicalDeviceSurfaceFormatsKHR");

    VkSurfaceFormatKHR surfaceFormat = formats[0]; // default
    for (const auto& fmt : formats) {
        if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB &&
            fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = fmt;
            break;
        }
    }
    // Fallback: if sRGB not found, look for UNORM
    if (surfaceFormat.format != VK_FORMAT_B8G8R8A8_SRGB) {
        for (const auto& fmt : formats) {
            if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM &&
                fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                surfaceFormat = fmt;
                break;
            }
        }
    }

    m_swapchainFormat = surfaceFormat.format;

    // --- Present mode ---
    // VK_PRESENT_MODE_FIFO_KHR (vsync) is guaranteed to be available.
    // VK_PRESENT_MODE_MAILBOX_KHR (triple buffering, no tearing) is preferred
    // but not guaranteed — fall back to FIFO.
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    {
        uint32_t modeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &modeCount, nullptr);
        std::vector<VkPresentModeKHR> modes(modeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &modeCount, modes.data());

        for (auto mode : modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                presentMode = mode;
                break;
            }
        }
    }

    // --- Extent (fixed 800x600) ---
    m_swapchainExtent = {800, 600};

    // --- Image count ---
    uint32_t imageCount = std::max(3u, capabilities.minImageCount);
    if (capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }

    // --- Create swapchain ---
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = m_swapchainExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VK_CHECK(vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain),
             "vkCreateSwapchainKHR");

    // --- Retrieve swapchain images ---
    uint32_t actualImageCount = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualImageCount, nullptr),
             "vkGetSwapchainImagesKHR count");
    m_swapchainImages.resize(actualImageCount);
    VK_CHECK(vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualImageCount, m_swapchainImages.data()),
             "vkGetSwapchainImagesKHR");

    spdlog::info("[Vulkan] Swapchain: {} images, {}x{}, format={}",
                 actualImageCount, m_swapchainExtent.width, m_swapchainExtent.height,
                 static_cast<int>(m_swapchainFormat));

    // --- Create image views ---
    m_swapchainImageViews.resize(actualImageCount);
    for (uint32_t i = 0; i < actualImageCount; i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_swapchainFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(m_device, &viewInfo, nullptr, &m_swapchainImageViews[i]),
                 "vkCreateImageView");
    }
}

// ===========================================================================
// createDefaultSampler() — linear filtering, repeat wrap, no anisotropy
// ===========================================================================
// Teaching note: In OpenGL, sampler state lives inside the texture object
// (glTexParameteri on a bound texture). Vulkan separates the sampler
// (VkSampler) from the image (VkImage/VkImageView). This means:
//   1. One image can be sampled with different filters in different passes.
//   2. Sampler objects are reusable across many textures.
//   3. This matches how GPUs work internally — samplers and images are
//      separate resources in the shader pipeline.
// ===========================================================================
void VulkanDevice::createDefaultSampler()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.mipLodBias = 0.0f;

    VK_CHECK(vkCreateSampler(m_device, &samplerInfo, nullptr, &m_defaultSampler),
             "vkCreateSampler");
}

// ===========================================================================
// findMemoryType() — locate a suitable memory heap index
// ===========================================================================
// Teaching note: Vulkan divides GPU memory into heaps (VRAM, system RAM) and
// types (within each heap). Each VkMemoryRequirements from a buffer/image
// tells you which type bits are allowed (typeFilter). You then find a type
// with the desired properties (DEVICE_LOCAL for fast GPU access,
// HOST_VISIBLE | HOST_COHERENT for CPU-mapped upload). OpenGL's glBufferData
// + usage hints abstract this entirely — but at the cost of unpredictable
// driver heuristics.
// ===========================================================================
uint32_t VulkanDevice::findMemoryType(uint32_t typeFilter,
                                       VkMemoryPropertyFlags props) const
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }

    spdlog::error("[Vulkan] No suitable memory type found (filter=0x{:x}, props=0x{:x})",
                  typeFilter, static_cast<uint32_t>(props));
    assert(false);
    return 0;
}

// ===========================================================================
// shutdown() — reverse-order teardown
// ===========================================================================
// Teaching note: Vulkan cleanup is strictly ordered — you must destroy
// objects in reverse creation order because later objects may reference
// earlier ones. Always vkDeviceWaitIdle first to ensure no GPU work is
// in flight when you start destroying resources.
// ===========================================================================
void VulkanDevice::shutdown()
{
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }

    // Destroy sampler
    if (m_defaultSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_defaultSampler, nullptr);
        m_defaultSampler = VK_NULL_HANDLE;
    }

    // Destroy swapchain image views
    for (auto view : m_swapchainImageViews) {
        vkDestroyImageView(m_device, view, nullptr);
    }
    m_swapchainImageViews.clear();
    m_swapchainImages.clear();

    // Destroy swapchain
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }

    // Destroy surface
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    // Destroy logical device
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    // Destroy debug messenger (before instance!)
    if (m_debugMessenger != VK_NULL_HANDLE) {
        destroyDebugMessenger(m_instance, m_debugMessenger);
        m_debugMessenger = VK_NULL_HANDLE;
    }

    // Destroy instance (last)
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}
