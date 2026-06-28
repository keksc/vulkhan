#include "water.hpp"

#include "../../buffer.hpp"
#include "../../debug.hpp"
#include "../../descriptors.hpp"
#include "../../deviceHelpers.hpp"
#include "../../engineContext.hpp"
#include "../../pipeline.hpp"
#include "../../scene.hpp"
#include "../../swapChain.hpp"

namespace vkh {

WaterSys::WaterSys(EngineContext &context, SkyboxSys &skyboxSys)
    : System{context}, modelTess{context}, skyboxSys{skyboxSys} {
  createDescriptorSetLayout();
  createPipeline();
  createMesh();
  createRenderData();
  prepare();
}

WaterSys::~WaterSys() {
  if (context.vulkan.device) {
    context.vulkan.device.destroyDescriptorSetLayout(setLayout, nullptr);
  }
}

void WaterSys::createMesh() {
  std::vector<Vertex> vertices = createGridVertices();
  std::vector<uint32_t> indices = createGridIndices();

  SceneCreateInfo<Vertex> meshInfo{.vertices = vertices, .indices = indices};
  scene = std::make_unique<Scene<Vertex>>(context, meshInfo);
}

void WaterSys::createPipeline() {
  PipelineCreateInfo pipelineInfo{};
  std::array<vk::DescriptorSetLayout, 3> setLayouts = {
      context.vulkan.globalDescriptorSetLayout, setLayout, skyboxSys.setLayout};

  pipelineInfo.layoutInfo.setLayoutCount =
      static_cast<uint32_t>(setLayouts.size());
  pipelineInfo.layoutInfo.pSetLayouts = setLayouts.data();
  pipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  pipelineInfo.inputAssemblyInfo.topology = vk::PrimitiveTopology::ePatchList;
  pipelineInfo.attributeDescriptions = Vertex::attribDescriptions;
  pipelineInfo.bindingDescriptions = Vertex::bindingDescriptions;
  pipelineInfo.rasterizationInfo.polygonMode = vk::PolygonMode::eFill;
  pipelineInfo.vertpath = "shaders/water/water.vert.spv";
  pipelineInfo.fragpath = "shaders/water/water.frag.spv";
  pipelineInfo.tescpath = "shaders/water/water.tesc.spv";
  pipelineInfo.tesepath = "shaders/water/water.tese.spv";

  pipelineInfo.subpass = 0;
  pipelineInfo.multisampleInfo.rasterizationSamples =
      static_cast<vk::SampleCountFlagBits>(context.vulkan.msaaSamples);

  pipeline = std::make_unique<GraphicsPipeline>(context, pipelineInfo, "water");
}

void WaterSys::createUniformBuffers(const uint32_t bufferCount) {
  const vk::DeviceSize bufferSize =
      getUniformBufferAlignment(context, sizeof(VertexUBO));

  uniformBuffers.reserve(bufferCount);
  for (size_t i = 0; i < bufferCount; i++) {
    auto &buffer = uniformBuffers.emplace_back(
        context, vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible, bufferSize);
    buffer.map();
  }
}

void WaterSys::createDescriptorSets(const uint32_t count) {
  sets.reserve(count);
  for (size_t i = 0; i < count; i++) {
    auto set = context.vulkan.globalDescriptorAllocator->allocate(setLayout);
    std::string str = std::format("set #{} for WaterSys", i);
    debug::setObjName(
        context, vk::ObjectType::eDescriptorSet,
        reinterpret_cast<uint64_t>(static_cast<VkDescriptorSet>(set)),
        str.c_str());
    sets.push_back(set);
  }
}

void WaterSys::createRenderData() {
  uint32_t imageCount = context.vulkan.swapChain->imageCount();
  createUniformBuffers(imageCount);
  createDescriptorSets(imageCount);
}

std::vector<WaterSys::Vertex> WaterSys::createGridVertices() {
  std::vector<Vertex> vertices;
  vertices.reserve(totalVertexCount);

  const int32_t halfSize = tileResolution / 2;

  for (int32_t y = -halfSize; y <= halfSize; ++y) {
    for (int32_t x = -halfSize; x <= halfSize; ++x) {
      vertices.emplace_back(
          // Position
          glm::vec3(static_cast<float>(x), 0.0f, static_cast<float>(y)) *
              vertexDistance,
          // Texcoords
          glm::vec2(static_cast<float>(x + halfSize),
                    static_cast<float>(y + halfSize)) /
              static_cast<float>(tileResolution));
    }
  }

  return vertices;
}

std::vector<uint32_t> WaterSys::createGridIndices() {
  const uint32_t vertexCount = tileResolution + 1;
  std::vector<uint32_t> indices;
  indices.reserve(tileResolution * tileResolution * 4);
  for (uint32_t y = 0; y < tileResolution; ++y) {
    for (uint32_t x = 0; x < tileResolution; ++x) {
      uint32_t v0 = y * vertexCount + x;
      uint32_t v1 = y * vertexCount + (x + 1);
      uint32_t v2 = (y + 1) * vertexCount + (x + 1);
      uint32_t v3 = (y + 1) * vertexCount + x;
      indices.emplace_back(v0);
      indices.emplace_back(v1);
      indices.emplace_back(v2);
      indices.emplace_back(v3);
    }
  }
  return indices;
}

void WaterSys::updateDescriptorSet(vk::DescriptorSet set) {
  // UBOs
  VkDescriptorBufferInfo bufferInfos[1]{};
  bufferInfos[0].buffer =
      static_cast<vk::Buffer>(uniformBuffers[context.frameInfo.frameIndex]);
  bufferInfos[0].offset = 0;
  bufferInfos[0].range = sizeof(VertexUBO);

  // Binding 1: Displacement Map
  VkDescriptorImageInfo imageInfo{};
  imageInfo = modelTess.getDisplacementFoamImage().getDescriptorInfo(
      context.vulkan.defaultSampler);
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  DescriptorWriter writer(context);
  writer.writeBuffer(0, bufferInfos[0], vk::DescriptorType::eUniformBuffer);
  writer.writeImage(1, imageInfo, vk::DescriptorType::eCombinedImageSampler);
  writer.updateSet(set);
}

void WaterSys::updateUniformBuffer() {
  auto &buffer = uniformBuffers[context.frameInfo.frameIndex];
  buffer.write(&vertexUBO, sizeof(vertexUBO));
  buffer.flush();
}

void WaterSys::createDescriptorSetLayout() {
  std::vector<vk::DescriptorSetLayoutBinding> bindings = {
      // Binding 0: UBO
      vk::DescriptorSetLayoutBinding{
          0, vk::DescriptorType::eUniformBuffer, 1,
          vk::ShaderStageFlagBits::eVertex |
              vk::ShaderStageFlagBits::eTessellationControl |
              vk::ShaderStageFlagBits::eTessellationEvaluation |
              vk::ShaderStageFlagBits::eFragment,
          nullptr},
      // Binding 1: Displacement Map (RGB), Foam (A)
      vk::DescriptorSetLayoutBinding{
          1, vk::DescriptorType::eCombinedImageSampler, 1,
          vk::ShaderStageFlagBits::eTessellationEvaluation |
              vk::ShaderStageFlagBits::eFragment,
          nullptr}};

  setLayout = buildDescriptorSetLayout(context, bindings);
  debug::setObjName(
      context, vk::ObjectType::eDescriptorSetLayout,
      reinterpret_cast<uint64_t>(static_cast<VkDescriptorSetLayout>(setLayout)),
      "water set layout");
}

void WaterSys::prepare() {
  auto cmd = beginSingleTimeCommands(context);
  modelTess.recordComputeWaves(cmd,
                               static_cast<float>(glfwGetTime()) * animSpeed);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
}

void WaterSys::update() {
  glm::vec3 camPos = context.camera.position;
  const float snapStep = vertexDistance;

  float snappedX = std::floor(camPos.x / snapStep) * snapStep;
  float snappedZ = std::floor(camPos.z / snapStep) * snapStep;

  vertexUBO.model =
      glm::translate(glm::mat4(1.0f), glm::vec3(snappedX, 0.0f, snappedZ));

  updateUniformBuffer();
  updateDescriptorSet(sets[context.frameInfo.frameIndex]);
  if (playAnimation) {
    modelTess.recordComputeWaves(context.frameInfo.cmd,
                                 static_cast<float>(glfwGetTime()) * animSpeed);
  }
}

void WaterSys::render() {
  auto cmd = context.frameInfo.cmd;
  debug::beginLabel(context, cmd, "WaterSys render", {0.f, 0.f, 1.f, 1.f});
  pipeline->bind(cmd);

  std::array<vk::DescriptorSet, 3> bindSets = {
      context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex],
      sets[context.frameInfo.frameIndex], skyboxSys.set};

  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline, 0,
                         static_cast<uint32_t>(bindSets.size()),
                         bindSets.data(), 0, nullptr);
  scene->bind(context, cmd, *pipeline);

  scene->meshes.begin()->primitives.begin()->draw(cmd);
  debug::endLabel(context, cmd);
}

void WaterSys::downloadDisplacementAtWorldPos() {
  modelTess.getDisplacementFoamImage();
}

} // namespace vkh
