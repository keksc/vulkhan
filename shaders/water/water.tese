#version 450

layout(quads, equal_spacing, ccw) in;

#include "../globalUbo.glsl"

layout(set = 1, binding = 0) uniform VertexUBO {
    mat4 model;
    float scale;
} vertexUBO;

layout(set=1, binding = 1) uniform sampler2D displacementMap;

layout(location = 0) in vec3 inPosTCS[];
layout(location = 1) in vec2 inUVTCS[];

layout(location = 0) out vec4 outPos;
layout(location = 1) out vec3 outNormal;

const float tileLength = 1000.0;

void main() {
    // 1. Interpolate UV and position from Quad
    vec2 uv = mix(mix(inUVTCS[0], inUVTCS[1], gl_TessCoord.x),
                  mix(inUVTCS[3], inUVTCS[2], gl_TessCoord.x),
                  gl_TessCoord.y);
                  
    vec3 pos = mix(mix(inPosTCS[0], inPosTCS[1], gl_TessCoord.x),
                   mix(inPosTCS[3], inPosTCS[2], gl_TessCoord.x),
                   gl_TessCoord.y);

    // 2. Texture Coordinate Setup
    vec2 texCoord = uv * vertexUBO.scale;
    ivec2 texSize = textureSize(displacementMap, 0);
    vec2 texelSize = 1.0 / vec2(texSize);

    // 3. Central Difference Sampling (Higher quality than Forward Diff)
    // We sample neighbors to calculate tangents.
    vec4 d_center = texture(displacementMap, texCoord);
    vec4 d_right  = texture(displacementMap, texCoord + vec2(texelSize.x, 0.0));
    vec4 d_left   = texture(displacementMap, texCoord - vec2(texelSize.x, 0.0));
    vec4 d_top    = texture(displacementMap, texCoord + vec2(0.0, texelSize.y));
    vec4 d_bot    = texture(displacementMap, texCoord - vec2(0.0, texelSize.y));

    // 4. Calculate Physical Step Size
    // How many physical meters does one texel represent?
    // If the map (1000m) repeats 'scale' times, one map covers 1000/scale meters.
    // One texel is that width divided by resolution (512).
    float worldTexelDist = (tileLength / vertexUBO.scale) / float(texSize.x);

    // 5. Construct Tangent Basis
    // Tangent corresponds to 2 steps (Left to Right)
    // Bitangent corresponds to 2 steps (Bottom to Top)
    
    // Position = GridPos + Displacement
    // Tangent = (GridRight - GridLeft) + (DispRight - DispLeft)
    vec3 tangent   = vec3(2.0 * worldTexelDist, 0.0, 0.0) + (d_right.xyz - d_left.xyz);
    vec3 bitangent = vec3(0.0, 0.0, 2.0 * worldTexelDist) + (d_top.xyz - d_bot.xyz);

    // 6. Final Normal
    vec3 normal = normalize(cross(bitangent, tangent));
    
    // 7. Output
    vec3 displacedPos = pos + d_center.xyz;
    
    outPos = vec4(displacedPos, d_center.w);
    outNormal = normal;
    
    gl_Position = ubo.projView * vec4(displacedPos, 1.0);
}
