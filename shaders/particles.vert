#version 450

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

const vec3 verts[6] = vec3[](
  vec3(0.0, 0.0, 1.0),
  vec3(0.5, 0.0, -1.0),
  vec3(0.0, 0.5, -0.5),
  vec3(0.3, 0.4, 0.5),
  vec3(0.2, 0.1, 0.2),
  vec3(1.0, 1.0, -0.2)
);

void main() {
  gl_Position = ubo.projection * ubo.view * vec4(verts[gl_VertexIndex], 1.0);
  gl_PointSize = 14.0;
}
