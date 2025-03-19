#include "hud.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vulkan/vulkan_core.h>

#include <memory>
#include <vector>

#include "../buffer.hpp"
#include "../descriptors.hpp"
#include "../image.hpp"
#include "../pipeline.hpp"
#include "../renderer.hpp"

namespace vkh {
namespace hudSys {
std::unique_ptr<GraphicsPipeline> pipeline;
std::unique_ptr<DescriptorSetLayout> descriptorSetLayout;
VkDescriptorSet descriptorSet;

std::vector<VkVertexInputAttributeDescription> attributeDescriptions = {
    {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position)},
    {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)}};
std::vector<VkVertexInputBindingDescription> bindingDescriptions = {
    {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}};

std::unique_ptr<Buffer> vertexBuffer;
std::unique_ptr<Buffer> indexBuffer;

std::vector<char> fontDataChar;
unsigned char *fontData;

std::vector<Vertex> vertices;
std::vector<uint32_t> indices;

std::shared_ptr<Image> fontAtlas;
const int maxHudRects = 10;
const int maxVertexCount = 4 * maxHudRects; // 4 vertices = 1 quad = 1 glyph
VkDeviceSize maxVertexSize = sizeof(Vertex) * maxVertexCount;

void createBuffers(EngineContext &context) {
  BufferCreateInfo bufInfo{};
  bufInfo.instanceSize = sizeof(Vertex);
  bufInfo.instanceCount = maxVertexCount;
  bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  bufInfo.memoryProperties =
      /*std::string text*/ VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  vertexBuffer = std::make_unique<Buffer>(context, bufInfo);
  vertexBuffer->map();

  bufInfo.instanceSize = sizeof(uint32_t);
  // bufInfo.instanceCount = maxIndexCount;
  bufInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  indexBuffer = std::make_unique<Buffer>(context, bufInfo);
  indexBuffer->map();
}

void createPipeline(EngineContext &context) {
  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  pipelineInfo.renderPass = renderer::getSwapChainRenderPass(context);
  pipelineInfo.attributeDescriptions = attributeDescriptions;
  pipelineInfo.bindingDescriptions = bindingDescriptions;
  pipeline = std::make_unique<GraphicsPipeline>(
      context, "shaders/hud.vert.spv", "shaders/hud.frag.spv", pipelineInfo);
}

void init(EngineContext &context) {
  createBuffers(context);

  createPipeline(context);
}
void update(EngineContext &context,
            std::vector<std::unique_ptr<Element>> &content) {
  vertices.clear();
  indices.clear();
  for (auto &element : content) {
    uint32_t baseIndex = vertices.size();
    auto& drawInfo = element->getDrawInfo(baseIndex);
    vertices.insert(vertices.end(), drawInfo.vertices.begin(), drawInfo.vertices.end());
    indices.insert(indices.end(), drawInfo.indices.begin(), drawInfo.indices.end());
  }
  vertexBuffer->write(vertices.data());
  indexBuffer->write(indices.data());
}

void cleanup(EngineContext &context) {
  vertexBuffer->unmap();
  vertexBuffer = nullptr;
  indexBuffer->unmap();
  indexBuffer = nullptr;
  pipeline = nullptr;
  fontAtlas = nullptr;
  descriptorSetLayout = nullptr;
}

void render(EngineContext &context) {
  pipeline->bind(context.frameInfo.commandBuffer);

  /*PushConstantData push{};

  vkCmdPushConstants(context.frameInfo.commandBuffer, pipelineLayout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, sizeof(PushConstantData), &push);*/

  // model->bind(context, context.frameInfo.commandBuffer, pipelineLayout);
  // model->draw(context.frameInfo.commandBuffer);
  VkBuffer buffers[] = {*vertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(context.frameInfo.commandBuffer, 0, 1, buffers,
                         offsets);
  vkCmdBindIndexBuffer(context.frameInfo.commandBuffer, *indexBuffer, 0,
                       VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(context.frameInfo.commandBuffer, indices.size(), 1, 0, 0, 0);
}
} // namespace hudSys
} // namespace vkh
