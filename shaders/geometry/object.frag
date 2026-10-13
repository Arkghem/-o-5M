#version 450

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec3 vWorldPos;

#
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(vUV, 0.25, 1.0);
}
