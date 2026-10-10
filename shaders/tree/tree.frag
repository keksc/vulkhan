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
layout(location = 8) in vec3 fragLocalPos;
layout(location = 9) flat in float fragSeed;
layout(location = 10) flat in vec3 fragBiomeTint;
layout(location = 11) in float fragCanopyAO;

layout(location = 0) out vec4 outColor;

#include "../globalUbo.glsl"
#include "../environment.glsl"

layout(set = 1, binding = 0) uniform sampler2D sceneTextures[256];

const float PI = 3.14159265359;
const uint FLAG_FOLIAGE = 1u;
const uint FLAG_ROCK = 2u;

// Fraction of light that passes through a leaf to its unlit side.
const float FOLIAGE_TRANSMISSION = 0.5;
// Wrap amount for foliage diffuse: >0 softens the light/dark terminator so
// there is no visible line where N.L crosses zero.
const float FOLIAGE_WRAP = 0.35;

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
        if (foliage) {
            // Sharpen the cutout alpha to ~1 pixel of transition, so alpha-
            // to-coverage gives crisp but anti-aliased needle edges that
            // stay stable across mip levels and under magnification.
            float w = max(fwidth(texel.a), 1e-4);
            albedo4.a = clamp((texel.a - 0.5) / w + 0.5, 0.0, 1.0);
        } else if (texel.a < 0.5) {
            discard;
        }
    }
    vec3 albedo = pow(albedo4.rgb, vec3(2.2));
    if(foliage) albedo *= fragBiomeTint;

    float metallic = fragMetallic;
    float roughness = fragRoughness;
    if (fragMRTexIndex >= 0) {
        vec3 mr = texture(sceneTextures[nonuniformEXT(fragMRTexIndex)], fragUV).rgb;
        metallic *= mr.b;
        roughness *= mr.g;
    }
    roughness = clamp(roughness, 0.05, 1.0);

    // NOTE: no per-fragment "flip N toward the viewer" for foliage. Card
    // normals are authored smooth (blended toward an outward direction), and
    // flipping on dot(N,V) < 0 put a hard lighting discontinuity across every
    // card. Foliage is instead lit two-sided and symmetrically below.
    vec3 N = normalize(fragNormalWorld);
    vec3 V = normalize(ubo.inverseView[3].xyz - fragPosWorld);
    float contactAO = clamp(fragCanopyAO,0.25,1.0);
    if ((fragFlags & FLAG_ROCK) != 0u) {
        vec3 P = fragPosWorld;
        float distanceFade = 1.0 - smoothstep(35.0, 150.0, distance(P, ubo.inverseView[3].xyz));
        vec3 weights = pow(abs(N), vec3(4.0));
        weights /= max(dot(weights, vec3(1.0)), 0.001);
        float mineral = weights.x * weatherNoise(P.zy * 4.0) +
                        weights.y * weatherNoise(P.xz * 4.0) +
                        weights.z * weatherNoise(P.xy * 4.0);
        float grain = mix(0.5, mineral, distanceFade);
        float weather = weatherNoise(P.xz * 0.35 + fragSeed);
        albedo = mix(vec3(0.14, 0.16, 0.17), vec3(0.30, 0.28, 0.24),
                      0.3 + 0.4 * weather + 0.12 * sin(fragSeed * 3.0));
        albedo *= 0.92 + 0.16 * grain;
        albedo *= fragBiomeTint;
        // Small fractures perturb actual geometric normals; the silhouette
        // and broad facets come from the mesh, not a painted round stamp.
        vec3 dx = dFdx(P), dy = dFdy(P);
        vec3 tx = cross(dy, N), ty = cross(N, dx);
        float determinant = dot(dx, tx);
        vec3 gradient = (dFdx(mineral) * tx + dFdy(mineral) * ty) *
                         sign(determinant) / max(abs(determinant), 1e-5);
        N = normalize(N - gradient * 0.022 * distanceFade);
        float moss = smoothstep(0.56, 0.78, weather) *
                     smoothstep(0.55, 0.95, N.y);
        albedo = mix(albedo, vec3(0.065, 0.10, 0.04), moss * 0.5);
        roughness = mix(0.68, 0.90, grain);
        metallic = 0.0;
        contactAO = mix(0.65, 1.0, smoothstep(-0.1, 0.45, fragLocalPos.y));
    }

    vec3 L = sunDirection();
    vec3 H = normalize(V + L);
    vec3 radiance = sunRadiance();

    float ndl = dot(N, L);
    vec3 color;

    if (foliage) {
        // Wrapped front + transmitted back lighting; continuous in N.L.
        float front = clamp((ndl + FOLIAGE_WRAP) / (1.0 + FOLIAGE_WRAP), 0.0, 1.0);
        float back  = clamp((-ndl + FOLIAGE_WRAP) / (1.0 + FOLIAGE_WRAP), 0.0, 1.0);
        vec3 diffuse = albedo / PI * (front + FOLIAGE_TRANSMISSION * back);
        vec3 ambient = albedo * hemisphereAmbient(N) * contactAO;
        color = ambient + diffuse * radiance; // needles are matte: no specular
    } else {
        float NdotV = max(dot(N, V), 0.0);
        float NdotL = max(ndl, 0.0);
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
        vec3 ambient = albedo * hemisphereAmbient(N) * contactAO;
        color = ambient + Lo;
    }

    BioHabitat habitat = bioHabitat(fragPosWorld.xz);
    bool rock = (fragFlags & FLAG_ROCK) != 0u;
    color += albedo * (rock ? bioIrradiance(habitat, N) * contactAO
                           : bioTreeIrradiance(habitat, N, contactAO, foliage));
    float footprint=max(length(dFdx(fragPosWorld)),length(dFdy(fragPosWorld)));
    if ((fragFlags & 2u) != 0u) {
      color += bioSurface(fragPosWorld,N,footprint,habitat);
    } else {
      float clusters=smoothstep(.67,.86,bioNoise(fragPosWorld.xz*.7+fragPosWorld.y*.23));
      float selected=smoothstep(.3,.65,fract(sin(fragSeed*13.17)*43758.54));
      float leaves=foliage ? 1.0 : .08;
      color += habitat.tint*habitat.density*clusters*selected*leaves*.22*contactAO;
    }
    color = sceneFog(color, fragPosWorld, V, distance(ubo.inverseView[3].xyz, fragPosWorld));

    outColor = vec4(color, albedo4.a);
}
