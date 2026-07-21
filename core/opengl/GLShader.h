#ifndef __GLSHADER_H__
#define __GLSHADER_H__

#include <glad/glad.h>

#include "IShader.h"

class GLShader : public IShader 
{
public:
    GLShader(E_SHADER_TYPE type, const char* source);

    bool compile(void) override;
    bool link(void) override;

    void setProgram(GLuint program) { m_program = program; }
    GLuint program(void) const;

    E_SHADER_TYPE type(void) const override { return m_type; }
    const std::string& compileLog(void) const override { return m_compileLog; }

    const std::vector<UniformBlock>& uniformBlocks(void) const override { return m_uniformBlocks; }
    const std::vector<TextureBinding>& textureBindings(void) const override { return m_textureBindings; }
private:
    void reflectUniformBlocks(void);
    void reflectTextureBindings(void);
    
    E_SHADER_TYPE m_type;
    std::string m_source;
    std::string m_compileLog;
    GLuint m_program = 0;
    GLuint m_shaderObj = 0;
    std::vector<UniformBlock> m_uniformBlocks;
    std::vector<TextureBinding> m_textureBindings;
};
#endif //__GLSHADER_H__
