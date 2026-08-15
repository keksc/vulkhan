#pragma once

#include "vkh/engineContext.hpp"
#include "vkh/scene.hpp"
#include "vkh/systems/entity/entities.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>

namespace vkh::mountain {

struct MountainCreateInfo {
  // Number of vertices along each side of the grid. Higher values give more
  // detail but cost more triangles (resolution.x * resolution.y quads).
  glm::uvec2 resolution{256u, 256u};

  // World-space footprint of the mesh on the XZ plane, centered on the
  // origin.
  glm::vec2 worldSize{100.f, 100.f};

  // Height (Y) of the lowest and highest points of the base landscape,
  // before erosion is layered on top.
  float baseHeight{0.f};
  float peakHeight{40.f};

  // Number of erosion octaves. Each octave adds a smaller, higher frequency
  // layer of gullies/ridges on top of the previous one, replacing (not
  // adding to) the running result wherever its mask permits.
  uint32_t octaves{6};

  // Overall strength of the erosion effect. Since gullyWeight (below) scales
  // gully magnitude down to keep peaks pointy, this should generally be set
  // to roughly 1/gullyWeight to compensate (e.g. gullyWeight=0.5 pairs well
  // with erosionStrength=2) - see "Pointy peaks" in the blog post.
  float erosionStrength{2.f};

  // Frequency multiplier applied to the stripe pattern each octave (>1
  // makes each successive octave finer). The post typically uses ~2.
  float lacunarity{2.f};

  // Amplitude multiplier applied to each octave's gully magnitude before
  // it's blended in (<1 makes each successive octave contribute less).
  float gain{0.55f};

  // Frequency of the base (largest) octave's stripe pattern, in stripes per
  // world unit, before per-point slope-based frequency scaling is applied.
  float baseFrequency{0.05f};

  // How strongly local slope maps to the [0,1] range used by the mask/
  // frequency shaping functions. Larger values saturate to "steep" at a
  // smaller actual slope. Needs tuning to the scale of your heightmap.
  float slopeSensitivity{3.f};

  // At zero slope, stripe frequency is scaled by this factor (<1, making
  // stripes thicker) instead of going all the way to zero/infinity, to
  // avoid numerical blowup - see "The frequency approach".
  float minFrequencyScale{0.08f};

  // Scales down each octave's gully height/gradient contribution before
  // blending, to keep mountain peaks pointy rather than rounded off by
  // gully noise - see "Pointy peaks". Pair with erosionStrength ~= 1/this.
  float gullyWeight{0.5f};

  // Detail parameter from the "Stacked fading" section: controls how
  // strongly finer octaves are restricted to steep slopes only, vs. being
  // allowed to add detail more broadly. Higher = more restricted/detailed
  // look, lower = smoother/broader.
  float detail{1.5f};

  uint32_t seed{1337u};
};

// Builds a heightmap-based mountain mesh implementing the technique
// described by Rune Skovbo Johansen in "Fast and Gorgeous Erosion Filter"
// (https://blog.runevision.com/2026/03/fast-and-gorgeous-erosion-filter.html),
// building on Clay John and Felix Westin (Fewes)'s original Shadertoy
// technique. This is an independent reimplementation written from the blog
// post's description and the utility snippets it gives verbatim
// (inverse_lerp, ease_out, pow_inv) - not a port of any Shadertoy source,
// which isn't published in full in the post itself.
//
// Implements: cell-blended directional stripe noise with pivot jitter (the
// base technique), the frequency approach (stripe thickness inversely
// proportional to slope, preserving peaks), the fade approach (per-octave
// fade-target/mask blending instead of naive octave summation, for crisp
// peaks *and* valleys simultaneously), stacked fading (mask updated each
// octave via pow_inv/detail so finer octaves can't disturb coarser
// ridges/creases), normalized gullies (interpolated cos/sin pair
// re-normalized to consistent magnitude via the scale-then-clamp trick),
// straight gullies (using sign(sin) rather than sin directly to orient
// child octaves, so they branch cleanly instead of curling), and pointy
// peaks (gullyWeight/erosionStrength compensation).
//
// Not implemented: separate ridge/crease edge rounding controls, and the
// water-drainage ridge-map overlay - both are described as separate,
// independent embellishments on top of the core technique above.
//
// The surface is colored via a small altitude-based gradient texture
// (rock -> dirt -> grass -> snow) sampled through vertex UV.x, which is set
// to the normalized altitude of each vertex; UV.y is unused and set to 0.5.
std::shared_ptr<Scene<EntitySys::Vertex>>
generate(EngineContext &context, vk::DescriptorSetLayout texturesSetLayout,
        const MountainCreateInfo &createInfo = {});

} // namespace vkh::mountain
