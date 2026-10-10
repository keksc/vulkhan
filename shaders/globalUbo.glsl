layout(set = 0, binding = 0) uniform GlobalUbo {
  mat4 proj;
  mat4 view;
  mat4 projView;
  mat4 inverseView;
  vec2 resolution;
  float aspectRatio;
  float time;
  vec4 sunDirection; // xyz: normalized direction toward the sun
  vec4 sunColor;     // rgb: linear colour, w: intensity
  vec4 wind;         // xy: travel direction, z: m/s, w: strength
  vec4 fogParams;    // density, height falloff, base height, anisotropy
  vec4 fogColor;
  vec4 skyAmbient;
  vec4 groundAmbient;
  vec4 lightGrid;
  vec4 lightPositions[121];
  vec4 lightColors[121];
  vec4 lightPaths[121];
} ubo;
