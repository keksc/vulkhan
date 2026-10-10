#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor; // r: wind stiffness [0 rigid .. 1 tip]

layout(location = 0) out vec3 fragPosWorld;
layout(location = 1) out vec3 fragNormalWorld;
layout(location = 2) out vec2 fragUV;
layout(location = 3) flat out int fragTexIndex;
layout(location = 4) flat out int fragMRTexIndex;
layout(location = 5) flat out float fragRoughness;
layout(location = 6) flat out float fragMetallic;
layout(location = 7) flat out uint fragFlags;
layout(location = 8) out vec3 fragLocalPos;
layout(location = 9) flat out float fragSeed;
layout(location = 10) flat out vec3 fragBiomeTint;
layout(location = 11) out float fragCanopyAO;

#include "../globalUbo.glsl"
#include "../environment.glsl"

struct GPUInstanceData {
    mat4 modelMatrix;
    mat4 normalMatrix;
    vec3 aabbMin;
    int textureIndex;
    vec3 aabbMax;
    int metallicRoughnessTextureIndex;
    float roughnessFactor;
    float metallicFactor;
    float windPhase;
    uint flags;
    uint groupIndex;
    float _pad0;
    float _pad1;
    float _pad2;
};
const uint FLAG_FOLIAGE = 1u;

layout(set = 2, binding = 0) readonly buffer InstanceBuffer {
    GPUInstanceData instances[];
} instanceBuffer;

layout(set = 2, binding = 1) uniform WindUbo {
    float time;
    float strength;
    vec2 dir;
} wind;

layout(push_constant) uniform PushConstants {
    vec4 clipPlane; // world-space plane; see EntitySys::render()'s doc on
                    // the same parameter. Default (0,1,0,1e9) never clips.
} push;

out gl_PerVertex {
    vec4 gl_Position;
    float gl_ClipDistance[1];
};

void main() {
    GPUInstanceData inst = instanceBuffer.instances[gl_InstanceIndex];
    bool foliage = (inst.flags & FLAG_FOLIAGE) != 0u;

    // Broad sway, same frequency for trunk and canopy so branches and their
    // leaves move together; scaled by per-vertex stiffness so trunk bases
    // stay put while branch tips and leaves swing.
    vec4 worldPos = inst.modelMatrix * vec4(inPosition, 1.0);
    vec2 rootXZ = inst.modelMatrix[3].xz;
    float gust = inColor.r > 0.0 ? windGust(rootXZ) : 0.0;
    float sway = (0.4 + 0.6 * sin(ubo.time * 1.15 + inst.windPhase)) *
                 gust * inColor.r;
    // Faster, smaller flutter layered on top, foliage only - this is what
    // reads as individual leaves catching the wind rather than the whole
    // tree swaying as one rigid shape.
    if (foliage) {
        sway += sin(ubo.time * 6.0 + inst.windPhase * 3.1) * 0.10 *
                gust * inColor.r;
    }

    // Wind stays world-aligned even when instances have different yaw.
    worldPos.xz += ubo.wind.xy * sway;
    fragPosWorld = worldPos.xyz;
    fragNormalWorld = normalize(mat3(inst.normalMatrix) * inNormal);
    fragUV = inUV;
    fragTexIndex = inst.textureIndex;
    fragMRTexIndex = inst.metallicRoughnessTextureIndex;
    fragRoughness = inst.roughnessFactor;
    fragMetallic = inst.metallicFactor;
    fragFlags = inst.flags;
    fragLocalPos = inPosition;
    fragSeed = inst.windPhase;
    fragBiomeTint = vec3(inst._pad0,inst._pad1,inst._pad2);
    fragCanopyAO = inColor.g;

    gl_Position = ubo.projView * worldPos;
    gl_ClipDistance[0] = dot(worldPos, push.clipPlane);
}
