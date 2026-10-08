#ifndef __O5MSWAPCHAIN__H
#define __O5MSWAPCHAIN__H

// Swapchain ownership + acquire/present orchestration (Phase 2 scaffold,
// AI-owned). Object CREATION stays in O5MDevice (centralized device law);
// this class only holds the bundle and drives acquire/present/rebuild.
//
// Sync contract for the frame loop (see docs/phase2-window-frameloop-design.md §1):
//   acquire(imageAvailable)  -- semaphore signaled when the image is writable
//   present(index, renderFinished) -- semaphore waited before the image shows

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "RHI/O5MDevice.h"

class O5MSwapchain {
public:
    O5MSwapchain(O5MDevice& device, vk::raii::SurfaceKHR& surface,
                 vk::Extent2D requestedExtent);

    O5MSwapchain(const O5MSwapchain&) = delete;
    O5MSwapchain& operator=(const O5MSwapchain&) = delete;

    // destroy + recreate at a new size; blocks until the GPU is idle
    void rebuild(vk::Extent2D requestedExtent);

    struct AcquireResult {
        uint32_t imageIndex = 0;
        bool needsRebuild = false;   // out-of-date / suboptimal
    };
    AcquireResult acquire(vk::raii::Semaphore& imageAvailable);

    // returns false when the swapchain needs a rebuild
    bool present(vk::Queue queue, uint32_t imageIndex,
                 vk::raii::Semaphore& renderFinished);

    vk::Format getFormat(void) const { return m_format; }
    vk::Extent2D getExtent(void) const { return m_extent; }
    uint32_t getImageCount(void) const { return m_imageCount; }
    // views/images aligned with acquire()'s imageIndex -- for importTexture
    const std::vector<vk::raii::ImageView>& getViews(void) const { return m_views; }
    const std::vector<vk::Image>& getImages(void) const { return m_images; }
    vk::raii::SwapchainKHR& getHandle(void) { return m_swapchain; }

private:
    void create(vk::Extent2D requestedExtent);

    O5MDevice& m_device;
    vk::raii::SurfaceKHR& m_surface;

    vk::raii::SwapchainKHR m_swapchain = nullptr;
    vk::Format m_format = vk::Format::eUndefined;
    vk::Extent2D m_extent;
    uint32_t m_imageCount = 0;
    std::vector<vk::Image> m_images;
    std::vector<vk::raii::ImageView> m_views;
};

#endif // !__O5MSWAPCHAIN__H
