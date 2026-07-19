#ifndef __IGRAPHICSPIPELINE_H
#define __IGRAPHICSPIPELINE_H

#include <memory>
#include <vector>
#include <string>

class IShader;

struct VertexInputLayout {
    struct Binding {
        uint32_t stride;
        bool perInstance;
    };

    struct Attribute {
        uint32_t location;
        uint32_t binding;
        enum FORMAT {
            FLOAT32, FLOAT32X2, FLOAT32X3, FLOAT32X4, 
            UINT8_UNORM, UINT8X2_UNORM, UINT8X4_UNORM,
            UINT16_UNORM, UINT16X2_UNORM, UINT16X4_UNORM,
            INT8_SNorm, INT8X2_SNORM, INT8X4_SNORM,
            UINT8, UINT8X2, UINT8X4,
            INT32, INT32X2, INT32X3, INT32X4,
        };
        FORMAT format;
        uint32_t offset;
    };

    std::vector<Binding> bindings; //one day we gonna write a better vector
    std::vector<Attribute> attributes;
};

class IGraphicsPipeline {
public:
    virtual ~IGraphicsPipeline() = default;

    virtual void setShaderStages(IShader* vertex, IShader* fragment, 
                            IShader* geometry = nullptr) = 0;
    virtual void setVertexInputLayout(const VertexInputLayout& layout) = 0;

    struct RasterizerState {
        enum PolygonMode { FILL, LINE };
        enum CullMode    { NONE, FRONT, BACK };
        PolygonMode polygonMode = FILL;
        CullMode cullMode = NONE;
        bool depthClamp = false;
    };

    virtual void setRasterizerState(const RasterizerState& state) = 0;

    struct DepthStencilState {
        bool depthTest = true;
        bool depthWrite = true;
        enum CompareOp { NEVER, LESS, EQUAL, LESS_OR_EQUAL, GREATER, ALWAYS };
        CompareOp depthCompareOp = LESS;
    };
    virtual void setDepthStencilState(const DepthStencilState& state) = 0;

    struct BlendState {
        bool enable = false;
        enum Factor { ZERO, ONE, SRC_ALPHA, DST_ALPHA, ONE_MINUS_SRC_ALPHA, ONE_MINUS_DST_ALPHA };
        Factor srcColorBlendFactor = ONE;
        Factor dstColorBlendFactor = ZERO;
    };

    virtual void setBlendState(int idx, const BlendState& state) = 0;

    virtual bool create(void) = 0;
};

#endif //__IGRAPHICSPIPELINE_H
