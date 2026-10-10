#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in float fragHeightFrac;
layout(location = 2) in vec3 fragNormal1;
layout(location = 3) in vec3 fragNormal2;
layout(location = 4) in float fragXSide;
layout(location = 5) in float fragHighLODOut;
layout(location = 6) in vec3 fragWorldPos;
layout(location = 7) in float fragClumpColor; // clump colour seed [0,1)
layout(location = 8) in vec3 fragTangent;
layout(location = 9) in float fragClumpVar;
layout(location = 10) in float fragDry;
layout(location = 11) flat in vec3 fragBiomeTint;

layout(location = 0) out vec4 outColor;

#include "../globalUbo.glsl"
#include "../environment.glsl"
#include "../grassLook.glsl"

// Set to false if the rest of your pipeline tonemaps / gamma-corrects after
// the scene pass (otherwise grass gets tonemapped twice and looks washed out).
const bool APPLY_TONEMAP = SCENE_TONEMAP_IN_SHADER; // shared with terrain.frag

// Palette + lighting live in grassLook.glsl so the terrain can match them.
const vec3 baseColorLo = GRASS_BASE_LO;
const vec3 baseColorHi = GRASS_BASE_HI;
const vec3 tipColorLo  = GRASS_TIP_LO;
const vec3 tipColorHi  = GRASS_TIP_HI;
const vec3 dryBase     = GRASS_DRY_BASE;
const vec3 dryTip      = GRASS_DRY_TIP;
const vec3 soilColor   = vec3(0.07, 0.05, 0.03);

float easeIn(float x, float t) { return pow(x, t); }

void main() {
  vec3 V = normalize(ubo.inverseView[3].xyz - fragWorldPos);

  // Blades are double sided: pick the normal side facing the viewer.
  vec3 N = normalize(mix(fragNormal1, fragNormal2, fragXSide));
  if (dot(fragNormal1 + fragNormal2, V) < 0.0) N = -N;
  // Far away, relax toward the up/clump normal so thin blades don't sparkle.
  N = normalize(mix(N, vec3(0.0, 1.0, 0.0), fragHighLODOut * 0.6));

  vec3 L = sunDirection();
  vec3 lightColor = sunRadiance() * 0.5;

  vec3 skyColor = ubo.skyAmbient.rgb * 1.4;
  vec3 groundColor = ubo.groundAmbient.rgb;
  float hemiMix = N.y * 0.5 + 0.5;

  // Ambient occlusion toward the root.
  float ao = mix(0.35, 1.0, smoothstep(0.0, 0.45, fragHeightFrac));
  vec3 ambient = mix(groundColor, skyColor, hemiMix) * 0.5 * ao;

  const float wrap = GRASS_WRAP;
  float dotNL = clamp((dot(N, L) + wrap) / (1.0 + wrap), 0.0, 1.0);
  vec3 diffuse = dotNL * lightColor * mix(0.55, 1.0, ao);

  float backLight = clamp((dot(V, -L) + wrap) / (1.0 + wrap), 0.0, 1.0);
  vec3 scatter = lightColor * backLight * 0.2 * (1.0 - fragHighLODOut) * ao;

  // Colour: per-clump green variation, blended to dry patches.
  float grad = easeIn(fragHeightFrac, 3.0);
  vec3 greenLo = mix(baseColorLo, tipColorLo, grad);
  vec3 greenHi = mix(baseColorHi, tipColorHi, grad);
  // Near/far palettes kept close so near grass isn't much brighter.
  vec3 green = mix(greenHi, greenLo, fragHighLODOut * 0.35);
  green = mix(green, green * vec3(1.15, 1.05, 0.8), fragClumpColor * 0.4);
  vec3 dry = mix(dryBase, dryTip, grad);
  vec3 albedo = mix(green, dry, fragDry);
  albedo *= clamp(fragBiomeTint / vec3(0.28,0.42,0.10),vec3(0),vec3(4));

  albedo *= mix(0.82, 1.2, fragClumpVar);
  albedo = mix(albedo, albedo * vec3(1.08, 1.0, 0.85), fragClumpVar);

  // Midrib highlight + slightly darker edges (cheap vein texture).
  float center = 1.0 - abs(fragUV.x * 2.0 - 1.0);
  albedo *= mix(0.9, 1.0, smoothstep(0.0, 0.5, center));
  albedo *= 1.0 + 0.08 * smoothstep(0.6, 1.0, center) * (1.0 - fragHighLODOut);

  // Fake soil contact: root fades toward ground colour.
  albedo = mix(albedo, soilColor, (1.0 - smoothstep(0.0, 0.12, fragHeightFrac)) * 0.5);

  // Anisotropic (Kajiya-Kay) highlight: tight, dim, and masked by N.L.
  vec3 T = normalize(fragTangent);
  vec3 H = normalize(L + V);
  float TdotH = dot(T, H);
  float sinTH = sqrt(clamp(1.0 - TdotH * TdotH, 0.0, 1.0));
  float aniso = pow(sinTH, 48.0) * (1.0 - fragHighLODOut) * ao *
                clamp(dot(N, L) + 0.3, 0.0, 1.0);
  vec3 specular = lightColor * aniso * 0.12;

  float fresnel = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 3.0);
  vec3 rim = skyColor * fresnel * 0.12 * (1.0 - fragHighLODOut);

  vec3 color = albedo * (diffuse * 0.95 + scatter + ambient) + specular + rim;
  BioHabitat habitat = bioHabitat(fragWorldPos.xz);
  color += albedo * bioIrradiance(habitat,N);
  float flowering=smoothstep(.55,.78,bioNoise(fragWorldPos.xz*.7));
  color += habitat.tint*habitat.density*flowering*pow(max(fragHeightFrac,0.0),8.0)*1.2;
  color *= vec3(1.04, 1.02, 0.98);
  color = sceneFog(color, fragWorldPos, V, distance(ubo.inverseView[3].xyz, fragWorldPos));
  if (APPLY_TONEMAP) {
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
  }
  outColor = vec4(color, 1.0);
}
