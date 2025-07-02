#include "pipeline.hpp"

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
VkShaderModule createShaderModule(EngineContext &context,
                                  const std::vector<char> &code) {
  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

  VkShaderModule shaderModule;
  if (vkCreateShaderModule(context.vulkan.device, &createInfo, nullptr,
                           &shaderModule) != VK_SUCCESS) {
    throw std::runtime_error("failed to create shader module");
  }
  return shaderModule;
}
GraphicsPipeline::GraphicsPipeline(EngineContext &context,
                                   const PipelineCreateInfo &createInfo)
    : Pipeline{context, VK_PIPELINE_BIND_POINT_GRAPHICS} {
  std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

  auto vertCode = readFile(createInfo.vertpath);
  auto fragCode = readFile(createInfo.fragpath);
  VkShaderModule vertShaderModule = createShaderModule(context, vertCode);
  VkShaderModule fragShaderModule = createShaderModule(context, fragCode);

  shaderStages.push_back(
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_VERTEX_BIT,
       .module = vertShaderModule,
       .pName = "main"});

  shaderStages.push_back(
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
       .module = fragShaderModule,
       .pName = "main"});

  // Optional tessellation
  VkShaderModule tescModule, teseModule;
  bool useTessellation = false;
  if (!createInfo.tescpath.empty() && !createInfo.tesepath.empty()) {
    useTessellation = true;
    auto tescCode = readFile(createInfo.tescpath);
    auto teseCode = readFile(createInfo.tesepath);
    tescModule = createShaderModule(context, tescCode);
    teseModule = createShaderModule(context, teseCode);

    shaderStages.push_back(
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
         .module = tescModule,
         .pName = "main"});

    shaderStages.push_back(
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
         .module = teseModule,
         .pName = "main"});
  }

  auto &bindingDescriptions = createInfo.bindingDescriptions;
  auto &attributeDescriptions = createInfo.attributeDescriptions;

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount =
      static_cast<uint32_t>(bindingDescriptions.size());
  vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(attributeDescriptions.size());
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

  VkPipelineTessellationStateCreateInfo tessellationInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
      .patchControlPoints = 4};

  VkPipelineDynamicStateCreateInfo dynamicStateInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount =
          static_cast<uint32_t>(createInfo.dynamicStateEnables.size()),
      .pDynamicStates = createInfo.dynamicStateEnables.data()};

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
  pipelineInfo.pStages = shaderStages.data();
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &createInfo.inputAssemblyInfo;
  pipelineInfo.pViewportState = &createInfo.viewportInfo;
  pipelineInfo.pRasterizationState = &createInfo.rasterizationInfo;
  pipelineInfo.pMultisampleState = &createInfo.multisampleInfo;
  pipelineInfo.pColorBlendState = &createInfo.colorBlendInfo;
  pipelineInfo.pDepthStencilState = &createInfo.depthStencilInfo;
  pipelineInfo.pDynamicState = &dynamicStateInfo;
  pipelineInfo.pTessellationState =
      useTessellation ? &tessellationInfo : nullptr;

  if (vkCreatePipelineLayout(context.vulkan.device, &createInfo.layoutInfo,
                             nullptr, &pipelineInfo.layout) != VK_SUCCESS)
    throw std::runtime_error("failed to create pipeline layout!");
  layout = pipelineInfo.layout;

  pipelineInfo.renderPass = createInfo.renderPass;
  pipelineInfo.subpass = createInfo.subpass;

  pipelineInfo.basePipelineIndex = -1;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

  if (vkCreateGraphicsPipelines(context.vulkan.device, VK_NULL_HANDLE, 1,
                                &pipelineInfo, nullptr,
                                &pipeline) != VK_SUCCESS)
    throw std::runtime_error("failed to create graphics pipeline");

  vkDestroyShaderModule(context.vulkan.device, vertShaderModule, nullptr);
  vkDestroyShaderModule(context.vulkan.device, fragShaderModule, nullptr);

  if (useTessellation) {
    vkDestroyShaderModule(context.vulkan.device, tescModule, nullptr);
    vkDestroyShaderModule(context.vulkan.device, teseModule, nullptr);
  }
}

ComputePipeline::ComputePipeline(EngineContext &context,
                                 const std::filesystem::path &shaderpath,
                                 VkPipelineLayoutCreateInfo layoutInfo)
    : Pipeline{context, VK_PIPELINE_BIND_POINT_COMPUTE} {
  VkComputePipelineCreateInfo pipelineInfo{};
  if (vkCreatePipelineLayout(context.vulkan.device, &layoutInfo, nullptr,
                             &pipelineInfo.layout) != VK_SUCCESS)
    throw std::runtime_error("failed to create pipeline layout!");
  layout = pipelineInfo.layout;

  auto shaderCode = readFile(shaderpath);

  VkShaderModule shaderModule = createShaderModule(context, shaderCode);

  pipelineInfo.stage = {.sType =
                            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                        .module = shaderModule,
                        .pName = "main"};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.layout = layout;
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
