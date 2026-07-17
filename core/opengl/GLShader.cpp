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

    GLuint shaderObj = glCreateShader(glType);
    const char* src = m_source.c_str();
    glShaderSource(shaderObj, 1, &src, nullptr);
    glCompileShader(shaderObj);

    GLint success;
    glGetShaderiv(shaderObj, GL_COMPILE_STATUS, &success);

    if (!success) {
        char log[1024];
        glGetShaderInfoLog(shaderObj, 1024, nullptr, log);
        m_compileLog = log;
        return false;
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, shaderObj);
    glLinkProgram(m_program);

    glGetProgramiv(m_program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(m_program, 1024, nullptr, log);
        m_compileLog = log;
        glDeleteShader(shaderObj);
        glDeleteProgram(m_program);
        return false;
    }

    glDeleteShader(shaderObj);

    reflectUniformBlocks();

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
