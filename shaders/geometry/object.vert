#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aUV;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vTangent;
layout(location = 3) out vec2 vUV;

#include "common/camera.glsl"
#include "common/model.glsl"

void main() {
    vec4 world = pc.model * vec4(aPos, 1.0);
    vec3 normal = mat3(transpose(inverse(pc.model))) * aNormal;
    vec3 tangent = pc.model * vec4(aTangenet);

    vNormal = normal;
    vUV = aUV;
    vWorldPos = world.xyz;
    vTangent = aTangent;
    gl_Position = camera.proj * camera.view * world;
}
