#pragma once

#include "../../buffer.hpp"
#include "../../scene.hpp"
#include "../entity/entities.hpp"
#include "../system.hpp"
#include "wolfPacks.hpp"

#include <array>
#include <memory>
#include <span>
#include <vector>

namespace vkh {

class FurSys;

// Rendering and animation only; never advances wildlife AI.
class WolfSys : public System {
public:
  WolfSys(EngineContext &context, vk::DescriptorSetLayout texturesLayout);
  ~WolfSys();
  // Called after the frame-slot fence is signaled, outside render scopes.
  void update(std::span<const wildlife::WolfPacks::Wolf> wolves);
  void updateStudio();
  void setFurVisible(bool value) { furVisible = value; }
  bool isFurVisible() const { return furVisible; }
  void rasterizeFur();
  void renderFur();
  void render(vk::DescriptorSet globalSet = {},
              const glm::vec4 &clipPlane = {0, 1, 0, 1e9f},
              const glm::mat4 *viewProj = nullptr);

private:
  static constexpr uint32_t kPoseCount = 48;
  struct alignas(16) Instance {
    glm::mat4 model{1};
    glm::vec4 animation{}; // pose frame, tint variation, locomotion blend, seed
    glm::vec4 coat{};      // RGB multiplier, whitening
  };
  struct Push {
    glm::vec4 clipPlane;
    glm::vec4 fur; // reserved, studio mode, pose count, joint count
  };
  struct DrawGroup {
    uint32_t first = 0;
    uint32_t count = 0;
    uint32_t lod = 0;
  };
  void createPipelines(vk::DescriptorSetLayout texturesLayout);
  void bakePoses();
  bool inFrustum(const std::array<glm::vec4, 6> &planes, uint32_t i) const;

  std::shared_ptr<Scene<EntitySys::Vertex>> scene;
  vk::DescriptorSetLayout instanceLayout{};
  std::unique_ptr<GraphicsPipeline> basePipeline;
  std::unique_ptr<FurSys> strandFur;
  bool useStrands = false;
  bool furVisible = true;
  std::unique_ptr<Buffer<glm::mat4>> poses;
  std::vector<std::unique_ptr<Buffer<Instance>>> instances;
  std::vector<vk::DescriptorSet> instanceSets;
  std::array<Instance, wildlife::WolfPacks::kWolfCount> cpuInstances{};
  std::array<DrawGroup, 3> groups{};
  std::array<size_t, 3> lodMeshes{};
  uint32_t jointCount = 0;
  bool studioMode = false;
  bool debugWolves = false;
  bool reportedStartup = false;
  float nextDebugReport = 0;
  uint32_t activeCount = 0;
  float nearestDistance = 0;
  glm::vec3 nearestPosition{};
};

} // namespace vkh
