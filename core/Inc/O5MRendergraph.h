#ifndef __O5MRENDERGRAPH__H
#define __O5MRENDERGRAPH__H

#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

class O5MRendergraph {
private:
    struct ResourceDesc {
        ResourceDesc& operator=(ResourceDesc& other) {
            name = other.name;
            format = other.format;
            extent = other.extent;
            usage = other.usage;
            initialLayout = other.initialLayout;
            finalLayout = other.finalLayout;

            return *this;
        };

        std::string name;
        vk::Format format;
        vk::Extent2D extent;
        vk::ImageUsageFlags usage;
        vk::ImageLayout initialLayout;
        vk::ImageLayout finalLayout;

        vk::raii::Image image = nullptr;
        vk::raii::DeviceMemory memory = nullptr;
        vk::raii::ImageView imageView = nullptr;
    };

    struct PassDesc {
        std::string name;
        std::vector<std::string> inputs;
        std::vector<std::string> outputs; //why string
        std::function<void(vk::raii::CommandBuffer&)> executeFuct;
    };
private:
    std::unordered_map<std::string, ResourceDesc> m_resources;
    std::vector<PassDesc> m_passDescs;
    std::vector<size_t> m_executionOrder;//?so this class is not just a node
    std::vector<vk::raii::Semaphore> m_semaphores;
    std::vector<std::pair<size_t,size_t>> m_semaphoreSignalWaitPairs;//signal pass, waiting pass, I thought we need two semaphore signals

    vk::raii::Device& m_device;
public:
    explicit O5MRendergraph(vk::raii::Device& device) : m_device(device) {}

    ResourceDesc* getResource(const std::string& name) {
        auto it = m_resources.find(name);
        return it != m_resources.end() ? &it->second : nullptr;
    }

    void addResource(const std::string& name, vk::Format format, vk::Extent2D extent,
                     vk::ImageUsageFlags uasge, vk::ImageLayout initialLayout,
                     vk::ImageLayout finalLayout);

    void addPass(const std::string& name, const std::vector<std::string>& inputs,
                 const std::vector<std::string>& outputs,
                 std::function<void(vk::raii::CommandBuffer&)> executeFunc);

    void compile(void);

    void execute(vk::raii::CommandBuffer& commandBuffer, vk::Queue queue);
};

#endif //__O5MRENDERGRAPH__H

