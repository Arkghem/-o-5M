#version 450

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec3 vWorldPos;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;

void main() {
    outColor = vec4(vColor, 1.0);
    outNormal = vec4(normalize(vNormal) * 0.5 + 0.5, 1.0);
}
