#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vTangent;
layout(location = 3) in vec2 vUV;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;

#include "common/model.glsl"
#include "common/material.glsl"

void main() {
    MaterialParams material = materials.item[pc.materialIndex];
    outColor = material.albedoFactor * texture(uBaseColorMap, vUV);

    vec4 norm = vec4(normalize(vNormal) * 0.5 + 0.5, 1.0);
    vec4 tan = vec4(normalize(vTangent), )
}
