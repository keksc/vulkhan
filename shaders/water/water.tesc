#version 450

layout(vertices = 4) out;

#include "../globalUbo.glsl"

layout(set = 1, binding = 0) uniform VertexUBO {
    mat4 model;
    float scale;
} vertexUBO;

layout(location = 0) in vec3 inPos[];
layout(location = 1) in vec2 inUV[];

layout(location = 0) out vec3 outPosTCS[];
layout(location = 1) out vec2 outUVTCS[];

void main() {
  outPosTCS[gl_InvocationID] = inPos[gl_InvocationID];
  outUVTCS[gl_InvocationID] = inUV[gl_InvocationID];

  if (gl_InvocationID == 0) {
    vec3 worldPos[4];
    for (int i = 0; i < 4; ++i) {
        worldPos[i] = (vertexUBO.model * vec4(inPos[i], 1.0)).xyz;
    }
    vec3 patchCenter = (worldPos[0] + worldPos[1] + worldPos[2] + worldPos[3]) / 4.0;
    vec3 camPos = vec3(ubo.inverseView[3]);
    float distance = length(patchCenter - camPos);
    float tessLevel = clamp(256.0 - distance * 0.2, 1.0, 16.0);

    gl_TessLevelOuter[0] = tessLevel;
    gl_TessLevelOuter[1] = tessLevel;
    gl_TessLevelOuter[2] = tessLevel;
    gl_TessLevelOuter[3] = tessLevel;
    gl_TessLevelInner[0] = tessLevel;
    gl_TessLevelInner[1] = tessLevel;
  }
}
