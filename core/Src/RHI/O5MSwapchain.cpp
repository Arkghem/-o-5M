#include "RHI/O5MSwapchain.h"

O5MSwapchain::O5MSwapchain(O5MDevice& device, vk::raii::SurfaceKHR& surface,
                           vk::Extent2D requestedExtent)
    : m_device(device), m_surface(surface) {
    create(requestedExtent);
}

void O5MSwapchain::create(vk::Extent2D requestedExtent) {
    O5MDevice::SwapchainBundle bundle = m_device.createSwapchain(m_surface, requestedExtent);
    m_swapchain = std::move(bundle.swapchain);
    m_format = bundle.format;
    m_extent = bundle.extent;
    m_imageCount = bundle.imageCount;
    m_images = std::move(bundle.images);
    m_views = std::move(bundle.views);
}

void O5MSwapchain::rebuild(vk::Extent2D requestedExtent) {
    // everything derived from the old swapchain (images, views, the graph's
    // imported backbuffer handles) must be gone before we destroy it
    m_device.getQueue().waitIdle();
    m_views.clear();
    m_images.clear();
    m_swapchain = nullptr;
    create(requestedExtent);
}

O5MSwapchain::AcquireResult O5MSwapchain::acquire(vk::raii::Semaphore& imageAvailable) {
    try {
        // UINT64_MAX: presentation engine lags are expected (FIFO), not fatal
        auto [result, index] = m_swapchain.acquireNextImage(UINT64_MAX, *imageAvailable);
        if (result == vk::Result::eErrorOutOfDateKHR)
            return { 0, true };
        return { index, result == vk::Result::eSuboptimalKHR };
    } catch (const vk::OutOfDateKHRError&) {
        return { 0, true };
    }
}

bool O5MSwapchain::present(vk::Queue queue, uint32_t imageIndex,
                           vk::raii::Semaphore& renderFinished) {
    vk::PresentInfoKHR info;
    info.setWaitSemaphores(*renderFinished);
    info.setSwapchains(*m_swapchain);
    info.setImageIndices(imageIndex);
    try {
        vk::Result result = queue.presentKHR(info);
        if (result == vk::Result::eSuboptimalKHR)
            return false;
        return true;
    } catch (const vk::OutOfDateKHRError&) {
        return false;
    }
}
