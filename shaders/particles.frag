#version 450

layout(location = 0) in vec3 color;

layout (location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform GlobalUbo {
  mat4 projection;
  mat4 view;
  mat4 inverseView;
  float aspectRatio;
} ubo;

const float PI = 3.141592653589793;
const int SIDES = 3;

void main() {
  vec2 p = gl_PointCoord * 2.0 - 1.0;

  float theta = atan(p.y, p.x);
  float rEdge = cos(PI / float(SIDES)) 
              / cos(mod(theta, 2.0*PI/float(SIDES)) - PI/float(SIDES));
  if (length(p) > rEdge) {
    discard;
  }

  vec3 n = normalize(vec3(p / rEdge, sqrt(max(0.0, 1.0 - dot(p/rEdge, p/rEdge)))));
  vec3 lightDir = normalize(vec3(0.5, 0.5, 1.0));
  float diff = max(dot(n, lightDir), 0.0) * 0.8 + 0.2;

  outColor = vec4(color * diff, 1.0);
}
