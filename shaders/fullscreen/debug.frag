#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sourceTexture;

layout(push_constant) uniform Push {
    vec4 params;
} pc;

#include "common/math.glsl"

void main() {
    if (pc.params.x < 0.5) {
        float depth = texture(sourceTexture, vUV).r;
        float z = linearizeDepth(depth, pc.params.y, pc.params.z);
        float value = clamp(z / max(pc.params.w, 1.0), 0.0, 1.0);
        outColor = vec4(vec3(1.0 - value), 1.0);
    } else {
        outColor = vec4(texture(sourceTexture, vUV).rgb, 1.0);
    }
}
