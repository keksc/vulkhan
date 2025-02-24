#include "skyModel.hpp"

#include <vulkan/vulkan_core.h>

#include "../../descriptors.hpp"
#include "../../deviceHelpers.hpp"
#include "../../renderer.hpp"

namespace vkh {
namespace skySys {

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
const Params &getParams() { return m_SkyUBO.params; }
std::unique_ptr<SkyPreetham> m_Sky{nullptr};
std::unique_ptr<DescriptorSetLayout> m_DescriptorSetLayout{nullptr};
struct DescriptorSet {
  bool isDirty{true};
  VkDescriptorSet set{VK_NULL_HANDLE};
};
std::vector<DescriptorSet> m_DescriptorSets;

std::unique_ptr<GraphicsPipeline> m_Pipeline{nullptr};

std::vector<Buffer> m_UniformBuffers;
void SetupPipeline(EngineContext &context) {
  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.rasterizationInfo.cullMode = VK_CULL_MODE_FRONT_BIT;
  pipelineInfo.rasterizationInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  pipelineInfo.renderPass = renderer::getSwapChainRenderPass(context);

  pipelineInfo.layoutInfo.setLayoutCount = 1;
  VkDescriptorSetLayout descSetLayout = *m_DescriptorSetLayout;
  pipelineInfo.layoutInfo.pSetLayouts = &descSetLayout;

  m_Pipeline = std::make_unique<GraphicsPipeline>(
      context, "shaders/fullscreenQuad.vert.spv",
      "shaders/skyPreetham.frag.spv", pipelineInfo);

  assert(m_DescriptorSetLayout != nullptr);
  assert(static_cast<VkDescriptorSetLayout>(*m_DescriptorSetLayout) !=
         VK_NULL_HANDLE);
}
void CreateDescriptorSetLayout(EngineContext &context) {
  uint32_t bindingPoint = 0;

  m_DescriptorSetLayout = DescriptorSetLayout::Builder(context)
                              // SkyUBO
                              .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                          VK_SHADER_STAGE_FRAGMENT_BIT)
                              .build();
}

void UpdateSkyUBO() { m_SkyUBO.params.props = m_Sky->GetProperties(); }
void init(EngineContext &context, const glm::vec3 &sunDir) {
  CreateDescriptorSetLayout(context);
  SetupPipeline(context);

  m_Sky = std::make_unique<SkyPreetham>(sunDir);
  UpdateSkyUBO();
}

void CreateUniformBuffers(EngineContext &context, const uint32_t kBufferCount) {
  const VkDeviceSize kBufferSize =
      getUniformBufferAlignment(context, sizeof(SkyUBO));

  m_UniformBuffers.reserve(kBufferCount);

  for (uint32_t i = 0; i < kBufferCount; ++i) {
    auto &buffer = m_UniformBuffers.emplace_back(
        context, kBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    buffer.map();
  }
}

void CreateDescriptorSets(EngineContext &context, const uint32_t kCount) {
  m_DescriptorSets.resize(kCount);

  VkResult err;
  for (auto &descriptor : m_DescriptorSets) {
    context.vulkan.globalDescriptorPool->allocateDescriptorSet(
        *m_DescriptorSetLayout, descriptor.set);
  }
}
void UpdateDescriptorSet(EngineContext &context, DescriptorSet &descriptor,
                         Buffer &buffer) {
  if (descriptor.isDirty == false)
    return;

  VkDescriptorBufferInfo bufferInfos[1] = {};
  bufferInfos[0].buffer = buffer;
  bufferInfos[0].offset = 0;
  bufferInfos[0].range = sizeof(SkyUBO);

  DescriptorWriter descriptorWriter(*m_DescriptorSetLayout,
                                    *context.vulkan.globalDescriptorPool);

  descriptorWriter.writeBuffer(0, &bufferInfos[0]);

  descriptorWriter.overwrite(descriptor.set);
  descriptor.isDirty = false;
}
void UpdateDescriptorSets(EngineContext &context) {
  assert(m_DescriptorSets.size() == m_UniformBuffers.size());

  for (uint32_t i = 0; i < m_DescriptorSets.size(); ++i) {
    UpdateDescriptorSet(context, m_DescriptorSets[i], m_UniformBuffers[i]);
  }
}
void createRenderData(EngineContext &context, VkRenderPass renderPass,
                      const uint32_t kImageCount,
                      const VkExtent2D kFramebufferExtent,
                      const bool kFramebufferHasDepthAttachment) {
  const bool kImageCountChanged = m_UniformBuffers.size() != kImageCount;
  if (kImageCountChanged) {
    m_UniformBuffers.clear();
    CreateUniformBuffers(context, kImageCount);

    m_DescriptorSets.clear();
    CreateDescriptorSets(context, kImageCount);

    UpdateDescriptorSets(context);
  }
}

void UpdateUniformBuffer(Buffer &buffer) {
  buffer.copyToMapped(&m_SkyUBO, sizeof(m_SkyUBO));
  buffer.flush();
}
void prepareRender(const uint32_t frameIndex, glm::uvec2 screenResolution,
                   const glm::vec3 &camPos, const glm::mat3 &camView,
                   float camFOV) {
  m_SkyUBO.resolution = screenResolution;
  m_SkyUBO.camPos = camPos;
  m_SkyUBO.camViewRow0 = camView[0];
  m_SkyUBO.camViewRow1 = camView[1];
  m_SkyUBO.camViewRow2 = camView[2];
  m_SkyUBO.camFOV = camFOV;

  UpdateUniformBuffer(m_UniformBuffers[frameIndex]);

  // TODO not needed
  // UpdateDescriptorSet(
  //    m_DescriptorSets[frameIndex],
  //    m_UniformBuffers[frameIndex]
  //);
}

void render(const uint32_t frameIndex, VkCommandBuffer cmdBuffer) {
  m_Pipeline->bind(cmdBuffer);

  const uint32_t kFirstSet = 0, kDescriptorSetCount = 1;
  const uint32_t kDynamicOffsetCount = 0;
  const uint32_t *kDynamicOffsets = nullptr;

  vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          *m_Pipeline, kFirstSet, kDescriptorSetCount,
                          &m_DescriptorSets[frameIndex].set,
                          kDynamicOffsetCount, kDynamicOffsets);

  // Draw fullscreen triangle
  const uint32_t kVertexCount = 3, kInstanceCount = 1;
  const uint32_t kFirstVertex = 0, kFirstInstance = 0;
  vkCmdDraw(cmdBuffer, kVertexCount, kInstanceCount, kFirstVertex,
            kFirstInstance);
}

void cleanup() {
  m_DescriptorSets.clear();
  m_Pipeline = nullptr;
  m_UniformBuffers.clear();
  m_DescriptorSetLayout = nullptr;
}

} // namespace skySys
} // namespace vkh
