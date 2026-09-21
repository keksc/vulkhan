#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 fragPosWorld;
layout(location = 1) in vec3 fragNormalWorld;
layout(location = 2) in vec2 fragUV;
layout(location = 3) flat in int fragTexIndex;
layout(location = 4) flat in int fragMRTexIndex;
layout(location = 5) flat in float fragRoughness;
layout(location = 6) flat in float fragMetallic;
layout(location = 7) flat in uint fragFlags;

layout(location = 0) out vec4 outColor;

#include "../globalUbo.glsl"

layout(set = 1, binding = 0) uniform sampler2D sceneTextures[256];

const float PI = 3.14159265359;
const uint FLAG_FOLIAGE = 1u;

// Fraction of light that passes through a leaf to its unlit side.
const float FOLIAGE_TRANSMISSION = 0.5;

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
    bool foliage = (fragFlags & FLAG_FOLIAGE) != 0u;

    vec4 albedo4 = vec4(1.0);
    if (fragTexIndex >= 0) {
        vec4 texel = texture(sceneTextures[nonuniformEXT(fragTexIndex)], fragUV);
        albedo4 *= texel;
        // Foliage cards rely on alphaToCoverageEnable (set on the pipeline
        // in treeSys.cpp) for soft, MSAA-resolved edges. Trunks have no
        // cutout texture in practice, but a hard discard is kept here as a
        // safe fallback for a trunk mesh authored with cutout alpha too.
        if (!foliage && texel.a < 0.5)
            discard;
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
    if (foliage && dot(N, V) < 0.0)
        N = -N;

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

    float NdotLback = foliage ? max(dot(-N, L), 0.0) : 0.0;
    vec3 diffuse = kD * albedo / PI * (NdotL + FOLIAGE_TRANSMISSION * NdotLback);
    vec3 Lo = (diffuse + specular * NdotL) * radiance;

    vec3 ambient = albedo * mix(0.12, 0.30, N.y * 0.5 + 0.5);
    vec3 color = ambient + Lo;

    color = color / (color + vec3(1.0)); // tonemap
    color = pow(color, vec3(1.0 / 2.2));  // gamma

    outColor = vec4(color, albedo4.a);
}
