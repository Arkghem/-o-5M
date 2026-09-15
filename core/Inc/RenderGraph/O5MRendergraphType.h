#ifndef O5M_RENDERGRAPH_TYPE_H
#define O5M_RENDERGRAPH_TYPE_H

#include <concepts>
#include <vector>
#include <string>
#include <cstdint>
#include <functional>

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
    
    using TexHandle = std::uint32_t;
    using BufHandle = std::uint32_t;

    //I though maybe I can encode this in handle, 
    //but it's public interface so we better expose error in compile time.
    //anyway, we go with variant
    struct UseDecl {
        uint32_t handle;
        std::variant<TexRead, TexWrite, TexRW, BufRead, BufWrite> use;
    };

    struct PassDesc {
         const std::string debugName;

         PassKind kind;
         std::vector<UseDecl> reads;
         std::vector<UseDecl> writes;
         std::vector<UseDecl> readWrites;
         std::function<void(O5MRenderContext&, vk::raii::CommandBuffer&)> executeFunc;
    };
};

#endif // !O5M_RENDERGRAPH_TYPE_H
