#ifndef __ISHADER_H
#define __ISHADER_H
#include <string>
#include <vector>

enum class E_SHADER_TYPE {
    VERTEX,
    FRAGMENT,
    GEOMETRY,
    COMPUTE,
};

class IShader {
public:
    virtual ~IShader() = default;

    virtual bool compile() = 0;
    virtual E_SHADER_TYPE type() const = 0;
    virtual const std::string& compileLog() const = 0;

    struct UniformBlock {
        std::string name;
        int binding;
        int byteSize;
        struct Member {
            std::string name;
            int offset;
            int byteSize;
        };
        std::vector<Member> members;
    };

    struct TextureBinding {
        std::string  name;
        int binding;
        enum Type { Sampler2D, SamplerCube, Sampler2DShadow };
        Type type;
    };

    virtual const std::vector<UniformBlock>& uniformBlocks() const = 0;
    virtual const std::vector<TextureBinding>& textureBindings() const = 0;
};

#endif //__ISHADER_H
