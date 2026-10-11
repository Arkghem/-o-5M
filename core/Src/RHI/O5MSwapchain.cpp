#include "RHI/O5MSwapchain.h"

#include <limits>
#include <stdexcept>

O5MSwapchain::O5MSwapchain(O5MDevice& device, vk::raii::SurfaceKHR& surface,
                           vk::Extent2D requestedExtent)
    : m_device(device), m_surface(surface) {
    create(requestedExtent);
}

void O5MSwapchain::create(vk::Extent2D requestedExtent) {
    O5MDevice::SwapchainBundle bundle = m_device.createSwapchain(m_surface, requestedExtent);
    m_swapchain = std::move(bundle.swapchain);
    m_extent = bundle.extent;
    m_imageCount = bundle.imageCount;
    m_images = bundle.images;
    m_views = std::move(bundle.views);
    m_format = bundle.format;
}

void O5MSwapchain::rebuild(vk::Extent2D requestedExtent) {
    m_device.getQueue().waitIdle();
    m_views.clear();
    m_swapchain = nullptr;
    create(requestedExtent);
}

O5MSwapchain::AcquireResult O5MSwapchain::acquire(vk::raii::Semaphore& imageAvailable) {
    try {
        auto [result, index] = m_swapchain.acquireNextImage(
            std::numeric_limits<uint64_t>::max(),
            *imageAvailable
        );
        switch (result) {
            case vk::Result::eSuccess:
                return { index, false };
            case vk::Result::eSuboptimalKHR:
                return { index, true };
            case vk::Result::eTimeout:
            case vk::Result::eNotReady:
                throw std::runtime_error("O5MSwapchain::acquire: no image available");
            default:
                throw std::runtime_error("O5MSwapchain::acquire: unexpected VkResult");
        }
    } catch (const vk::OutOfDateKHRError&) {
        return { 0, true };
    }
}

bool O5MSwapchain::present(vk::Queue queue, uint32_t imageIndex,
                          vk::raii::Semaphore& renderFinished) {
    vk::PresentInfoKHR presentInfo;
    presentInfo.setWaitSemaphores(*renderFinished);
    presentInfo.setSwapchains(*m_swapchain);
    presentInfo.setImageIndices(imageIndex);

    try {
        switch (queue.presentKHR(presentInfo)) {
            case vk::Result::eSuccess:
                return true;
            case vk::Result::eSuboptimalKHR:
                return false;
            default:
                throw std::runtime_error("O5MSwapchain::present: unexpected VkResult");
        }
    } catch (const vk::OutOfDateKHRError&) {
        return false;
    }
}
