#pragma once

#include <filesystem>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace vkh {

class EngineContext;

struct PipelineCreateInfo {
  PipelineCreateInfo() = default;
  PipelineCreateInfo(const PipelineCreateInfo &) = delete;
  PipelineCreateInfo &operator=(const PipelineCreateInfo &) = delete;

  std::filesystem::path vertpath;
  std::filesystem::path fragpath;
  std::filesystem::path tescpath;
  std::filesystem::path tesepath;

  std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
  std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;

  vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
      {}, vk::PrimitiveTopology::eTriangleList, false};

  vk::PipelineViewportStateCreateInfo viewportInfo{{}, 1, nullptr, 1, nullptr};

  vk::PipelineRasterizationStateCreateInfo rasterizationInfo{
      {},
      false,
      false,
      vk::PolygonMode::eFill,
      vk::CullModeFlagBits::eNone,
      vk::FrontFace::eClockwise,
      false,
      0.0f,
      0.0f,
      0.0f,
      1.0f};

  vk::PipelineMultisampleStateCreateInfo multisampleInfo{
      {}, vk::SampleCountFlagBits::e1, false, 1.0f, nullptr, false, false};

  vk::PipelineColorBlendAttachmentState colorBlendAttachment{
      false,
      vk::BlendFactor::eZero,
      vk::BlendFactor::eZero,
      vk::BlendOp::eAdd,
      vk::BlendFactor::eZero,
      vk::BlendFactor::eZero,
      vk::BlendOp::eAdd,
      vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

  vk::PipelineColorBlendStateCreateInfo colorBlendInfo{
      {}, false, vk::LogicOp::eCopy, 1, &colorBlendAttachment};

  vk::PipelineDepthStencilStateCreateInfo depthStencilInfo{
      {}, true, true, vk::CompareOp::eGreater, false, false, {},
      {}, 0.0f, 1.0f};

  std::vector<vk::DynamicState> dynamicStateEnables{
      vk::DynamicState::eViewport,
      vk::DynamicState::eScissor,
  };

  vk::RenderPass renderPass = nullptr;
  vk::PipelineLayoutCreateInfo layoutInfo{};
  uint32_t subpass = 0;
};

class Pipeline {
public:
  Pipeline(EngineContext &context, vk::PipelineBindPoint bindPoint);
  virtual ~Pipeline();

  Pipeline(const Pipeline &) = delete;
  Pipeline &operator=(const Pipeline &) = delete;

  vk::PipelineLayout getLayout() { return layout; }

  operator vk::Pipeline() { return pipeline; }
  operator vk::PipelineLayout() { return layout; }

  void bind(vk::CommandBuffer commandBuffer);

protected:
  EngineContext &context;
  vk::Pipeline pipeline;
  vk::PipelineBindPoint bindPoint;
  vk::PipelineLayout layout;
};

class GraphicsPipeline : public Pipeline {
public:
  GraphicsPipeline(EngineContext &context, const PipelineCreateInfo &configInfo,
                   const char *name = "Unnamed");

  static void enableAlphaBlending(PipelineCreateInfo &configInfo);
};

class ComputePipeline : public Pipeline {
public:
  ComputePipeline(EngineContext &context,
                  const std::filesystem::path &shaderpath,
                  vk::PipelineLayoutCreateInfo layoutInfo = {},
                  const char *name = "Unnamed pipeline",
                  vk::SpecializationInfo *specializationInfo = nullptr);
};

} // namespace vkh
