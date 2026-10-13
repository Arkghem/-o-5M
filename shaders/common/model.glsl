layout(push_constant) uniform Push {
    mat4 model;
} pc;

layout(set = 1, binding = 0) uniform ObjectData {
    mat4 normalMatrix;
} obj;

