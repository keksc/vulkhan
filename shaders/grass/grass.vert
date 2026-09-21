#version 450
layout(location = 0) out vec2 fragUV;
layout(location = 1) out float fragHeightFrac;
layout(location = 2) out vec3 fragNormal1;
layout(location = 3) out vec3 fragNormal2;
layout(location = 4) out float fragXSide;
layout(location = 5) out float fragHighLODOut;
layout(location = 6) out vec3 fragWorldPos;
layout(location = 7) out float fragSpecies;
layout(location = 8) out vec3 fragTangent;
layout(location = 9) out float fragClumpVar;
#include "../globalUbo.glsl"
struct BladeInstance {
  vec3 basePos;
  float bendAngle;
  float facingAngle;
  float height;
  float width;
  float windPhase;
  float bendAngle2;
  float species; // always 0.0 now (flowers removed) - kept only so this
                  // struct's layout still matches BladeInstance on the
                  // host side and in generateGrass.comp without touching
                  // either.
  vec2 pad2;
  vec3 aabbMin;
  float pad0;
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
const float PI = 3.14159265359;
float easeOut(float x, float t) { return 1.0 - pow(1.0 - x, t); }
float easeIn(float x, float t) { return pow(x, t); }
float remap(float v, float inMin, float inMax, float outMin, float outMax) {
  return mix(outMin, outMax, (v - inMin) / (inMax - inMin));
}
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
mat3 rotateX(float theta) {
  float c = cos(theta), s = sin(theta);
  return mat3(vec3(1, 0, 0), vec3(0, c, -s), vec3(0, s, c));
}
mat3 rotateY(float theta) {
  float c = cos(theta), s = sin(theta);
  return mat3(vec3(c, 0, s), vec3(0, 1, 0), vec3(-s, 0, c));
}
mat3 rotateAxis(vec3 axis, float angle) {
  axis = normalize(axis);
  float s = sin(angle), c = cos(angle), oc = 1.0 - c;
  return mat3(
      oc * axis.x * axis.x + c,          oc * axis.x * axis.y - axis.z * s, oc * axis.z * axis.x + axis.y * s,
      oc * axis.x * axis.y + axis.z * s, oc * axis.y * axis.y + c,          oc * axis.y * axis.z - axis.x * s,
      oc * axis.z * axis.x - axis.y * s, oc * axis.y * axis.z + axis.x * s, oc * axis.z * axis.z + c);
}
void main() {
  BladeInstance b = blades[gl_InstanceIndex];
  int vi = gl_VertexIndex;
  int seg = vi / 2;
  bool isRight = (vi % 2) == 1;
  bool isTip = (vi == VERTS_PER_BLADE - 1);
  float t = isTip ? 1.0 : float(seg) / float(SEGMENTS);
  float xSide = isRight ? 1.0 : 0.0;
  // --- Continuous LOD blend (Quick_Grass GRASS_LOD_DIST equivalent) ---
  vec3 camPos = ubo.inverseView[3].xyz;
  float distToCam = distance(camPos, b.basePos);
  float highLODOut =
      smoothstep(wind.lodNearDist * 0.15, wind.lodNearDist * 1.8, distToCam);
  float widthHigh = easeOut(1.0 - t, 2.0);
  float widthLow = mix(easeOut(1.0 - t, 2.0), 1.0 - t, 0.6);
  float widthBase = mix(widthHigh, widthLow, highLODOut);
  float width = b.width * widthBase * 0.5;
  // --- Edge-on width compensation ---
  vec2 facingDirHoriz = vec2(cos(b.facingAngle), sin(b.facingAngle));
  vec2 toBladeHoriz = normalize(b.basePos.xz - camPos.xz);
  float facingAlignment = abs(dot(facingDirHoriz, toBladeHoriz));
  float edgeOnAmount = smoothstep(0.6, 1.0, 1.0 - facingAlignment);
  width *= 1.0 + edgeOnAmount * 2.5;
  // --- Base curve ---
  float easedHeight = mix(easeIn(t, 2.0), mix(easeIn(t, 2.0), 1.0, 0.6), highLODOut);
  vec2 bendDir = vec2(cos(b.bendAngle), sin(b.bendAngle));
  vec2 bendDir2 = vec2(cos(b.bendAngle2), sin(b.bendAngle2));
  float sCurveMix = smoothstep(0.35, 1.0, t);
  vec2 curveDir = normalize(mix(bendDir, bendDir2, sCurveMix * 0.6));
  float curve = easedHeight;
  float bendAmount = 0.32;
  vec3 localPos = vec3(0.0, t * b.height, 0.0);
  localPos.xz += curveDir * curve * b.height * bendAmount;
  if (!isTip) {
    vec2 facing = vec2(cos(b.facingAngle), sin(b.facingAngle));
    vec2 side = vec2(-facing.y, facing.x) * width * (isRight ? 1.0 : -1.0);
    localPos.xz += side;
  }
  // --- Noise-driven wind ---
  float windDirN = noise12(b.basePos.xz * 0.015 + 0.03 * wind.time);
  float windSample = noise12(b.basePos.xz * 0.045 + wind.time * 0.6);
  float windLeanAngle =
      easeIn(remap(windSample, -1.0, 1.0, 0.2, 1.0), 2.0) * 1.9 * wind.strength;
  windLeanAngle *= t;
  // Hard cap the lean angle regardless of wind.strength. Past ~90 degrees
  // a rotation around a horizontal axis starts folding the blade back
  // down and eventually below y = 0 (into the ground); capping well
  // short of that (75 degrees) keeps the motion looking like an
  // increasingly extreme bend rather than a snap-through flip, no matter
  // how large wind.strength is set to.
  const float MAX_WIND_LEAN_ANGLE = radians(75.0);
  windLeanAngle = min(windLeanAngle, MAX_WIND_LEAN_ANGLE);
  vec3 windAxis = vec3(cos(windDirN), 0.0, sin(windDirN));
  mat3 grassMat = rotateAxis(windAxis, windLeanAngle);
  localPos = grassMat * localPos;
  // Backstop: even with the angle capped, the bend-curve offset added to
  // localPos.xz before this rotation (see curveDir * curve * ... above)
  // means the pre-rotation vector isn't purely vertical, so the angle
  // cap alone doesn't perfectly guarantee y >= 0 in every case. Clamping
  // here guarantees no vertex ever ends up below its own base, for any
  // wind strength or bend/curve combination.
  localPos.y = max(localPos.y, 0.0);
  vec3 worldPos = b.basePos + localPos;
  // --- Dual fake-round normals ---
  vec3 curveTangent = vec3(0.0, 1.0, 0.0);
  vec3 grassVertexNormal = normalize(vec3(0.0, -bendDir.y, bendDir.x) * curve + curveTangent * (1.0 - curve));
  vec3 normal1 = normalize(grassMat * (rotateY(PI * 0.3) * grassVertexNormal));
  vec3 normal2 = normalize(grassMat * (rotateY(-PI * 0.3) * grassVertexNormal));
  vec3 tangent = normalize(grassMat * vec3(curveDir * curve * bendAmount, 1.0).xzy);
  float clumpVar = noise12(b.basePos.xz * 0.02) * 0.5 + 0.5;
  gl_Position = ubo.projView * vec4(worldPos, 1.0);
  fragUV = vec2(xSide, t);
  fragHeightFrac = t;
  fragNormal1 = normal1;
  fragNormal2 = normal2;
  fragXSide = xSide;
  fragHighLODOut = highLODOut;
  fragWorldPos = worldPos;
  fragSpecies = b.species;
  fragTangent = tangent;
  fragClumpVar = clumpVar;
}
