#pragma once

#include <glm/glm.hpp>

namespace vkh {

// Capacity of the lights SSBO (see SmokeCompositeEffect::lightsBuffer).
// Allocated once at this size and never resized - see the comment on
// ComputePass's descriptor set lifetime in computePass.hpp for why a
// truly dynamic-capacity buffer isn't a same-day change. Cheap to raise
// (32 bytes/light): lights beyond this count are clamped (dropped, not
// UB) by setPointLights().
constexpr int MAX_LIGHT_CAPACITY = 256;

// Capacity of the per-light transmittance (self-shadowing) volume array -
// see NUM_TRANSMITTANCE_SLOTS-worth of single-channel r32f Image3Ds in
// compositeEffect.hpp. Declared as a fixed-size descriptor array
// (VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT) rather than a true
// variable-count one, so growing the *active* shadowed-light count within
// this capacity needs no descriptor rewrite - only descriptors for
// activeLightCount slots are ever written, the rest are left unbound and
// never read (the shaders only loop up to shadowedLightCount). Lights
// beyond this index still light the smoke directly (see
// postProcessing.comp's main light loop) but don't self-shadow. Raising
// this costs one Image3D-sized allocation per slot per swapchain image
// (see the memory note on SmokeCompositeEffect::transmittanceVolumes) -
// keep it well below MAX_LIGHT_CAPACITY unless you actually need every
// light to cast shadows through smoke.
constexpr int TRANSMITTANCE_CAPACITY = 16;

// std430-compatible: every member already sits on a 16-byte boundary, so
// no explicit padding is needed - std430 array rules force each
// PointLight element to a 16-byte stride regardless. Matches the SSBO's
// `PointLight lights[]` runtime array in postProcessing.comp/
// transmittance.comp.
struct PointLight {
  glm::vec4 positionAndRadius; // xyz = world pos, w = falloff radius (0 = pure inverse-square)
  glm::vec4 colorAndIntensity; // rgb = color, w = intensity
};

} // namespace vkh
