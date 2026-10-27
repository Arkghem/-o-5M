#include "Resources/O5MMaterialResource.h"

#include <cmath>

namespace {

bool isFinite(const glm::vec4& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z) && std::isfinite(value.w);
}

bool inUnitRange(float value) {
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

} // namespace

bool O5MMaterialResource::doLoad(void) {
    if (!isFinite(m_params.albedoFactor) ||
        !isFinite(m_params.mrFactor) ||
        !isFinite(m_params.emissiveFactor) ||
        !isFinite(m_params.flags)) {
        return false;
    }

    if (!inUnitRange(m_params.mrFactor.x) ||
        !inUnitRange(m_params.mrFactor.y) ||
        !inUnitRange(m_params.flags.y) ||
        !inUnitRange(m_params.flags.w)) {
        return false;
    }

    // flags.z is normalTexture.scale; the glTF spec does not constrain its
    // sign (a negative scale flips the tangent-space normal map).
    const uint32_t alphaMode =
        (static_cast<uint32_t>(m_params.flags.x) & kAlphaModeMask) >>
        kAlphaModeShift;
    return alphaMode <= 2u;
}

void O5MMaterialResource::doUnload(void) {
    // Pure CPU data: nothing to release. The base class owns the loaded flag.
}
