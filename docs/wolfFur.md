# Wolf surface fur

The wolf is an editable, hairless Blender mesh. Vulkhan grows and shades its
coat at runtime from per-vertex data in the wolf's ordinary skinned glTF
triangles. The renderer adapts ideas from [Deferred Software Rasterization for
Efficient Real-time Hair Rendering](https://arxiv.org/html/2607.04230v1); it is
not a full reproduction of that paper.

## Blender to glTF

`assets/wolf/wolf.blend` contains the sculpted surface, coat and bare-feature
colors, 24-bone rig, run action and three skin LODs. It has no fur geometry.
Each LOD stores Blender point attributes which export as standard glTF custom
vertex attributes:

- `_VKH_FUR`: strand length in metres, density mask, and lift from the skin
  normal, each in the range 0–1 except length (0–1 metres).
- `_VKH_FLOW`: normalized direction in which the fur lies, expressed in glTF
  coordinates.

The nose, eyes, mouth, ear inset and claws have zero fur length and density.
The existing UV atlas colors the skin and roots. There is no glTF extension or
extra primitive for fur placement. `sceneBuilder.hpp` reads these accessors
with fastgltf; missing or mismatched fur attributes are reported while loading.

Vulkhan distributes roots by triangle area at a density of 45,000 strands per
square metre, subdivides large triangles, and interpolates skin weights, UVs,
length, flow and lift. The guides remain attached to the existing wolf rig.
GPU passes select screen-size LOD, rasterize guide strands, shade visible
samples and composite them over the opaque skin. The studio disables wind and
holds the wolf in its rest pose.

## Edit and export

Edit the surface attributes on all three LOD meshes in Blender; keep the
standard skinning groups, UV map and vertex order aligned with the mesh. To
regenerate the source, `createWolf.py` applies the anatomy refinement before
it saves the hairless scene and exports the skin:

```sh
blender -b -noaudio --factory-startup -t 2 -P tools/createWolf.py
blender -b -noaudio -t 2 -P tools/exportWolf.py
python3 tests/validateWolf.py
cp models/wolf.glb build/models/wolf.glb
```

The saved `assets/wolf/wolf.blend` and exported `models/wolf.glb` already
include the current refined skin and fur attributes. `tools/exportWolf.py`
exports those Blender edits without regenerating the mesh. Reconfigure CMake
if model-copy rules have changed.

## Wolf studio

From the build directory, run `VULKHAN_WOLF_STUDIO=1 ./vulkhan`. The studio
shows only one stationary, fully lit wolf against a plain background. Press
`1` for three-quarter, `2` for side, `3` for front, `F` to toggle fur and
Escape to close. Set `VULKHAN_WOLF_STUDIO_CAPTURE=/path/to/wolf.png` to save a
PNG after the fur has rendered. The reviewed engine capture is
[`assets/wolf/wolfStudio.png`](../assets/wolf/wolfStudio.png).

## Verification and limits

`python3 tests/validateWolf.py` checks the embedded GLB, 24-bone skin, looping
animation, hairless LODs, custom fur attributes, bare regions, direction data
and triangle winding. `python3 tests/runSceneFurGroom.py` exercises the engine
loader and checks that each skin LOD produces surface groom patches. The
headless Vulkan fixture in `tests/furGpu.cpp` covers strand visibility,
depth rejection, wind, skeletal deformation, LOD and near-plane clipping on
drivers with 64-bit buffer atomics.

This implementation uses a spatial reconstruction filter and simplified
fiber lighting. It has no temporal accumulation, strand collisions, deep
opacity shadows or close-up strand geometry. The studio capture verifies a
real RTX 5060 render and Vulkan image layout; it is not an FPS benchmark or a
claim of flicker-free motion at every camera distance.
