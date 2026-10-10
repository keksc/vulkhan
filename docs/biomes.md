# Customizing the world

Edit `config/world.toml`, run `cmake -S . -B build` to copy it into the build,
and restart the game. To use the source file directly, launch with an absolute
path (the app changes its working directory):

```sh
VULKHAN_WORLD_CONFIG="$PWD/config/world.toml" ./build/vulkhan
```

## Profiles and regions

Profiles are named collections of linear RGB `ground`, `rock`, `grass`, and
`foliage` colors, plus `grassDensity`, `treeDensity`, `dryness`, `broadleaf`, and
`snow`. Density multiplies placement probability; values above one make forests
denser. The last three controls are in [0,1]. `broadleaf` is the probability of
choosing a broadleaf crown instead of a conifer.

Meadow, steppe, desert, forest, tundra, autumn, and volcanic defaults are built
in. TOML fields override those defaults. Add a new table under `profiles` and
reference it from a region; no shader editing or recompilation is necessary.
For example:

```toml
[profiles.scarlet]
foliage = [0.55, 0.04, 0.015]
broadleaf = 1
snow = 0

[[regions]]
profile = "scarlet"
center = [250, -100]
radius = 450
blend = 150
```

Regions blend over their outer `blend` metres. Later regions take precedence.
The shipped example has autumn at (220,-100) and volcanic terrain at (700,300).
The climate-based distribution fills the rest of the world. Optional `climate`
settings are `temperatureFeatureSize`, `moistureFeatureSize`, `altitudeLapse`,
`homeRadiusInner`, and `homeRadiusOuter`.

## Terrain and volcanoes

`terrain` exposes base/peak height, base-noise feature size, erosion scale and
strength. `volcanoes` entries specify XZ `center`, `radius`, additive `height`,
`crater` radius fraction, `depth`, `active`, `plumeHeight`, and `plumeColor`.
Active vents emit lava-colored ground and wind-driven ash; their crater lakes
are excluded. Extinct volcanoes keep their crater geometry and may hold water.

Geometry extensions use `MountainCreateInfo::heightModifier`, a pure,
thread-safe `(XZ, worldHeight) -> worldHeight` function. It applies after erosion
and feeds chunks, heightmaps, lakes, and vegetation consistently. `lakeAllowed`
is an optional basin filter. Register custom feature layers there; update the
terrain's vertical padding when adding large positive or negative heights.

The current GPU tables support 16 profiles, 16 regions, and 8 volcanoes. The
loader validates types, dimensions and names and reports malformed TOML with
the filename and parser location.

## Preview and performance

Use `VULKHAN_PREVIEW=autumn` or `VULKHAN_PREVIEW=volcano` when launching to hold
an elevated camera over an example. Unset the variable for normal controls.
Tree atlases are generated once per size/type and shared across species; lower
LODs share meshes. Volcanic ash marches only inside bounded plume volumes.
The local smoke solver stays idle until disturbed, defaults to 30Hz and 12
pressure sweeps, and caches transmittance between simulation/light changes.
`SmokeSys::dt` and `pressureIterations` remain adjustable.
