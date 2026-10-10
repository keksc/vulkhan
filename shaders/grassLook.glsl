// grassLook.glsl - single source of truth for how the grass LOOKS, shared by
// grass.frag (the blades) and terrain.frag (the ground under / beyond them) so
// the two can never drift apart. Place next to globalUbo.glsl.
//
// Everything here is pre-tonemap, linear light.
#ifndef GRASS_LOOK_GLSL
#define GRASS_LOOK_GLSL

// ---------------------------------------------------------------------------
// Output stage. grass.frag and terrain.frag MUST agree on this, otherwise the
// ground and the blades end up in different color spaces (the classic
// "terrain looks greyer/darker than the grass" seam).
//   false: a post-process pass tonemaps + gamma-corrects the whole scene.
//   true : every scene shader does it itself.
// ---------------------------------------------------------------------------
const bool SCENE_TONEMAP_IN_SHADER = false;

// ---------------------------------------------------------------------------
// Blade palette (moved here verbatim from grass.frag)
// ---------------------------------------------------------------------------
const vec3 GRASS_BASE_LO = vec3(0.03, 0.16, 0.03);
const vec3 GRASS_BASE_HI = vec3(0.06, 0.24, 0.05);
const vec3 GRASS_TIP_LO  = vec3(0.28, 0.42, 0.10);
const vec3 GRASS_TIP_HI  = vec3(0.40, 0.55, 0.20);
const vec3 GRASS_DRY_BASE = vec3(0.16, 0.12, 0.04);
const vec3 GRASS_DRY_TIP  = vec3(0.55, 0.45, 0.18);

// Sun and hemisphere lighting come from the shared environment UBO.
const float GRASS_WRAP        = 0.5;

// ---------------------------------------------------------------------------
// Tuning for the "average look of the grass field seen from above".
// ---------------------------------------------------------------------------
// Where along the blade the canopy reads from above (0 = root, 1 = tip).
// Blades lean and overlap, so the canopy shows mostly mid-to-upper blade.
const float GRASS_CANOPY_T    = 0.70;
// Final brightness trim for the canopy approximation (hand-tune to taste).
const float GRASS_CANOPY_GAIN = 1.0;
// Keep in sync with ProceduralGrassSys::lodNearDist / maxDist.
const float GRASS_LOD_NEAR    = 15.0;
const float GRASS_FIELD_END   = 120.0;
// How fast terrain "grass cover" rises with suitability (blades are sparse
// where suitability is low, so cover rises faster than linearly).
const float GRASS_COVER_CURVE = 2.5;

// ---------------------------------------------------------------------------
// Spawn suitability: exact port of mountain.cpp computeGrassSuitability /
// grassField.hpp grassSuitability, so the terrain knows where blades exist.
//   hNorm     = (y - baseHeight) / heightRange, clamped to [0,1]
//   slopeNorm = (1 - normal.y) / (1 - cos(rockSlopeAngle)), clamped to [0,1]
// ---------------------------------------------------------------------------
float grassSuitability(float hNorm, float slopeNorm) {
  float altitudeScore =
      1.0 - clamp((abs(hNorm - 0.5) - 0.15) / 0.2, 0.0, 1.0);
  float slopeScore = 1.0 - smoothstep(0.15, 0.5, slopeNorm);
  return clamp(altitudeScore * slopeScore, 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// Value noise bit-identical to grassGenerate.comp's vnoise / grass.vert's
// noise12, so dry patches and low-frequency clump shading line up exactly.
// ---------------------------------------------------------------------------
uint grassHashU(uint n) {
  n = (n ^ 61u) ^ (n >> 16u);
  n *= 9u;
  n = n ^ (n >> 4u);
  n *= 0x27d4eb2du;
  n = n ^ (n >> 15u);
  return n;
}
float grassCellHash(ivec2 c) {
  return float(grassHashU(uint(c.x) * 73856093u ^ uint(c.y) * 19349663u)) *
         (1.0 / 4294967296.0);
}
float grassVnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  ivec2 c = ivec2(i);
  return mix(mix(grassCellHash(c), grassCellHash(c + ivec2(1, 0)), u.x),
             mix(grassCellHash(c + ivec2(0, 1)), grassCellHash(c + ivec2(1, 1)), u.x),
             u.y);
}

// ---------------------------------------------------------------------------
// What the grass field looks like from above at world position xz, lit with
// the grass shader's own lighting model. `sunVis` scales the sun terms
// (1 = lit, 0 = terrain shadow).
// ---------------------------------------------------------------------------
vec3 grassCanopyLit(vec3 P, vec3 N, vec3 V, float dist, float sunVis, float biomeDry, vec3 biomeTint, vec3 streamLight) {
  vec2 xz = P.xz;
  float lodOut = smoothstep(GRASS_LOD_NEAR * 0.15, GRASS_LOD_NEAR * 1.8, dist);

  // --- albedo: same recipe as grass.frag, per-blade randomness at its mean ---
  float grad = GRASS_CANOPY_T * GRASS_CANOPY_T * GRASS_CANOPY_T; // easeIn(t,3)
  vec3 greenLo = mix(GRASS_BASE_LO, GRASS_TIP_LO, grad);
  vec3 greenHi = mix(GRASS_BASE_HI, GRASS_TIP_HI, grad);
  vec3 green = mix(greenHi, greenLo, lodOut * 0.35);
  green *= vec3(1.03, 1.01, 0.96); // mean of the per-clump warm tint (seed 0.5)
  vec3 dry = mix(GRASS_DRY_BASE, GRASS_DRY_TIP, grad);
  // Dry patches: same noise as the compute shader; per-clump factor
  // (0.6 + 0.4*rand) at its mean of 0.8.
  float dryness = smoothstep(0.62, 0.85, grassVnoise(xz * 0.03 + 100.0)) * 0.8;
  // Biome yellowing (steppe): grassGenerate.comp applies biomeDry * (0.75 + 0.25*rand).
  dryness = max(dryness, biomeDry * 0.875);
  vec3 albedo = mix(green, dry, dryness);
  albedo *= clamp(biomeTint / vec3(0.28,0.42,0.10),vec3(0),vec3(4));
  float cv = grassVnoise(xz * 0.02); // fragClumpVar
  albedo *= mix(0.82, 1.2, cv);
  albedo = mix(albedo, albedo * vec3(1.08, 1.0, 0.85), cv);
  albedo *= 0.96; // mean midrib / edge darkening

  // --- lighting: grass.frag's model with AO = 1 (canopy top) ---
  vec3 L = sunDirection();
  float hemi = N.y * 0.5 + 0.5;
  vec3 ambient = mix(ubo.groundAmbient.rgb, ubo.skyAmbient.rgb * 1.4, hemi) * 0.5;
  float dotNL = clamp((dot(N, L) + GRASS_WRAP) / (1.0 + GRASS_WRAP), 0.0, 1.0);
  vec3 diffuse = dotNL * sunRadiance() * 0.5;
  float backLight = clamp((dot(V, -L) + GRASS_WRAP) / (1.0 + GRASS_WRAP), 0.0, 1.0);
  vec3 scatter = sunRadiance() * 0.5 * backLight * 0.2 * (1.0 - lodOut);
  float fres = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 3.0);
  vec3 rim = ubo.skyAmbient.rgb * 1.4 * fres * 0.12 * (1.0 - lodOut);

  vec3 color = albedo * ((diffuse * 0.95 + scatter) * sunVis + ambient) + rim;
  color += albedo * streamLight;
  color *= vec3(1.04, 1.02, 0.98);
  return color * GRASS_CANOPY_GAIN;
}

// ---------------------------------------------------------------------------
// Atmospheric lighting is shared by all systems in environment.glsl.
#endif
