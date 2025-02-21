#include "skyModel.hpp"

#include <vulkan/vulkan_core.h>

#include "../../descriptors.hpp"
#include "../../deviceHelpers.hpp"
#include "../../renderer.hpp"

namespace vkh {

SkyModel::SkyModel(EngineContext &context, const glm::vec3 &sunDir)
    : context{context} {
  CreateDescriptorSetLayout();
  SetupPipeline();

  m_Sky.reset(new SkyPreetham(sunDir));
  UpdateSkyUBO();
}

SkyModel::~SkyModel() {}

void SkyModel::CreateRenderData(VkRenderPass renderPass,
                                const uint32_t kImageCount,
                                const VkExtent2D kFramebufferExtent,
                                const bool kFramebufferHasDepthAttachment) {
  const bool kImageCountChanged = m_UniformBuffers.size() != kImageCount;
  if (kImageCountChanged) {
    m_UniformBuffers.clear();
    CreateUniformBuffers(kImageCount);

    m_DescriptorSets.clear();
    CreateDescriptorSets(kImageCount);

    UpdateDescriptorSets();
  }
}

void SkyModel::Update(float dt) {}

void SkyModel::PrepareRender(const uint32_t frameIndex,
                             glm::uvec2 screenResolution,
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

void SkyModel::Render(const uint32_t frameIndex, VkCommandBuffer cmdBuffer) {
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

// -----------------------------------------------------------------------------
// Update data functions

void SkyModel::UpdateUniformBuffer(Buffer &buffer) {
  buffer.copyToMapped(&m_SkyUBO, sizeof(m_SkyUBO));
  buffer.flush();
}

void SkyModel::UpdateSkyUBO() {
  m_SkyUBO.params.props = m_Sky->GetProperties();
}

void SkyModel::UpdateDescriptorSets() {
  assert(m_DescriptorSets.size() == m_UniformBuffers.size());

  for (uint32_t i = 0; i < m_DescriptorSets.size(); ++i) {
    UpdateDescriptorSet(m_DescriptorSets[i], m_UniformBuffers[i]);
  }
}

void SkyModel::UpdateDescriptorSet(DescriptorSet &descriptor, Buffer &buffer) {
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

// -----------------------------------------------------------------------------
// Creation functions

void SkyModel::CreateDescriptorSetLayout() {
  uint32_t bindingPoint = 0;

  m_DescriptorSetLayout = DescriptorSetLayout::Builder(context)
                              // SkyUBO
                              .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                          VK_SHADER_STAGE_FRAGMENT_BIT)
                              .build();
}

void SkyModel::CreateUniformBuffers(const uint32_t kBufferCount) {
  const VkDeviceSize kBufferSize =
      getUniformBufferAlignment(context, sizeof(SkyUBO));

  m_UniformBuffers.reserve(kBufferCount);

  for (uint32_t i = 0; i < kBufferCount; ++i) {
    auto &buffer = m_UniformBuffers.emplace_back(context, kBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    buffer.map();
  }
}

void SkyModel::SetupPipeline() {
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

void SkyModel::CreateDescriptorSets(const uint32_t kCount) {
  m_DescriptorSets.resize(kCount);

  VkResult err;
  for (auto &descriptor : m_DescriptorSets) {
    context.vulkan.globalDescriptorPool->allocateDescriptorSet(
        *m_DescriptorSetLayout, descriptor.set);
  }
}

// =============================================================================
// GUI
/*
static glm::vec3 GetDirFromAngles(float inclination, float azimuth) {
  return glm::normalize(glm::vec3(glm::sin(inclination) * glm::cos(azimuth),
                                  glm::cos(inclination),
                                  glm::sin(inclination) * glm::sin(azimuth)));
}

void SkyModel::ShowGUISettings() {
  if (ImGui::CollapsingHeader("Lighting Settings"))
  //, ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    ShowLightingSettings();
    ImGui::PopItemWidth();
    ImGui::NewLine();
  }
}

void SkyModel::ShowLightingSettings() {
  static float sunAzimuth = glm::radians(90.0f);     // [-pi, pi]
  static float sunInclination = glm::radians(60.0f); // [-pi/2, pi/2]
  static glm::vec3 sunDir = m_Sky->GetSunDirection();

  ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.29f);

  // TODO change to wider range
  bool paramsChanged =
      ImGui::SliderAngle("##Sun Inclination", &sunInclination, -90.f, 90.f);
  ImGui::SameLine();
  paramsChanged |=
      ImGui::SliderAngle("Sun angles##Sun Azimuth", &sunAzimuth, -180.f, 180.f);
  if (paramsChanged) {
    sunDir = GetDirFromAngles(sunInclination, sunAzimuth);
  }
  ImGui::PopItemWidth();

  paramsChanged |=
      ImGui::DragFloat3("Sun Direction", glm::value_ptr(sunDir), 0.1f);

  ImGui::SliderFloat("Sun Intensity", &m_SkyUBO.params.sunIntensity, 0.f, 10.f);
  ImGui::ColorEdit3("Sun Color", glm::value_ptr(m_SkyUBO.params.sunColor));

  static float turbidity = m_Sky->GetTurbidity();
  paramsChanged |= ImGui::SliderFloat("Sky Turbidity", &turbidity, 2.0f, 6.f);

  if (paramsChanged) {
    m_Sky->SetSunDirection(sunDir);
    m_Sky->SetTurbidity(turbidity);
    m_Sky->Update();
    UpdateSkyUBO();
  }
}*/
} // namespace vkh
