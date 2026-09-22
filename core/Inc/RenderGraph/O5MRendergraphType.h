#ifndef O5M_RENDERGRAPH_TYPE_H
#define O5M_RENDERGRAPH_TYPE_H

#include <concepts>
#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

class O5MRenderContext;
class O5MPassBuilder;

namespace O5MRendergraphNS {
    enum class TexRead  { Color, Depth, Storage, TransferSrc }; 
    enum class TexWrite { ColorClear, ColorStore, Depth, Storage, TransferDst }; //mind the difference with Color
    enum class TexRW    { Storage }; //Host or Device
    enum class BufRead  { Uniform, Storage, VertexIndex, Indirect, TransferSrc };
    enum class BufWrite { Storage, TransferDst, Uniform };

    enum class PassKind { Graphic, Compute };
    enum class ResourceKind { Buffer, Image };
    
    //using TexHandle = std::uint32_t;
    //using BufHandle = std::uint32_t;

    using ResourceHandle = const std::uint32_t;
    using UseID = const std::uint32_t;

    struct PhysicalResource {
        ResourceKind kind;
        vk::raii::DeviceMemory memory = nullptr;
        vk::raii::Image image = nullptr;
        vk::raii::ImageView view = nullptr;
        vk::raii::Buffer buffer = nullptr;
    };

    //Minimal handle auto-allocation: name interning.
    inline ResourceHandle internResourceName(const std::string& name) {
        static std::unordered_map<std::string, ResourceHandle> table; //name -> handle
        static uint32_t nextHandle = 0;

        auto [it, inserted] = table.try_emplace(name, nextHandle);
        if (inserted) ++nextHandle;
        return it->second;
    }

    inline UseID allocateUseID(void) {
        static uint32_t nextID = 0;
        return ++nextID;
    };

    struct BufferInfo {
        vk::DeviceSize size;
        vk::BufferUsageFlags usage;
    };

    struct ImageInfo {
        vk::Extent2D extent;
        vk::Format format;
        vk::ImageUsageFlags usage;
    };

    struct ResourceInfo {
        std::string debugName;
        ResourceHandle handle;

        ResourceKind kind;
        uint32_t firstUse = UINT32_MAX;
        uint32_t lastUse = 0;

        std::variant<ImageInfo, BufferInfo> info;

        ResourceInfo(const ResourceInfo& other)
            : debugName(other.debugName),
              handle(other.handle),
              kind(other.kind),
              firstUse(other.firstUse),
              lastUse(other.lastUse),
              info(other.info) {}

        ResourceInfo(
            std::string& debugName,
            vk::Extent2D extent,
            vk::Format format
        )
            :debugName(debugName),
            handle(internResourceName(debugName)),
            kind(ResourceKind::Image), 
            info(ImageInfo{ extent, format, {},}) {}

        ResourceInfo(
            std::string& debugName,
            vk::DeviceSize size
        )
            : debugName(debugName),
            handle(internResourceName(debugName)), 
            kind(ResourceKind::Buffer), 
            info(BufferInfo{size, {},}) {}
    };

    inline ResourceKind declareKind(std::variant<TexRead, TexWrite, TexRW, BufRead, BufWrite> use) {
        return std::visit([](auto&& arg) -> ResourceKind {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, TexRead> || std::is_same_v<T, TexWrite> || std::is_same_v<T, TexRW>) {
                return ResourceKind::Image;
            } else {
                return ResourceKind::Buffer;
            }
        }, use);
    }

    //barrier got use this, wondering how
    struct SyncScope {
        vk::PipelineStageFlags stages;
        vk::AccessFlags access;
        vk::ImageLayout layout;
    };

    //bug: alot alot of bugs here, tired of write this, so do it later
    SyncScope fromUseToSyncScope(PassKind kind, std::variant<TexRead, TexWrite, TexRW, BufRead, BufWrite> use) {
        return std::visit([kind](auto&& arg) -> SyncScope {
            using T = std::decay_t<decltype(arg)>;
            if (kind == PassKind::Graphic) { 
                if constexpr (std::is_same_v<T, TexRead>) {
                    switch (arg) {
                        case TexRead::Color: return { vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eShaderReadOnlyOptimal };
                        case TexRead::Depth: return { vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eShaderReadOnlyOptimal };
                        case TexRead::Storage: return { vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eGeneral };
                        case TexRead::TransferSrc: return { vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead, vk::ImageLayout::eTransferSrcOptimal };
                    }
                } else if constexpr (std::is_same_v<T, TexWrite>) {
                    switch (arg) {
                        case TexWrite::ColorClear: return { vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::AccessFlagBits::eColorAttachmentWrite, vk::ImageLayout::eColorAttachmentOptimal };
                        case TexWrite::ColorStore: return { vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::AccessFlagBits::eColorAttachmentWrite, vk::ImageLayout::eColorAttachmentOptimal };
                        case TexWrite::Depth: return { vk::PipelineStageFlagBits::eEarlyFragmentTests, vk::AccessFlagBits::eDepthStencilAttachmentWrite, vk::ImageLayout::eDepthStencilAttachmentOptimal };
                        case TexWrite::Storage: return { vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::ImageLayout::eGeneral };
                        case TexWrite::TransferDst: return { vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eTransferDstOptimal };
                    }
                } else if constexpr (std::is_same_v<T, TexRW>) {
                    switch (arg) {
                        case TexRW::Storage: return { vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite, vk::ImageLayout::eGeneral };
                    }
                } else if constexpr (std::is_same_v<T, BufRead>) {
                    switch (arg) {
                        case BufRead::Uniform: return { vk::PipelineStageFlagBits::eVertexShader, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eUndefined };
                        case BufRead::Storage: return { vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eUndefined };
                        case BufRead::VertexIndex: return { vk::PipelineStageFlagBits::eVertexInput, vk::AccessFlagBits::eIndexRead | vk::AccessFlagBits::eVertexAttributeRead, vk::ImageLayout::eUndefined };
                        case BufRead::Indirect: return { vk::PipelineStageFlagBits::eDrawIndirect, vk::AccessFlagBits::eIndirectCommandRead, vk::ImageLayout::eUndefined };
                        case BufRead::TransferSrc: return { vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead, vk::ImageLayout::eUndefined };
                    }
                } else if constexpr (std::is_same_v<T, BufWrite>) {
                    switch (arg) {
                        case BufWrite::Storage: return { vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::ImageLayout::eUndefined };
                        case BufWrite::TransferDst: return { vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eUndefined };
                        case BufWrite::Uniform: return { vk::PipelineStageFlagBits::eVertexShader, vk::AccessFlagBits::eShaderWrite, vk::ImageLayout::eUndefined };
                    }
                }
            } else if (kind == PassKind::Compute) { 
                if constexpr (std::is_same_v<T, TexRead>) {
                    switch (arg) {
                        case TexRead::Color: return { vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eShaderReadOnlyOptimal };
                        case TexRead::Depth: return { vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eShaderReadOnlyOptimal };
                        case TexRead::Storage: return { vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eGeneral };
                        case TexRead::TransferSrc: return { vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead, vk::ImageLayout::eTransferSrcOptimal };
                    }
                } else if constexpr (std::is_same_v<T, TexWrite>) {
                    switch (arg) {
                        case TexWrite::ColorClear: return { vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::AccessFlagBits::eColorAttachmentWrite, vk::ImageLayout::eColorAttachmentOptimal };
                        case TexWrite::ColorStore: return { vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::AccessFlagBits::eColorAttachmentWrite, vk::ImageLayout::eColorAttachmentOptimal };
                        case TexWrite::Depth: return { vk::PipelineStageFlagBits::eEarlyFragmentTests, vk::AccessFlagBits::eDepthStencilAttachmentWrite, vk::ImageLayout::eDepthStencilAttachmentOptimal };
                        case TexWrite::Storage: return { vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::ImageLayout::eGeneral };
                        case TexWrite::TransferDst: return { vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eTransferDstOptimal };
                    }
                }
            }
        }, use);
    }

    
    //I though maybe I can encode this in handle, 
    //but it's public interface so we better expose error in compile time.
    //anyway, we go with variant
    struct UseDecl {
        UseID id;

        ResourceHandle handle; 
        //auto-allocated from debugName, don't fill it by hand
        std::variant<TexRead, TexWrite, TexRW, BufRead, BufWrite> use;

        //e.g. builder.read({"sceneColor", TexRead::Color})
        UseDecl(ResourceInfo resourceHandle,
                std::variant<TexRead, TexWrite, TexRW, BufRead, BufWrite> u):
            handle(resourceHandle.handle), use(std::move(u)), id(allocateUseID()){
                //store the resourceUse Info
            }
    };

    //bug: now if we input Buf.. won't throw an error
    inline vk::ImageUsageFlags toImageUsage(std::variant<TexRead, TexWrite, TexRW, BufRead, BufWrite> use) {
        return std::visit([](auto&& arg) -> vk::ImageUsageFlags {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, TexRead>) {
                switch (arg) {
                    case TexRead::Color: return vk::ImageUsageFlagBits::eSampled;
                    case TexRead::Depth: return vk::ImageUsageFlagBits::eSampled;
                    case TexRead::Storage: return vk::ImageUsageFlagBits::eStorage;
                    case TexRead::TransferSrc: return vk::ImageUsageFlagBits::eTransferSrc;
                }
            } else if constexpr (std::is_same_v<T, TexWrite>) {
                switch (arg) {
                    case TexWrite::ColorClear: return vk::ImageUsageFlagBits::eColorAttachment;
                    case TexWrite::ColorStore: return vk::ImageUsageFlagBits::eColorAttachment;
                    case TexWrite::Depth: return vk::ImageUsageFlagBits::eDepthStencilAttachment;
                    case TexWrite::Storage: return vk::ImageUsageFlagBits::eStorage;
                    case TexWrite::TransferDst: return vk::ImageUsageFlagBits::eTransferDst;
                }
            } else {
                return vk::ImageUsageFlagBits::eStorage;
            }
        }, use);
    }

    //bug: now if we input Tex.. won't throw an error
    inline vk::BufferUsageFlags toBufferUsage (std::variant<TexRead, TexWrite, TexRW, BufRead, BufWrite> use) {
        return std::visit([](auto&& arg) -> vk::BufferUsageFlags {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, BufRead>) {
                switch (arg) {
                    case BufRead::Uniform: return vk::BufferUsageFlagBits::eUniformBuffer;
                    case BufRead::Storage: return vk::BufferUsageFlagBits::eStorageBuffer;
                    case BufRead::VertexIndex: return vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer;
                    case BufRead::Indirect: return vk::BufferUsageFlagBits::eIndirectBuffer;
                    case BufRead::TransferSrc: return vk::BufferUsageFlagBits::eTransferSrc;
                }
            } else if constexpr (std::is_same_v<T, BufWrite>) {
                switch (arg) {
                    case BufWrite::Storage: return vk::BufferUsageFlagBits::eStorageBuffer;
                    case BufWrite::TransferDst: return vk::BufferUsageFlagBits::eTransferDst;
                    case BufWrite::Uniform: return vk::BufferUsageFlagBits::eUniformBuffer;
                }
            }
        }, use);
    }

    struct BarrierState {
        ResourceHandle handle;

        SyncScope from;
        SyncScope to;
    };
    struct PassDesc {
        struct Compiled {
            std::vector<BarrierState> enterBarrier;//from to
        } compiled; //this will be filled after "compile"

        const std::string debugName;

        PassKind kind;
        std::vector<UseDecl> reads;
        std::vector<UseDecl> writes;
        std::vector<UseDecl> readWrites;
        std::function<void(O5MRenderContext&, vk::raii::CommandBuffer&)> executeFunc;
    };
};

#endif // !O5M_RENDERGRAPH_TYPE_H
