#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in uvec4 jointIndices;
layout(location = 4) in vec4 jointWeights;
layout(location = 0) out vec3 fragPosWorld;
layout(location = 1) out vec3 fragNormalWorld;
layout(location = 2) out vec2 fragUV;
layout(location = 3) flat out float fragTint;
layout(location = 4) flat out float fragSeed;
layout(location = 5) flat out vec4 fragCoat;
layout(location = 6) out vec2 fragCoatUV;
layout(location = 7) out vec3 fragFlowWorld;
out float gl_ClipDistance[1];

#include "../globalUbo.glsl"
#include "../environment.glsl"

struct Instance {
  mat4 model;
  vec4 animation;
  vec4 coat;
};
layout(std430, set = 2, binding = 0) readonly buffer Instances {
  Instance wolves[];};
layout(std430, set = 2, binding = 1) readonly buffer Poses {
  mat4 joints[];
};
layout(push_constant) uniform Push {
  vec4 clipPlane;
  vec4 fur;
} push;

void main() {
  Instance wolf = wolves[gl_InstanceIndex];
  int count = int(push.fur.z);
  int stride = int(push.fur.w);
  int frame = int(wolf.animation.x);
  int nextFrame = (frame + 1) % count;
  float fraction = fract(wolf.animation.x);
  // Bind geometry is the standing pose. Blend toward a moving pose as
  // locomotion starts; idle animals never freeze with lifted running paws.
  float motion = clamp(wolf.animation.z, 0.0, 1.0);
  mat4 skin = mat4(1.0);
  if (motion > 0.001) {
    mat4 movingSkin = mat4(0.0);
    for (int i = 0; i < 4; ++i) {
      if (jointWeights[i] > 0.0) {
        int joint = int(jointIndices[i]);
        movingSkin += (joints[frame * stride + joint] * (1.0 - fraction) +
                       joints[nextFrame * stride + joint] * fraction) * jointWeights[i];
      }
    }
    skin = skin * (1.0 - motion) + movingSkin * motion;
  }
  vec3 n = normalize(mat3(skin) * normal);
  vec3 p = (skin * vec4(position, 1.0)).xyz;
  // Guard-hair card geometry is authored and bent in Blender. The same
  // cached skeleton deforms skin and cards; no shell extrusion passes.
  vec4 world = wolf.model * vec4(p, 1.0);
  vec3 worldNormal = normalize(mat3(wolf.model) * n);
  if (push.fur.x > 0.5) {
    // Card UV V is root=0, tip=1. Skin first, then bend in world wind;
    // fixed roots cannot slide off the wolf when its heading changes.
    float tip = clamp(uv.y, 0.0, 1.0);
    float speedWeight = smoothstep(0.2, 8.0, max(ubo.wind.z, 0.0));
    float gust = min(windGust(wolf.model[3].xz), 1.5) * speedWeight;
    vec3 windDirection = vec3(ubo.wind.x, 0.0, ubo.wind.y);
    vec3 bendDirection = windDirection - worldNormal *
                         min(dot(windDirection, worldNormal), 0.0);
    float phase = ubo.time * 4.2 + uv.x * 1.618 + wolf.animation.w * 6.28;
    float flutter = sin(phase) * 0.0025 * gust;
    float bend = (0.010 * gust + flutter) * tip * tip;
    world.xyz += bendDirection * bend;
    worldNormal = normalize(worldNormal - bendDirection * bend * 2.0);
  }
  fragPosWorld = world.xyz;
  fragNormalWorld = worldNormal;
  fragFlowWorld = normalize(mat3(wolf.model) * mat3(skin) * vec3(0,-0.25,-1));
  fragUV = uv;
  fragCoatUV = vec2(clamp((1.65-position.z)/3.3, 0.01, 0.99),
                    clamp(0.96-position.y*(0.68/1.9), 0.26, 0.96));
  fragTint = wolf.animation.y;
  fragSeed = wolf.animation.w;
  fragCoat = wolf.coat;
  gl_Position = ubo.projView * world;
  gl_ClipDistance[0] = dot(world, push.clipPlane);
}
