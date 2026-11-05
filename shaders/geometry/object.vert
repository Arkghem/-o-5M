#version 450

// Locations mirror O5MMeshResource::Vertex as bound in apps/o5m_viewer.cpp:
// 0 = position, 1 = vertex color (unused here), 2 = normal, 3 = tangent,
// 4 = uv.
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec4 aTangent;
layout(location = 4) in vec2 aUV;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vTangent;
layout(location = 3) out vec2 vUV;
layout(location = 4) out float vBitangentSign;

#include "common/camera.glsl"
#include "common/model.glsl"

void main() {
    const vec4 world = pc.model * vec4(aPos, 1.0);

    // Normals need the inverse transpose when the scale is non-uniform;
    // tangents are directions and only use the model matrix's linear part.
    vWorldPos = world.xyz;
    vNormal = mat3(transpose(inverse(pc.model))) * aNormal;
    vTangent = mat3(pc.model) * aTangent.xyz;

    // glTF stores handedness in TANGENT.w. The vertex format is still
    // R32G32B32Sfloat, so this reads 1.0 until Vertex::m_tangent becomes a
    // vec4 and the attribute format becomes R32G32B32A32Sfloat.
    vBitangentSign = aTangent.w;

    vUV = aUV;
    gl_Position = camera.proj * camera.view * world;
}
