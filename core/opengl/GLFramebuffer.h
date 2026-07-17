#ifndef __GLFRAMEBUFFER_H__
#define __GLFRAMEBUFFER_H__

#include <glad/glad.h>
#include <map>

#include "IFramebuffer.h"

class GLFramebuffer : public IFramebuffer {
public:   
    bool create(void) override;
    void attachColor(int idx, ITexture* tex, int mipLevel = 0, int layer = 0) override;
    void attachDepthStencil(ITexture* tex, int mipLevel = 0, int layer = 0) override;

    GLuint handle(void) const;
    int colorCount(void) const;
private:
    struct Attachment {
        ITexture* texture;
        int mipLevel;
        int layer;
    };

    std::map<int, Attachment> m_colorAttachments;
    Attachment m_depthAttachment;
    GLuint m_handle = 0;
};
#endif //__GLFRAMEBUFFER_H__
