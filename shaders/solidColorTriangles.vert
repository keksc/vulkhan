#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in int texIndex;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out flat int fragTexIndex; // flat = dont interpolate

#include "globalUbo.glsl"

void main() {
  vec3 pos = position;
  vec2 uvScreen = position.xy / ubo.resolution;
  // pos.x += 0.01 * (sin(ubo.time + uv.y + uvScreen.x * 2.0 + 3.0) + 1.0);
  // pos.y += 0.01 * (sin(ubo.time + uv.x + uvScreen.y * 3.0) + 1.0);
  gl_Position = vec4(pos, 1.0);
  fragUv = uv;
  fragTexIndex = texIndex;
}
