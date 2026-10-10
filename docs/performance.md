# Frame-time measurement

144 fps requires CPU and GPU frame times below 6.94 ms. Measure an optimized
build; the existing `build` directory is configured as Debug with unoptimized
C++ and validation enabled. Keep Debug for correctness checks.

In `nix develop`, configure a separate optimized build:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j 2
cd build-release
VULKHAN_GPU_TIMINGS=1 ./vulkhan
```

The optional profiler prints mean GPU milliseconds once per second for
simulation, reflections/waves, scene, postprocessing, and HUD. It reads old
queries after the frame slot's fence completes, without waiting for query
results. Timestamps at pass boundaries can constrain GPU overlap, so turn
profiling off for final FPS measurements. GPU total excludes CPU work,
image acquisition and presentation waits; a low GPU total with low FPS calls
for CPU profiling and checking display refresh rate. These averages do not
measure worst-case stutters.

Use the same window size, camera route, world settings and warm-up for each
comparison. Include views with several lakes, dense grass/trees, smoke, and
both visible and hidden black holes. Check Debug validation output, resize
the window, and compare screenshots of silhouettes and light shafts.

## Changes and comparisons

- Mailbox presentation is preferred when supported, with FIFO fallback.
  `VULKHAN_PRESENT_MODE=fifo` restores FIFO for comparison. Mailbox prevents
  queued stale frames; it does not make GPU rendering faster or guarantee
  144 displayed frames on a lower-refresh display. Swapchain image count
  respects the surface minimum and maximum and requests an extra image
  when allowed; frame slots keep their existing count.
- God rays cache the original five-texel, full-resolution reversed-Z depth
  visibility in an R32F image. Each ray step reads one cached value rather
  than recomputing up to five depth reads. Sample counts, color filtering,
  thresholds, depth dilation, jitter and ray resolution are unchanged.
  This adds one full-resolution compute pass and four bytes per scene pixel
  per swapchain image. Sparse or hidden rays may benefit less, or regress;
  compare `VULKHAN_GODRAYS_CACHE=0` with the default on the target GPU.
- Auxiliary postprocessing dispatches run after scene input transitions.
  Resolved depth is stored after the particle pass so postprocessing can
  sample it reliably.

No FPS improvement has been measured in the editing environment: it exposes
no `/dev/dri` GPU device. A 144 fps result remains to be verified on hardware.

## Fast-travel hitches

Streaming grass windows now record their transfer and layout barriers into
an ordinary graphics frame instead of submitting a synchronous upload and
waiting for the graphics queue to become idle. Per-frame staging buffers are
reused only after their fence retires; old heightmap images stay alive in the
grass descriptor slots until those slots adopt the replacement. Sampled-image
barriers include compute shaders, which consume the grass heightmap.

Wolf AI reads existing terrain meshes only. It no longer invokes expensive
procedural erosion when fast travel outruns loaded chunks. Packs suspend on
missing ground and retry spawning once streaming catches up. Pathfinding has
both per-search and shared per-frame terrain-query budgets. Lake analysis is
limited to three concurrent jobs and one new launch per frame.

To identify remaining hitches, run:

```sh
VULKHAN_CPU_PROFILE=1 ./build/vulkhan
```

This prints `[CPU hitch]` entries for scopes taking at least 8 ms: the whole
frame, acquisition/fence wait, terrain streaming, wolf AI, grass upload/update,
lake streaming, and submission/presentation. Measurements include blocking
and OS scheduling within each scope; they do not prove CPU execution alone
caused the delay. GPU timings can be enabled alongside these with
`VULKHAN_GPU_TIMINGS=1`. Both diagnostics are disabled by default.

CPU regressions cover streamed-terrain loss/recovery and the total height
query limit across repeated unreachable searches, in addition to existing
pack behavior tests. The build passes; fast-travel visual quality, Vulkan
validation, and actual hitch reduction require a run on the target GPU.
