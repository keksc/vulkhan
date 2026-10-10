# Terrain checks

These CPU checks need a C++23 compiler and GLM, provided by `nix develop`.
Run from the repository root:

```sh
c++ -std=c++23 -O2 tests/terrainChunks.cpp src/vkh/systems/terrain/terrainChunks.cpp -o /tmp/terrain-chunks-tests
/tmp/terrain-chunks-tests
c++ -std=c++23 -O2 tests/environment.cpp -o /tmp/environment-tests
/tmp/environment-tests
```

Keep assertions enabled. The checks cover surface-only topology, mixed-LOD
seams and corners, negative chunk coordinates, reflection simplification,
resident draw selection, and the startup leaf budget. They also report time
spent stitching; this is a CPU measurement, not a frame-rate benchmark.
The environment checks cover safe direction normalization, wave wind-speed
mapping, and reproducible rock geometry with valid normals and bounds.

For GPU validation, build in Debug and launch `./vulkhan` from `build/` with
`VK_LAYER_KHRONOS_validation` installed. Inspect hills and shorelines while
moving across LOD boundaries, including their water reflections.

World configuration and geometry checks (inside the development environment):

```sh
c++ -std=c++23 -O2 tests/world.cpp src/world/config.cpp -o /tmp/world-tests
/tmp/world-tests
c++ -std=c++23 -O2 -DGLM_ENABLE_EXPERIMENTAL -DGLFW_INCLUDE_VULKAN -I3rdparty/include tests/trees.cpp -o /tmp/tree-tests
/tmp/tree-tests
```

These validate profile overrides, new named profiles, blend continuity, crater
geometry, active-vent lake exclusion, and conifer/broadleaf mesh bounds,
normals, wood branches, LOD simplification, and leaf atlases.


Direwolf grounding and hill traversal checks:

```sh
c++ -std=c++23 -O2 tests/terrainSurface.cpp src/vkh/systems/terrain/terrainChunks.cpp -o /tmp/terrain-surface-tests
/tmp/terrain-surface-tests
c++ -std=c++23 -O2 -DGLM_ENABLE_EXPERIMENTAL tests/wolfPacks.cpp src/vkh/systems/wolf/wolfPacks.cpp src/vkh/systems/wolf/wolfNavigation.cpp -o /tmp/wolf-packs-tests
/tmp/wolf-packs-tests
python3 tests/validateWolf.py
```

Surface checks compare queries to points constructed on indexed triangles,
including fitted edges, corners, negative coordinates and four LOD levels.
Pack checks cover deterministic movement, grounded interpolation, water
avoidance, bounded stalls, inactive wet spawns, and climbing/descending a
synthetic landscape with a flat core. Encounter checks cover cardinal camera directions and nearby pack recycling
after travel. The GLB checks cover projected outward-face winding, rig weights,
loop continuity, all three geometry LODs, and skinned, bent hair-card primitives. GPU appearance and performance
still require in-game validation.


Behavior and navigation checks:

```sh
c++ -std=c++23 -O2 -DGLM_ENABLE_EXPERIMENTAL tests/wolfAI.cpp src/vkh/systems/wolf/wolfPacks.cpp src/vkh/systems/wolf/wolfNavigation.cpp -o /tmp/wolf-ai-tests
/tmp/wolf-ai-tests
```

These check persistent goals, planned detours to unchanged destinations,
terrain-aware A*, rest/travel cycles, bounded turning and acceleration,
formation/body spacing, shared fleeing, perception occlusion, hysteresis and
spectator exclusion, streamed-terrain loss/recovery, and shared route-query
budgets, bounded alert watching, and shorter escape routes. A limited streamed-area regression verifies that packs keep moving
when their original territory destinations are unavailable, without stepping
outside loaded ground. They also report average CPU time on synthetic terrain.


UI geometry and graphics settings checks:

```sh
c++ -std=c++23 -O2 tests/uiGeometry.cpp -o /tmp/ui-geometry-test
/tmp/ui-geometry-test
c++ -std=c++23 -O2 tests/graphicsSettings.cpp -o /tmp/graphics-settings-test
XDG_CACHE_HOME=/tmp /tmp/graphics-settings-test
```

These cover concave triangulation, both windings, edge hits, non-square
transforms, hardware MSAA fallback, older settings files, persistence,
Off values and invalid preset recovery. See `docs/uiGraphics.md` for manual
GPU checks and a clearly labelled layout illustration.

UI resize checks: compile and run `tests/uiLayout.cpp` as above. These cover
wide, tall and small windows, centered placement, preserved button proportions,
and minimized-window handling.

Text editing checks: `c++ -std=c++23 -O2 tests/textEditing.cpp -o
/tmp/text-editing-test` followed by `/tmp/text-editing-test`. These exercise
empty fields, click-to-caret placement, multiline boundaries, word deletion,
and equivalent hit positions after uniform scaling.

Mouse capture transition checks:

```sh
c++ -std=c++23 -O2 tests/mouseLook.cpp -o /tmp/mouse-look-test
/tmp/mouse-look-test
```

These cover initial capture, menu cursor movement, recapture at unrelated
coordinates, normal movement after resuming, same-poll pause/resume, and repeated
focus resets. Manually open ESC, move the cursor widely, navigate pause pages,
and resume using both ESC and the Resume button; the camera should retain its
direction. Repeat after resizing, fullscreen changes and alt-tab.

Slider checks:

```sh
c++ -std=c++23 -O2 tests/slider.cpp -o /tmp/slider-test
/tmp/slider-test
```

Checks cover endpoint clamping, stepped values, a zero-width range and continuous
mode. Graphics persistence checks also verify intermediate FOV/intensity values.
Manual GPU checks: click track endpoints, drag across and outside the control,
release outside, resize/fullscreen, and restart to confirm saved values.

Environmental particle placement checks:

```sh
c++ -std=c++23 -O2 tests/particleEnvironment.cpp -o /tmp/particle-environment-test
/tmp/particle-environment-test
```

These exercise bounded patch counts, flat-ground heights, blocked placement,
unloaded ground, steep slopes, grid stability and distant camera relocation.
See `docs/particles.md` for GPU/runtime checks and effect controls.

Night lighting grid checks:

```sh
c++ -std=c++23 -O2 tests/nightLights.cpp -o /tmp/night-lights-test
/tmp/night-lights-test
```

These verify stable jittered world cells, all three LOD spacings, and particle
capacity. See `docs/nightLighting.md` for the current environmental lighting.

Entity API checks:

```sh
c++ -std=c++23 -O2 tests/entityHandles.cpp -o /tmp/entity-handles-test
/tmp/entity-handles-test
c++ -std=c++23 -O2 tests/entityBatching.cpp -o /tmp/entity-batching-test
/tmp/entity-batching-test
```

Handle checks cover stable identity across growth, foreign/stale handles and
repeated slot reuse. Batching checks cover interleaved multi-mesh models, stable
identity, unique complete permutations and empty input. See
`docs/entityRendering.md` for the simplified API and runtime GPU checks.

Extracted application math checks:

```sh
c++ -std=c++23 -O2 tests/appMath.cpp src/app/math.cpp -o /tmp/app-math-test
/tmp/app-math-test
```

These verify Vulkan near-plane conventions, conservative sphere culling and
bounded attractor transforms after extraction. See `docs/applicationLayout.md`
for build dependency and incremental compilation verification.

Stream terrain cache checks:

```sh
c++ -std=c++23 -O2 tests/streamLightCache.cpp -o /tmp/stream-light-cache-test
/tmp/stream-light-cache-test
```

These check terrain-query budgets, overlapping world-cell identity and bounded
history during travel, including paused-clock movement.

Stream stability checks:

```sh
c++ -std=c++23 -O2 tests/streamRetention.cpp -o /tmp/stream-retention-test
/tmp/stream-retention-test
```

These cover long terrain gaps without expiration, explicit blocked-source fades,
restoration and interpolated height changes.

Headless environmental GPU checks (Vulkan driver required):

```sh
glslc tests/environmentField.comp -o /tmp/environmentField.spv
c++ -std=c++23 -O2 tests/environmentGpu.cpp -lvulkan -o /tmp/environment-gpu-test
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation /tmp/environment-gpu-test
```

Checks actual shader field continuity and invariance under camera/time/residency
changes, plus near/middle/far particle LODs, flowers, petals and smooth motion.
The PPM output is a field diagnostic, not a full-scene visual acceptance test.
