#version 450
layout(location = 0) in vec2 uv;
layout(location = 1) flat in int texIndex;
layout(location = 0) out vec4 outColor;
layout(set = 1, binding = 0) uniform sampler2D texSamplers[64];

float grain(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
void main() {
  // -1 is the canvas paper material; never index the texture array with it.
  if (texIndex == -1) {
    vec2 paper = uv * vec2(800.0, 600.0);
    float fine = grain(floor(paper * 2.0)) - 0.5;
    float fibers = grain(floor(paper * vec2(0.8, 5.0))) - 0.5;
    float cloud = sin(paper.x * 0.021 + sin(paper.y * 0.017)) *
                  sin(paper.y * 0.013) * 0.006;
    vec2 edge = min(uv, 1.0 - uv);
    float worn = 1.0 - smoothstep(0.0, 0.045, min(edge.x, edge.y));
    vec3 paperColor = vec3(0.94, 0.89, 0.79);
    paperColor += fine * 0.025 + fibers * 0.012 + cloud;
    paperColor -= worn * vec3(0.025, 0.03, 0.035);
    outColor = vec4(paperColor, 1.0);
    return;
  }
  outColor = texture(texSamplers[texIndex], uv);
}
