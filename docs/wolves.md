# Running direwolf packs

The world contains three packs of six original direwolf-inspired wolves.
They have heavier shoulders and necks, broader muzzles, smaller ears and
recessed eyes. Runtime scales vary from 1.20 to 1.45; the three packs have
ash-gray, pale white (muted crimson eyes), and charcoal coats.

One encounter pack searches dry, traversable terrain 35–75 m ahead of the
camera, checking sight lines and room for the whole pack. Two farther packs
search a 100–220 m ring. Population streaming still recycles hidden, distant
packs to make wildlife observable as the player travels.

Behavior is now a state machine: territory travel, rest, alert, flee and
regroup. Routes persist, followers use the leader's trail, and A* handles
terrain exclusions. Wolves watch nearby players and flee on a close approach;
they resume travelling after the threat subsides. See `wolfAI.md` for rules,
budgets, tests and the AI/rendering split.

Grounding samples the currently drawn terrain triangles, including the
vertex shader's fitted LOD edges. CPU height arrays retain four bytes per
terrain vertex (at most about 8.3 MiB at the default 512-chunk capacity).
Queries use the drawn chunk index and interpolation, without GPU readback or
per-frame terrain generation. Outside resident terrain, the same triangle
lattice is reconstructed from the height function. Interpolated wolves are
reprojected to that surface each frame; slope avoidance uses a slope limit
rather than the previous speed-dependent absolute height step.

The bounded simulation ticks at 30 Hz and interpolates transforms for
rendering. `WolfPacks` runs in the main loop; `WolfSys` only consumes its
snapshot and renders/animates it. One original Blender GLB and one 48-frame, 24-joint pose cache are
shared by all 18 wolves. The renderer turns the GLB's +Z muzzle
into the steering system's -Z forward with a proper Y half-turn, including
reflection views. The pose cache samples the authored run clip in reverse so
planted paws travel toward the tail while the body advances. The vertex
shader interpolates poses and skins each wolf at its individual,
speed-driven phase; no per-wolf CPU skeleton updates
or repeated asset loads occur each frame.

The renderer uses:

| Camera distance | Body triangles | Hair cards / triangles |
| --- | --- | --- |
| Under 45 m | LOD0: 10,492 | 1,600 / 9,600 |
| 45–110 m | LOD1: 4,406 | 650 / 3,900 |
| Over 110 m | LOD2: 1,678 | 200 / 1,200 |

Bent, skinned geometry strips form the neck ruff, cheek guard hairs, coat and
tail. Their four strongest root-bone weights are transferred from the body;
the same pose cache deforms both primitives. UV-anchored strand coverage
breaks up each card into tapered hairs. Card colours sample the coat atlas at
their authored root region, preserving the gray, pale and charcoal palettes.
Bare nose, eyes, mouth, ear inset, claws and paws have no cards.

The body uses counterclockwise back-face culling. Cards are two-sided and
use MSAA alpha-to-coverage with depth writes. There is one body draw and one
card draw per visible contiguous instance batch; no shell extrusion loop.
Lighting uses the global sun, ambient light and fog. Reflection views use
skinned LOD1 bodies and cards with their own frustum and water clip plane.
Instance data is uploaded once per fenced frame slot and stays unchanged
through every reflection pass and the main pass.

## Build and review

Reconfigure after adding the sources/shaders and regenerating the model;
CMake discovers them and copies `models/wolf.glb` into the runtime directory.
In the development environment, from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2
cd build
VULKHAN_PREVIEW_WOLVES=1 ./vulkhan
```

The preview flag follows the first active pack across the hills with a
camera kept above the drawn terrain. Without it, normal player movement
works as before. `VULKHAN_WOLF_FUR=0` draws only the skinned bodies for an A/B
comparison. `VULKHAN_GPU_TIMINGS=1` measures the scene cost together with other
scene rendering. Use Release builds for FPS measurements (see
`performance.md`).

For visibility diagnostics, run `VULKHAN_WOLF_DEBUG=1 ./vulkhan`. Every five
seconds it logs active wolves, main-view draw count, and the nearest wolf's
distance and position, plus each pack's behavior state and route diagnostics. A startup report is printed even without the flag.
`drawn` counts submitted instances passing CPU frustum/range checks; it does
not prove their fragments survived depth tests or terrain occlusion.

CPU and asset checks:

```sh
c++ -std=c++23 -O2 -DGLM_ENABLE_EXPERIMENTAL tests/wolfPacks.cpp \
  src/vkh/systems/wolf/wolfPacks.cpp src/vkh/systems/wolf/wolfNavigation.cpp \
  -o /tmp/wolf-packs-test
/tmp/wolf-packs-test
python3 tests/validateWolf.py
```

Manual GPU checks: inspect the run and coat near the camera, cross the fur
and mesh LOD distances, watch slopes and lake shores, check wolves in planar
reflections, resize the window and inspect Vulkan validation output. The
current terrain following aligns the body; it does not perform independent
foot IK against uneven ground. Trees do not yet provide collision shapes to
the wildlife simulation.

Validation during implementation: Debug application build, shader compilation
and SPIR-V validation, GLB contract checks, 64,800 simulated wolf states, a climb/descent regression, 25,600 terrain
triangle surface samples, existing terrain seam/draw-list checks, and
Blender studio/run renders. The editing environment has no `/dev/dri` device,
so in-game appearance, validation-layer runtime output and FPS are unverified.

The revised groom uses a fuller neck ruff, flatter body hairs, finer strand
coverage, a mottled dark dorsal saddle and lighter cheeks and guard-hair tips.
The coat atlas is regenerated and repacked with the groom; its bare-skin
swatches retain their original UV contract. Shader sheen follows the fiber
direction. The Blender review is an approximation, not an in-game screenshot.

Hair-card wind uses the global weather direction, speed and travelling gusts
also driving vegetation. Card roots (V=0) remain fixed after skinning; bend and
small per-card flutter increase quadratically toward V=1 tips. Zero wind
strength or speed gives zero displacement. This is a bounded vertex-shader
approximation, without a separate strand-physics solver. Blender wind
simulation is not exported as engine physics.
