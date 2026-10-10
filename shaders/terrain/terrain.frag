#version 450

layout(location = 0) in vec3 fragPosWorld;
layout(location = 1) in vec3 fragNormalWorld;

layout(location = 0) out vec4 outColor;

// Must match TerrainSys::PushConstants (terrain.hpp) exactly.
layout(push_constant) uniform PushConstants {
  vec4 clipPlane;     // vertex stage only
  vec4 heightParams;  // x: baseHeight, y: heightRange, z: cos(rockSlopeAngle),
                      // w: shadow march steps (0 = off)
  vec4 heightmapRect; // xy: heightmap worldMin, zw: heightmap worldSize
  uvec4 meshParams;   // vertex-stage stitching
} push;

#include "../globalUbo.glsl"
#include "../environment.glsl"
#include "../grassLook.glsl"
#include "../biome.glsl"

// r = world-space height (Y). Only r is used.
layout(set = 1, binding = 0) uniform sampler2D heightmap;

const float PI = 3.14159265359;

// ---------------------------------------------------------------------------
// Tuning constants. World units are meters-ish (grass tiles are 1.0 wide).
// "BUMP" values are slope strengths of the detail normal (0 = flat).
// ---------------------------------------------------------------------------
const float BROAD_FREQ   = 0.27;  // ground: gentle 3-4 m undulation
const float GROUND_FREQ  = 1.7;   // ground: clods / crumbs
const float ROCK_FREQ    = 0.18;  // broad weathered stone faces
const float RIDGE_FREQ   = 0.22;  // fractured outcrops, not fine corrugation
const float BROAD_BUMP   = 0.12;
const float GROUND_BUMP  = 0.07;
const float ROCK_BUMP    = 0.12;
const float RIDGE_BUMP   = 0.20;
const float CRACK_AMP    = 0.003; // restrained joints beneath the broad form
const float RIPPLE_WAVELENGTH = 0.55;
const float SHADOW_SOFTNESS = 12.0; // higher = harder shadow edges
const float NOISE_MEAN = 0.47;   // approx. mean of fbm() output
const float RIDGE_MEAN = 0.29;   // approx. mean of ridged() output

// Linear-space palette. Grass-covered ground is NOT colored here: it uses
// grassCanopyLit() from grassLook.glsl, the same palette + lighting the blades use.
// ground
const vec3 SCREE       = vec3(0.14, 0.10, 0.07);
const vec3 DIRT        = vec3(0.20, 0.13, 0.06);
const vec3 STEPPE_SOIL = vec3(0.30, 0.21, 0.11);
const vec3 SAND        = vec3(0.50, 0.37, 0.20);
const vec3 SAND_DARK   = vec3(0.34, 0.22, 0.11);
const vec3 LITTER      = vec3(0.07, 0.045, 0.025);
const vec3 MOSS        = vec3(0.045, 0.10, 0.03);
const vec3 TUNDRA_GND  = vec3(0.17, 0.16, 0.11);
const vec3 LICHEN      = vec3(0.30, 0.32, 0.14);
const vec3 ALPINE      = vec3(0.26, 0.24, 0.20);
// rock
const vec3 GRANITE_DARK  = vec3(0.15, 0.13, 0.11);
const vec3 GRANITE_LIGHT = vec3(0.34, 0.32, 0.29);
const vec3 SAND_CREAM    = vec3(0.52, 0.39, 0.25);
const vec3 SAND_RED      = vec3(0.34, 0.13, 0.07);
const vec3 SAND_SEAM     = vec3(0.16, 0.08, 0.05);
const vec3 SNOW          = vec3(0.80, 0.84, 0.90);

// ---------------------------------------------------------------------------
// Noise
// ---------------------------------------------------------------------------
float hash12(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}

// Interleaved gradient noise: much better distributed than a white hash for
// per-pixel jitter (shadow march, dithering).
float ign(vec2 fragCoord) {
  return fract(52.9829189 * fract(dot(fragCoord, vec2(0.06711056, 0.00583715))));
}

// Value noise with analytic derivatives: (value, d/dx, d/dy).
vec3 noised(vec2 x) {
  vec2 i = floor(x);
  vec2 f = fract(x);
  vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
  vec2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);

  float a = hash12(i);
  float b = hash12(i + vec2(1.0, 0.0));
  float c = hash12(i + vec2(0.0, 1.0));
  float d = hash12(i + vec2(1.0, 1.0));

  float k1 = b - a;
  float k2 = c - a;
  float k4 = a - b - c + d;
  return vec3(a + k1 * u.x + k2 * u.y + k4 * u.x * u.y,
              du * (vec2(k1, k2) + k4 * u.yx));
}

// Fractal sum: (value, d/dx, d/dy) with derivatives w.r.t. the INPUT p.
vec3 fbm(vec2 p, int octaves) {
  float amp = 0.5;
  float freq = 1.0;
  vec3 sum = vec3(0.0);
  for (int i = 0; i < octaves; i++) {
    vec3 n = noised(p * freq + float(i) * 17.31);
    sum += vec3(n.x, n.yz * freq) * amp;
    freq *= 2.03;
    amp *= 0.5;
  }
  return sum;
}

// Triplanar fbm: (value, d/dx, d/dy, d/dz) w.r.t. the INPUT p. Planes whose
// blend weight is negligible are skipped (flat ground costs a single fbm).
vec4 triFbm(vec3 p, vec3 w, int octaves) {
  vec4 r = vec4(0.0);
  if (w.y > 0.01) {
    vec3 n = fbm(p.xz, octaves);
    r += w.y * vec4(n.x, n.y, 0.0, n.z);
  }
  if (w.x > 0.01) {
    vec3 n = fbm(p.zy, octaves);
    r += w.x * vec4(n.x, 0.0, n.z, n.y);
  }
  if (w.z > 0.01) {
    vec3 n = fbm(p.xy, octaves);
    r += w.z * vec4(n.x, n.y, n.z, 0.0);
  }
  return r;
}


// ---------------------------------------------------------------------------
// Extra noise: ridged fractal, cellular (stones / cracked earth), strata, joints
// ---------------------------------------------------------------------------

// Ridged fractal: sharp crests. (value, d/dx, d/dy) w.r.t. p. Each octave is
// weighted by the previous one so crests stay crisp while valleys stay calm.
vec3 ridged(vec2 p, int octaves) {
  float amp = 0.5, freq = 1.0, w = 1.0;
  vec3 sum = vec3(0.0);
  for (int i = 0; i < octaves; i++) {
    vec3 n = noised(p * freq + float(i) * 31.7);
    float a = 2.0 * n.x - 1.0;
    float r = 1.0 - abs(a);
    vec2 dr = -sign(a) * 2.0 * n.yz;
    sum += vec3(r * r, 2.0 * r * dr * freq) * amp * w;
    w = clamp(r * r * 2.0, 0.0, 1.0);
    freq *= 2.1;
    amp *= 0.5;
  }
  return sum;
}

vec4 triRidged(vec3 p, vec3 w, int octaves) {
  vec4 r = vec4(0.0);
  if (w.y > 0.01) { vec3 n = ridged(p.xz, octaves); r += w.y * vec4(n.x, n.y, 0.0, n.z); }
  if (w.x > 0.01) { vec3 n = ridged(p.zy, octaves); r += w.x * vec4(n.x, 0.0, n.z, n.y); }
  if (w.z > 0.01) { vec3 n = ridged(p.xy, octaves); r += w.z * vec4(n.x, n.y, n.z, 0.0); }
  return r;
}

// Layered sediment beds along world Y.
// returns (protrusion [0,1], d protrusion / dy, thick-bed tone, thin-bed tone).
// Hard beds stand proud, soft beds are recessed, giving real ledges.
vec4 strata(float y) {
  vec3 s1 = noised(vec2(y * 0.11, 1.7));  // ~9 m beds
  vec3 s2 = noised(vec2(y * 0.45, 5.3));  // ~2 m beds
  vec3 s3 = noised(vec2(y * 1.70, 9.1));  // ~0.6 m seams
  float hard  = 0.55 * s1.x + 0.30 * s2.x + 0.15 * s3.x;
  float dHard = 0.55 * s1.y * 0.11 + 0.30 * s2.y * 0.45 + 0.15 * s3.y * 1.70;
  float t = (hard - 0.35) * 4.0;
  float tc = clamp(t, 0.0, 1.0);
  float prot = tc * tc * (3.0 - 2.0 * tc);
  float dprot = (t > 0.0 && t < 1.0) ? 6.0 * tc * (1.0 - tc) * 4.0 * dHard : 0.0;
  return vec4(prot, dprot, s1.x, s3.x * 0.6 + s2.x * 0.4);
}

// Crack lines along isolines of a noise field: (mask, d/dx, d/dy).
vec3 crackLines(vec2 q, float w) {
  vec3 n = noised(q);
  float a = n.x - 0.5;
  float t = abs(a) / w;
  if (t >= 1.0) return vec3(0.0);
  float c = 1.0 - t * t * (3.0 - 2.0 * t);
  float dc = -6.0 * t * (1.0 - t) * sign(a) / w;
  return vec3(c, dc * n.yz);
}

// Joints: mostly vertical on cliff faces (stretched along Y), polygonal on
// flat rock. Returns the mask; `grad` is the gradient of the crack mask in world space.
float jointCracks(vec3 P, vec3 tw, float fh, float fv, float w, out vec3 grad) {
  float m = 0.0;
  grad = vec3(0.0);
  if (tw.x > 0.01) {
    vec3 c = crackLines(vec2(P.z * fh, P.y * fv), w);
    m += tw.x * c.x; grad += tw.x * vec3(0.0, c.z * fv, c.y * fh);
  }
  if (tw.z > 0.01) {
    vec3 c = crackLines(vec2(P.x * fh, P.y * fv), w);
    m += tw.z * c.x; grad += tw.z * vec3(c.y * fh, c.z * fv, 0.0);
  }
  if (tw.y > 0.01) {
    vec3 c = crackLines(P.xz * fh * 0.8, w);
    m += tw.y * c.x; grad += tw.y * vec3(c.y * fh * 0.8, 0.0, c.z * fh * 0.8);
  }
  return m;
}

// ---------------------------------------------------------------------------
// Heightmap-based shadowing / occlusion
// ---------------------------------------------------------------------------
float heightAt(vec2 xz) {
  vec2 uv = (xz - push.heightmapRect.xy) / push.heightmapRect.zw;
  return textureLod(heightmap, uv, 0.0).r;
}

float heightmapCoverage(vec2 xz, float margin) {
  vec2 rel = xz - push.heightmapRect.xy;
  vec2 edge = min(rel, push.heightmapRect.zw - rel);
  return smoothstep(margin, margin + 24.0, min(edge.x, edge.y));
}

// Cheap cavity AO: if the surrounding terrain (two rings of 4 taps at
// different radii) sits above this point, it's in a gully/valley and gets
// darker. Two scales catch both small gullies and broad basins.
float ringAvg(vec2 xz, float R) {
  return 0.25 * (heightAt(xz + vec2(R, 0.0)) + heightAt(xz - vec2(R, 0.0)) +
                 heightAt(xz + vec2(0.0, R)) + heightAt(xz - vec2(0.0, R)));
}

float macroOcclusion(vec3 P) {
  float coverage = heightmapCoverage(P.xz, 12.0);
  if (coverage <= 0.0) return 1.0;
  float occNear = ringAvg(P.xz, 3.0) - P.y;
  float occFar  = ringAvg(P.xz, 12.0) - P.y;
  float occ = 0.6 * smoothstep(0.0, 3.0, occNear) + 0.4 * smoothstep(0.0, 10.0, occFar);
  return 1.0 - 0.6 * occ * coverage;
}

// Soft sun shadow cast by the terrain onto itself: march from the surface
// toward the sun through the heightmap. Samples are distributed
// quadratically (dense near the surface, sparse far away) and jittered per
// pixel to turn banding into noise.
float terrainShadow(vec3 origin, vec3 L, float dist) {
  int steps = int(push.heightParams.w);
  float coverage = heightmapCoverage(origin.xz, 1.0);
  if (steps <= 0 || coverage <= 0.0)
    return 1.0;
  // Distant pixels are small and low-contrast: spend fewer taps on them.
  steps = max(int(float(steps) * mix(1.0, 0.4, smoothstep(100.0, 600.0, dist))),
              min(steps, 4));

  float maxY = push.heightParams.x + push.heightParams.y;
  // Distance at which the ray climbs above the tallest possible terrain.
  float tMax = (maxY - origin.y) / max(L.y, 0.05);
  if (tMax <= 0.0)
    return 1.0;

  float jitter = ign(gl_FragCoord.xy);
  float res = 1.0;
  for (int i = 0; i < steps; i++) {
    float s = (float(i) + jitter) / float(steps);
    float t = 0.4 + tMax * s * s;
    vec3 q = origin + L * t;
    vec2 uv = (q.xz - push.heightmapRect.xy) / push.heightmapRect.zw;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
      break;
    float d = q.y - textureLod(heightmap, uv, 0.0).r;
    res = min(res, clamp(SHADOW_SOFTNESS * d / t, 0.0, 1.0));
    if (res <= 0.0)
      break;
  }
  return mix(1.0, smoothstep(0.0, 1.0, res), coverage);
}

// ---------------------------------------------------------------------------
// BRDF (same terms as entities.frag, so terrain matches the rest of the scene)
// ---------------------------------------------------------------------------
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
  return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(float NdotH, float roughness) {
  float a2 = pow(roughness * roughness, 2.0);
  float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
  return a2 / (PI * d * d);
}

float GeometrySchlickGGX(float NdotX, float k) {
  return NdotX / (NdotX * (1.0 - k) + k);
}

float GeometrySmith(float NdotV, float NdotL, float roughness) {
  float r = roughness + 1.0;
  float k = (r * r) / 8.0;
  return GeometrySchlickGGX(NdotV, k) * GeometrySchlickGGX(NdotL, k);
}

void main() {
  const vec3 P = fragPosWorld;
  const vec3 Ng = normalize(fragNormalWorld);
  vec3 toCam = ubo.inverseView[3].xyz - P;
  float dist = length(toCam);
  vec3 V = toCam / max(dist, 1e-4);

  // Detail fades out with distance (hides aliasing, saves fetches).
  float fineFade = 1.0 - smoothstep(25.0, 140.0, dist);
  float midFade = 1.0 - smoothstep(120.0, 500.0, dist);
  // Screen-space filtering also handles grazing ground and reflections.
  float pixelWorld = max(length(dFdx(P)), length(dFdy(P)));
  fineFade *= 1.0 - smoothstep(0.035, 0.22, pixelWorld);
  midFade *= 1.0 - smoothstep(0.6, 3.0, pixelWorld);

  // ---- Per-pixel altitude / slope ----
  float baseH = push.heightParams.x;
  float rangeH = max(push.heightParams.y, 1e-3);
  float cosRock = push.heightParams.z;
  float alt = clamp((P.y - baseH) / rangeH, 0.0, 1.0);
  float slope = clamp((1.0 - Ng.y) / max(1.0 - cosRock, 1e-3), 0.0, 1.0);

  // ---- Biome ----
  Climate clim = climateAt(P.xz, alt);
  Biome bio = biomeAt(clim);
  WorldProfile profile = worldProfile(bio, P.xz);
  float sandy = clamp(bio.desert + 0.65 * bio.steppe, 0.0, 1.0); // sandstone vs granite

  // Large-scale variation so layer boundaries wander.
  float macroA = fbm(P.xz * 0.012, 3).x - NOISE_MEAN;
  float macroB = fbm(P.xz * 0.031 + 41.7, 3).x - NOISE_MEAN;
  float gAlt = alt + macroA * 0.20;

  // Where grass actually grows: the compute shader's spawn mask (altitude +
  // slope suitability) times the biome density, which biome.hpp mirrors on the CPU.
  float grassSuit = grassSuitability(alt, slope) * profile.controls.x;
  float grassCover = 1.0 - pow(1.0 - grassSuit, GRASS_COVER_CURVE);

  // ---- Layer weights ----
  float rockW = smoothstep(0.38 - 0.10 * bio.desert, 0.75, slope + macroA * 0.35);
  rockW = max(rockW, 0.6 * smoothstep(0.72, 0.90, gAlt)); // bare summit rock
  float snowA = biomeSnowAmount(clim.temp + macroB * 0.15) * profile.ground.a;
  float snowW = snowA * (1.0 - smoothstep(0.30, 0.65, slope + macroB * 0.2));
  float grassW = grassCover * (1.0 - snowW);

  vec3 tw = pow(abs(Ng), vec3(6.0));
  tw /= (tw.x + tw.y + tw.z);

  // =========================================================================
  // Geometry detail (normals). Everything is a height-field gradient that is
  // summed, faded with distance, then applied once.
  // =========================================================================
  // ---- Ground ----
  vec3 gDet = vec3(NOISE_MEAN, 0.0, 0.0);   // (value, d/dx, d/dz)
  vec3 groundGrad = vec3(0.0);
  float rip = 0.5;
  if (rockW < 0.999 && midFade > 0.01) {
    if (grassW < 0.98) {
      if (fineFade > 0.01)
        gDet = fbm(P.xz * GROUND_FREQ, 3);
      vec3 broad = fbm(P.xz * BROAD_FREQ, 2);
      groundGrad += vec3(broad.y, 0.0, broad.z) * BROAD_FREQ * BROAD_BUMP * midFade;
      groundGrad += vec3(gDet.y, 0.0, gDet.z) * GROUND_FREQ * GROUND_BUMP * fineFade;
    }

    // Desert ripples on gentle sand slopes.
    float flatness = 1.0 - smoothstep(0.10, 0.35, slope);
    float rippleW = bio.desert * flatness * fineFade;
    if (rippleW > 0.01) {
      vec2 rd = ubo.wind.xy;
      float k = 6.2831853 / RIPPLE_WAVELENGTH;
      float warpR = (fbm(P.xz * 0.15, 2).x - NOISE_MEAN) * 9.0;
      float ph = dot(P.xz, rd) * k + warpR;
      rip = 0.5 + 0.5 * sin(ph);
      groundGrad += vec3(rd.x, 0.0, rd.y) * (0.5 * cos(ph) * k * 0.012) * rippleW;
    }

  }

  // ---- Rock (cliffs) ----
  vec4 rDet4 = vec4(NOISE_MEAN, 0.0, 0.0, 0.0);
  vec4 rRid = vec4(RIDGE_MEAN, 0.0, 0.0, 0.0);
  vec4 st4 = vec4(0.5, 0.0, 0.5, 0.5);
  vec3 rockGrad = vec3(0.0);
  float crackM = 0.0;
  if (rockW > 0.001 && midFade > 0.01) {
    rDet4 = triFbm(P * ROCK_FREQ, tw, 3);
    rRid = triRidged(P * RIDGE_FREQ, tw, 2);
    // Beds tilt gently and undulate so strata never look like contour lines.
    st4 = strata(P.y + macroA * 14.0 + dot(P.xz, vec2(0.035, 0.012)));
    float jointFade = (1.0 - smoothstep(50.0, 220.0, dist)) *
                      (1.0 - smoothstep(0.04, 0.30, pixelWorld));
    vec3 cg = vec3(0.0);
    if (jointFade > 0.01) {
      vec3 g1, g2;
      float m1 = jointCracks(P, tw, 0.9, 0.09, 0.045, g1); // big blocky joints
      float m2 = jointCracks(P, tw, 3.1, 0.34, 0.070, g2); // fine fractures
      float jointAmt = jointFade * mix(1.0, 0.35, sandy);   // sandstone: fewer joints
      crackM = clamp(m1 + 0.6 * m2, 0.0, 1.0) * jointAmt;
      cg = -(g1 * CRACK_AMP + g2 * CRACK_AMP * 0.5) * jointAmt;
    }
    float strataAmp = mix(0.12, 0.28, sandy);
    rockGrad = rDet4.yzw * ROCK_FREQ * ROCK_BUMP +
               rRid.yzw * RIDGE_FREQ * RIDGE_BUMP +
               vec3(0.0, st4.y * strataAmp, 0.0) + cg;
    float gl = length(rockGrad);
    if (gl > 2.5) rockGrad *= 2.5 / gl; // keep sharp joints from going unstable
  }

  vec3 grad = mix(groundGrad * (1.0 - grassW),
                  rockGrad * midFade, rockW);
  grad *= 1.0 - 0.7 * snowW; // snow smooths things over
  vec3 N = normalize(Ng - (grad - Ng * dot(grad, Ng)));

  // =========================================================================
  // Materials
  // =========================================================================
  float gV = mix(NOISE_MEAN, gDet.x, fineFade);
  float rV = mix(NOISE_MEAN, rDet4.x, midFade);
  float ridgeV = mix(RIDGE_MEAN, rRid.x, midFade);
  float patchN = noised(P.xz * 0.45 + 3.1).x; // moss / lichen patches

  // ---- Bare ground, per biome ----
  vec3 gMeadow = mix(SCREE, DIRT, smoothstep(0.02, 0.18, gAlt));
  vec3 gSteppe = mix(STEPPE_SOIL * 0.8, STEPPE_SOIL * 1.2, smoothstep(0.25, 0.7, gV));
  vec3 gDesert = mix(SAND_DARK, SAND, smoothstep(0.15, 0.85, 0.65 * gV + 0.35 * rip + 0.1));
  vec3 gForest = mix(LITTER, MOSS, smoothstep(0.52, 0.72, patchN) * 0.9);
  vec3 gTundra = mix(TUNDRA_GND, ALPINE, smoothstep(0.35, 0.80, gAlt));
  gTundra = mix(gTundra, LICHEN, smoothstep(0.50, 0.78, patchN) * 0.55);
  vec3 ground = bio.meadow * gMeadow + bio.steppe * gSteppe + bio.desert * gDesert +
                bio.forest * gForest + bio.tundra * gTundra;
  ground = profile.ground.rgb * (0.84 + 0.34 * gV);

  // Large stones are actual streamed meshes. Fine soil variation stays
  // restrained so it does not look like a repeating field of round stamps.

  // ---- Rock: granite vs. banded sandstone ----
  vec3 graniteCol = mix(GRANITE_DARK, GRANITE_LIGHT, smoothstep(0.20, 0.75, rV));
  graniteCol *= 0.88 + 0.24 * st4.w;
  graniteCol = mix(graniteCol, graniteCol * vec3(1.15, 0.95, 0.80),
                   smoothstep(-0.10, 0.20, macroB)); // iron staining
  vec3 sandCol = mix(SAND_RED, SAND_CREAM, smoothstep(0.30, 0.70, st4.z));
  sandCol = mix(sandCol, SAND_SEAM, 0.55 * smoothstep(0.62, 0.92, st4.w));
  sandCol *= 0.80 + 0.40 * st4.x; // protruding beds catch more light
  vec3 rockCol = mix(graniteCol, sandCol, sandy);
  rockCol = profile.rock.rgb * (0.80 + 0.36 * rV);
  rockCol *= 0.92 + 0.12 * smoothstep(0.08, 0.60, ridgeV);
  rockCol *= 1.0 - 0.55 * crackM;

  // Weathering streaks running down the face.
  float horiz = (tw.x * P.z + tw.z * P.x) / (tw.x + tw.z + 1e-3);
  float streak = midFade > 0.01 ? noised(vec2(horiz * 2.5, P.y * 0.08)).x : 0.5;
  rockCol *= 1.0 - mix(0.30, 0.45, sandy) * smoothstep(0.55, 0.85, streak) * (1.0 - tw.y);

  // Moss / lichen on damp, upward-facing rock.
  float damp = clamp(clim.moist * 1.4 - 0.25, 0.0, 1.0) * (1.0 - bio.desert) * (1.0 - snowA);
  float mossNoise = midFade > 0.01 ? noised(P.xz * 0.8 + P.y * 0.7 + 91.0).x : 0.5;
  float mossM = smoothstep(0.52, 0.72, mossNoise) * damp * (0.25 + 0.75 * smoothstep(0.1, 0.8, N.y));
  vec3 mossCol = mix(MOSS * 1.6, LICHEN * 0.8, bio.tundra + 0.4 * bio.meadow);
  rockCol = mix(rockCol, mossCol, 0.85 * mossM);

  // Ledges: any near-horizontal part of a cliff collects dust, and snow in the cold.
  float ledge = smoothstep(0.55, 0.90, N.y);
  rockCol = mix(rockCol, ground, 0.55 * ledge * (1.0 - mossM));
  float snowLedge = clamp(snowA * 1.3 + bio.tundra * 0.15, 0.0, 1.0) * ledge;
  rockCol = mix(rockCol, SNOW * 0.95, snowLedge);

  vec3 albedo = mix(ground, rockCol, rockW);
  albedo = mix(albedo, SNOW * (0.92 + 0.16 * gV), snowW);

  // ---- Roughness: terrain is rough. Snow slightly smoother. ----
  float dV = mix(gV, rV, rockW);
  float roughness = mix(0.93, mix(0.86, 0.93, sandy), rockW);
  roughness = mix(roughness, 0.97, rockW * (crackM * 0.5 + mossM * 0.5));
  roughness = mix(roughness, 0.62, max(snowW, rockW * snowLedge));
  roughness = clamp(roughness + (dV - NOISE_MEAN) * 0.15, 0.5, 1.0);

  // ---- Occlusion: fine cavities + heightmap curvature ----
  float aoDetail = mix(1.0, clamp(0.40 + 1.20 * dV, 0.35, 1.0), midFade);
  float aoRock = (1.0 - 0.55 * crackM) *
                 mix(0.55, 1.0, smoothstep(0.08, 0.55, ridgeV)) *
                 mix(0.70, 1.0, st4.x);
  aoDetail *= mix(1.0, mix(1.0, aoRock, midFade), rockW);
  float ao = aoDetail * macroOcclusion(P);

  // ---- Lighting (sun matches entities.frag so everything agrees) ----
  vec3 L = sunDirection();
  vec3 H = normalize(V + L);
  vec3 radiance = sunRadiance();

  float NdotV = max(dot(N, V), 0.0);
  float NdotL = max(dot(N, L), 0.0);
  float NdotH = max(dot(N, H), 0.0);
  float HdotV = max(dot(H, V), 0.0);

  float NgL = dot(Ng, L);
  float shadow = 0.0;
  if (NgL > 0.0)
    shadow = smoothstep(0.0, 0.08, NgL) * terrainShadow(P + Ng * 0.3, L, dist);

  vec3 F0 = vec3(0.04); // dielectric, metallic = 0
  vec3 F = fresnelSchlick(HdotV, F0);
  float NDF = DistributionGGX(NdotH, roughness);
  float G = GeometrySmith(NdotV, NdotL, roughness);

  vec3 kD = 1.0 - F;
  vec3 specular = min((NDF * G * F) / (4.0 * NdotV * NdotL + 0.0001), vec3(4.0));
  vec3 diffuse = kD * albedo / PI;
  vec3 Lo = (diffuse + specular) * NdotL * radiance * shadow;

  // Tinted hemisphere ambient: bluish sky from above, warm ground bounce from below.
  vec3 skyAmbient = ubo.skyAmbient.rgb;
  vec3 groundAmbient = ubo.groundAmbient.rgb;
  vec3 ambient = albedo * mix(groundAmbient, skyAmbient, N.y * 0.5 + 0.5) * ao;

  // Sky reflection at grazing angles (soft sheen on snow / smooth rock).
  float gloss = 1.0 - roughness;
  vec3 Famb = F0 + (max(vec3(gloss), F0) - F0) * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);
  vec3 ambSpec = Famb * skyAmbient * ao * (1.0 - roughness * 0.6) * 0.5;

  BioHabitat habitat = bioHabitat(P.xz);
  vec3 streamLight = bioIrradiance(habitat, Ng);
  vec3 terrainColor = ambient + ambSpec + Lo + albedo * streamLight;

  // ---- Grass-covered ground ----
  // Lit with grass.frag's own model, so ground and blades match in color AND
  // brightness. grass.frag has no terrain shadows, so near the camera (where
  // real blades are drawn) the ground ignores them too; the shadow fades in
  // past the end of the field, where only the ground is visible.
  float sunVisGrass = mix(1.0, shadow, smoothstep(GRASS_FIELD_END * 0.6, GRASS_FIELD_END, dist));
  vec3 grassColor = grassCanopyLit(P, Ng, V, dist, sunVisGrass, profile.controls.z, profile.grass.rgb, streamLight);
  vec3 color = mix(terrainColor, grassColor, grassW);
  // Active crater floors and narrow branching lava seams emit linear light.
  for(uint i=0u;i<world.counts.z;++i) {
    WorldVolcano v=world.volcanoes[i];
    float r=distance(P.xz,v.positionRadius.xz);
    float crater=v.positionRadius.w*v.shape.y;
    if(v.shape.w<0.5 || r>crater*0.85) continue;
    float core=1.0-smoothstep(crater*0.45,crater*0.82,r);
    float fissure=1.0-smoothstep(0.035,0.09,abs(noised(P.xz*0.13).x-0.5));
    float heat=v.shape.w*core*(0.22+0.78*fissure);
    color=mix(color,vec3(1.4,0.17,0.012),heat);
  }

  // ---- Aerial perspective (shared with grass.frag) ----
  color += bioSurface(P, Ng, max(length(dFdx(P)), length(dFdy(P))), habitat);
  color = sceneFog(color, P, V, dist);

  // Must match grass.frag (both read SCENE_TONEMAP_IN_SHADER).
  if (SCENE_TONEMAP_IN_SHADER) {
    color = color / (color + vec3(1.0)); // tonemap (same as entities.frag)
    color = pow(color, vec3(1.0 / 2.2));  // gamma
    // Dither to hide 8-bit banding in smooth gradients.
    color += (ign(gl_FragCoord.xy + 5.588) - 0.5) / 255.0;
  }

  outColor = vec4(color, 1.0);
}
