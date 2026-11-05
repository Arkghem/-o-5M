#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vTangent;
layout(location = 3) in vec2 vUV;
layout(location = 4) in float vBitangentSign;

layout(location = 0) out vec4 gAlbedo;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gMaterial;
layout(location = 3) out vec4 gEmissive;

#include "common/model.glsl"
#include "common/material.glsl"

void main() {
    MaterialParams material = materials.items[pc.materialIndex];

    gAlbedo = vec4(material.albedoFactor * texture(uBaseColorMap, vUV));

    vec3 N = normalize(vNormal);
    vec3 T = normalize(vTangent);
    T = normalize(T - N * dot(N, T));
    vec3 B = cross(N, T) * vBitangentSign;

    mat3 TBN = mat3(T, B, N);

    vec3 normalTexel = vec3(texture(uNormalMap, vUV) * 2.0 - 1.0);
    normalTexel.xy *= material.flags.z;
    gNormal = vec4(normalize(TBN * normalTexel) * 0.5 + 0.5, 1.0);

    vec4 mr = texture(uMetallicRoughnessMap, vUV);
    float metallic = material.mrFactor.x * mr.b;
    float roughness = material.mrFactor.y * mr.g;
    float ao = 1.0 + material.flags.w * (texture(uOccusionMap, vUV).r - 1.0);
    gMaterial = vec4(metallic, roughness, ao, 0);

    vec3 emissive = material.emissiveFactor.rgb * texture(uEmissiveMap, vUV).rgb;
    gEmissive = vec4(emissive * material.emissiveFactor.w, 1.0);
}
