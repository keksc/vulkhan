#include "pipeline.hpp"

#include <vulkan/vulkan_core.h>

#include <stdexcept>

#include "debug.hpp"
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
    throw std::runtime_error("failed to create shader");
  }
  return shaderModule;
}
GraphicsPipeline::GraphicsPipeline(EngineContext &context,
                                   const PipelineCreateInfo &createInfo,
                                   const char *name)
    : Pipeline{context, VK_PIPELINE_BIND_POINT_GRAPHICS} {
  std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

  auto vertCode = readFile(createInfo.vertpath);
  auto fragCode = readFile(createInfo.fragpath);
  VkShaderModule vertShaderModule = createShaderModule(context, vertCode);
  VkShaderModule fragShaderModule = createShaderModule(context, fragCode);

  shaderStages.emplace_back(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                            nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
                            vertShaderModule, "main", nullptr);
  auto str = std::format("vertex shader for pipeline {}", name);
  debug::setObjName(context, VK_OBJECT_TYPE_SHADER_MODULE,
                    reinterpret_cast<uint64_t>(vertShaderModule), str.c_str());

  shaderStages.emplace_back(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                            nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
                            fragShaderModule, "main", nullptr);
  str = std::format("fragment shader for pipeline {}", name);
  debug::setObjName(context, VK_OBJECT_TYPE_SHADER_MODULE,
                    reinterpret_cast<uint64_t>(fragShaderModule), str.c_str());

  // Optional tessellation
  VkShaderModule tescShaderModule, teseShaderModule;
  bool useTessellation = false;
  if (createInfo.tescpath.empty() != createInfo.tesepath.empty()) {
    throw std::runtime_error(std::format(
        "One but not both tesselation control and eval shader have been "
        "provided to the pipeline ({}, {}). Provide both or none.",
        createInfo.tescpath.string(), createInfo.tesepath.string()));
  }
  if (!createInfo.tescpath.empty() && !createInfo.tesepath.empty()) {
    useTessellation = true;
    auto tescCode = readFile(createInfo.tescpath);
    auto teseCode = readFile(createInfo.tesepath);
    tescShaderModule = createShaderModule(context, tescCode);
    teseShaderModule = createShaderModule(context, teseCode);

    shaderStages.emplace_back(
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, tescShaderModule, "main",
        nullptr);
    str = std::format("tesselation control shader for pipeline {}", name);
    debug::setObjName(context, VK_OBJECT_TYPE_SHADER_MODULE,
                      reinterpret_cast<uint64_t>(tescShaderModule),
                      str.c_str());

    shaderStages.emplace_back(
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, teseShaderModule, "main",
        nullptr);
    str = std::format("tesselation eval shader for pipeline {}", name);
    debug::setObjName(context, VK_OBJECT_TYPE_SHADER_MODULE,
                      reinterpret_cast<uint64_t>(teseShaderModule),
                      str.c_str());
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
  str = std::format("pipeline layout for pipeline {}", name);
  debug::setObjName(context, VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                    reinterpret_cast<uint64_t>(layout), str.c_str());

  pipelineInfo.renderPass = createInfo.renderPass;
  pipelineInfo.subpass = createInfo.subpass;

  pipelineInfo.basePipelineIndex = -1;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
  pipelineInfo.flags = 0;

  if (vkCreateGraphicsPipelines(context.vulkan.device, VK_NULL_HANDLE, 1,
                                &pipelineInfo, nullptr,
                                &pipeline) != VK_SUCCESS)
    throw std::runtime_error("failed to create graphics pipeline");

  vkDestroyShaderModule(context.vulkan.device, vertShaderModule, nullptr);
  vkDestroyShaderModule(context.vulkan.device, fragShaderModule, nullptr);

  if (useTessellation) {
    vkDestroyShaderModule(context.vulkan.device, tescShaderModule, nullptr);
    vkDestroyShaderModule(context.vulkan.device, teseShaderModule, nullptr);
  }

  str = std::format("{} graphics pipeline", name);
  debug::setObjName(context, VK_OBJECT_TYPE_PIPELINE,
                    reinterpret_cast<uint64_t>(pipeline), str.c_str());
}

ComputePipeline::ComputePipeline(
    EngineContext &context, const std::filesystem::path &shaderpath,
    VkPipelineLayoutCreateInfo layoutInfo, const char *name,
    VkSpecializationInfo *specializationInfo)
    : Pipeline{context, VK_PIPELINE_BIND_POINT_COMPUTE} {
  VkComputePipelineCreateInfo pipelineInfo{};
  if (vkCreatePipelineLayout(context.vulkan.device, &layoutInfo, nullptr,
                             &pipelineInfo.layout) != VK_SUCCESS)
    throw std::runtime_error("failed to create pipeline layout!");
  layout = pipelineInfo.layout;
  std::string str = std::format("pipeline layout for pipeline {}", name);
  debug::setObjName(context, VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                    reinterpret_cast<uint64_t>(layout), str.c_str());

  auto shaderCode = readFile(shaderpath);

  VkShaderModule shaderModule = createShaderModule(context, shaderCode);
  str = std::format("compute shader for pipeline {}", name);
  debug::setObjName(context, VK_OBJECT_TYPE_SHADER_MODULE,
                    reinterpret_cast<uint64_t>(shaderModule), str.c_str());

  pipelineInfo.stage = {.sType =
                            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                        .module = shaderModule,
                        .pName = "main",
                        .pSpecializationInfo = specializationInfo};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.layout = layout;

  pipelineInfo.basePipelineIndex = -1;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
  pipelineInfo.flags = 0;

  if (vkCreateComputePipelines(context.vulkan.device, VK_NULL_HANDLE, 1,
                               &pipelineInfo, nullptr,
                               &pipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create compute pipeline");
  }
  vkDestroyShaderModule(context.vulkan.device, shaderModule, nullptr);
  str = std::format("{} compute pipeline", name);
  debug::setObjName(context, VK_OBJECT_TYPE_PIPELINE,
                    reinterpret_cast<uint64_t>(pipeline), str.c_str());
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
