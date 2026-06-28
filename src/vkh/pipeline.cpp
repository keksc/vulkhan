#include "pipeline.hpp"

#include <format>
#include <stdexcept>
#include <vulkan/vulkan.hpp>

#include "debug.hpp"
#include "deviceHelpers.hpp"

namespace vkh {

Pipeline::Pipeline(EngineContext &context, vk::PipelineBindPoint bindPoint)
    : context{context}, bindPoint{bindPoint} {}

Pipeline::~Pipeline() {
  if (context.vulkan.device) {
    context.vulkan.device.destroyPipeline(pipeline, nullptr);
    context.vulkan.device.destroyPipelineLayout(layout, nullptr);
  }
}

vk::ShaderModule createShaderModule(EngineContext &context,
                                    const std::vector<char> &code) {
  vk::ShaderModuleCreateInfo createInfo{};
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

  vk::ShaderModule shaderModule;
  if (context.vulkan.device.createShaderModule(
          &createInfo, nullptr, &shaderModule) != vk::Result::eSuccess) {
    throw std::runtime_error("failed to create shader");
  }
  return shaderModule;
}

GraphicsPipeline::GraphicsPipeline(EngineContext &context,
                                   const PipelineCreateInfo &createInfo,
                                   const char *name)
    : Pipeline{context, vk::PipelineBindPoint::eGraphics} {
  std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;

  auto vertCode = readFile(createInfo.vertpath);
  auto fragCode = readFile(createInfo.fragpath);
  vk::ShaderModule vertShaderModule = createShaderModule(context, vertCode);
  vk::ShaderModule fragShaderModule = createShaderModule(context, fragCode);

  shaderStages.emplace_back(vk::PipelineShaderStageCreateFlags{},
                            vk::ShaderStageFlagBits::eVertex, vertShaderModule,
                            "main", nullptr);
  auto str = std::format("vertex shader for pipeline {}", name);
  debug::setObjName(
      context, vk::ObjectType::eShaderModule,
      reinterpret_cast<uint64_t>(static_cast<VkShaderModule>(vertShaderModule)),
      str.c_str());

  shaderStages.emplace_back(vk::PipelineShaderStageCreateFlags{},
                            vk::ShaderStageFlagBits::eFragment,
                            fragShaderModule, "main", nullptr);
  str = std::format("fragment shader for pipeline {}", name);
  debug::setObjName(
      context, vk::ObjectType::eShaderModule,
      reinterpret_cast<uint64_t>(static_cast<VkShaderModule>(fragShaderModule)),
      str.c_str());

  // Optional tessellation
  vk::ShaderModule tescShaderModule, teseShaderModule;
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

    shaderStages.emplace_back(vk::PipelineShaderStageCreateFlags{},
                              vk::ShaderStageFlagBits::eTessellationControl,
                              tescShaderModule, "main", nullptr);
    str = std::format("tesselation control shader for pipeline {}", name);
    debug::setObjName(context, vk::ObjectType::eShaderModule,
                      reinterpret_cast<uint64_t>(
                          static_cast<VkShaderModule>(tescShaderModule)),
                      str.c_str());

    shaderStages.emplace_back(vk::PipelineShaderStageCreateFlags{},
                              vk::ShaderStageFlagBits::eTessellationEvaluation,
                              teseShaderModule, "main", nullptr);
    str = std::format("tesselation eval shader for pipeline {}", name);
    debug::setObjName(context, vk::ObjectType::eShaderModule,
                      reinterpret_cast<uint64_t>(
                          static_cast<VkShaderModule>(teseShaderModule)),
                      str.c_str());
  }

  auto &bindingDescriptions = createInfo.bindingDescriptions;
  auto &attributeDescriptions = createInfo.attributeDescriptions;

  vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.vertexBindingDescriptionCount =
      static_cast<uint32_t>(bindingDescriptions.size());
  vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(attributeDescriptions.size());
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

  vk::PipelineTessellationStateCreateInfo tessellationInfo{{}, 4};

  vk::PipelineDynamicStateCreateInfo dynamicStateInfo{
      {},
      static_cast<uint32_t>(createInfo.dynamicStateEnables.size()),
      createInfo.dynamicStateEnables.data()};

  vk::GraphicsPipelineCreateInfo pipelineInfo{};
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

  if (context.vulkan.device.createPipelineLayout(
          &createInfo.layoutInfo, nullptr, &pipelineInfo.layout) !=
      vk::Result::eSuccess)
    throw std::runtime_error("failed to create pipeline layout!");

  layout = pipelineInfo.layout;
  str = std::format("pipeline layout for pipeline {}", name);
  debug::setObjName(
      context, vk::ObjectType::ePipelineLayout,
      reinterpret_cast<uint64_t>(static_cast<VkPipelineLayout>(layout)),
      str.c_str());

  pipelineInfo.renderPass = createInfo.renderPass;
  pipelineInfo.subpass = createInfo.subpass;
  pipelineInfo.basePipelineIndex = -1;
  pipelineInfo.basePipelineHandle = nullptr;
  pipelineInfo.flags = vk::PipelineCreateFlags{};

  auto result = context.vulkan.device.createGraphicsPipelines(
      nullptr, 1, &pipelineInfo, nullptr, &pipeline);
  if (result != vk::Result::eSuccess)
    throw std::runtime_error("failed to create graphics pipeline");

  context.vulkan.device.destroyShaderModule(vertShaderModule, nullptr);
  context.vulkan.device.destroyShaderModule(fragShaderModule, nullptr);

  if (useTessellation) {
    context.vulkan.device.destroyShaderModule(tescShaderModule, nullptr);
    context.vulkan.device.destroyShaderModule(teseShaderModule, nullptr);
  }

  str = std::format("{} graphics pipeline", name);
  debug::setObjName(
      context, vk::ObjectType::ePipeline,
      reinterpret_cast<uint64_t>(static_cast<VkPipeline>(pipeline)),
      str.c_str());
}

ComputePipeline::ComputePipeline(EngineContext &context,
                                 const std::filesystem::path &shaderpath,
                                 vk::PipelineLayoutCreateInfo layoutInfo,
                                 const char *name,
                                 vk::SpecializationInfo *specializationInfo)
    : Pipeline{context, vk::PipelineBindPoint::eCompute} {
  vk::ComputePipelineCreateInfo pipelineInfo{};

  if (context.vulkan.device.createPipelineLayout(
          &layoutInfo, nullptr, &pipelineInfo.layout) != vk::Result::eSuccess)
    throw std::runtime_error("failed to create pipeline layout!");

  layout = pipelineInfo.layout;
  std::string str = std::format("pipeline layout for pipeline {}", name);
  debug::setObjName(
      context, vk::ObjectType::ePipelineLayout,
      reinterpret_cast<uint64_t>(static_cast<VkPipelineLayout>(layout)),
      str.c_str());

  auto shaderCode = readFile(shaderpath);
  vk::ShaderModule shaderModule = createShaderModule(context, shaderCode);
  str = std::format("compute shader for pipeline {}", name);
  debug::setObjName(
      context, vk::ObjectType::eShaderModule,
      reinterpret_cast<uint64_t>(static_cast<VkShaderModule>(shaderModule)),
      str.c_str());

  pipelineInfo.stage =
      vk::PipelineShaderStageCreateInfo{{},
                                        vk::ShaderStageFlagBits::eCompute,
                                        shaderModule,
                                        "main",
                                        specializationInfo};
  pipelineInfo.layout = layout;
  pipelineInfo.basePipelineIndex = -1;
  pipelineInfo.basePipelineHandle = nullptr;
  pipelineInfo.flags = vk::PipelineCreateFlags{};

  auto result = context.vulkan.device.createComputePipelines(
      nullptr, 1, &pipelineInfo, nullptr, &pipeline);
  if (result != vk::Result::eSuccess) {
    throw std::runtime_error("failed to create compute pipeline");
  }

  context.vulkan.device.destroyShaderModule(shaderModule, nullptr);
  str = std::format("{} compute pipeline", name);
  debug::setObjName(
      context, vk::ObjectType::ePipeline,
      reinterpret_cast<uint64_t>(static_cast<VkPipeline>(pipeline)),
      str.c_str());
}

void Pipeline::bind(vk::CommandBuffer commandBuffer) {
  commandBuffer.bindPipeline(bindPoint, pipeline);
}

void GraphicsPipeline::enableAlphaBlending(PipelineCreateInfo &configInfo) {
  configInfo.colorBlendAttachment.blendEnable = true;
  configInfo.colorBlendAttachment.colorWriteMask =
      vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
      vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
  configInfo.colorBlendAttachment.srcColorBlendFactor =
      vk::BlendFactor::eSrcAlpha;
  configInfo.colorBlendAttachment.dstColorBlendFactor =
      vk::BlendFactor::eOneMinusSrcAlpha;
  configInfo.colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
  configInfo.colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
  configInfo.colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
  configInfo.colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
}

} // namespace vkh
