#include "GLShader.h"

GLShader::GLShader(E_SHADER_TYPE type, const char* source)
    : m_type(type), m_source(source) {}

bool GLShader::compile(void) {
    GLenum glType = GL_VERTEX_SHADER;
    if (m_type == E_SHADER_TYPE::FRAGMENT) {
        glType = GL_FRAGMENT_SHADER;
    } else if (m_type == E_SHADER_TYPE::GEOMETRY) {
        glType = GL_GEOMETRY_SHADER;
    } else if (m_type == E_SHADER_TYPE::COMPUTE) {
        glType = GL_COMPUTE_SHADER;
    }

    m_shaderObj = glCreateShader(glType);
    const char* src = m_source.c_str();
    glShaderSource(m_shaderObj, 1, &src, nullptr);
    glCompileShader(m_shaderObj);

    GLint success;
    glGetShaderiv(m_shaderObj, GL_COMPILE_STATUS, &success);

    if (!success) {
        char log[1024];
        glGetShaderInfoLog(m_shaderObj, 1024, nullptr, log);
        m_compileLog = log;
        return false;
    }

    return true;
}

bool GLShader::link(void) { 
    if (!m_program) {
        return false;
    }

    glAttachShader(m_program, m_shaderObj);
    glLinkProgram(m_program);

    GLint success;
    glGetProgramiv(m_program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(m_program, 1024, nullptr, log);
        m_compileLog = log;
        glDeleteShader(m_shaderObj);
        return false;
    }

    glDeleteShader(m_shaderObj);

    reflectUniformBlocks();
    reflectTextureBindings();

    return true;
}

GLuint GLShader::program(void) const { return m_program; }

void GLShader::reflectUniformBlocks(void) {
    int numBlocks = 0;
    glGetProgramInterfaceiv(m_program, GL_UNIFORM_BLOCK, GL_ACTIVE_RESOURCES, &numBlocks);

    for (int i = 0; i < numBlocks; i++) {
        UniformBlock block;

        char name[256];
        glGetProgramResourceName(m_program, GL_UNIFORM_BLOCK, i, sizeof(name), nullptr, name);
        block.name = name;
        
        GLenum prop = GL_BUFFER_BINDING;
        glGetProgramResourceiv(m_program, GL_UNIFORM_BLOCK, i, 1, &prop, 1, nullptr, &block.binding);

        prop = GL_BUFFER_DATA_SIZE;
        glGetProgramResourceiv(m_program, GL_UNIFORM_BLOCK, i, 1, &prop, 1, nullptr, &block.byteSize);

        int numMembers = 0;
        prop = GL_NUM_ACTIVE_VARIABLES;
        glGetProgramResourceiv(m_program, GL_UNIFORM_BLOCK, i, 1, &prop, 1, nullptr, &numMembers);

        if (numMembers > 0) {
            std::vector<GLint> memberIndices(numMembers);
            prop = GL_ACTIVE_VARIABLES;
            glGetProgramResourceiv(m_program, GL_UNIFORM_BLOCK, i, numMembers, &prop, numMembers, nullptr, memberIndices.data());

            for (int m : memberIndices) {
                UniformBlock::Member member;

                glGetProgramResourceName(m_program, GL_UNIFORM, m, sizeof(name), nullptr, name);
                member.name = name;

                GLenum props[] = { GL_OFFSET, GL_BUFFER_DATA_SIZE };
                GLint values[2];
                glGetProgramResourceiv(m_program, GL_UNIFORM, m, 2, props, 2, nullptr, values);
                member.offset = values[0];
                member.byteSize = values[1];
                
                block.members.push_back(member);
            }
        }
        m_uniformBlocks.push_back(block);
    }
}

void GLShader::reflectTextureBindings(void) {
    int numUniforms;
    glGetProgramInterfaceiv(m_program, GL_UNIFORM, GL_ACTIVE_RESOURCES, &numUniforms);

    for (int i = 0; i < numUniforms; i++) {
        GLenum prop = GL_TYPE;
        GLint type;
        glGetProgramResourceiv(m_program, GL_UNIFORM, i, 1, &prop, 1, nullptr, &type);

        TextureBinding::Type bindingType;
        switch (type) {
            case GL_SAMPLER_2D: bindingType = TextureBinding::Sampler2D; break;
            case GL_SAMPLER_CUBE: bindingType = TextureBinding::SamplerCube; break;
            case GL_SAMPLER_2D_SHADOW: bindingType = TextureBinding::Sampler2DShadow; break;
            default: continue;
        }

        TextureBinding binding;

        char name[256];
        glGetProgramResourceName(m_program, GL_UNIFORM, i, sizeof(name), nullptr, name);

        binding.name = name;
        binding.type = bindingType;

        prop = GL_LOCATION;
        glGetProgramResourceiv(m_program, GL_UNIFORM, i, 1, &prop, 1, nullptr, &binding.binding);
        
        m_textureBindings.push_back(binding);
    }
}
