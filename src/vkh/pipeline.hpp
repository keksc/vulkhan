#pragma once

#include "engineContext.hpp"

#include <filesystem>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace vkh {

struct PipelineCreateInfo {
  PipelineCreateInfo() = default;
  PipelineCreateInfo(const PipelineCreateInfo &) = delete;
  PipelineCreateInfo &operator=(const PipelineCreateInfo &) = delete;

  std::vector<VkVertexInputBindingDescription> bindingDescriptions;
  std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
  VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE};
  VkPipelineViewportStateCreateInfo viewportInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1};
  VkPipelineRasterizationStateCreateInfo rasterizationInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_NONE,
      .frontFace = VK_FRONT_FACE_CLOCKWISE,
      .lineWidth = 1.0f};
  VkPipelineMultisampleStateCreateInfo multisampleInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
      .minSampleShading = 1.0f};
  VkPipelineColorBlendAttachmentState colorBlendAttachment{
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
  VkPipelineColorBlendStateCreateInfo colorBlendInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &colorBlendAttachment};
  VkPipelineDepthStencilStateCreateInfo depthStencilInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .depthCompareOp = VK_COMPARE_OP_LESS};
  std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT,
                                                     VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicStateInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size()),
      .pDynamicStates = dynamicStateEnables.data()};
  VkPipelineLayout pipelineLayout = nullptr;
  VkRenderPass renderPass = nullptr;
  uint32_t subpass = 0;
};

class Pipeline {
public:
  Pipeline(EngineContext &context, VkPipelineBindPoint bindPoint);
  ~Pipeline();

  Pipeline(const Pipeline &) = delete;
  Pipeline &operator=(const Pipeline &) = delete;

  void bind(VkCommandBuffer commandBuffer);

protected:
  EngineContext &context;

  VkPipeline pipeline;
  VkPipelineBindPoint bindPoint;
};

class GraphicsPipeline : public Pipeline {
public:
  GraphicsPipeline(EngineContext &context,
                   const std::filesystem::path &vertpath,
                   const std::filesystem::path &fragpath,
                   const PipelineCreateInfo &configInfo);

  static void enableAlphaBlending(PipelineCreateInfo &configInfo);
};
class ComputePipeline : public Pipeline {
public:
  ComputePipeline(EngineContext &context,
                  const std::filesystem::path &shaderpath,
                  VkPipelineLayout pipelineLayout);
};
} // namespace vkh
