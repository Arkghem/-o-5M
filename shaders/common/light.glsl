//common/light.glsl

struct DirLight { vec4 direct; vec4 color_intensity; };
struct PointLight { vec4 position_radius; vec4 color_intensity; };

layout(set = 0, binding = 1, std140) uniform Lights {
     DirLight      sun;
     PointLight[8] pointLights;
     int           pointCount;
} lights;
