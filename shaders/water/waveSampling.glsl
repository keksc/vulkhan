#ifndef WATER_WAVE_SAMPLING_GLSL
#define WATER_WAVE_SAMPLING_GLSL

// Superpose two continuous wave fields instead of visibly repeating one
// square FFT patch. No per-tile random offsets: those would introduce seams.
// Used for both geometry/normal taps and foam so their crests stay aligned.
vec4 sampleWaterField(sampler2D field, vec2 uv, float lod) {
  const mat2 rotation = mat2(0.8, 0.6, -0.6, 0.8);
  const float frequency = 0.731;
  vec4 primary = textureLod(field, uv, lod);
  vec4 secondary = textureLod(field,
      rotation * uv * frequency + vec2(0.37, 0.61),
      max(lod + log2(frequency), 0.0));
  // Horizontal displacement is a vector: rotate it back to lake space.
  secondary.xz = transpose(rotation) * secondary.xz;
  // Preserve height variance rather than averaging away the FFT energy.
  // Foam remains a bounded coverage blend, independent of wave amplitude.
  return vec4((primary.xyz * 0.55 + secondary.xyz * 0.45) /
              sqrt(0.55 * 0.55 + 0.45 * 0.45),
              primary.a * 0.55 + secondary.a * 0.45);
}

vec4 sampleWaterWaves(vec2 uv, float lod) {
  return sampleWaterField(displacementFoamMap, uv, lod);
}

#endif
