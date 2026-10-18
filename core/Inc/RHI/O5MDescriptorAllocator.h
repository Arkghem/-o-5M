#ifndef O5M_DESCRIPTOR_ALLOCATOR_H
#define O5M_DESCRIPTOR_ALLOCATOR_H

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "O5MDevice.h"

class O5MDescriptorAllocator {
public:
    O5MDescriptorAllocator(O5MDevice& device):m_device(device){}
    ~O5MDescriptorAllocator() = default;

    void create(void);
private:
    O5MDevice& m_device;
    vk::raii::DescriptorPool m_pool = nullptr;
};

#endif // !O5M_DESCRIPTOR_ALLOCATOR_H
