#pragma once

#include <vulkan/vulkan_core.h>

#include <memory>
#include <vector>

#include "../../buffer.hpp"
#include "../../engineContext.hpp"
#include "../../pipeline.hpp"
#include "skyPreetham.hpp"

namespace vkh {
class SkyModel {
public:
  struct Params {
    alignas(16) glm::vec3 sunColor{1.0};
    float sunIntensity{1.0};
    // -------------------------------------------------
    SkyPreetham::Props props;
  };

public:
  SkyModel(EngineContext &context, const glm::vec3 &sunDir);
  ~SkyModel();

  void CreateRenderData(VkRenderPass renderPass, const uint32_t kImageCount,
                        const VkExtent2D kFramebufferExtent,
                        const bool kFramebufferHasDepthAttachment);

  void Update(float dt);

  void PrepareRender(const uint32_t frameIndex, glm::uvec2 screenResolution,
                     const glm::vec3 &camPos, const glm::mat3 &camView,
                     float camFOV);

  void Render(const uint32_t frameIndex, VkCommandBuffer cmdBuffer);

  const Params &GetParams() const { return m_SkyUBO.params; }

private:
  struct DescriptorSet;

  void CreateDescriptorSetLayout();
  void SetupPipeline();

  void CreateUniformBuffers(const uint32_t kBufferCount);
  void CreateDescriptorSets(const uint32_t kCount);

  void UpdateDescriptorSets();
  void UpdateDescriptorSet(DescriptorSet &descriptor,  Buffer &buffer);
  void UpdateUniformBuffer( Buffer &buffer);
  void UpdateSkyUBO();

  void ShowLightingSettings();

private:
  EngineContext &context;

  // =========================================================================
  std::unique_ptr<DescriptorSetLayout> m_DescriptorSetLayout{nullptr};

  struct DescriptorSet {
    bool isDirty{true};
    VkDescriptorSet set{VK_NULL_HANDLE};
  };
  std::vector<DescriptorSet> m_DescriptorSets;

  std::unique_ptr<GraphicsPipeline> m_Pipeline{nullptr};

  std::vector<Buffer> m_UniformBuffers;

  // =========================================================================

  struct SkyUBO {
    alignas(16) glm::vec2 resolution;
    alignas(16) glm::vec3 camViewRow0;
    alignas(16) glm::vec3 camViewRow1;
    alignas(16) glm::vec3 camViewRow2;
    alignas(16) glm::vec3 camPos;
    float camFOV;
    Params params;
  };
  SkyUBO m_SkyUBO;

  std::unique_ptr<SkyPreetham> m_Sky{nullptr};
};
} // namespace vkh
