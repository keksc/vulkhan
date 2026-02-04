#version 450

layout(quads, equal_spacing, ccw) in;

#include "../globalUbo.glsl"

layout(set = 1, binding = 0) uniform VertexUBO {
    mat4 model;
    float scale;
} vertexUBO;

layout(set = 1, binding = 1) uniform sampler2D displacementFoamMap;

layout(location = 0) in vec3 inPosTCS[];
layout(location = 1) in vec2 inUVTCS[];

layout(location = 0) out vec4 outPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV; 

const float tileLength = 1000.0;

void main() {
    vec3 pos = mix(mix(inPosTCS[0], inPosTCS[1], gl_TessCoord.x),
                   mix(inPosTCS[3], inPosTCS[2], gl_TessCoord.x),
                   gl_TessCoord.y);

    // Calculate World Position of the vertex (before displacement)
    vec4 worldPosBase = vertexUBO.model * vec4(pos, 1.0);

    // Infinite Ocean: Derive UVs from World Position
    // This ensures the texture repeats seamlessly across spatial infinity
    vec2 worldUV = worldPosBase.xz / tileLength;
    
    // Safety: ensure texture wraps (GL_REPEAT must be set in sampler)
    vec2 texCoord = worldUV; 

    ivec2 texSize = textureSize(displacementFoamMap, 0);
    vec2 texelSize = 1.0 / vec2(texSize);

    // Sample displacement using World UVs
    vec4 d_center = texture(displacementFoamMap, texCoord);
    vec4 d_right  = texture(displacementFoamMap, texCoord + vec2(texelSize.x, 0.0));
    vec4 d_left   = texture(displacementFoamMap, texCoord - vec2(texelSize.x, 0.0));
    vec4 d_top    = texture(displacementFoamMap, texCoord + vec2(0.0, texelSize.y));
    vec4 d_bot    = texture(displacementFoamMap, texCoord - vec2(0.0, texelSize.y));

    float worldTexelDist = (tileLength) / float(texSize.x); // Removed scale dependency for distance
    vec3 tangent   = vec3(2.0 * worldTexelDist, 0.0, 0.0) + (d_right.xyz - d_left.xyz);
    vec3 bitangent = vec3(0.0, 0.0, 2.0 * worldTexelDist) + (d_top.xyz - d_bot.xyz);

    outUV = texCoord;

    vec3 normal = normalize(cross(bitangent, tangent));
    
    // Apply displacement to world position
    vec3 displacedPos = worldPosBase.xyz + d_center.xyz;
    
    outPos = vec4(displacedPos, d_center.w);
    outNormal = normal;
    
    gl_Position = ubo.projView * vec4(displacedPos, 1.0);
}
