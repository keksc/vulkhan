#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include "../../pipeline.hpp"
#include "../../image.hpp"
#include "../system.hpp"
#include "grassField.hpp"

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
    uint32_t _pad0; // packed biome RGB, populated by the generation shader
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
    // Vertical bounds of the terrain the image covers. Only used by the
    // infinite field's block frustum culling (see below); the defaults
    // disable vertical culling, which is always safe.
    float minY{-1.0e4f};
    float maxY{1.0e4f};
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
  //
  // infiniteField: ignore `patches` and cover EVERYTHING around the camera
  // out to maxDist. No field-sized tile list exists in this mode: tiles are
  // enumerated around the camera each frame, in blocks that intersect the
  // view frustum, so cost and memory depend only on maxDist (never on how
  // far the camera has travelled). The heightmap is expected to be a window
  // that follows the camera - hand fresh ones to setHeightmap() as it moves
  // (see grassField.hpp's HeightWindow); it must cover at least
  // maxDist + 4 tiles around the camera at all times.
  ProceduralGrassSys(EngineContext &context,
                     std::vector<GrassPatch> patches = {},
                     float tileSize = 1.0f, uint32_t bladesPerTileSide = 8,
                     std::optional<HeightmapBinding> heightmap = std::nullopt,
                     bool infiniteField = false);
  ~ProceduralGrassSys();

  // Swaps the terrain heightmap. Render thread only; cheap (stores the new
  // binding). Each frame-in-flight slot adopts it in its next updateBuffers()
  // by rewriting its own descriptor, and keeps its previous image alive
  // until then, so frames still in flight never see a destroyed image.
  void setHeightmap(HeightmapBinding heightmap);

  void cull(vk::CommandBuffer cmd);
  void render();
  void updateBuffers(); // called once per frame; recomputes frustum + wind


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
  void adoptHeightmap(int frameIndex); // see setHeightmap()
  // Upper bound on how many tiles can be within maxDist of the camera (plus
  // margin) at once, from the field's full tileOrigins_ - see its call
  // site's comment. Used to size activeTileOriginBuffers and
  // compactedInstanceBuffers instead of the whole field's tile/blade count.
  uint32_t computeActiveTileCapacity() const;

  const std::vector<GrassPatch> patches_;
  const float tileSize_;
  const uint32_t bladesPerTileSide_;
  std::vector<glm::vec2> tileOrigins_; // world-space min-corner of each tile
                                       // in the WHOLE configured field - can
                                       // be very large (main.cpp currently
                                       // covers the entire mountain terrain
                                       // with this); see
                                       // computeActiveTileCapacity()'s
                                       // comment for why this full list is
                                       // no longer uploaded to the GPU
                                       // wholesale.

  // Sized once at construction from computeActiveTileCapacity(), using
  // whatever maxDist was THEN; changing maxDist afterward does not resize
  // these buffers (see that method's comment for what happens if you push
  // maxDist well past this).
  uint32_t activeTileCapacity_ = 0;
  // The actual number of tiles uploaded to activeTileOriginBuffers[frame]
  // THIS frame (<= activeTileCapacity_) - set by updateBuffers(), read back
  // by cull() to size its dispatch. Only ever meaningfully current for
  // whichever frameIndex updateBuffers() was most recently called with;
  // fine in practice since main.cpp always calls updateBuffers() then
  // cull() back to back for the same frame, same as every other system
  // here.
  uint32_t currentActiveTileCount_ = 0;
  // Per-frame-in-flight, each capacity activeTileCapacity_ - NOT the whole
  // field. updateBuffers() re-fills the front of this with whichever of
  // tileOrigins_ currently fall within maxDist of the camera before every
  // cull() dispatch; see updateBuffers()'s own comment for why a per-frame
  // CPU-side distance filter is cheap enough to redo every frame compared
  // to what it replaces.
  std::vector<std::unique_ptr<Buffer<glm::vec2>>> activeTileOriginBuffers;
  // Reused scratch space for the per-frame filter pass, to avoid
  // reallocating a multi-thousand-entry vector every frame.
  std::vector<glm::vec2> frameTileScratch_;

  // Heightmap binding (see HeightmapBinding above). heightmapImage_ is
  // null if no heightmap was supplied to the constructor, in which case
  // fallbackHeightmapImage_ (a throwaway 1x1 image) is bound instead just
  // to satisfy the descriptor set layout - grassGenerate.comp never
  // actually samples it, since it checks GrassUbo::heightmapSize == 0
  // first and falls back to flat ground at y=0.
  std::optional<HeightmapBinding> heightmap_;
  std::shared_ptr<Image2D> fallbackHeightmapImage_;

  // What each frame-in-flight slot currently has bound (binding 5 + the
  // origin/size written to that slot's UBO), so a swap never touches a
  // descriptor a pending frame may still be reading.
  struct HeightmapSlot {
    std::shared_ptr<Image2D> image;
    glm::vec2 origin{0.f};
    glm::vec2 size{0.f};
    uint64_t version{0};
  };
  std::vector<HeightmapSlot> heightmapSlots_;
  uint64_t heightmapVersion_{1};

  // Infinite-field mode, see the constructor's doc comment.
  bool infinite_{false};
  std::vector<grass::TileBlockRef> blockScratch_;

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
