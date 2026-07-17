#ifndef __GLSHADER_H__
#define __GLSHADER_H__

#include <glad/glad.h>

#include "IShader.h"

class GLShader : public IShader 
{
public:
    GLShader(E_SHADER_TYPE type, const char* source);

    bool compile(void) override;

    GLuint program(void) const;
private:
    void reflectUniformBlocks(void);
    
    E_SHADER_TYPE m_type;
    std::string m_source;
    std::string m_compileLog;
    GLuint m_program = 0;
    std::vector<UniformBlock> m_uniformBlocks;
};
#endif //__GLSHADER_H__
