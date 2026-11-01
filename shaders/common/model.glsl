#ifndef O5M_MODEL_GLSL
#define O5M_MODEL_GLSL

layout(push_constant) uniform Push {
    mat4 model;         // offset 0
    uint materialIndex; // offset 64
    float width;        // offset 68
    uint debugFlags;    // offset 72
    uint pad;           // offset 76 -> 80
} pc;

#endif //O5M_MODEL_GLSL
