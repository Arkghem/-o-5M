#ifndef __O5MSHADERCOMPILER__H
#define __O5MSHADERCOMPILER__H

#include <cstdint>
#include <string>
#include <vector>


enum class ShaderStage { Vertex, Fragment };

// 编译失败抛 std::runtime_error（携带 shaderc 的错误信息，含行号）。
std::vector<uint32_t> compileGLSL(ShaderStage stage, const std::string& glslSource,
                                  const std::string& debugName = "shader");


#endif //__O5MSHADERCOMPILER__H
