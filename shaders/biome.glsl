// biome.glsl - shared climate + biome weights. Place next to grassLook.glsl.
// biome.hpp is the C++ mirror (same integer hash, same constants): keep the
// two in sync so grass / trees follow exactly what the terrain shows.
//
// Five soft biomes, blended (never hard-switched):
//   meadow  temperate grassland (the original look; always used near spawn)
//   steppe  dry grassland: golden sparse grass over tan soil, red-ochre cliffs
//   desert  sand + ripples + cracked earth, banded sandstone cliffs, no grass
//   forest  moist: dark needle litter + moss, mossy cliffs, reduced grass
//   tundra  cold / high: lichen + gravel, little grass, snow
#ifndef BIOME_GLSL
#define BIOME_GLSL
#include "worldData.glsl"

// ---- Tuning (mirror in biome.hpp) -------------------------------------------
#define BIOME_TEMP_SCALE (world.climate.x)
#define BIOME_MOIST_SCALE (world.climate.y)
#define BIOME_LAPSE (world.climate.z)
#define BIOME_HOME_R0 (world.map.x)
#define BIOME_HOME_R1 (world.map.y)

uint bHash(uint n) {
  n = (n ^ 61u) ^ (n >> 16u);
  n *= 9u;
  n = n ^ (n >> 4u);
  n *= 0x27d4eb2du;
  n = n ^ (n >> 15u);
  return n;
}
float bCell(ivec2 c, uint seed) {
  return float(bHash(uint(c.x) * 73856093u ^ uint(c.y) * 19349663u ^ seed)) *
         (1.0 / 4294967296.0);
}
float bNoise(vec2 p, uint seed) {
  vec2 i = floor(p), f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  ivec2 c = ivec2(i);
  return mix(mix(bCell(c, seed), bCell(c + ivec2(1, 0), seed), u.x),
             mix(bCell(c + ivec2(0, 1), seed), bCell(c + ivec2(1, 1), seed), u.x),
             u.y);
}
// 3 octaves, normalized to [0,1].
float bFbm(vec2 p, uint seed) {
  float v = bNoise(p, seed) * 0.5 + bNoise(p * 2.0 + 17.0, seed) * 0.25 +
            bNoise(p * 4.0 + 41.0, seed) * 0.125;
  return v / 0.875;
}
// Contrast curve: value noise clusters around 0.5; this spreads it out so every
// biome actually occurs.
float bSpread(float x) { return smoothstep(0.18, 0.82, x); }

struct Climate {
  float tempBase; // temperature at sea level (no altitude lapse), ~[0.05, 1.1]
  float temp;     // tempBase - lapse * altitude
  float moist;    // [0, 1]
};

// alt = (y - baseHeight) / heightRange, clamped to [0,1].
Climate climateAt(vec2 xz, float alt) {
  float tn = bSpread(bFbm(xz * BIOME_TEMP_SCALE, 0x1234u));
  float mn = bSpread(bFbm(xz * BIOME_MOIST_SCALE + 7.7, 0x5678u));
  float rg = bNoise(xz * (1.0 / 140.0), 0x9abcu) - 0.5; // ragged boundaries
  // Near the spawn point keep the original temperate look.
  float away = smoothstep(BIOME_HOME_R0, BIOME_HOME_R1, length(xz));
  tn = mix(0.55, tn, away);
  mn = mix(0.52, mn, away);
  Climate c;
  c.tempBase = mix(0.15, 1.0, tn) + rg * 0.10 * away;
  c.temp     = c.tempBase - BIOME_LAPSE * alt;
  c.moist    = clamp(mn - rg * 0.10 * away, 0.0, 1.0);
  return c;
}

// Warm-climate distribution, independent of altitude: (meadow, steppe, desert,
// forest), sums to 1. (Altitude only adds the tundra overlay in biomeAt, so
// the grass compute shader can evaluate this from XZ alone.)
vec4 warmDistribution(float tempBase, float moist) {
  float hot = smoothstep(0.55, 0.85, tempBase);
  float dry = 1.0 - smoothstep(0.25, 0.50, moist);
  float wet = smoothstep(0.55, 0.80, moist);
  float desert = hot * dry;
  float steppe = (1.0 - desert) * dry * (0.80 + 0.20 * hot);
  float forest = (1.0 - hot * 0.7) * wet * (1.0 - dry) * (1.0 - desert - steppe);
  float meadow = max(1.0 - desert - steppe - forest, 0.0) + 1e-3;
  return vec4(meadow, steppe, desert, forest) / (meadow + steppe + desert + forest);
}

struct Biome {
  float meadow, steppe, desert, forest, tundra; // sum to 1
};

Biome biomeAt(Climate c) {
  float tundra = 1.0 - smoothstep(0.02, 0.30, c.temp);
  vec4 w = warmDistribution(c.tempBase, c.moist) * (1.0 - tundra);
  Biome b;
  b.meadow = w.x; b.steppe = w.y; b.desert = w.z; b.forest = w.w;
  b.tundra = tundra;
  return b;
}

WorldProfile mixProfile(WorldProfile a, WorldProfile b, float t) {
  return WorldProfile(mix(a.ground,b.ground,t),mix(a.rock,b.rock,t),
    mix(a.grass,b.grass,t),mix(a.foliage,b.foliage,t),mix(a.controls,b.controls,t));
}
WorldProfile worldProfile(Biome b, vec2 xz) {
  float weights[5]=float[5](b.meadow,b.steppe,b.desert,b.forest,b.tundra);
  WorldProfile p=WorldProfile(vec4(0),vec4(0),vec4(0),vec4(0),vec4(0));
  for(int i=0;i<5;++i) {
    float w=weights[i];
    p.ground+=world.profiles[i].ground*w; p.rock+=world.profiles[i].rock*w;
    p.grass+=world.profiles[i].grass*w; p.foliage+=world.profiles[i].foliage*w;
    p.controls+=world.profiles[i].controls*w;
  }
  for(uint i=0u;i<world.counts.y;++i) {
    vec4 r=world.regions[i].centerRadius;
    float w=1.0-smoothstep(max(0.0,r.z-r.w),r.z,distance(xz,r.xy));
    p=mixProfile(p,world.profiles[uint(world.regions[i].selection.x)],w);
  }
  return p;
}
WorldProfile worldProfileXZ(vec2 xz,float altitude) {
  return worldProfile(biomeAt(climateAt(xz,altitude)),xz);
}

// How densely grass spawns (multiplies the spawn suitability).
float biomeGrassDensity(Biome b) {
  return b.meadow + b.steppe * 0.55 + b.forest * 0.45 + b.tundra * 0.06;
}
// Extra yellowing pushed into grass (0 = green). Altitude-free on purpose.
float biomeGrassDryness(Climate c) {
  vec4 w = warmDistribution(c.tempBase, c.moist);
  return clamp(w.y * 0.85 + w.w * 0.05, 0.0, 1.0);
}
float biomeGrassDrynessXZ(vec2 xz) { return worldProfileXZ(xz, 0.0).controls.z; }
// Tree density multiplier (for the CPU tree sampler; see biome.hpp).
float biomeTreeDensity(Biome b) {
  return b.meadow + b.forest * 1.5 + b.steppe * 0.2 + b.tundra * 0.25;
}
// Snow starts where it is cold enough. Replaces the fixed altitude snow line.
float biomeSnowAmount(float temp) { return 1.0 - smoothstep(-0.02, 0.14, temp); }

#endif
