#version 450

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;

void main() {
    outColor = vec4(1.0, 0.45, 0.1, 1.0);
    outNormal = vec4(0.0);
}
