layout(set = 0, binding = 0) uniform GlobalUbo {
  mat4 projView;
  mat4 inverseView;
  float aspectRatio;
  float time;
} ubo;
