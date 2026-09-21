#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 fragPosWorld;
layout(location = 1) in vec3 fragNormalWorld;
layout(location = 2) in vec2 fragUV;
layout(location = 3) flat in vec4 fragColor;
layout(location = 4) flat in int fragTexIndex;
layout(location = 5) flat in int fragMRTexIndex;
layout(location = 6) flat in float fragRoughness;
layout(location = 7) flat in float fragMetallic;

layout(location = 0) out vec4 outColor;

#include "../globalUbo.glsl"

layout(set = 1, binding = 0) uniform sampler2D sceneTextures[256];

const float PI = 3.14159265359;

// All BRDF terms below take precomputed dot products (clamped to >=0 by the
// caller) instead of vectors, so each of N.V/N.L/N.H/H.V is computed once
// in main() and reused, rather than re-derived per function.

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(float NdotH, float roughness) {
    float a2 = pow(roughness * roughness, 2.0);
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float GeometrySchlickGGX(float NdotX, float k) {
    return NdotX / (NdotX * (1.0 - k) + k);
}

float GeometrySmith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return GeometrySchlickGGX(NdotV, k) * GeometrySchlickGGX(NdotL, k);
}

void main() {
    // fragTexIndex >= ALPHA_CUTOUT_FLAG marks an alpha-cutout material (set
    // on the CPU for LOD/foliage scenes only); real texture index is the
    // value minus the flag.
    const int ALPHA_CUTOUT_FLAG = 1024;
    bool cutout = fragTexIndex >= ALPHA_CUTOUT_FLAG;
    int texIndex = cutout ? fragTexIndex - ALPHA_CUTOUT_FLAG : fragTexIndex;

    vec4 albedo4 = fragColor;
    if (texIndex >= 0) {
        vec4 texel = texture(sceneTextures[nonuniformEXT(texIndex)], fragUV);
        albedo4 *= texel;
        if (cutout && texel.a < 0.5) discard;
    }
    vec3 albedo = pow(albedo4.rgb, vec3(2.2));

    float metallic = fragMetallic;
    float roughness = fragRoughness;
    if (fragMRTexIndex >= 0) {
        vec3 mr = texture(sceneTextures[nonuniformEXT(fragMRTexIndex)], fragUV).rgb;
        metallic *= mr.b;
        roughness *= mr.g;
    }
    roughness = clamp(roughness, 0.05, 1.0);

    vec3 N = normalize(fragNormalWorld);
    vec3 V = normalize(ubo.inverseView[3].xyz - fragPosWorld);
    vec3 L = normalize(vec3(1.0, 1.0, 0.5));
    vec3 H = normalize(V + L);
    vec3 radiance = vec3(2.0);

    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = fresnelSchlick(HdotV, F0);
    float NDF = DistributionGGX(NdotH, roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);

    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 specular = min((NDF * G * F) / (4.0 * NdotV * NdotL + 0.0001), vec3(4.0));

    vec3 diffuse = kD * albedo / PI * NdotL;
    vec3 Lo = (diffuse + specular * NdotL) * radiance;

    vec3 ambient = albedo * mix(0.12, 0.30, N.y * 0.5 + 0.5);
    vec3 color = ambient + Lo;

    color = color / (color + vec3(1.0)); // tonemap
    color = pow(color, vec3(1.0 / 2.2));  // gamma

    outColor = vec4(color, 1.0);
}
