# Environmental bioluminescence

The night landscape uses irregular, world-anchored bioluminescent habitats,
rather than moving parallel ribbons or a list of point lights. The moon remains
dim. The shared `shaders/bioluminescence.glsl` field supplies warm amber, gold and
cream, with occasional muted teal habitats. Density uses a squared response:
bright colonies alternate with darker gaps instead of sharing a large ambient
floor. Differently rotated noise octaves avoid axis-aligned habitat patterns.
Analytic field gradients aim the aggregate lighting toward denser emitters
without extra noise queries.

## Sources and illumination

* Terrain and rocks have phosphorescent colonies and small flecks. Subpixel
  detail converges to its average emission instead of sparkling at distance.
* Selected trees have luminous foliage clusters and a restrained bark glow.
* Grass tips glow in flowering patches; close terrain samples also host small
  five-petal flower billboards.
* Fireflies and flying petals move in independently phased, curved paths.
  Wind and the existing attractor transforms perturb their flight smoothly.

The habitat field supplies aggregate **unshadowed irradiance**, shared by
terrain, grass, trees, entities, water and fur through `localLighting`.
Tree lighting treats environmental emitters as predominantly below the canopy.
Front-facing leaves receive more light than their reverse sides; limited
transmission keeps the back side readable. Canopy occlusion attenuates this
lighting as well as the ambient term. Sparse leaf emission is restrained so it
does not erase branch shading.

It approximates the contribution of many small emitters; individual insects
are not shadow-casting lights, and their pulses do not drive landscape brightness.
It is not a physical global-illumination solution. There is no camera, frame
clock, chunk index or resident-source lookup in the illumination equation.
That removes the former residency cutoffs and straight ribbon patterns.

## Particle LOD and terrain residency

The existing 121-entry environment UBO is retained. Only 99 entries are used:
49 near cells at 64 m spacing, 25 middle cells at 256 m and 25 far cells at
1024 m. Centers are independently jittered inside each world cell. GPU particle
IDs derive from the world center, never the recycled array slot.

Near/middle/far tiles generate up to 64/16/4 insects each. Their distance bands
crossfade at roughly 110–170 m and 330–470 m; distant insects fade by 1800 m.
The fixed dispatch capacity is 6336 habitat slots plus at most 200 local slots,
below the existing 8192-particle allocation. Inactive records write zero alpha.
Billboards remain depth tested, additive, and do not write depth.

Terrain queries use only resident geometry, with a budget of 24 tiles / 120
height samples per update. Center and four radius samples estimate ground
slope and airborne clearance. Valid samples survive temporary missing terrain
without a timeout. New heights and clearance interpolate over one second;
arrival and explicit exclusion fade over 0.6 seconds. CPU history is bounded at
2048 tiles, including travel while the game clock is paused. Unvisited missing
terrain cannot produce grounded insects until a height becomes available;
material emission and illumination remain independent of that availability.

## Verification

`tests/nightLights.cpp`, `streamLightCache.cpp`, and `streamRetention.cpp` cover
jitter, world anchoring, query budget, bounded history, long terrain gaps,
explicit removal, recovery and interpolation.

`tests/environmentGpu.cpp` runs the real particle compute shader and a probe
of the shared illumination shader. Compile `tests/environmentField.comp` to
`/tmp/environmentField.spv` with glslc, build the C++ fixture with `-lvulkan`,
then run from the repository root. It checks continuity at world-grid
boundaries, bit-identical lighting after changing camera/time/residency,
nonuniform habitat coverage with substantial dark regions, strong front/back
leaf-lighting contrast, canopy occlusion, all three particle LODs, flower/petal types,
determinism and small-step motion. It writes `/tmp/environment-habitats.ppm`,
a top-down **irradiance diagnostic**, not a screenshot of the game.

Debug compilation, SPIR-V validation, CPU regressions and headless Vulkan
validation are the available automated checks. Full-scene appearance and
144 Hz performance still need verification on the target GPU: walk and fly
across tile boundaries, inspect hills at several terrain LODs, change wind,
and watch newly streamed terrain and flowers at ground level.
