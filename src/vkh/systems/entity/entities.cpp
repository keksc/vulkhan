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
EntitySys::~EntitySys() {}

EntitySys::EntitySys(EngineContext &context, std::vector<Entity> &entities,
                     SkyboxSys &skyboxSys)
    : System(context), entities{entities}, skyboxSys{skyboxSys} {
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(PushConstantData);

  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      *context.vulkan.globalDescriptorSetLayout,
      *context.vulkan.sceneDescriptorSetLayout, *skyboxSys.setLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount =
      static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  pipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  pipelineInfo.attributeDescriptions = Vertex::getAttributeDescriptions();
  pipelineInfo.bindingDescriptions = Vertex::getBindingDescriptions();
  pipelineInfo.vertpath = "shaders/entities.vert.spv";
  pipelineInfo.fragpath = "shaders/entities.frag.spv";
  pipeline = std::make_unique<GraphicsPipeline>(context, pipelineInfo);
}

void EntitySys::compactDraws() {
  batches.clear();
  batches.emplace_back(&entities[0], 1);
  for (std::size_t i = 1; i < entities.size(); i++) {
    if ((batches.end() - 1)->entity->scene == entities[i].scene)
      (batches.end() - 1)->count++;
    else
      batches.emplace_back(&entities[i], 1);
  }
}

void EntitySys::render() {
  pipeline->bind(context.frameInfo.cmd);

  vkCmdBindDescriptorSets(context.frameInfo.cmd,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, 1,
                          &context.frameInfo.globalDescriptorSet, 0, nullptr);
  vkCmdBindDescriptorSets(context.frameInfo.cmd,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 2, 1,
                          &skyboxSys.set, 0, nullptr);

  // compactDraws();

  // std::println("batches: {}", batches.size());
  // std::println("entities: {}", entities.size());

  std::shared_ptr<Scene<Vertex>> currentScene = nullptr;
  for (auto &entity : entities) {
    auto &scene = entity.scene;
    if (scene != currentScene) {
      currentScene = scene;
      currentScene->bind(context, context.frameInfo.cmd, *pipeline);
    }

    PushConstantData push{};
    auto *mesh = &entity.getMesh();
    push.modelMatrix = entity.transform.mat4() * mesh->transform;
    push.normalMatrix = entity.transform.normalMatrix();
    Scene<Vertex>::Material *currentMaterial = nullptr;
    for (auto &primitive : mesh->primitives) {
      auto &mat = scene->materials[primitive.materialIndex];
      if (&mat != currentMaterial) {
        if (mat.baseColorTextureIndex.has_value()) {
          push.useColorTexture = true;
          vkCmdBindDescriptorSets(
              context.frameInfo.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline,
              1, 1,
              &scene->imageDescriptorSets[mat.baseColorTextureIndex.value()], 0,
              nullptr);
        } else {
          push.useColorTexture = false;
          push.color = mat.baseColorFactor;
        }
        vkCmdPushConstants(context.frameInfo.cmd, *pipeline,
                           VK_SHADER_STAGE_VERTEX_BIT |
                               VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(PushConstantData), &push);
      }
      primitive.draw(context.frameInfo.cmd);
    }
  }
}
} // namespace vkh
