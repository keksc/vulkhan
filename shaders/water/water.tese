#version 450

layout(quads, equal_spacing, ccw) in;

layout(set = 0, binding = 0) uniform VertexUBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    float WSChoppy;
    float scale;
} vertexUBO;

layout(set = 0, binding = 1) uniform WaterSurfaceUBO {
    vec3 camPos;
    vec3 sunDir;
} waterUBO;

layout(binding = 2) uniform sampler2D displacementMap;
layout(binding = 3) uniform sampler2D normalMap;

layout(location = 0) in vec3 inPosTCS[];
layout(location = 1) in vec2 inUVTCS[];

layout(location = 0) out vec4 outPos;
layout(location = 1) out vec3 outNormal;

void main() {
    // Interpolate UV and position
    vec2 uv = mix(mix(inUVTCS[0], inUVTCS[1], gl_TessCoord.x),
                  mix(inUVTCS[3], inUVTCS[2], gl_TessCoord.x),
                  gl_TessCoord.y);
    vec3 pos = mix(mix(inPosTCS[0], inPosTCS[1], gl_TessCoord.x),
                   mix(inPosTCS[3], inPosTCS[2], gl_TessCoord.x),
                   gl_TessCoord.y);

    // Apply displacement
    vec4 D = texture(displacementMap, uv * vertexUBO.scale);
    vec3 displacedPos = pos + D.xyz;

    // Transform to world space
    vec3 worldPos = (vertexUBO.model * vec4(displacedPos, 1.0)).xyz;

    // Compute normal
    vec4 slope = texture(normalMap, uv * vertexUBO.scale);
    vec3 normal = normalize(vec3(
        - (slope.x / (1.0f + vertexUBO.WSChoppy * slope.z)),
        1.0f,
        - (slope.y / (1.0f + vertexUBO.WSChoppy * slope.w))
    ));

    // Output attributes
    outPos = vec4(worldPos, D.w);
    outNormal = normal;
    gl_Position = vertexUBO.proj * vertexUBO.view * vec4(worldPos, 1.0);
}
