#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vWorldPos;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;

#include "common/model.glsl"
#include "common/material.glsl"

void main() {
    MaterialParams material = materials.item[pc.materialIndex];
    outColor = material.albedoFactor * texture(uBaseColorMap, vUV);

    outNormal = vec4(normalize(vNormal) * 0.5 + 0.5, 1.0);
}
