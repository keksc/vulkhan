#pragma once

#include <vulkan/vulkan_core.h>

#include <memory>
#include <vector>

#include "../../buffer.hpp"
#include "../../engineContext.hpp"
#include "../../pipeline.hpp"
#include "skyPreetham.hpp"

namespace vkh {
namespace skySys {

void init(EngineContext &context, const glm::vec3 &sunDir);

void createRenderData(EngineContext &context, VkRenderPass renderPass,
                      const uint32_t kImageCount,
                      const VkExtent2D kFramebufferExtent,
                      const bool kFramebufferHasDepthAttachment);

void prepareRender(const uint32_t frameIndex, glm::uvec2 screenResolution,
                   const glm::vec3 &camPos, const glm::mat3 &camView,
                   float camFOV);

void render(const uint32_t frameIndex, VkCommandBuffer cmdBuffer);
void cleanup();

struct Params {
  alignas(16) glm::vec3 sunColor{1.0};
  float sunIntensity{1.0};
  // -------------------------------------------------
  SkyPreetham::Props props;
};
const Params &getParams();
} // namespace skySys
} // namespace vkh
