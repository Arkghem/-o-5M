#version 450

layout(location = 0) in vec3 aPos;
layout(location = 2) in vec3 aNormal;

layout(push_constant) uniform Push {
    mat4 model;
    float width;
} pc;

#include "common/camera.glsl"

void main() {
    vec4 world = pc.model * vec4(aPos, 1.0);
    vec3 normal = mat3(transpose(inverse(pc.model))) * aNormal;
    world.xyz += normalize(normal) * pc.width;
    gl_Position = camera.proj * camera.view * world;
}
