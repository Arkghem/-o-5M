#ifndef O5M_PBR_GLSL
#define O5M_PBR_GLSL

struct Surface {
    vec3 albedo;
    vec3 normal;
    vec3 view;
    float metallic;
    float roughness;
    float ao;
}

float distributionGGX(float NdotH, float alpha) {

}

float geometrySmith(float NdotV, float NdotL, float roughness) {

}

vec3 fresnelSchilick(float VdotH, vec3 F0) {

}

vec3 evaluateBRDF(Surface s, vec3 L) {

}

void accumulateDirect(inout vec3 Lo, Surface s, vec3 L, vec3 radience) {
    float NdotL = dot(s.N, L);
    if (NdotL <= 0.0) return;
    Lo+=evaluateBRDF(s, L) * radiance * NdotL;
}
#endif //! O5M_PBR_GLSL
