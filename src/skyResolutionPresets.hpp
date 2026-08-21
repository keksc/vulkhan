#pragma once
#include <cstdint>

namespace vkh {

// Independent resolution options for each baked sky texture. Kept as
// small preset lists (rather than free-form input) since an arbitrary
// resolution risks absurd bake times/memory use.
//
// These were originally paired 1:1 (each milky way size locked to a
// fixed disc size), which meant you couldn't pick e.g. a coarse milky
// way with a finer disc turbulence texture or vice versa -- the two are
// visually independent (different bake shaders, different roles in
// sky.frag), so they're now two separate lists, each with its own
// setting and its own UI button. See settings.hpp
// (skyMilkyWayFaceSize/skyDiscTurbulenceSize) and UI.cpp.
//
// Single source of truth: UI.cpp's resolution buttons cycle through
// these, and settings::validateSkyResolution() falls back to the middle
// entry of each when a loaded value doesn't match. Add or remove a
// resolution here and both automatically pick it up.
//
// Notes if you add an entry:
//  - milky way face size drives cubemap VRAM as 6 * face^2 *
//    formatSize(R16G16B16A16Sfloat) (8 bytes/texel) -- 2048 is already
//    ~805 MiB across all 6 faces of a cubemap using half-float RGBA.
//  - disc turbulence is a single 2D R8Unorm texture, so it's far
//    cheaper per-texel (1 byte) -- 1024 there is only 1 MiB, so it has
//    much more headroom than the cubemap does.
//  - keep values reasonable powers of two; the compute bake dispatch
//    (see SkySys::bakeEnvironment) sizes its workgroups as
//    (size + 7) / 8, so non-multiples-of-8 just waste a few threads at
//    the edge rather than breaking anything, but there's no benefit to
//    going off the power-of-two grid.
//  - resolution alone does NOT control how visually busy/noisy the bake
//    looks -- that's typically a star-density/noise-frequency parameter
//    inside bakeMilkyWay.comp itself, independent of texture size. If
//    lowering resolution isn't reducing perceived detail the way you'd
//    expect, that shader-side parameter is likely what needs exposing,
//    not this list.
inline constexpr uint32_t milkyWayFaceSizePresets[] = {128, 256, 512, 1024,
                                                        2048};
inline constexpr uint32_t discTurbulenceSizePresets[] = {64, 128, 256, 512,
                                                          1024};

} // namespace vkh
