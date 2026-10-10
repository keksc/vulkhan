#version 450

layout(location = 0) in vec4 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inReflClipPos;
layout(location = 4) in float inWaterDepth;

layout(location = 0) out vec4 outColor;

#include "../globalUbo.glsl"
#include "../environment.glsl"

layout(set = 1, binding = 0) uniform VertexUBO {
    mat4 model;
    float scale;
    vec2 uvRepeat;
    float time;
    float chopScale;
} vertexUBO;

layout(set = 1, binding = 1) uniform sampler2D displacementFoamMap;
layout(set = 1, binding = 2) uniform sampler2D planarReflectionMap;
layout(set = 1, binding = 3) uniform sampler2D detailWaveMap;

#include "waveSampling.glsl"

// layout(set = 2, binding = 0) uniform samplerCube skybox;

// DEBUG ONLY - RULED IN then OFF: confirmed the reflection texture itself
// is correct (clip plane fix worked - see main.cpp). Set back to 0 so
// water renders normally again. The harsh, unlit, grid-seam-visible look
// reported after turning this on is expected of this debug path itself
// (it bypasses ALL shading - water tint, fresnel, lighting - and shows
// the raw render target directly) and is not necessarily a real bug on
// its own; re-check once this is off before chasing it further.
#define REFL_DEBUG_SHOW_RAW_TEXTURE 0

void main() {
  vec3 worldPos = inPos.xyz;

  // Foam intensity: the raw value baked in postFFT.comp is wave-strength
  // dependent, not per-instance, so a calm small lake (low
  // vertexUBO.scale) still got full-strength foam before this - scaling
  // by it here ties foam coverage to how energetic this patch's water
  // actually is, on top of the postFFT-side threshold/decay tuning.
  float waveLod = max(textureQueryLod(displacementFoamMap, inUV).y, 0.0);
  float foamMask = sampleWaterWaves(inUV, waveLod).a;
  foamMask = pow(foamMask, 1.6) * vertexUBO.scale;
  foamMask = clamp(foamMask, 0.0, 1.0);

  vec3 N = normalize(inNormal);
  // The 8m FFT cascade resolves centimetre ripples independently of mesh
  // spacing. Keep its coordinates attached to the undisplaced water UVs.
  vec2 detailUV = inUV * (1000.0 / 8.0);
  float detailLod = max(textureQueryLod(detailWaveMap, detailUV).y, 0.0);
  vec2 detailTexel = exp2(detailLod) /
                     vec2(textureSize(detailWaveMap, 0));
  float hx = sampleWaterField(detailWaveMap,
               detailUV + vec2(detailTexel.x, 0.0), detailLod).y -
             sampleWaterField(detailWaveMap,
               detailUV - vec2(detailTexel.x, 0.0), detailLod).y;
  float hz = sampleWaterField(detailWaveMap,
               detailUV + vec2(0.0, detailTexel.y), detailLod).y -
             sampleWaterField(detailWaveMap,
               detailUV - vec2(0.0, detailTexel.y), detailLod).y;
  vec2 detailSlope = vec2(hx, hz) / (2.0 * detailTexel * 8.0);
  N = normalize(N - vec3(detailSlope.x, 0.0, detailSlope.y) * N.y);

  vec3 camPos = vec3(ubo.inverseView[3]);
  vec3 V = normalize(camPos - worldPos);

  // Real reflections now come from the SSR compute pass (see ssr.comp),
  // which reads this fragment's alpha to find water pixels and their
  // Fresnel weight, then adds a proper screen-space-traced reflection on
  // top of whatever color this shader outputs below. This shader no
  // longer bakes any flat placeholder reflection tint into color itself -
  // doing so used to wash the whole lake out toward flat white at grazing
  // angles (a flat vec3(1.0)-ish tint times a Fresnel term pushing toward
  // 1.0 across most of the visible surface), and would now also double up
  // with SSR's own reflection on top of it.
  vec3 waterColor = vec3(0.03, 0.11, 0.16);
  vec3 foamColor = vec3(0.88);

  // F0/exponent/cap here are art-directed, not physically accurate on
  // purpose - real calm water reflects almost nothing when viewed close
  // to straight down (F0 ~= 0.02, a sharp pow(x,5) falloff), which is
  // correct but under-shows a reflection that's genuinely good-looking in
  // this scene (mountains + starry sky/nebula). Pushed up across all
  // three knobs together so more of the surface reads as reflective, not
  // just the steepest grazing angles:
  //   - F0 0.02 -> 0.12: meaningful reflection even near-overhead,
  //     instead of needing a steep angle to see anything at all.
  //   - pow exponent 5.0 -> 2.5: reflection ramps in over a much wider
  //     range of angles instead of staying near-zero until quite close to
  //     grazing, then suddenly appearing.
  float F0 = 0.12;
  float base = 1.0 - max(dot(N, V), 0.0);
  float fresnel = F0 + (1.0 - F0) * pow(base, 2.5);
  // Cap raised 0.75 -> 0.92 - still under 0.999 (ssr.comp's own "not a
  // water pixel" threshold - see its doc comment, unchanged reasoning),
  // just allowing grazing angles to read as almost fully reflective
  // instead of always keeping ~25% water tint mixed in.
  fresnel = min(fresnel, 0.92);

  vec3 finalColor = mix(waterColor, foamColor, foamMask) *
      (hemisphereAmbient(N) + sunRadiance() * .4 + vec3(.025));

  // Foam (whitecaps) doesn't reflect like a smooth water surface - zeroing
  // Fresnel under foam means neither SSR's mix(base, reflection, fresnel)
  // nor the Planar blend below contributes anything there, leaving foam
  // reading as flat bright color rather than picking up a reflection that
  // wouldn't make physical sense on turbulent, aerated water.
  float reflectWeight = fresnel * (1.0 - foamMask);

  // Planar: sample the pre-rendered mirrored-camera texture directly
  // and blend it in HERE, rather than leaving it to ssr.comp - a real
  // planar reflection is already a full, non-screen-space-limited
  // render of the reflected scene, so there's no "trace a ray and
  // maybe miss" step the way SSR has; it's just a direct texture
  // lookup once you have the right UV.
  vec2 reflUV = (inReflClipPos.xy / inReflClipPos.w) * 0.5 + 0.5;
  // Vulkan's clip-space Y is already flipped relative to GL's, and
  // PlanarReflectionSys renders its target with the same convention
  // ubo.projView itself uses (see its own doc comment) - so unlike a
  // classic OpenGL planar-reflection writeup, this UV does NOT need an
  // extra manual Y-flip on top of the standard *0.5+0.5 remap. If the
  // reflection comes out upside-down once this is wired up, flip
  // reflUV.y here rather than in PlanarReflectionSys - keeps the flip
  // co-located with the one place that actually consumes the texture.

  // A water fragment that's plainly visible to the REAL camera can
  // still land at a non-positive w in the MIRRORED camera's clip space
  // - mirroring effectively flips the camera's view direction, so
  // certain fragment positions/view angles relative to the mirror
  // plane fall outside the mirrored camera's forward half-space.
  // Dividing by a near-zero or negative w above doesn't give a
  // slightly-wrong UV - it can flip sign entirely or blow up toward
  // +-infinity, and once clamped below lands on an essentially
  // arbitrary spot in the reflection texture. That's exactly what read
  // as the reflection "breaking"/going haywire at certain view angles.
  // Fade the reflection's contribution to 0 as w approaches that
  // boundary (smoothstep, not a hard cutoff, so the fade itself has no
  // visible hard edge) rather than trying to salvage a UV that has no
  // sensible answer there - the division above still runs (touching a
  // garbage UV is harmless once its blend weight is zeroed), just its
  // result no longer reaches finalColor.
  float validReflW = smoothstep(0.0, 0.05, inReflClipPos.w);
  float planarWeight = reflectWeight * validReflW;

  // Distort the sample position by the water's own normal so the
  // reflection ripples with the waves instead of looking like a static
  // decal - same spirit as a normal map driving refraction/reflection
  // offset in most real-time water implementations. Scaled down a lot
  // because this is a UV-space offset (0..1 range), not a world-space
  // one - N itself is a world-space normal.
  //
  // Was 0.02 - against a 960px-wide target that's ~19 texels of jitter,
  // re-sampled fresh every frame as the wave normal shifts. The
  // resolved reflection target has no mip chain (single level - see
  // PlanarReflectionSys::resolvedColorTargets), so there's nothing to
  // filter that jitter across; right at high-contrast silhouette edges
  // (mountain-against-sky in the reflection) it reads as shimmer/
  // flicker rather than a subtle ripple. Dropped to keep the ripple
  // effect while cutting the jitter magnitude well below what's
  // visible as aliasing at those edges - raise cautiously and only
  // alongside giving resolvedColorTargets an actual mip chain (like
  // WSTessendorf's own displacementFoamMap has) if more ripple is
  // wanted later.
  const float distortion = 0.006;
  reflUV += N.xz * distortion;
  reflUV = clamp(reflUV, 0.001, 0.999); // stay off the edge - no eRepeat
                                        // wraparound smear from a
                                        // distorted sample landing just
                                        // past the target's border

  vec3 planarColor = texture(planarReflectionMap, reflUV).rgb;

  // Art-directed boost, not a correctness fix: a straight 1:1 texture
  // sample of the mirrored scene tends to read as slightly flatter/
  // duller than looking at the real geometry directly (no extra
  // lighting pass happens on the way into this texture beyond the
  // opaque scene's own shading). Nudging contrast and saturation up a
  // little makes the reflection itself look more vivid, which is the
  // actual ask here - keep this restrained, a heavy-handed version
  // reads as an obvious color-grade rather than "nicer reflection."
  float reflLuma = dot(planarColor, vec3(0.2126, 0.7152, 0.0722));
  planarColor = mix(vec3(reflLuma), planarColor, 1.25); // saturation
  planarColor = mix(vec3(reflLuma), planarColor, 1.1);  // mild contrast
  planarColor = clamp(planarColor, 0.0, 1.0);

  finalColor = mix(finalColor, planarColor, planarWeight);
  // A dielectric sun glint follows the same light as terrain and foliage.
  vec3 H = normalize(V + sunDirection());
  float nh = max(dot(N, H), 0.0);
  float nl = max(dot(N, sunDirection()), 0.0);
  float nv = max(dot(N, V), 0.001);
  const float a2 = 0.0004;
  float denom = nh * nh * (a2 - 1.0) + 1.0;
  float distribution = a2 / max(3.14159265 * denom * denom, 1e-6);
  float sunFresnel = 0.02 + 0.98 * pow(1.0 - max(dot(H, V), 0.0), 5.0);
  finalColor += sunRadiance() * min(distribution * sunFresnel * nl /
                    max(4.0 * nv, 0.04), 1.5) * (1.0 - foamMask);
  finalColor += localLighting(worldPos, N) * vec3(.12,.18,.22);
  finalColor = sceneFog(finalColor, worldPos, V, distance(camPos, worldPos));

  // Shallow water reveals the actual rendered bed. Greater optical path
  // length darkens deep water, while grazing angles and foam stay opaque.
  float opticalDepth = max(inWaterDepth, 0.0) / max(abs(dot(N, V)), 0.25);
  float tintOpacity = 1.0 - exp(-opticalDepth * 0.20);
  float opacity = mix(max(0.20, tintOpacity), 0.98, reflectWeight);
  opacity = mix(opacity, 1.0, foamMask);
  outColor = vec4(finalColor, clamp(opacity, 0.20, 0.98));
}
