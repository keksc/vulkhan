#include "pipeline.hpp"

#include <format>
#include <stdexcept>
#include <vulkan/vulkan.hpp>

#include "debug.hpp"
#include "deviceHelpers.hpp"
#include "engineContext.hpp"
#include "paths.hpp"
#include "swapChain.hpp"

namespace vkh {

void Pipeline::loadCache(EngineContext &context) {
  std::filesystem::path cachePath = paths::cacheDir() / "pipeline_cache.bin";
  std::vector<char> cacheData;

  // Use the optimized readFile if the cache exists
  if (std::filesystem::exists(cachePath)) {
    cacheData = readFile(cachePath);
  }

  vk::PipelineCacheCreateInfo createInfo{};
  if (!cacheData.empty()) {
    createInfo.initialDataSize = cacheData.size();
    createInfo.pInitialData = cacheData.data();
  }

  if (context.vulkan.device.createPipelineCache(
          &createInfo, nullptr, &context.vulkan.pipelineCache) !=
      vk::Result::eSuccess) {
    throw std::runtime_error("failed to create pipeline cache!");
  }
}

void Pipeline::saveAndCleanCache(EngineContext &context) {
  std::filesystem::path cachePath = paths::cacheDir() / "pipeline_cache.bin";

  auto cacheData =
      context.vulkan.device.getPipelineCacheData(context.vulkan.pipelineCache);

  if (!cacheData.empty()) {
    writeFile(cachePath, cacheData.data(), cacheData.size());
  }

  context.vulkan.device.destroyPipelineCache(context.vulkan.pipelineCache);
}

Pipeline::Pipeline(EngineContext &context, vk::PipelineBindPoint bindPoint)
    : context{context}, bindPoint{bindPoint} {}

Pipeline::~Pipeline() {
  if (context.vulkan.device) {
    context.vulkan.device.destroyPipeline(pipeline, nullptr);
    if (ownsLayout)
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

  // Dynamic rendering: no VkRenderPass/subpass. The pipeline instead
  // declares the attachment formats it will be used with, via pNext.
  // Every pipeline currently targets the swapchain's own formats, so we
  // read them straight from the swapchain rather than threading them
  // through PipelineCreateInfo.
  vk::Format colorFormat = context.vulkan.swapChain->getSwapChainImageFormat();
  vk::Format depthFormat = context.vulkan.swapChain->getSwapChainDepthFormat();

  vk::PipelineRenderingCreateInfo renderingInfo{};
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachmentFormats = &colorFormat;
  renderingInfo.depthAttachmentFormat = depthFormat;

  pipelineInfo.pNext = &renderingInfo;
  pipelineInfo.basePipelineIndex = -1;
  pipelineInfo.basePipelineHandle = nullptr;
  pipelineInfo.flags = vk::PipelineCreateFlags{};

  auto result = context.vulkan.device.createGraphicsPipelines(
      context.vulkan.pipelineCache, 1, &pipelineInfo, nullptr, &pipeline);
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
  this->ownsLayout = true;

  if (context.vulkan.device.createPipelineLayout(
          &layoutInfo, nullptr, &layout) != vk::Result::eSuccess)
    throw std::runtime_error("failed to create pipeline layout!");

  std::string str = std::format("pipeline layout for pipeline {}", name);
  debug::setObjName(
      context, vk::ObjectType::ePipelineLayout,
      reinterpret_cast<uint64_t>(static_cast<VkPipelineLayout>(layout)),
      str.c_str());

  createPipeline(context, shaderpath, name, specializationInfo);
}
ComputePipeline::ComputePipeline(EngineContext &context,
                                 const std::filesystem::path &shaderpath,
                                 vk::PipelineLayout layout, const char *name,
                                 vk::SpecializationInfo *specializationInfo)
    : Pipeline{context, vk::PipelineBindPoint::eCompute} {
  this->ownsLayout = false;
  this->layout = layout;

  std::string str = std::format("pipeline layout for pipeline {}", name);
  debug::setObjName(
      context, vk::ObjectType::ePipelineLayout,
      reinterpret_cast<uint64_t>(static_cast<VkPipelineLayout>(layout)),
      str.c_str());

  createPipeline(context, shaderpath, name, specializationInfo);
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
void ComputePipeline::createPipeline(
    EngineContext &context, const std::filesystem::path &shaderpath,
    const char *name, vk::SpecializationInfo *specializationInfo) {
  vk::ComputePipelineCreateInfo pipelineInfo{};

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
      context.vulkan.pipelineCache, 1, &pipelineInfo, nullptr, &pipeline);
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

} // namespace vkh
