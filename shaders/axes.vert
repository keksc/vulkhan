#version 450

layout (location = 0) out vec3 outColor;

struct PointLight {
  vec4 position; // ignore w
  vec4 color; // w is intensity
};

layout(set = 0, binding = 0) uniform GlobalUbo {
  mat4 projection;
  mat4 view;
  mat4 invView;
  vec4 ambientLightColor; // w is intensity
  PointLight pointLights[10];
  int numLights;
  float aspectRatio;
} ubo;

vec3 positions[6] = vec3[](
    vec3(0.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0),  // X-axis (red)
    vec3(0.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0),  // Y-axis (green)
    vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0)   // Z-axis (blue)
);

vec3 colors[6] = vec3[](
    vec3(1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0),  // Red for X-axis
    vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0),  // Green for Y-axis
    vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, 1.0)   // Blue for Z-axis
);

void main() {
  gl_Position = ubo.projection * ubo.view * vec4(positions[gl_VertexIndex], 1.0);
  outColor = colors[gl_VertexIndex];
}
