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

#include "globalUbo.glsl"

layout(set = 1, binding = 0) uniform sampler2D sceneTextures[256];

const float PI = 3.14159265359;

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (k) + (1.0 - k);

    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

void main() {
    vec4 albedo4 = fragColor;
    if (fragTexIndex >= 0) {
        albedo4 *= texture(sceneTextures[nonuniformEXT(fragTexIndex)], fragUV);
    }
    vec3 albedo = pow(albedo4.rgb, vec3(2.2)); // Convert to linear space

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

    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);

    // Direct lighting (one directional light for now)
    vec3 L = normalize(vec3(1.0, 1.0, 0.5));
    vec3 H = normalize(V + L);
    vec3 radiance = vec3(2.0); // Light color/intensity

    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= (1.0 - metallic);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    float NdotL = max(dot(N, L), 0.0);
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    vec3 ambient = vec3(0.03) * albedo;
    vec3 color = ambient + Lo;

    // HDR tonemapping and gamma correction
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

    outColor = vec4(color, albedo4.a);
}
