#version 450
layout(location = 0) out vec2 fragUV;
layout(location = 1) out float fragHeightFrac;
layout(location = 2) out vec3 fragNormal1;
layout(location = 3) out vec3 fragNormal2;
layout(location = 4) out float fragXSide;
layout(location = 5) out float fragHighLODOut;
layout(location = 6) out vec3 fragWorldPos;
layout(location = 7) out float fragSpecies; // now: clump colour seed
layout(location = 8) out vec3 fragTangent;
layout(location = 9) out float fragClumpVar;
layout(location = 10) out float fragDry;
layout(location = 11) flat out vec3 fragBiomeTint;
#include "../globalUbo.glsl"
#include "../environment.glsl"
struct BladeInstance {
  vec3 basePos;
  float bendAngle;
  float facingAngle;
  float height;
  float width;
  float windPhase;
  float bendAngle2;
  float species; // clump colour seed
  vec2 pad2;     // x = dryness
  vec3 aabbMin;
  uint pad0;
  vec3 aabbMax;
  float pad1;
};
layout(std430, set = 1, binding = 0) readonly buffer BladeBuffer {
  BladeInstance blades[];
};
layout(set = 1, binding = 1) uniform WindUbo {
  float time;
  float strength;
  vec2 dir;
  float lodNearDist;
} wind;
const int SEGMENTS = 3;
const int VERTS_PER_BLADE = SEGMENTS * 2 + 1;

float easeOut(float x, float t) { return 1.0 - pow(1.0 - x, t); }
uint hashU(uint n) {
  n = (n ^ 61u) ^ (n >> 16u);
  n *= 9u;
  n = n ^ (n >> 4u);
  n *= 0x27d4eb2du;
  n = n ^ (n >> 15u);
  return n;
}
float hash1(vec2 p) {
  ivec2 i = ivec2(floor(p));
  uint h = uint(i.x) * 73856093u ^ uint(i.y) * 19349663u;
  return float(hashU(h)) * (1.0 / 4294967296.0);
}
float noise12(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  float a = hash1(i);
  float b = hash1(i + vec2(1.0, 0.0));
  float c = hash1(i + vec2(0.0, 1.0));
  float d = hash1(i + vec2(1.0, 1.0));
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 2.0 - 1.0; // [-1,1]
}
// Standard right-handed Rodrigues rotation of v about unit axis k.
vec3 rotAxis(vec3 k, vec3 v, float a) {
  float s = sin(a), c = cos(a);
  return v * c + cross(k, v) * s + k * dot(k, v) * (1.0 - c);
}

void main() {
  BladeInstance b = blades[gl_InstanceIndex];
  int vi = gl_VertexIndex;
  int seg = vi / 2;
  bool isRight = (vi % 2) == 1;
  bool isTip = (vi == VERTS_PER_BLADE - 1);
  float t = isTip ? 1.0 : float(seg) / float(SEGMENTS);
  float xSide = isRight ? 1.0 : 0.0;
  float h = b.height;

  // --- Continuous LOD blend ---
  vec3 camPos = ubo.inverseView[3].xyz;
  float distToCam = distance(camPos, b.basePos);
  float highLODOut =
      smoothstep(wind.lodNearDist * 0.15, wind.lodNearDist * 1.8, distToCam);

  // --- Width profile + edge-on compensation ---
  float widthHigh = easeOut(1.0 - t, 2.0);
  float widthLow = mix(widthHigh, 1.0 - t, 0.6);
  float width = b.width * mix(widthHigh, widthLow, highLODOut) * 0.5;

  vec2 facing = vec2(cos(b.facingAngle), sin(b.facingAngle));
  vec2 toBladeHoriz = b.basePos.xz - camPos.xz;
  toBladeHoriz /= max(length(toBladeHoriz), 1e-4);
  float facingAlignment = abs(dot(facing, toBladeHoriz));
  float edgeOnAmount = smoothstep(0.6, 1.0, 1.0 - facingAlignment);
  width *= 1.0 + edgeOnAmount * 2.5;

  // --- Cubic Bezier control points (curve lies in the facing plane) ---
  vec2 bd1 = vec2(cos(b.bendAngle), sin(b.bendAngle));
  vec2 bd2 = normalize(mix(bd1, vec2(cos(b.bendAngle2), sin(b.bendAngle2)), 0.6));
  float bendAmt = 0.22 + 0.22 * fract(b.windPhase * 3.7);
  vec3 p0 = vec3(0.0);
  vec3 p1 = vec3(bd1.x, 0.0, bd1.y) * h * bendAmt * 0.08 + vec3(0.0, h * 0.38, 0.0);
  vec3 p2 = vec3(bd1.x, 0.0, bd1.y) * h * bendAmt * 0.45 + vec3(0.0, h * 0.74, 0.0);
  vec3 p3 = vec3(bd2.x, 0.0, bd2.y) * h * bendAmt * 1.10 + vec3(0.0, h * 0.93, 0.0);

  // --- Travelling directional wind, applied to the control points ---
  vec2 wd = ubo.wind.xy;
  vec2 xz = b.basePos.xz;
  float leanAmt = windGust(xz);
  leanAmt = clamp(leanAmt, 0.0, 1.6);
  float flutter = sin(ubo.time * 4.0 + b.windPhase * 2.0) * 0.12 * ubo.wind.w;
  vec3 wv = vec3(wd.x, 0.0, wd.y);
  vec3 wperp = vec3(-wd.y, 0.0, wd.x);
  p1 += wv * leanAmt * h * 0.10;
  p2 += wv * leanAmt * h * 0.40 + wperp * flutter * h * 0.15;
  p3 += wv * leanAmt * h * 0.85 + wperp * flutter * h * 0.40;
  float droop = min(leanAmt, 1.5);
  p2.y -= h * 0.08 * droop;
  p3.y = max(p3.y - h * 0.30 * droop, h * 0.25);

  float mt = 1.0 - t;
  vec3 B = mt * mt * mt * p0 + 3.0 * mt * mt * t * p1 + 3.0 * mt * t * t * p2 + t * t * t * p3;
  vec3 dB = 3.0 * mt * mt * (p1 - p0) + 6.0 * mt * t * (p2 - p1) + 3.0 * t * t * (p3 - p2);

  vec3 sideV = vec3(-facing.y, 0.0, facing.x);
  vec3 localPos = B;
  if (!isTip) localPos += sideV * width * (isRight ? 1.0 : -1.0);
  localPos.y = max(localPos.y, 0.0);
  vec3 worldPos = b.basePos + localPos;

  // --- Real surface normal from Bezier derivative x side vector ---
  vec3 T = normalize(dB);
  vec3 N0 = normalize(cross(T, sideV));
  // +angle tilts toward -side (left edge), -angle toward +side (right edge).
  vec3 normal1 = rotAxis(T, N0, 0.9);
  vec3 normal2 = rotAxis(T, N0, -0.9);

  gl_Position = ubo.projView * vec4(worldPos, 1.0);
  fragUV = vec2(xSide, t);
  fragHeightFrac = t;
  fragNormal1 = normal1;
  fragNormal2 = normal2;
  fragXSide = xSide;
  fragHighLODOut = highLODOut;
  fragWorldPos = worldPos;
  fragSpecies = b.species;
  fragTangent = T;
  fragClumpVar = noise12(b.basePos.xz * 0.02) * 0.5 + 0.5;
  fragDry = b.pad2.x;
  fragBiomeTint = unpackUnorm4x8(b.pad0).rgb;
}
