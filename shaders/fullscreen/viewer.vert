#version 450

// P2 acceptance shader: a world-space triangle transformed by proj * view, so
// camera motion (WASD / mouse) becomes visible on screen.
//
// Vertex data is generated from gl_VertexIndex -- there is no vertex buffer
// yet, that belongs to P3's geometry pipeline. The real per-vertex version
// this replaces is parked at shaders/geometry/object.vert.
//
// ---------------------------------------------------------------------------
// TODO(user) [4] -- two things are deliberately left undone:
//
//   1. Y FLIP. Vulkan NDC has +Y pointing DOWN, while glm::perspective()
//      follows the OpenGL convention with +Y up. As written, the image comes
//      out upside down. Run it and see. Fix it either here (one line) or by
//      folding it into the projection on the C++ side -- both are defensible,
//      pick one and be able to say why.
//
//   2. DEPTH RANGE. glm::perspective() emits the OpenGL [-1, 1] depth range
//      unless GLM_FORCE_DEPTH_ZERO_TO_ONE is defined before any glm header is
//      included. Invisible today (there is no depth attachment yet), but P3
//      needs it -- see docs/phase2-exercises.md.
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
