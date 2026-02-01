#include "water.hpp"

#include <vulkan/vulkan_core.h>

#include "../../audio.hpp"
#include "../../debug.hpp"
#include "../../pipeline.hpp"
#include "../../swapChain.hpp"
#include "WSTessendorf.hpp"

#include <format>
#include <memory>

namespace vkh {
WaterSys::WaterSys(EngineContext &context, SkyboxSys &skyboxSys)
    : System{context}, modelTess{context},
      skyboxSys{skyboxSys} {
  createDescriptorSetLayout();

  createPipeline();

  createMesh();

  createRenderData();
  prepare();
}
WaterSys::~WaterSys() {
  vkDestroyDescriptorSetLayout(context.vulkan.device, setLayout, nullptr);
}
void WaterSys::createPipeline() {
  PipelineCreateInfo pipelineInfo{};
  const VkDescriptorSetLayout setLayouts[] = {context.vulkan.globalDescriptorSetLayout, setLayout, skyboxSys.setLayout};

  pipelineInfo.layoutInfo.setLayoutCount = 3;
  pipelineInfo.layoutInfo.pSetLayouts = setLayouts;
  pipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  pipelineInfo.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
  pipelineInfo.attributeDescriptions = Vertex::attribDescriptions;
  pipelineInfo.bindingDescriptions = Vertex::bindingDescriptions;
  pipelineInfo.vertpath = "shaders/water/water.vert.spv";
  pipelineInfo.fragpath = "shaders/water/water.frag.spv";
  pipelineInfo.tescpath = "shaders/water/water.tesc.spv";
  pipelineInfo.tesepath = "shaders/water/water.tese.spv";
  pipeline = std::make_unique<GraphicsPipeline>(context, pipelineInfo, "water");
}
void WaterSys::createUniformBuffers(const uint32_t bufferCount) {
  const VkDeviceSize bufferSize =
      getUniformBufferAlignment(context, sizeof(VertexUBO));

  uniformBuffers.reserve(bufferCount);
  for (size_t i = 0; i < bufferCount; i++) {
    auto &buffer = uniformBuffers.emplace_back(
        context, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, bufferSize);
    buffer.map();
  }
}
void WaterSys::createDescriptorSets(const uint32_t count) {
  sets.reserve(count);
  for (size_t i = 0; i < count; i++) {
    auto &set = sets.emplace_back(
        context.vulkan.globalDescriptorAllocator->allocate(setLayout));
    std::string str = std::format("set #{} for WaterSys", i);
    debug::setObjName(context, VK_OBJECT_TYPE_DESCRIPTOR_SET,
                      reinterpret_cast<uint64_t>(set), str.c_str());
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
          glm::vec3(static_cast<float>(x), // x
                    0.0f,                  // y
                    static_cast<float>(y)  // z
                    ) *
              vertexDistance,
          // Texcoords
          glm::vec2(static_cast<float>(x + halfSize), // u
                    static_cast<float>(y + halfSize)  // v
                    ) /
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
void WaterSys::updateDescriptorSet(VkDescriptorSet set) {
  // UBOs
  VkDescriptorBufferInfo bufferInfos[1] = {};
  bufferInfos[0].buffer = uniformBuffers[context.frameInfo.frameIndex];
  bufferInfos[0].offset = 0;
  bufferInfos[0].range = sizeof(VertexUBO);

  VkDescriptorImageInfo imageInfo = {};
  imageInfo = modelTess.getDisplacementsImage().getDescriptorInfo(
      context.vulkan.defaultSampler);
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  DescriptorWriter writer(context);

  writer.writeBuffer(0, bufferInfos[0], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
  writer.writeImage(1, imageInfo,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
  writer.updateSet(set);
}
void WaterSys::updateUniformBuffer() {
  auto &buffer = uniformBuffers[context.frameInfo.frameIndex];

  buffer.write(&vertexUBO, sizeof(vertexUBO));

  buffer.flush();
}
void WaterSys::prepare() {
  auto cmd = beginSingleTimeCommands(context);

  modelTess.recordComputeWaves(cmd,
                               static_cast<float>(glfwGetTime()) * animSpeed);

  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
}

void WaterSys::update() {
  vertexUBO.model = glm::mat4(1.0f);

  updateUniformBuffer();
  updateDescriptorSet(sets[context.frameInfo.frameIndex]);
  if (playAnimation) {
    modelTess.recordComputeWaves(context.frameInfo.cmd,
                                 static_cast<float>(glfwGetTime()) * animSpeed);
  }
}

void WaterSys::render() {
  debug::beginLabel(context, context.frameInfo.cmd, "WaterSys render",
                    {0.f, 0.f, 1.f, 1.f});
  pipeline->bind(context.frameInfo.cmd);

  VkDescriptorSet bindSets[] = {context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex], sets[context.frameInfo.frameIndex],
                                skyboxSys.set};

  vkCmdBindDescriptorSets(context.frameInfo.cmd,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, 3,
                          bindSets, 0, nullptr);
  scene->bind(context, context.frameInfo.cmd, *pipeline);

  scene->meshes.begin()->primitives.begin()->draw(context.frameInfo.cmd);
  debug::endLabel(context, context.frameInfo.cmd);
}

void WaterSys::createDescriptorSetLayout() {
  setLayout = buildDescriptorSetLayout(
      context, {VkDescriptorSetLayoutBinding{
                    .binding = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                  VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                                  VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                },
                VkDescriptorSetLayoutBinding{
                    .binding = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                }});
  debug::setObjName(context, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                    reinterpret_cast<uint64_t>(setLayout), "water set layout");
}

void WaterSys::createMesh() {
  std::vector<Vertex> vertices = createGridVertices();
  std::vector<uint32_t> indices = createGridIndices();

  SceneCreateInfo<Vertex> meshInfo{.vertices = vertices, .indices = indices};
  scene = std::make_unique<Scene<Vertex>>(context, meshInfo);
}
} // namespace vkh
