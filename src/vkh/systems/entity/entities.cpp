#include "entities.hpp"
#include <memory>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vulkan/vulkan_core.h>

#include <print>
#include <vector>

#include "../../descriptors.hpp"
#include "../../pipeline.hpp"
#include "../../scene.hpp"
#include "../../swapChain.hpp"

namespace vkh {

struct PushConstantData {
  glm::mat4 modelMatrix{1.f};
  glm::mat3 normalMatrix{1.f};
  alignas(16) glm::vec4 color;
  alignas(1) bool useColorTexture;
  alignas(1) bool useMetallicRoughnessTexture;
  alignas(4) float metallic;
  alignas(4) float roughness;
};
void EntitySys::createSetLayout() {
  VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{
      .sType =
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
  VkDescriptorBindingFlags bindingFlags[] = {
      VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
      VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT};
  bindingFlagsInfo.pBindingFlags = bindingFlags;
  bindingFlagsInfo.bindingCount = 2;
  setLayout = buildDescriptorSetLayout(
      context,
      {VkDescriptorSetLayoutBinding{
           .binding = 0,
           .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
           .descriptorCount = 1,
           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
       },
       VkDescriptorSetLayoutBinding{
           .binding = 1,
           .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
           .descriptorCount = 1,
           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
       }},
      0, &bindingFlagsInfo);
}
EntitySys::EntitySys(EngineContext &context, SkyboxSys &skyboxSys)
    : System(context), skyboxSys{skyboxSys} {
  createSetLayout();

  dummySet = context.vulkan.globalDescriptorAllocator->allocate(setLayout);
  DescriptorWriter writer(context);
  VkDescriptorImageInfo imageInfo{.sampler = VK_NULL_HANDLE,
                                  .imageView = VK_NULL_HANDLE,
                                  .imageLayout =
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  writer.writeImage(0, imageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
  writer.updateSet(dummySet);

  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(PushConstantData);

  std::vector<VkDescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout, setLayout, skyboxSys.setLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  pipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  pipelineInfo.attributeDescriptions = Vertex::getAttributeDescriptions();
  pipelineInfo.bindingDescriptions = Vertex::getBindingDescriptions();
  pipelineInfo.vertpath = "shaders/entities.vert.spv";
  pipelineInfo.fragpath = "shaders/entities.frag.spv";
  pipeline =
      std::make_unique<GraphicsPipeline>(context, pipelineInfo, "entities");
}
EntitySys::~EntitySys() {
  vkDestroyDescriptorSetLayout(context.vulkan.device, setLayout, nullptr);
}
void EntitySys::setEntities(std::vector<Entity> &entities) {
  Buffer<VkDrawIndexedIndirectCommand> cmdsBuf(
      context, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkDrawIndexedIndirectCommand cmd{};
  cmd.instanceCount = 1;
  batches.clear();
  batches.emplace_back(entities[0], 1);
  for (std::size_t i = 1; i < entities.size(); i++) {
    if ((batches.end() - 1)->entity.scene == entities[i].scene)
      (batches.end() - 1)->count++;
    else
      batches.emplace_back(entities[i], 1);
  }
}

void EntitySys::render() {
  debug::beginLabel(context, context.frameInfo.cmd, "entities rendering",
                    {.7f, .3f, 1.f, 1.f});
  pipeline->bind(context.frameInfo.cmd);

  vkCmdBindDescriptorSets(
      context.frameInfo.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, 1,
      &context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex], 0,
      nullptr);
  vkCmdBindDescriptorSets(context.frameInfo.cmd,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 2, 1,
                          &skyboxSys.set, 0, nullptr);

  // compactDraws();

  // std::println("batches: {}", batches.size());
  // std::println("entities: {}", entities.size());

  // std::shared_ptr<Scene<Vertex>> currentScene = nullptr;
  // for (auto &entity : entities) {
  //   auto &scene = entity.scene;
  //   if (scene != currentScene) {
  //     currentScene = scene;
  //     currentScene->bind(context, context.frameInfo.cmd, *pipeline);
  //   }
  //
  //   PushConstantData push{};
  //   auto *mesh = &entity.getMesh();
  //   push.modelMatrix = entity.transform.mat4() * mesh->transform;
  //   push.normalMatrix = entity.transform.normalMatrix();
  //   Scene<Vertex>::Material *currentMaterial = nullptr;
  //   for (auto &primitive : mesh->primitives) {
  //     auto &mat = scene->materials[primitive.materialIndex];
  //     if (&mat != currentMaterial) {
  //       if (mat.baseColorTextureIndex.has_value()) {
  //         push.useColorTexture = true;
  //         vkCmdBindDescriptorSets(
  //             context.frameInfo.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline,
  //             1, 1,
  //             &scene->imageDescriptorSets[mat.baseColorTextureIndex.value()], 0,
  //             nullptr);
  //       } else {
  //         vkCmdBindDescriptorSets(context.frameInfo.cmd,
  //                                 VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 1,
  //                                 1, &dummySet, 0, nullptr);
  //         push.useColorTexture = false;
  //         push.color = mat.baseColorFactor;
  //       }
  //       vkCmdPushConstants(context.frameInfo.cmd, *pipeline,
  //                          VK_SHADER_STAGE_VERTEX_BIT |
  //                              VK_SHADER_STAGE_FRAGMENT_BIT,
  //                          0, sizeof(PushConstantData), &push);
  //     }
  //     primitive.draw(context.frameInfo.cmd);
  //   }
  // }
  debug::endLabel(context, context.frameInfo.cmd);
}
} // namespace vkh
