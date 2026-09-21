#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in float fragHeightFrac;
layout(location = 2) in vec3 fragNormal1;
layout(location = 3) in vec3 fragNormal2;
layout(location = 4) in float fragXSide;
layout(location = 5) in float fragHighLODOut;
layout(location = 6) in vec3 fragWorldPos;
layout(location = 7) in float fragSpecies;
layout(location = 8) in vec3 fragTangent;
layout(location = 9) in float fragClumpVar;

layout(location = 0) out vec4 outColor;

#include "../globalUbo.glsl"

const vec3 baseColorLo = vec3(0.03, 0.16, 0.03);
const vec3 baseColorHi = vec3(0.06, 0.24, 0.05);
const vec3 tipColorLo  = vec3(0.28, 0.42, 0.10);
const vec3 tipColorHi  = vec3(0.40, 0.55, 0.20);

// Petal colors for the flower species' flared tip. Blended in only near
// the head (see petalMix below) so the stem below it still reads as
// green, like a real flower.
const vec3 petalColorLo = vec3(0.55, 0.16, 0.38);
const vec3 petalColorHi = vec3(0.88, 0.58, 0.78);

float easeIn(float x, float t) { return pow(x, t); }

void main() {
  // Blend the two curved-surface normals from the vertex shader so the
  // blade shades like a rounded stalk rather than a flat double-sided
  // quad - this is what actually sells the "3D blade" look, not the
  // geometry itself.
  vec3 N = normalize(mix(fragNormal1, fragNormal2, fragXSide));

  // Sun direction - arbitrary golden-hour angle. Swap for a uniform later
  // if the engine gains a day/night cycle.
  vec3 L = normalize(vec3(0.4, 1.0, 0.3));
  vec3 V = normalize(ubo.inverseView[3].xyz - fragWorldPos);
  vec3 lightColor = vec3(1.0, 0.98, 0.92);

  // Hemisphere ambient: cool sky color from above, warm ground-bounce
  // color from below, blended by the world-space up component of N. Does
  // more for a "gorgeous" look than tuning specular alone - a flat
  // ambient constant leaves shadowed sides of blades looking dead/grey,
  // whereas real grass always picks up a green/gold bounce from the field
  // around it and a cool tint from open sky.
  const vec3 skyColor = vec3(0.35, 0.45, 0.55);
  const vec3 groundColor = vec3(0.10, 0.10, 0.04);
  float hemiMix = N.y * 0.5 + 0.5;
  vec3 ambient = mix(groundColor, skyColor, hemiMix) * 0.5;

  // Half-Lambert wrap diffuse: softens the terminator so blades don't go
  // fully black on their shadowed side, which reads better at grass scale
  // than a hard N.L cutoff.
  const float wrap = 0.5;
  float dotNL = clamp((dot(N, L) + wrap) / (1.0 + wrap), 0.0, 1.0);
  vec3 diffuse = dotNL * lightColor;

  // Fake backscatter/translucency: light "shining through" thin blades
  // when the view direction roughly opposes the light. Faded out at high
  // LOD (fragHighLODOut) since distant/flatter blades don't sell it well
  // and it's not worth the extra ALU there.
  float backLight = clamp((dot(V, -L) + wrap) / (1.0 + wrap), 0.0, 1.0);
  vec3 scatter = lightColor * backLight * 0.5 * (1.0 - fragHighLODOut);

  vec3 albedoLo = mix(baseColorLo, tipColorLo, easeIn(fragHeightFrac, 4.0));
  vec3 albedoHi = mix(baseColorHi, tipColorHi, easeIn(fragHeightFrac, 4.0));
  vec3 albedo = mix(albedoHi, albedoLo, fragHighLODOut);

  // Clump-level color variance: shift brightness/warmth per patch of
  // ground (fragClumpVar comes from a very low-frequency noise sampled
  // once per blade position in the vertex shader) instead of every blade
  // sharing the exact same albedo curve. This is most of what actually
  // reads as "field" rather than "identical blades pasted everywhere" at
  // a distance - individual blade shape barely matters once you're more
  // than a few meters back.
  albedo *= mix(0.82, 1.2, fragClumpVar);
  albedo = mix(albedo, albedo * vec3(1.08, 1.0, 0.85), fragClumpVar); // warmer in brighter clumps

  // Blend to a petal hue over the flared top of flower instances only -
  // fragSpecies gates it off entirely for plain grass, and the height
  // smoothstep keeps the stem itself green.
  float petalMix = fragSpecies * smoothstep(0.55, 0.85, fragHeightFrac);
  vec3 petalColor = mix(petalColorLo, petalColorHi, fragHighLODOut);
  albedo = mix(albedo, petalColor, petalMix);

  // Anisotropic (Kajiya-Kay) highlight along the blade's tangent - the
  // glossy streak that catches light along a windswept blade's length,
  // rather than a normal isotropic specular blob. This is a big part of
  // why real/high-end grass reads as glossy and "alive" instead of matte
  // fuzz. Faded out at high LOD since it isn't worth the ALU on distant,
  // barely-resolved blades.
  vec3 T = normalize(fragTangent);
  vec3 H = normalize(L + V);
  float TdotH = dot(T, H);
  float sinTH = sqrt(clamp(1.0 - TdotH * TdotH, 0.0, 1.0));
  // Petals get a tighter, brighter highlight than stem/blade material -
  // reads as a slightly waxy/glossy petal surface vs. matte grass.
  float specPow = mix(12.0, 24.0, petalMix);
  float specGain = mix(0.6, 1.0, petalMix);
  float aniso = pow(sinTH, specPow) * (1.0 - fragHighLODOut);
  vec3 specular = lightColor * aniso * specGain;

  // Rim/fresnel light: grazing-angle kick lit by the sky rather than the
  // sun, so blade edges silhouette softly against open sky instead of the
  // whole field looking flat-lit from one direction. Skipped at high LOD
  // for the same ALU-budget reason as the other fine-detail terms.
  float fresnel = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 3.0);
  vec3 rim = skyColor * fresnel * 0.35 * (1.0 - fragHighLODOut);

  vec3 color = albedo * (diffuse * 1.1 + scatter + ambient) + specular + rim;
  // Slight exposure lift + warm push before tonemapping - the reference
  // footage's backlit golden-hour look is doing a lot of the work there,
  // more than any blade-shape detail would.
  color *= vec3(1.12, 1.06, 0.95);
  color = color / (color + vec3(1.0));
  color = pow(color, vec3(1.0 / 2.2));

  outColor = vec4(color, 1.0);
}
