# Shared environment

`EngineContext::environment` in `src/vkh/environment.hpp` controls the scene.
The main view and planar reflections upload identical environment values and
animation time through `GlobalUbo`; `shaders/environment.glsl` implements the
shared lighting, gusts, and atmosphere.

- `sunDirection` points **toward** the sun in world space. It is normalized
  safely before upload. `sunColor` is linear RGB; `sunIntensity` scales it.
- `windDirection` is the travel direction on XZ. `windSpeed` is metres per
  second and drives gust travel and the water spectrum. `windStrength` controls
  vegetation bending. Set both speed and strength to zero for calm vegetation;
  the wave spectrum retains its numerical minimum of 0.5 m/s.
- `fogDensity`, `fogHeightFalloff`, and `fogBaseHeight` control exponential
  height fog. `fogColor` controls the cool haze; `fogAnisotropy` controls the
  warm forward-scattering sun glow. Density zero disables fog and sun shafts.
- `skyAmbient` and `groundAmbient` control hemisphere illumination.

The default palette uses warm sunlight, cool valley haze, and broad travelling
wind fronts. The existing black hole remains an independent emissive source.
Sun shafts use the shared direction in the half-resolution effects pass.

Rocks use six seeded mesh variants, a procedural mineral material, GPU
instancing/culling, and the existing asynchronous scenery streamer. Only a
90-metre near field is prewarmed. Spatially binned circular footprints suppress
grass beneath the rocks without scanning every rock for every grass texel.

Scene materials output linear light; the shared post-processing pass applies
gamma once. Shader dependency files ensure edits to shared GLSL includes
rebuild their consumers.

Reflection attachments match the main graphics pipeline's colour/depth formats.
Presentation compute shaders use write-only unformatted image stores so RGBA
and BGRA swapchains both work; device selection verifies and enables
`shaderStorageImageWriteWithoutFormat`.
