#version 450

layout(location = 0) in vec3 fragPosWorld;
layout(location = 1) in vec3 fragNormalWorld;
layout(location = 2) in vec2 fragUV;
layout(location = 3) flat in float fragTint;
layout(location = 4) flat in float fragSeed;
layout(location = 5) flat in vec4 fragCoat;
layout(location = 6) in vec2 fragCoatUV;
layout(location = 7) in vec3 fragFlowWorld;
layout(location = 0) out vec4 outColor;
layout(set = 1, binding = 0) uniform sampler2D coatAtlas[256];
layout(push_constant) uniform Push {
  vec4 clipPlane;
  vec4 fur;
} push;

#include "../globalUbo.glsl"
#include "../environment.glsl"

float hash(vec2 p) {
  vec3 q = fract(vec3(p.xyx) * 0.1031);
  q += dot(q, q.yzx + 33.33);
  return fract((q.x + q.y) * q.z);
}

void main() {
  bool cards = push.fur.x > 0.5;
  float coverage = 1.0;
  vec2 coatUV = fragUV;
  if (cards) {
    // Each strip carries root-to-tip UVs and a persistent card identifier.
    // Curved strands and unequal tips break up the silhouette without
    // repeating a full extruded copy of the wolf's mesh.
    float id = floor(fragUV.x);
    float u = fract(fragUV.x);
    float t = clamp(fragUV.y, 0.0, 1.0);
    float strand = floor(u * 24.0);
    float jitter = hash(vec2(id, strand));
    float bentU = u * 24.0 + sin(t * 3.0 + jitter * 6.28) * t * 0.28;
    float d = abs(fract(bentU) - 0.5);
    float tip = 0.72 + 0.28 * jitter;
    float radius = mix(0.46, 0.025, smoothstep(0.12, tip, t));
    float aa = max(fwidth(bentU)*0.65, 0.035);
    coverage = 1.0 - smoothstep(radius - aa, radius + aa, d);
    coverage *= smoothstep(0.0, 0.06, u) * smoothstep(0.0, 0.06, 1.0-u);
    coverage *= 1.0 - smoothstep(tip - 0.08, tip, t);
    if (coverage < 0.03) discard;
    coatUV = fragCoatUV;
  }
  // The imported diffuse image is sRGB and hardware decodes it to linear.
  vec3 albedo = texture(coatAtlas[0], coatUV).rgb;
  if (cards)
    albedo = mix(albedo, max(albedo, vec3(0.35,0.365,0.34)), fragUV.y*0.18);
  if (cards || fragUV.y >= 0.24) {
    albedo = mix(albedo, vec3(0.72, 0.75, 0.78), fragCoat.a) *
             fragCoat.rgb * fragTint;
  } else if (fragCoat.a > 0.5 && abs(fragUV.x - 0.25) < 0.02) {
    albedo = vec3(0.22, 0.018, 0.014);
  }
  vec3 N = normalize(fragNormalWorld);
  vec3 V = normalize(ubo.inverseView[3].xyz - fragPosWorld);
  vec3 L = sunDirection();
  float diffuse = max(dot(N, L), 0.0);
  float wrap = max((dot(N, L) + 0.35) / 1.35, 0.0);
  float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);
  // Broad fiber sheen rather than shiny plastic. Root darkening gives the
  // short coat depth while the outer tips catch backlighting.
  float rootShade = cards ? mix(0.90, 1.06, fragUV.y) : 1.0;
  vec3 color = albedo * rootShade *
      (hemisphereAmbient(N) + sunRadiance() * mix(diffuse, wrap, 0.35) / 3.14159);
  // Broad anisotropic guard-hair sheen follows the groom instead of
  // producing a polished plastic highlight across the whole animal.
  vec3 flow = fragFlowWorld - N*dot(fragFlowWorld,N);
  float flowLength = length(flow);
  vec3 T = flow / max(flowLength, 0.001);
  vec3 H = normalize(L+V);
  float fiberSheen = pow(max(1.0-pow(dot(T,H),2.0),0.0),18.0);
  color += sunRadiance() * albedo * fiberSheen * diffuse * 0.06 *
           smoothstep(0.0,0.1,flowLength);
  color += sunRadiance() * albedo * rim * max(dot(-L, V), 0.0) * 0.18;
  color += albedo * rootShade * localLighting(fragPosWorld, N);
  color = sceneFog(color, fragPosWorld, V,
                   distance(ubo.inverseView[3].xyz, fragPosWorld));
  outColor = vec4(color, coverage);
}
