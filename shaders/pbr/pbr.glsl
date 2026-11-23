#ifndef O5M_PBR_GLSL
#define O5M_PBR_GLSL

#define PHI 3.1415926

// ----------------------------------------------------------------------
// - Experimental functions
// ----------------------------------------------------------------------

float Chi(float param) {
    return param > 0 ? 1.0f : 0;
}

float GGXLambda(float NdotS, float alpha) {
    NdotS = clamp(NdotS, 1e-4f, 1.0f - 1e-4f);
    float a = NdotS / (alpha * sqrt(1.0f - NdotS * NdotS));
    return (sqrt(1.0f + 1.0f/ (a * a)) - 1.0f) / 2;
}

float geometrySmithHeightRelated(float NdotV, float NdotL, float roughness) {
    return Chi(NdotV) * Chi(NdotL) / (1 + GGXLambda(NdotL, roughness * roughness) + GGXLambda(NdotV, roughness * roughness));
}

// ----------------------------------------------------------------------
// - core render code
// ----------------------------------------------------------------------

struct Surface {
    vec3 albedo;
    vec3 normal;
    vec3 view;
    float metallic;
    float roughness;
    float ao;
};

float distributionGGX(float NdotH, float alpha) {
    float tmp = (NdotH * NdotH) * (alpha * alpha - 1) + 1; 
    return Chi(NdotH) * alpha * alpha / (PHI * tmp * tmp);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    return geometrySmithHeightRelated(NdotV, NdotL, roughness);
}

vec3 fresnelSchlick(float VdotH, vec3 F0) {
    float tmp = 1 - max(VdotH, 0);
    return F0 + (1-F0) * tmp * tmp * tmp * tmp * tmp;
}

vec3 evaluateBRDF(Surface s, vec3 L) {
    vec3 H = normalize(L + s.view);
    float NdotH = dot(H, s.normal);
    float NdotV = max(dot(s.normal, s.view), 1e-4);
    float NdotL = dot(s.normal, L);
    if (NdotL <= 0.0) return vec3(0.0);
    NdotL = max(NdotL, 1e-4);
    float VdotH = dot(s.view, H);
    float LdotH = dot(L, H);

    vec3 F0 = mix(vec3(0.04), s.albedo, s.metallic);
    vec3 F = fresnelSchlick(VdotH, F0);

    return F * geometrySmith(NdotV, NdotL, s.roughness) * distributionGGX(NdotH, s.roughness * s.roughness) / (4 * NdotL * NdotV) + (1.0 - F) * (1.0 - s.metallic) * s.albedo / PHI;
}

void accumulateDirect(inout vec3 Lo, Surface s, vec3 L, vec3 radiance) {
    float NdotL = dot(s.normal, L);
    if (NdotL <= 0.0) return;
    Lo+=evaluateBRDF(s, L) * radiance * NdotL;
}

#endif //! O5M_PBR_GLSL
