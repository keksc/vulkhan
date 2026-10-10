#pragma once

#include "../../buffer.hpp"
#include "../../pipeline.hpp"
#include "../system.hpp"
#include "furGroom.hpp"

#include <memory>
#include <span>
#include <vector>

namespace vkh {

// Experimental deferred strand fur. prepare() runs after the frame fence;
// rasterize() runs between opaque rendering and the 1x composite pass.
class FurSys : public System {
public:
  FurSys(EngineContext &context, vk::DescriptorSetLayout textures,
         vk::DescriptorSetLayout instances, std::span<const FurBundle> groom,
         vk::DescriptorImageInfo coat);
  ~FurSys();
  bool prepare(std::span<const glm::mat4> models);
  void rasterize(vk::DescriptorSet textures, vk::DescriptorSet instances,
                 uint32_t poseCount, uint32_t jointCount);
  void render(vk::DescriptorSet textures, vk::DescriptorSet instances);

private:
  struct Push {
    glm::mat4 inverseProjView{1};
    glm::uvec4 window{}; // width, height, absolute origin x/y
    glm::uvec4 counts{}; // instance, pose count, joint count, bundle count
  };
  struct Dispatch {
    uint32_t x, y, z, strands, segments;
    float density;
    uint32_t padding[2];
  };
  static_assert(sizeof(Push) == 96 && sizeof(Dispatch) == 32);
  struct Frame {
    std::unique_ptr<Buffer<uint64_t>> center, conservative;
    std::unique_ptr<Buffer<uint32_t>> shades;
    std::unique_ptr<Buffer<Dispatch>> dispatch;
    vk::DescriptorSet set{};
    uint32_t capacity = 0;
  };
  void bind(vk::CommandBuffer cmd, Pipeline &pipeline,
            vk::PipelineBindPoint point, vk::DescriptorSet textures,
            vk::DescriptorSet instances);
  vk::DescriptorSetLayout layout{};
  vk::Sampler depthSampler{};
  vk::DescriptorImageInfo coat;
  std::unique_ptr<Buffer<FurBundle>> bundles;
  std::unique_ptr<ComputePipeline> lod, raster, shade;
  std::unique_ptr<GraphicsPipeline> composite;
  std::vector<Frame> frames;
  std::vector<uint32_t> visible;
  Push push{};
  vk::Rect2D scissor{};
  uint32_t bundleCount = 0, instanceCount = 0;
  bool ready = false;
};

} // namespace vkh
