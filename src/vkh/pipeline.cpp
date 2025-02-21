#include "pipeline.hpp"

#include <fmt/core.h>
#include <string>
#include <vulkan/vulkan_core.h>

#include <stdexcept>

#include "deviceHelpers.hpp"

namespace vkh {

Pipeline::Pipeline(EngineContext &context, VkPipelineBindPoint bindPoint)
    : context{context}, bindPoint{bindPoint} {}
Pipeline::~Pipeline() {
  vkDestroyPipeline(context.vulkan.device, pipeline, nullptr);
  vkDestroyPipelineLayout(context.vulkan.device, layout, nullptr);
}
void createShaderModule(EngineContext &context, const std::vector<char> &code,
                        VkShaderModule *shaderModule) {
  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

  if (vkCreateShaderModule(context.vulkan.device, &createInfo, nullptr,
                           shaderModule) != VK_SUCCESS) {
    throw std::runtime_error("failed to create shader module");
  }
}
GraphicsPipeline::GraphicsPipeline(EngineContext &context,
                                   const std::filesystem::path &vertpath,
                                   const std::filesystem::path &fragpath,
                                   const PipelineCreateInfo &configInfo)
    : Pipeline{context, VK_PIPELINE_BIND_POINT_GRAPHICS} {
  auto vertCode = readFile(vertpath);
  auto fragCode = readFile(fragpath);

  VkShaderModule vertShaderModule;
  VkShaderModule fragShaderModule;
  createShaderModule(context, vertCode, &vertShaderModule);
  createShaderModule(context, fragCode, &fragShaderModule);

  VkPipelineShaderStageCreateInfo shaderStages[2] = {
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_VERTEX_BIT,
       .module = vertShaderModule,
       .pName = "main"},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
       .module = fragShaderModule,
       .pName = "main"}};

  auto &bindingDescriptions = configInfo.bindingDescriptions;
  auto &attributeDescriptions = configInfo.attributeDescriptions;
  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(attributeDescriptions.size());
  vertexInputInfo.vertexBindingDescriptionCount =
      static_cast<uint32_t>(bindingDescriptions.size());
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
  vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = shaderStages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &configInfo.inputAssemblyInfo;
  pipelineInfo.pViewportState = &configInfo.viewportInfo;
  pipelineInfo.pRasterizationState = &configInfo.rasterizationInfo;
  pipelineInfo.pMultisampleState = &configInfo.multisampleInfo;
  pipelineInfo.pColorBlendState = &configInfo.colorBlendInfo;
  pipelineInfo.pDepthStencilState = &configInfo.depthStencilInfo;
  pipelineInfo.pDynamicState = &configInfo.dynamicStateInfo;

  if (vkCreatePipelineLayout(context.vulkan.device, &configInfo.layoutInfo,
                             nullptr, &pipelineInfo.layout) != VK_SUCCESS)
    throw std::runtime_error("failed to create pipeline layout!");
  layout = pipelineInfo.layout;

  pipelineInfo.renderPass = configInfo.renderPass;
  pipelineInfo.subpass = configInfo.subpass;

  pipelineInfo.basePipelineIndex = -1;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

  if (vkCreateGraphicsPipelines(context.vulkan.device, VK_NULL_HANDLE, 1,
                                &pipelineInfo, nullptr,
                                &pipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create graphics pipeline");
  }

  vkDestroyShaderModule(context.vulkan.device, vertShaderModule, nullptr);
  vkDestroyShaderModule(context.vulkan.device, fragShaderModule, nullptr);
}

ComputePipeline::ComputePipeline(EngineContext &context,
                                 const std::filesystem::path &shaderpath,
                                 VkPipelineLayout pipelineLayout)
    : Pipeline{context, VK_PIPELINE_BIND_POINT_COMPUTE} {
  auto shaderCode = readFile(shaderpath);

  VkShaderModule shaderModule;
  createShaderModule(context, shaderCode, &shaderModule);

  VkComputePipelineCreateInfo pipelineInfo{};
  pipelineInfo.layout = pipelineLayout;
  pipelineInfo.stage = {.sType =
                            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                        .module = shaderModule,
                        .pName = "main"};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.basePipelineIndex = -1;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
  if (vkCreateComputePipelines(context.vulkan.device, VK_NULL_HANDLE, 1,
                               &pipelineInfo, nullptr,
                               &pipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create compute pipeline");
  }
  vkDestroyShaderModule(context.vulkan.device, shaderModule, nullptr);
}
void Pipeline::bind(VkCommandBuffer commandBuffer) {
  vkCmdBindPipeline(commandBuffer, bindPoint, pipeline);
}
void GraphicsPipeline::enableAlphaBlending(PipelineCreateInfo &configInfo) {
  configInfo.colorBlendAttachment.blendEnable = VK_TRUE;

  configInfo.colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  configInfo.colorBlendAttachment.srcColorBlendFactor =
      VK_BLEND_FACTOR_SRC_ALPHA;
  configInfo.colorBlendAttachment.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  configInfo.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  configInfo.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  configInfo.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  configInfo.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
}
} // namespace vkh
