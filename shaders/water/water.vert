#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec4 outPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec3 outViewVec;
layout(location = 4) out vec3 outLightVec;

layout(set = 0, binding = 0) uniform VertexUBO
{
  mat4 model;
  mat4 view;
  mat4 proj;
  float WSHeightAmp;
  float WSChoppy;
  float scale;
} ubo;

layout(binding = 2) uniform sampler2D displacementMap;
layout(binding = 3) uniform sampler2D normalMap;

void main()
{
  vec4 D = texture(displacementMap, inUV * ubo.scale);
  D.y   *= ubo.WSHeightAmp;
  outPos.xyz = inPos + D.xyz;
  outPos.w = D.w;     // jacobian
  // TODO optimize MVP
  gl_Position = ubo.proj * ubo.view * ubo.model * vec4(outPos.xyz, 1.0);

  const vec4 slope = texture(normalMap, inUV * ubo.scale);
  outNormal = normalize(vec3(
      - ( slope.x / (1.0f + ubo.WSChoppy * slope.z) ),
      1.0f,
      - ( slope.y / (1.0f + ubo.WSChoppy * slope.w) )
  ));

  outUV = inUV;
	vec3 lightPos = vec3(0.0f, -5.0f, 5.0f);
	outLightVec = lightPos.xyz - outPos.xyz;
	outViewVec = -outPos.xyz;
}
