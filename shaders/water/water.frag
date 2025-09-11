#version 450

layout(location = 0) in vec4 inPos;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform WaterUBO
{
  vec3 camPos;
} ubo;

layout(set = 1, binding = 0) uniform samplerCube skybox;

void main()
{
    vec3 worldPos = inPos.xyz;
    vec3 V = normalize(ubo.camPos - worldPos);
    vec3 N = normalize(inNormal);
    vec3 R = reflect(-V, N);
    vec3 envColor = texture(skybox, R).rgb;

    outColor = vec4(vec3(0.0)/*envColor*/, 1.0);
}
