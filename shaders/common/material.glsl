#ifndef O5M_MATERIAL_GLSL
#define O5M_MATERIAL_GLSL

struct MaterialParams {
    vec4 albedoFactor;
    vec4 mrFactor;
    vec4 emissiveFactor;
    vec4 flags;
};

const uint kMaterialTableCapacity = 64u;

layout(set = 1, binding = 0) uniform Materials {MaterialParams items[kMaterialTableCapacity];} materials;

//sampler array is not allowed.
//merge draw call which share the same texture
layout(set = 2, binding = 0) uniform sampler2D uBaseColorMap; 
layout(set = 2, binding = 1) uniform sampler2D uMetallicRoughnessMap;
layout(set = 2, binding = 2) uniform sampler2D uNormalMap;
layout(set = 2, binding = 3) uniform sampler2D uOccusionMap;
layout(set = 2, binding = 4) uniform sampler2D uEmissiveMap;

#endif // !O5M_MATERIAL_GLSL
