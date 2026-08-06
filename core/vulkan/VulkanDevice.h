#ifndef __VULKANDEVICE_H__
#define __VULKANDEVICE_H__

// ===========================================================================
// VulkanDevice — Vulkan RHI backend singleton (device + swapchain + surface)
// ===========================================================================
//
// Teaching note: unlike OpenGL where a GL context is implicitly global and
// managed by the window system (glfwMakeContextCurrent), Vulkan requires you
// to explicitly create every object: VkInstance (driver connection) →
// VkPhysicalDevice (GPU handle) → VkDevice (logical device with queues) →
// VkSurfaceKHR (window surface) → VkSwapchainKHR (presentable images).
//
// This class owns the full Vulkan bootstrap chain. All VK* resource classes
// (VKBuffer, VKTexture, etc.) will reference this singleton for memory
// allocation, queue submission, and swapchain queries.
//
// Design mirror: GLRhi::init(GLFWwindow*) — same call pattern, different
// GPU API underneath. The IRhi interface is not implemented here (VulkanRhi
// will wrap this device and implement IRhi later).
// ===========================================================================

#include <vulkan/vulkan.h>
#include <vector>

struct GLFWwindow;

class VulkanDevice {
public:
    /// Initialize the full Vulkan bootstrap chain.
    /// Mirrors GLRhi::init(GLFWwindow*) call pattern — returns true on success.
    bool init(GLFWwindow* window);

    /// Reverse-order teardown: wait idle → destroy all Vulkan objects.
    void shutdown();

    // --- Queries (used by VK* resource classes) ---

    VkDevice         device()          const { return m_device; }
    VkQueue          queue()           const { return m_graphicsQueue; }
    uint32_t         queueFamilyIndex() const { return m_queueFamilyIndex; }
    VkFormat         swapchainFormat()  const { return m_swapchainFormat; }
    VkExtent2D       swapchainExtent()  const { return m_swapchainExtent; }
    uint32_t         imageCount()       const { return static_cast<uint32_t>(m_swapchainImageViews.size()); }
    VkImageView      swapchainImageView(uint32_t i) const { return m_swapchainImageViews[i]; }
    VkSwapchainKHR   swapchain()        const { return m_swapchain; }
    VkSurfaceKHR     surface()           const { return m_surface; }

    // --- Memory helper (used by VKMemoryAllocator) ---

    /// Teaching note: Vulkan exposes physical memory heaps/types explicitly.
    /// Unlike GL where the driver hides memory allocation, Vulkan requires you
    /// to query VkPhysicalDeviceMemoryProperties and pick a memory type index
    /// that satisfies both the required typeFilter bits AND the desired
    /// propertyFlags (e.g. DEVICE_LOCAL for GPU-only, HOST_VISIBLE for upload).
    ///
    /// typeFilter is a bitmask from VkMemoryRequirements::memoryTypeBits.
    /// Returns the index of the first matching memory type.
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const;

    // --- Default sampler (used by VKShaderResourceBindings for texture binds) ---

    /// Teaching note: In OpenGL, sampler state (filtering, wrapping, anisotropy)
    /// lives inside the texture object (glTexParameteri). Vulkan separates image
    /// (VkImage/VkImageView) from sampling (VkSampler). This is closer to how
    /// GPUs actually work: the same image memory can be sampled with different
    /// filters depending on the shader's needs.
    VkSampler defaultSampler() const { return m_defaultSampler; }

private:
    // --- Bootstrap steps (called sequentially by init()) ---

    void createInstance();                // VkInstance + validation layers + debug messenger
    void pickPhysicalDevice();           // Select GPU (prefer discrete)
    void createLogicalDevice();          // VkDevice + graphics queue
    void createSurface(GLFWwindow* window); // VkSurfaceKHR via GLFW
    void createSwapchain();              // VkSwapchainKHR + image views
    void createDefaultSampler();         // Linear filtering, repeat wrap

    // --- Vulkan object handles ---

    VkInstance               m_instance       = VK_NULL_HANDLE;
    VkPhysicalDevice         m_physicalDevice = VK_NULL_HANDLE;
    VkDevice                 m_device         = VK_NULL_HANDLE;
    VkQueue                  m_graphicsQueue  = VK_NULL_HANDLE;
    uint32_t                 m_queueFamilyIndex = 0;
    VkSurfaceKHR             m_surface        = VK_NULL_HANDLE;
    VkSwapchainKHR           m_swapchain      = VK_NULL_HANDLE;
    std::vector<VkImage>     m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
    VkFormat                 m_swapchainFormat  = VK_FORMAT_UNDEFINED;
    VkExtent2D               m_swapchainExtent  = {800, 600};
    VkDebugUtilsMessengerEXT m_debugMessenger   = VK_NULL_HANDLE;
    VkSampler                m_defaultSampler   = VK_NULL_HANDLE;

    bool m_validationEnabled = false;        // true if layers were successfully loaded
};

#endif // __VULKANDEVICE_H__
