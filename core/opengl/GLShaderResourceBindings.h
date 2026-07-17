#ifndef __GLSHADERRESOURCEBINDINGS_H__
#define __GLSHADERRESOURCEBINDINGS_H__

#include <map>

#include "IShaderResourceBindings.h"

class GLTexture;
class GLBuffer;

class GLShaderResourceBindings : public IShaderResourceBindings {
public:
    struct UBOBinding {
        GLBuffer* buffer;
        size_t offset;
        size_t size;
    };

    void bindUniformBuffer(int binding, IBuffer* buffer, size_t offset = 0, size_t size = 0) override;
    void bindTexture(int binding, ITexture* texture) override;

    bool create(void) override;

    const std::map<int, UBOBinding>& ubos(void) const;
    const std::map<int, GLTexture*>& textures(void) const;
private:
    std::map<int, UBOBinding> m_UBOs;
    std::map<int, GLTexture*> m_textures;
};
#endif //__GLSHADERRESOURCEBINDINGS_H__
