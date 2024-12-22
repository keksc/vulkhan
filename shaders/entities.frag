#version 450

layout (location = 0) in vec3 fragColor;
layout (location = 1) in vec3 fragPosWorld;
layout (location = 2) in vec3 fragNormalWorld;
layout (location = 3) in vec2 uv;

layout (location = 0) out vec4 outColor;

struct Particle {
  vec3 position;
  vec3 color;
};

layout(set = 0, binding = 0) uniform GlobalUbo {
  mat4 projection;
  mat4 view;
  mat4 invView;
  Particle particles[10];
  int numParticles;
  float aspectRatio;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(push_constant) uniform Push {
  mat4 modelMatrix;
  mat4 normalMatrix;
} push;

void main() {
  vec3 ambientLightColor = vec3(.02, .02, .02);
  /*vec3 diffuseLight = ambientLightColor;
  vec3 specularLight = vec3(0.0);
  vec3 surfaceNormal = normalize(fragNormalWorld);

  vec3 cameraPosWorld = ubo.invView[3].xyz;
  vec3 viewDirection = normalize(cameraPosWorld - fragPosWorld);

  for (int i = 0; i < ubo.numParticles; i++) {
    PointLight light = ubo.pointLights[i];
    vec3 directionToLight = light.position.xyz - fragPosWorld;
    float attenuation = 1.0 / dot(directionToLight, directionToLight); // distance squared
    directionToLight = normalize(directionToLight);

    float cosAngIncidence = max(dot(surfaceNormal, directionToLight), 0);
    vec3 intensity = light.color.xyz * light.color.w * attenuation;

    diffuseLight += intensity * cosAngIncidence;

    // specular lighting
    vec3 halfAngle = normalize(directionToLight + viewDirection);
    float blinnTerm = dot(surfaceNormal, halfAngle);
    blinnTerm = clamp(blinnTerm, 0, 1);
    blinnTerm = pow(blinnTerm, 512.0); // higher values -> sharper highlight
    specularLight += intensity * blinnTerm;
  }
  
  outColor = vec4(diffuseLight * fragColor + specularLight * fragColor, 1.0);*/

  /*vec4 color = texture(texSampler, uv);
  vec3 light = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w;

  vec3 surfaceNormal = normalize(fragNormalWorld);

  vec3 cameraPosWorld = ubo.invView[3].xyz;
  vec3 viewDirection = normalize(cameraPosWorld - fragPosWorld);
  for(int i=0;i<ubo.numLights;i++) {
    PointLight light = ubo.pointLights[i];
    vec3 dirToLight = normalize(light.position.xyz*fragPosWorld);
    light *= dot(surfaceNormal, dirToLight);
  }
  outColor = color*light;*/
  vec4 color = texture(texSampler, uv);
  outColor = color;
}
