#version 450

// TerrainVertex: position and normal, 24 bytes. Materials are computed per pixel.
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec3 fragPosWorld;
layout(location = 1) out vec3 fragNormalWorld;

// Water-plane clip for PlanarReflectionSys's reflection pass - same
// mechanism as entities.vert (needs shaderClipDistance).
out float gl_ClipDistance[1];

// Must match TerrainSys::PushConstants (terrain.hpp) exactly. The vertex
// stage reads clipPlane and meshParams.
layout(push_constant) uniform PushConstants {
  vec4 clipPlane;
  vec4 heightParams;
  vec4 heightmapRect;
  uvec4 meshParams;
} push;

layout(set = 1, binding = 1, std430) readonly buffer StitchedEdges {
  vec4 edgeSamples[]; // height, normal.xyz
};

#include "../globalUbo.glsl"

void main() {
  // Terrain vertices are already in world space (identity model matrix).
  vec4 positionWorld = vec4(position, 1.0);
  vec3 surfaceNormal = normal;
  uint Q = push.meshParams.x;
  uint G = Q + 1u;
  uint local = uint(gl_VertexIndex) % (G * G);
  uint x = local % G, z = local / G;
  uint edge = 4u, tap = 0u;
  if (z == 0u) { edge = 0u; tap = x; }
  else if (z == Q) { edge = 1u; tap = x; }
  else if (x == 0u) { edge = 2u; tap = z; }
  else if (x == Q) { edge = 3u; tap = z; }
  if (edge < 4u) {
    vec4 seam = edgeSamples[push.meshParams.y + edge * G + tap];
    positionWorld.y = seam.x;
    surfaceNormal = seam.yzw;
  }

  gl_Position = ubo.projView * positionWorld;
  gl_ClipDistance[0] = dot(positionWorld, push.clipPlane);

  fragPosWorld = positionWorld.xyz;
  fragNormalWorld = normalize(surfaceNormal);
}
