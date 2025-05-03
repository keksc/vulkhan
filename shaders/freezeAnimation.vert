#version 450

layout (location = 0) out vec2 outPos;

#include "globalUbo.glsl"

const vec2 verts[6] = vec2[](
  vec2(-1.0, -1.0),
  vec2(1.0, -1.0),
  vec2(1.0, 1.0),
  vec2(1.0, 1.0),
  vec2(-1.0, 1.0),
  vec2(-1.0, -1.0)
);

void main() {
  gl_Position = vec4(verts[gl_VertexIndex], 0.0, 1.0);
  outPos = gl_Position.xy;
}
