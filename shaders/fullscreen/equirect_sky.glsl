#version 450

layout(local_size_x = 32, local_size_y = 32) in;

layout(set = 0, binding = 0, rgba16f) writeonly uniform image2D uSky;

layout(push_constant) uniform Params {
    vec4 sun_color_intensity;
    vec4 sun_direction;
    vec4 zenith_color;
    vec4 horizon_color;
    vec4 ground_color;
} pc; // 80 bytes

const float PI = 3.14159265358979;

void main() {
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(uSky);

    if (texel.x >= size.x || texel.y >= size.y)
        return;

    uv = (vec2(texel) + 0.5) / vec2(size);
    
    float phi   = (uv.x * 2.0 - 1.0) * PI;
    float theta = (0.5 - uv.y) * PI;   

    vec3 L_dir = vec3(cos(theta) * sin(phi), sin(theta), -cos(theta) * cos(phi));

    vec3 up = mix(pc.horizon_color.rgb, pc.zenith_color.rgb, smoothstep(0.0, 0.45, L_dir.y));
    vec3 down = mix(pc.horizon_color.rgb, pc.ground_color.rgb, smoothstep(0.0, 0.45, -L_dir.y));

    vec3 color = L_dir.y > 0.0 ? up : down;

    const float sunRadius = 1.5 * PI / 180.0;
    const float sunEdge   = 0.6 * PI / 180.0;
    float disk = smoothstep(cos(sunRadius + sunEdge), cos(sunRadius),
                            dot(dir, pc.sunDirIntensity.xyz));

    color += pc.sunColor.rgb * pc.sunDirIntensity.w * disk;

    imageStore(uSky, texel, vec4(color, 1.0f));
}
