#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include <optional>
#include <vector>

#include "../../pipeline.hpp"
#include "../../image.hpp"
#include "../system.hpp"

namespace vkh {

class GraphicsPipeline;
template <typename T> class Buffer;

// GPU-driven procedural grass and flowers, generated fresh every frame
// over one or more fixed world-space patches (Ghost of Tsushima /
// GDC-style approach): there is no CPU-authored blade list at all. The
// patches' tiles are enumerated once at construction into a small
// tile-origin buffer; each frame a compute pass hashes (tile, slot-in-tile)
// into a deterministic blade position/pose - grass or, with probability
// flowerChance, a flower - frustum-culls it, and compacts survivors
// straight into the buffer the vertex shader reads.
class ProceduralGrassSys : public System {
public:
  struct BladeInstance {
    glm::vec3 basePos;
    float bendAngle;   // random per-blade lean direction (radians)
    float facingAngle; // random per-blade yaw (radians)
    float height;
    float width;
    float windPhase;
    float bendAngle2; // secondary lean direction the stem eases toward near
                       // the tip, giving an S-curve instead of one uniform
                       // arc - matches how real stems settle
    float species;     // 0.0 = grass blade, 1.0 = flower (flared/bell-shaped
                        // tip + petal coloring in the fragment shader)
    glm::vec2 _pad2;
    glm::vec3 aabbMin;
    float _pad0;
    glm::vec3 aabbMax;
    float _pad1;
  };

  // A rectangular patch of ground the field covers, in world-space XZ.
  // Replaces the old infinite camera-centered grid: the field now has
  // real edges you place, and several patches can be combined (e.g. two
  // separate meadows). Tiles are generated once at construction and
  // deduplicated across overlapping patches.
  struct GrassPatch {
    glm::vec2 center; // world-space XZ center
    glm::vec2 size;   // world-space width (X) / depth (Z)
  };

  struct WindUbo {
    float time;
    float strength;
    glm::vec2 dir;
    float lodNearDist; // duplicated from GrassUbo - vertex shader only
                        // binds WindUbo, not GrassUbo, so it rides along
                        // here instead of adding a new descriptor binding
  };

  // NOTE ON LAYOUT: keep the fixed-size array first, then any vec2/vec3
  // fields, THEN trailing scalars. std140 requires vec2 on an 8-byte
  // boundary; putting it right after the 16-byte-aligned array keeps the
  // C++ and GLSL offsets identical with zero manual padding. Reordering
  // this (e.g. putting a scalar between the array and origin) reintroduces
  // a silent CPU/GPU layout mismatch with no validation error to catch it.
  struct GrassUbo {
    glm::vec4 frustumPlanes[6];
    glm::vec2 camPos;           // camera position, world-space XZ - used only
                                 // for the maxDist early-out below, no longer
                                 // for placing tiles (the field is fixed now)
    glm::vec2 heightmapOrigin;  // world-space XZ of the heightmap's texel
                                 // (0,0), or unused (heightmapSize == 0) if
                                 // no heightmap was supplied - see
                                 // ProceduralGrassSys::HeightmapBinding.
    glm::vec2 heightmapSize;    // world-space size the heightmap covers;
                                 // (0,0) is the sentinel grassGenerate.comp
                                 // checks for "no heightmap bound, use flat
                                 // ground at y=0" (the old behavior).
    float tileSize;             // meters per tile side
    uint32_t bladesPerTileSide; // blades per tile side (density)
    uint32_t tileCount;         // number of entries in the tile-origin buffer
    float maxDist;    // hard distance cutoff (meters) - blades beyond this
                       // never get generated, independent of frustum test
    float lodNearDist; // distance at which blades start transitioning to the
                        // cheap/low LOD silhouette (also read by the vertex
                        // shader for the continuous LOD blend)
    float flowerChance; // probability [0,1] that a given blade slot spawns
                         // as a flower instead of grass
  };

  // Binds a terrain heightmap (e.g. from vkh::mountain::generate()'s
  // outHeightmap) so blades sample their world-space Y position and a
  // per-texel spawn-suitability mask from it, instead of assuming flat
  // ground at y=0. Deliberately a generic struct (not a dependency on the
  // mountain module) - any system that can produce an R32G32_SFLOAT
  // (world height, suitability [0,1]) texture over a known world-space
  // rectangle can supply one of these.
  struct HeightmapBinding {
    std::shared_ptr<Image2D> image;
    glm::vec2 worldMin;  // world-space XZ of the image's texel (0, 0)
    glm::vec2 worldSize; // world-space size the image covers
  };

  static constexpr int SEGMENTS = 3;
  static constexpr int VERTS_PER_BLADE = SEGMENTS * 2 + 1;

  // patches: the ground area(s) the field covers. Defaults to a single
  // 70x70m patch centered at the origin if left empty.
  // tileSize/bladesPerTileSide size the fixed GPU buffers up front and,
  // unlike the old design, are only ever read here at construction -
  // there is no live setter for them since the tile-origin buffer and
  // instance buffers are sized once from these values.
  // heightmap: optional terrain binding (see HeightmapBinding above) so
  // blades conform to non-flat ground and only spawn where the terrain's
  // suitability mask allows. Omit for flat ground at y=0 (old behavior).
  ProceduralGrassSys(EngineContext &context,
                     std::vector<GrassPatch> patches = {},
                     float tileSize = 1.0f, uint32_t bladesPerTileSide = 8,
                     std::optional<HeightmapBinding> heightmap = std::nullopt);
  ~ProceduralGrassSys();

  void cull(vk::CommandBuffer cmd);
  void render();
  void updateBuffers(); // called once per frame; recomputes frustum + wind

  float windStrength = 0.35f;
  glm::vec2 windDir = glm::normalize(glm::vec2(1.f, 0.3f));

  // Distance-based behavior, read every frame so these ARE safe to tweak
  // live (unlike the patch layout / tile density above).
  float maxDist = 120.0f;    // hard cutoff: no blade generated beyond this
  float lodNearDist = 15.0f; // start blending to cheap/flat LOD past this
  float flowerChance = 0.06f; // fraction of slots that spawn as flowers

private:
  void createSetLayouts();
  void createPipeline();
  void createGeneratePipeline();
  void createFinalizePipeline();
  void buildTileOrigins(); // fills tileOrigins_ from patches_, deduplicated

  const std::vector<GrassPatch> patches_;
  const float tileSize_;
  const uint32_t bladesPerTileSide_;
  std::vector<glm::vec2> tileOrigins_; // world-space min-corner of each tile
  std::unique_ptr<Buffer<glm::vec2>> tileOriginBuffer;

  // Heightmap binding (see HeightmapBinding above). heightmapImage_ is
  // null if no heightmap was supplied to the constructor, in which case
  // fallbackHeightmapImage_ (a throwaway 1x1 image) is bound instead just
  // to satisfy the descriptor set layout - grassGenerate.comp never
  // actually samples it, since it checks GrassUbo::heightmapSize == 0
  // first and falls back to flat ground at y=0.
  std::optional<HeightmapBinding> heightmap_;
  std::shared_ptr<Image2D> fallbackHeightmapImage_;

  uint32_t maxBladeCount() const {
    return static_cast<uint32_t>(tileOrigins_.size()) * bladesPerTileSide_ *
           bladesPerTileSide_;
  }

  std::unique_ptr<GraphicsPipeline> pipeline;
  vk::DescriptorSetLayout instanceSetLayout;
  vk::DescriptorSetLayout generateSetLayout;

  std::unique_ptr<ComputePipeline> generatePipeline;
  std::unique_ptr<ComputePipeline> finalizePipeline;

  // Fixed-size, created once - no CPU blade data means no growth/upload
  // logic is needed at all.
  std::vector<std::unique_ptr<Buffer<BladeInstance>>> compactedInstanceBuffers;
  std::vector<std::unique_ptr<Buffer<uint32_t>>> visibleCountBuffers;
  std::vector<std::unique_ptr<Buffer<vk::DrawIndirectCommand>>>
      indirectDrawBuffers;
  std::vector<std::unique_ptr<Buffer<GrassUbo>>> grassUboBuffers;
  std::vector<std::unique_ptr<Buffer<WindUbo>>> windBuffers;

  std::vector<vk::DescriptorSet> instanceDescriptorSets;
  std::vector<vk::DescriptorSet> generateDescriptorSets;
};

} // namespace vkh
