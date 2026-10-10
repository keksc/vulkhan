#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "../globalUbo.glsl"

struct FurInstance { mat4 model; vec4 animation; vec4 coat; };
layout(std430, set=2, binding=0) readonly buffer FurInstances {
  FurInstance wolves[];
};
layout(std430, set=2, binding=1) readonly buffer FurPoses { mat4 joints[]; };
struct Guide { vec4 position; vec4 normal; uvec4 joints; vec4 weights; };
struct Bundle { Guide guides[12]; vec4 parameters; };
layout(std430, set=3, binding=0) readonly buffer FurBundles {
  Bundle bundles[];
};
#ifdef FUR_RASTER
layout(std430, set=3, binding=1) buffer FurCenter { uint64_t center[]; };
layout(std430, set=3, binding=2) buffer FurConservative { uint64_t conservative[]; };
#else
layout(std430, set=3, binding=1) readonly buffer FurCenter { uint64_t center[]; };
layout(std430, set=3, binding=2) readonly buffer FurConservative { uint64_t conservative[]; };
#endif
struct Dispatch {
  uint x, y, z, strands, segments;
  float density;
  uint padding0, padding1;
};
#ifdef FUR_LOD
layout(std430, set=3, binding=3) buffer FurDispatches { Dispatch dispatches[]; };
#else
layout(std430, set=3, binding=3) readonly buffer FurDispatches { Dispatch dispatches[]; };
#endif
#ifdef FUR_SHADE
layout(std430, set=3, binding=4) buffer FurShades { uint shades[]; };
#else
layout(std430, set=3, binding=4) readonly buffer FurShades { uint shades[]; };
#endif
layout(push_constant) uniform FurPush {
  mat4 inverseProjView;
  uvec4 window;
  uvec4 counts;
} fur;

const uint64_t EMPTY = ~uint64_t(0);
const uint DEPTH_MAX = 16777214u;

// RGBE keeps linear HDR shading in four bytes instead of a float4 per pixel.
uint packFurColor(vec3 color) {
  float largest=max(color.r,max(color.g,color.b));
  if (largest<1e-20) return 0u;
  int exponent=clamp(int(ceil(log2(largest))),-126,127);
  uvec3 rgb=uvec3(round(clamp(color/exp2(float(exponent)),0.0,1.0)*255.0));
  return rgb.r | (rgb.g<<8) | (rgb.b<<16) | (uint(exponent+127)<<24);
}
vec3 unpackFurColor(uint packed) {
  vec3 rgb=vec3(packed&255u,(packed>>8)&255u,(packed>>16)&255u);
  return rgb*(exp2(float(int(packed>>24)-127))/255.0);
}

float randomFur(uint v) {
  v ^= v >> 16; v *= 0x7feb352du;
  v ^= v >> 15; v *= 0x846ca68bu; v ^= v >> 16;
  return float(v >> 8) / 16777216.0;
}

mat4 guideSkin(Guide guide, FurInstance wolf) {
  float motion = clamp(wolf.animation.z, 0.0, 1.0);
  uint frame = uint(wolf.animation.x);
  uint nextFrame = (frame + 1u) % fur.counts.y;
  float fraction = fract(wolf.animation.x);
  mat4 skin = mat4(0.0);
  for (uint j=0u; j<4u; ++j) {
    if (guide.weights[j] > 0.0) {
      uint joint = guide.joints[j];
      skin += (joints[frame * fur.counts.z + joint] * (1.0-fraction) +
               joints[nextFrame * fur.counts.z + joint] * fraction) *
              guide.weights[j];
    }
  }
  return mat4(1.0) * (1.0-motion) + skin * motion;
}

vec2 octEncode(vec3 n) {
  n /= abs(n.x)+abs(n.y)+abs(n.z);
  vec2 s = vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
  return (n.z >= 0.0 ? n.xy : (1.0-abs(n.yx))*s) * .5 + .5;
}
vec3 octDecode(vec2 e) {
  vec2 f = e*2.0-1.0;
  vec3 n = vec3(f, 1.0-abs(f.x)-abs(f.y));
  float t = max(-n.z, 0.0);
  n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
  return normalize(n);
}
uint64_t furPayload(float depth, vec3 tangent, vec2 coatUV, uint wolf) {
  // Positive float bits are monotonic. Keep the top 24 bits (exponent and
  // 15 mantissa bits), round, and invert for atomicMin/reverse-Z. Uniform
  // 24-bit UNORM depth would lose metres of precision on distant wolves.
  uint encoded=(floatBitsToUint(clamp(depth,0.0,1.0))+128u)>>8;
  uint d=DEPTH_MAX-encoded;
  uvec2 t = uvec2(round(octEncode(tangent)*1023.0));
  uvec2 uv = uvec2(round(clamp(coatUV,0.0,1.0)*127.0));
  return (uint64_t(d)<<40) | (uint64_t(wolf & 63u)<<34) |
         (uint64_t(uv.x)<<27) | (uint64_t(uv.y)<<20) |
         (uint64_t(t.x)<<10) | uint64_t(t.y);
}
float furDepth(uint64_t data) {
  return uintBitsToFloat((DEPTH_MAX-uint(data>>40))<<8);
}
uint furWolf(uint64_t data) { return uint((data>>34)&uint64_t(63)); }
vec2 furCoatUV(uint64_t data) {
  return vec2(uint((data>>27)&uint64_t(127)),
              uint((data>>20)&uint64_t(127)))/127.0;
}
vec3 furTangent(uint64_t data) {
  return octDecode(vec2(uint((data>>10)&uint64_t(1023)),
                       uint(data&uint64_t(1023)))/1023.0);
}
uint furPixel(ivec2 p) { return uint(p.y)*fur.window.x+uint(p.x); }
bool furInside(ivec2 p) {
  return all(greaterThanEqual(p,ivec2(0))) &&
         all(lessThan(p,ivec2(fur.window.xy)));
}
vec3 furPosition(ivec2 p, float depth) {
  vec2 uv = (vec2(p)+vec2(fur.window.zw)+.5)/ubo.resolution;
  vec4 world = fur.inverseProjView*vec4(uv*2.0-1.0,depth,1);
  return world.xyz/world.w;
}
float furViewDepth(float depth) {
  return abs(ubo.proj[3][2]/(depth+ubo.proj[2][2]));
}
