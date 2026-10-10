# Environmental particles

See [nightLighting.md](nightLighting.md) for the current habitat field,
illumination, particle LODs, budgets and verification steps.

The compute shader evaluates deterministic world-seeded motion each frame.
It never reads prior particle contents or accumulates frame-dependent drift.
The existing attractor transforms perturb firefly flight; R still regenerates
the transforms with the application's bounded blend.

Local flowers and petals use up to 25 jittered patches on an 8 m grid. They
refresh twice per second or when crossing a cell. Center and corner samples
reject missing ground, lakes, rocks and steep slopes. Flower roots use the
center's sampled height, not the highest corner. Petals flutter above them.

The 48-byte particle layout carries position/radius, color/visibility and
shape/rotation. Instanced billboards distinguish soft fireflies, rotating
elongated petals and upright five-petal flowers. Additive blending, depth
testing and disabled depth writes preserve opaque terrain occlusion.
Per-frame buffers and descriptors are reused after their fence retires, with
a compute-write to vertex-input barrier. No device-wide wait is added.

Local placement checks are in `tests/particleEnvironment.cpp`. Shader behavior
is exercised by `tests/environmentGpu.cpp`; a headless software Vulkan pass
does not replace in-game visual checks or measurements on the target GPU.
