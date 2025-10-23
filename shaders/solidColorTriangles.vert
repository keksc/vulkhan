#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in int texIndex;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out flat int fragTexIndex; // flat = dont interpolate

void main() {
  gl_Position = vec4(position, 1.0);
  fragUv = uv;
  fragTexIndex = texIndex;
}
