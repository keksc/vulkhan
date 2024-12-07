#version 450

layout (location = 0) in vec3 posWorld;
layout (location = 1) in vec3 normalWorld;

layout (location = 0) out vec4 outColor;

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

void main() {
  // diffuse
  vec3 lightDirection = normalize(vec3(1.0, -3.0, -1.0));
  float intensity = max(dot(lightDirection, normalize(normalWorld)), 0.0);

  /*vec3 cameraPosWorld = ubo.invView[3].xyz;
  vec3 viewDirection = normalize(cameraPosWorld - posWorld);

  vec3 halfway = normalize(viewDirection + lightDirection);
  float specular = max(dot(halfway, normalWorld), 0.0);

  specular = pow(specular, 777);*/
  //float base = 0.01;//+specular;
  //vec3 fragColor = vec3(base, base, base+incidence);
  outColor = vec4(intensity * vec3(0.0, 0.0, 1.0), 1.0);
}
