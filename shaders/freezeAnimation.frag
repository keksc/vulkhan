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

vec2 snowflakes[] = {
  vec2(-0.8, -0.6),
  vec2(0.8, -0.7),
  vec2(-0.8, 0.5),
  vec2(0.7, 0.8)
};

void main() {
  vec2 uv = inPos;
  uv.x *= ubo.aspectRatio;
  uv *= 1.0 / min(pow(push.time * 0.5, 2.0) + 0.5, 1.0);

  // Adjust snowflakes array positions for aspect ratio
  vec2 closestSnowflake = vec2(snowflakes[0].x * ubo.aspectRatio, snowflakes[0].y);
  float closestSnowflakeDist = distance(closestSnowflake, uv);

  for(int i = 1; i < snowflakes.length(); i++) {
    vec2 adjustedSnowflake = vec2(snowflakes[i].x * ubo.aspectRatio, snowflakes[i].y);
    float dist = distance(adjustedSnowflake, uv);
    if(closestSnowflakeDist > dist) {
      closestSnowflakeDist = dist;
      closestSnowflake = adjustedSnowflake;
    }
  }

  vec2 pos = closestSnowflake - uv;

  float r = length(pos) * 6.0;
  float a = atan(pos.y, pos.x);

  float f = pow(abs(cos(a * 12.0) * sin(a * 3.0)) * 0.8 + 0.1, 4);

  float opacity = 1.0 - smoothstep(f, f + 0.02, r);
  outColor = vec4(0.054, 0.57, 0.8, opacity);
}
