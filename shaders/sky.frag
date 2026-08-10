#version 450

#include "globalUbo.glsl"
#include "consts.glsl"

layout (location = 0) out vec4 outColor;

const float FOV_Y = 1.919862177;

const float STAR_DISTANCE = 150.0;
const float STAR_DENSITY = 0.05;
const float STAR_BRIGHTNESS = 0.5;

const float DAY_LENGTH_SECONDS = 60.0;

float hash13(vec3 p3) {
	p3  = fract(p3 * vec3(.1031,.11369,.13787));
  p3 += dot(p3, p3.yzx + 19.19);
  return fract((p3.x + p3.y) * p3.z);
}

// reconstruct a world-space view ray from the fragment's screen position
vec3 getWorldRayDir()
{
  vec2 ndc = (gl_FragCoord.xy / ubo.resolution) * 2.0 - 1.0;
  ndc.y = -ndc.y; // flip for Vulkan's top-left origin / y-down NDC

  float tanHalfFov = tan(FOV_Y * 0.5);
  vec3 viewDir = normalize(vec3(ndc.x * tanHalfFov * ubo.aspectRatio,
                                 ndc.y * tanHalfFov,
                                 -1.0));

  vec3 worldDir = mat3(ubo.inverseView) * viewDir;
  return normalize(worldDir);
}

void main()
{
  vec3 rayDir = getWorldRayDir();
  vec3 finalColor = vec3(0.0);

  vec3 p = rayDir * STAR_DISTANCE;
  float brightness = smoothstep(1.0 - STAR_DENSITY, 1.0, hash13(floor(p)));
  float twinkle = cos(ubo.time * 2.0 + hash13(floor(p) + 17.0) * 13.0) * 0.5 + 0.5;
  finalColor += vec3(smoothstep(STAR_BRIGHTNESS, 0.0, length(fract(p) - 0.5))) * brightness * twinkle;

  outColor = vec4(finalColor, 1.0);
}
