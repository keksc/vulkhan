#version 450

layout (location = 0) in vec2 inPos;

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

layout(push_constant) uniform Push {
  float time;
} push;

//xy is pos, z is initial angle
vec3 snowflakes[] = {
  vec3(-0.8, -0.6, 0.0),
  vec3(-0.8, -0.55, 0.0),
  vec3(0.8, -0.7, 0.0),
  vec3(0.9, -0.6, 2.0),
  vec3(-0.8, 0.5, 0.0),
  vec3(0.7, 0.8, 0.0)
};

void main() {
  vec2 uv = inPos;
  uv.x *= ubo.aspectRatio;
  uv *= 1.0 / min(pow(push.time * 0.5, 2.0) + 0.5, 1.0);

  // Adjust snowflakes array positions for aspect ratio
  vec3 closestSnowflake = vec3(snowflakes[0].x * ubo.aspectRatio, snowflakes[0].yz);
  float closestSnowflakeDist = distance(closestSnowflake.xy, uv);

  for(int i = 1; i < snowflakes.length(); i++) {
    vec3 adjustedSnowflake = vec3(snowflakes[i].x * ubo.aspectRatio, snowflakes[i].yz);
    float dist = distance(adjustedSnowflake.xy, uv);
    if(closestSnowflakeDist > dist) {
      closestSnowflakeDist = dist;
      closestSnowflake = adjustedSnowflake;
    }
  }

  vec2 pos = closestSnowflake.xy - uv;

  float radius = length(pos)*6.0;
  float angle = atan(pos.y, pos.x)+closestSnowflake.z;

  float func = pow(abs(cos(angle * 12.0) * sin(angle * 3.0)) * 0.8 + 0.1, 4);

  float alpha = smoothstep(0.0, 1.0, (func-radius)*6.0);

  outColor = vec4(0.054, 0.57, 0.8, alpha);
}
