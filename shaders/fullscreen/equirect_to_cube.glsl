#version 450

layout(local_size_x = 32, local_size_y = 32) in;

layout(set = 0, binding = 0) uniform sampler2D uSky;
layout(set = 0, binding = 1, rgba16f) writeonly uniform image2DArray uSkyCube;

const float PI = 3.14159265358979;

void main() {
    int face = int(gl_GlobalInvocationID.z);
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);

    ivec2 size = imageSize(uSkyCube).xy;
    vec2 uvc = (vec2(texel) + 0.5) / vec2(size);
    vec2 st = 2 * uvc - 1.0f;

    vec3 dir;
    if (face == 0)      dir = vec3(1,     -st.y, -st.x);
    else if (face == 1) dir = vec3(-1,    -st.y, st.x);
    else if (face == 2) dir = vec3(st.x,  1,     st.y);
    else if (face == 3) dir = vec3(st.x,  -1,    -st.y);
    else if (face == 4) dir = vec3(st.x,  -st.y, 1);
    else                dir = vec3(-st.x, -st.y, -1);
    dir = normalize(dir);

    // no idea about how we get this equation.
    // yes, I hate geometry.
    vec2 uv = vec2(0.5 + atan(dir.x, -dir.z) / (2.0 * PI), acos(clamp(dir.y, -1.0, 1.0)) / PI);
    vec3 color = textureLod(uSky, uv, 0.0).rgb;

    imageStore(uSkyCube, ivec3(texel, face), vec4(color, 1.0f));
}
