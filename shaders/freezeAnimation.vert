#version 450

layout (location = 0) out vec2 outPos;

layout(set = 0, binding = 0) uniform GlobalUbo {
  mat4 projection;
  mat4 view;
  mat4 inverseView;
  float aspectRatio;
} ubo;

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
