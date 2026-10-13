#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec3 vWorldPos;

#include "common/camera.glsl"
#include "common/model.glsl"

void main() {
    vec4 world = pc.model * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = mat3(obj.normalMatrix.aNormal) * aNormal;
    vUV = aUV;
    gl_Position = camera.proj * camera.view * world;
}
