#version 450

layout(location = 0) in vec4 inPos;      // world‐space position of the fragment
layout(location = 1) in vec3 inNormal;   // world‐space normal
layout(location = 2) in vec2 inUV;       // (unused here)

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform WateruboUBO
{
    mat4 invView;           // (not used below, but kept for other passes)
    vec3  camPos;           // camera world‐space position
    float height;           // (unused in this simplified scattering)
    vec3  absorpCoef;       // absorption coefficient (not used in this simple approx)
    vec3  scatterCoef;      // subsurface scattering tint/color of the water
    vec3  backscatterCoef;  // backscatter color (grazing‐angle light scattering)
    // ------------- Terrain
    vec3 terrainColor;      // (unused in water pass)
    // ------------ Sky
    float skyIntensity;     // ambient multiplier for sky contribution
    float specularIntensity;// F₀ for Fresnel (base reflectivity)
    float specularHighlights; // shininess exponent
    vec3  sunColor;         // color of the sun
    float sunIntensity;     // brightness of the sun
    // ------------- Sun Direction (kept for lighting calculations)
    vec3  sunDir;           // normalized, world‐space direction toward sun
} ubo;

// Cubemap sampler for skybox
layout(set = 1, binding = 0) uniform samplerCube skybox;

/**
 * @brief Sample sky color (optionally adding a tiny “sun disk”)
 * @param kSunDir Normalized vector to sun
 * @param kViewDir Normalized vector to eye
 */
vec3 SkyLuminance(const in vec3 kSunDir, const in vec3 kViewDir)
{
    // Sample the skybox cubemap using the view direction
    vec3 skyColor = texture(skybox, kViewDir).rgb;

    // Add a small “sun glow” when looking nearly directly at the sun
    float kSunDisk = smoothstep(0.997, 1.0, max(dot(kViewDir, kSunDir), 0.0));
    return skyColor + kSunDisk * ubo.sunColor * ubo.sunIntensity;
}

void main()
{
    vec3 worldPos = inPos.xyz;
    vec3 N = normalize(inNormal);

    vec3 V = normalize(ubo.camPos - worldPos);

    vec3 L = normalize(-ubo.sunDir);

    vec3 R = reflect(-V, N);

    vec3 envColor = texture(skybox, R).rgb;

    float NdotV = max(dot(N, V), 0.0);
    float F0 = ubo.specularIntensity;
    float fresnel = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);

    float NdotL = max(dot(N, L), 0.0);
    vec3 scatterTerm = ubo.scatterCoef   * NdotL * (1.0 - fresnel);
    vec3 backscatterTerm = ubo.backscatterCoef * (1.0 - NdotV) * (1.0 - fresnel);

    vec3 subsurface = scatterTerm + backscatterTerm;

    vec3 ambient = envColor * ubo.skyIntensity * (1.0 - fresnel);

    vec3 halfVec = normalize(L + V);
    float NdotH  = max(dot(N, halfVec), 0.0);
    float specPower = pow(NdotH, ubo.specularHighlights);
    vec3 specular = ubo.sunColor * ubo.sunIntensity * ubo.specularIntensity * specPower;

    vec3 reflected = envColor * fresnel;
    vec3 finalColor = reflected + subsurface + ambient + specular;

    outColor = vec4(finalColor, 1.0);
}
