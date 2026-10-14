#version 450

// P2 acceptance shader: a world-space triangle transformed by proj * view, so
// camera motion (WASD / mouse) becomes visible on screen.
//
// Vertex data is generated from gl_VertexIndex -- there is no vertex buffer
// yet, that belongs to P3's geometry pipeline. The real per-vertex version
// this replaces is parked at shaders/geometry/object.vert.
//
// ---------------------------------------------------------------------------
// Vulkan projection adaptation (P2 exercise [4], resolved):
//   - Y flip is folded into the projection on the C++ side
//     (O5MCameraComponent.cpp: m_projectionMatrix[1][1] *= -1.0f).
//   - Depth range [0, 1] comes from GLM_FORCE_DEPTH_ZERO_TO_ONE, defined
//     globally in the root CMakeLists so every TU sees the same glm config.
// ---------------------------------------------------------------------------

layout(location = 0) out vec2 vUV;

#include "common/camera.glsl"

// Deliberately asymmetric in Y: a symmetric shape would hide the Y flip.
// z = 0 puts the triangle 3 units in front of the camera, which starts at
// (0, 0, 3) looking down -Z. FOV is 120 deg, so the visible half-height at
// that distance is 3 * tan(60) ~= 5.2 -- these coordinates cover a good
// chunk of the screen.
const vec3 kWorld[3] = vec3[3](
    vec3( 0.0,  2.0, 0.0),   // apex
    vec3(-1.2, -1.2, 0.0),   // bottom left
    vec3( 1.7, -0.5, 0.0)    // bottom right, noticeably higher than the left
);

void main() {
    vec3 world = kWorld[gl_VertexIndex];
    vUV = world.xy * 0.25 + 0.5;
    gl_Position = camera.proj * camera.view * vec4(world, 1.0);
}
