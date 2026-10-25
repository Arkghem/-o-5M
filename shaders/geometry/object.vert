#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec2 aUV;

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec3 vWorldPos;

#include "common/camera.glsl"
#include "common/model.glsl"

void main() {
    vec4 world = pc.model * vec4(aPos, 1.0);
    vec3 normal = mat3(transpose(inverse(pc.model))) * aNormal;

    vColor = aColor;
    vNormal = normal;
    vUV = aUV;
    vWorldPos = world.xyz;
    gl_Position = camera.proj * camera.view * world;
}
