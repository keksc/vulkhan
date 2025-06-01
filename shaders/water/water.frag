#version 450

layout(location = 0) in vec4 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inViewVec;
layout(location = 4) in vec3 inLightVec;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform WaterSurfaceUBO
{
  mat4 invModel;
    vec3  camPos;
    float height;
    vec3 absorpCoef;
    vec3 scatterCoef;
    vec3 backscatterCoef;
    // ------------- Terrain
    vec3 terrainColor;
    // ------------ Sky
    float skyIntensity;
    float specularIntensity;
    float specularHighlights;
    vec3  sunColor;
    float sunIntensity;
    // ------------- Sun Direction (kept for lighting calculations)
    vec3  sunDir;
} surface;

// Add cubemap sampler for skybox
layout(set = 1, binding = 0) uniform samplerCube skybox;

#define M_PI 3.14159265358979323846
#define ONE_OVER_PI (1.0 / M_PI)

// Water and Air indices of refraction
#define WATER_IOR 1.33477
#define AIR_IOR 1.0
#define IOR_AIR2WATER (AIR_IOR / WATER_IOR)

#define NORMAL_WORLD_UP vec3(0.0, 1.0, 0.0)

#define TERRAIN_HEIGHT 0.0f
vec4 kTerrainBoundPlane = vec4(NORMAL_WORLD_UP, TERRAIN_HEIGHT);

struct Ray
{
    vec3 org;
    vec3 dir;
};

/**
 * @brief Returns sky color from skybox at view direction
 * @param kSunDir Normalized vector to sun
 * @param kViewDir Normalized vector to eye
 */
vec3 SkyLuminance(const in vec3 kSunDir, const in vec3 kViewDir)
{
    // Sample the skybox cubemap using the view direction
    vec3 skyColor = texture(skybox, kViewDir).rgb;
    
    // Optionally enhance with sun disk effect (kept from original)
    const float kSunDisk = smoothstep(0.997f, 1.f, max(dot(kViewDir, kSunDir), 0.0f));
    return skyColor + kSunDisk * surface.sunColor * surface.sunIntensity;
}

/**
 * @brief Finds an intersection point of ray with the terrain
 * @return Distance along the ray; positive if intersects, else negative
 */
float IntersectTerrain(const in Ray ray)
{
    return -(dot(ray.org, kTerrainBoundPlane.xyz) - kTerrainBoundPlane.w) /
           dot(ray.dir, kTerrainBoundPlane.xyz);
}

vec3 TerrainNormal(const in vec2 p);

vec3 TerrainColor(const in vec2 p);

/**
 * @brief Fresnel reflectance for unpolarized incident light
 */
float FresnelFull(in float theta_i, in float theta_t)
{
    if (theta_i <= 0.00001)
    {
        const float at0 = (WATER_IOR-1.0) / (WATER_IOR+1.0);
        return at0*at0;
    }

    const float t1 = sin(theta_i-theta_t) / sin(theta_i+theta_t);
    const float t2 = tan(theta_i-theta_t) / tan(theta_i+theta_t);
    return 0.5 * (t1*t1 + t2*t2);
}

vec3 Attenuate(const in float kDistance)
{
    vec3 extinction = surface.absorpCoef + surface.scatterCoef;
    return exp(-extinction * kDistance);
}

/**
 * @brief Refraction of air incident light
 */
vec3 RefractAirIncident(const in vec3 kIncident, const in vec3 kNormal)
{
    return refract(kIncident, kNormal, IOR_AIR2WATER);
}

vec3 ComputeWaterSurfaceColor(const in Ray ray, const in vec3 p_w, const in vec3 kNormal)
{
    const vec3 kLightDir = normalize(surface.sunDir);
    const vec3 kViewDir = normalize(ray.dir);

    // Reflection direction
    const vec3 kReflectDir = reflect(-kViewDir, kNormal);
    const vec3 reflectColor = SkyLuminance(kLightDir, kReflectDir);

    // Fresnel term (Schlick approximation)
    float cosTheta = dot(kNormal, kViewDir);
    float fresnel = pow(1.0 - cosTheta, 5.0) * (1.0 - 0.02) + 0.02;

    // Refracted ray
    const vec3 kRefractDir = refract(-kViewDir, kNormal, IOR_AIR2WATER);
    Ray refractRay = Ray(p_w, kRefractDir);
    float t_g = IntersectTerrain(refractRay);
    vec3 p_g = refractRay.org + t_g * refractRay.dir;
    vec3 terrainColor = TerrainColor(p_g.xz);

    // Attenuate the terrain color based on the distance through water
    vec3 refractColor = terrainColor * Attenuate(t_g);

    // In-scattered light (simplified diffuse term)
    vec3 irradiance = SkyLuminance(kLightDir, kNormal); // Approximate irradiance
    vec3 diffuse = (0.33 * surface.backscatterCoef) / surface.absorpCoef * irradiance * ONE_OVER_PI;
    vec3 waterColor = refractColor + diffuse * (1.0 - Attenuate(t_g));

    // Combine reflection and refraction based on Fresnel term
    vec3 color = mix(waterColor, reflectColor, fresnel);

    // Add specular highlight
    vec3 halfDir = normalize(kLightDir + kViewDir);
    float spec = pow(max(dot(kNormal, halfDir), 0.0), surface.specularHighlights);
    vec3 specular = surface.specularIntensity * spec * surface.sunColor * surface.sunIntensity;

    return color + specular;
}

void main()
{
  vec3 cI = normalize (vec3(inPos));
  vec3 cR = reflect (cI, normalize(inNormal));

  cR = vec3(surface.invModel * vec4(cR, 0.0));
  // Convert cubemap coordinates into Vulkan coordinate space
  cR.xy *= -1.0;

  vec4 color = texture(skybox, cR);

  vec3 N = normalize(inNormal);
  vec3 L = normalize(inLightVec);
  vec3 V = normalize(inViewVec);
  vec3 R = reflect(-L, N);
  vec3 ambient = vec3(0.5) * color.rgb;
  vec3 diffuse = max(dot(N, L), 0.0) * vec3(1.0);
  vec3 specular = pow(max(dot(R, V), 0.0), 16.0) * vec3(0.5);
  outColor = vec4(ambient + diffuse * color.rgb + specular, 1.0);
    // const Ray ray = Ray(surface.camPos, normalize(inPos.xyz - surface.camPos));
    // const vec3 kNormal = normalize(inNormal);
    //
    // const vec3 p_w = vec3(inPos.x, inPos.y + surface.height, inPos.z);
    // vec3 color = ComputeWaterSurfaceColor(ray, p_w, kNormal);
    //
    // if (inPos.w < 0.0)
    //     color = vec3(1.0);
    //
    // fragColor = vec4(color, 1.0);
    // fragColor = vec4(1.0) - exp(-fragColor * 2.0f);
}

// Terrain functions
float Fbm4Noise2D(in vec2 p);

float TerrainHeight(const in vec2 p)
{
    return TERRAIN_HEIGHT - 8.f * Fbm4Noise2D(p.yx * 0.02f);
}

vec3 TerrainNormal(const in vec2 p)
{
    const vec2 kEpsilon = vec2(0.0001, 0.0);
    return normalize(
        vec3(
            TerrainHeight(p - kEpsilon.xy) - TerrainHeight(p + kEpsilon.xy), 
            10.0f * kEpsilon.x,
            TerrainHeight(p - kEpsilon.yx) - TerrainHeight(p + kEpsilon.yx)
        )
    );
}

vec3 TerrainColor(const in vec2 p)
{
    float n = clamp(Fbm4Noise2D(p.yx * 0.02 * 2.), 0.6, 0.9);
    return n * surface.terrainColor;
}

// Noise functions
float hash1(in vec2 i)
{
    i = 50.0 * fract(i * ONE_OVER_PI);
    return fract(i.x * i.y * (i.x + i.y));
}

float Noise2D(const in vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = hash1(i + vec2(0,0));
    float b = hash1(i + vec2(1,0));
    float c = hash1(i + vec2(0,1));
    float d = hash1(i + vec2(1,1));
    return -1.0 + 2.0 * (a + (b - a) * u.x + (c - a) * u.y + (a - b - c + d) * u.x * u.y);
}

const mat2 MAT345 = mat2(4./5., -3./5., 3./5., 4./5.);

float Fbm4Noise2D(in vec2 p)
{
    const float kFreq = 2.0;
    const float kGain = 0.5;
    float amplitude = 0.5;
    float value = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        value += amplitude * Noise2D(p);
        amplitude *= kGain;
        p = kFreq * MAT345 * p;
    }
    return value;
}
