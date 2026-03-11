#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D smokeSampler;

void main() {
  float density = texture(smokeSampler, uv).r;
    
  outColor = vec4(vec3(1.0), density);
}
