#version 450

// Debug gradient over the triangle: R and G come from the interpolated UV, B is
// constant. Note the swapchain format is B8G8R8A8Srgb, so these linear values
// are sRGB-encoded on write and look brighter than the raw numbers suggest.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(vUV, 0.25, 1.0);
}
