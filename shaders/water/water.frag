#version 450

layout(location = 0) in vec4 inPos;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform WaterUBO
{
  vec3 camPos;
} ubo;

layout(set = 1, binding = 0) uniform samplerCube skybox;

void main() {
    vec3 worldPos = inPos.xyz;
    vec3 V = normalize(ubo.camPos - worldPos);
    vec3 N = normalize(inNormal);
    vec3 R = reflect(-V, N);
    
    vec3 reflection = texture(skybox, R).rgb;
    vec3 waterColor = vec4(0.01, 0.05, 0.1, 1.0).rgb; // Deep blue base

    // Fresnel Schlick Approximation
    float F0 = 0.02; // Reflectance at normal incidence for water
    float fresnel = F0 + (1.0 - F0) * pow(1.0 - max(dot(N, V), 0.0), 5.0);

    // Mix the base water color with the skybox reflection
    vec3 finalColor = mix(waterColor, reflection, fresnel);

    outColor = vec4(finalColor, 1.0);
}
