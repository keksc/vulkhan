#pragma once

#include "../../image.hpp"
#include "../../descriptors.hpp"
#include "../system.hpp"
#include "hudElements.hpp"

namespace vkh {
class HudSys : public System {
public:
  HudSys(EngineContext &context);
  void setView(hud::View *newView);
  hud::View *getView();
  void render();

private:
  void createBuffers();
  void createPipeline();
  void addElementToDraw(std::shared_ptr<hud::Element> element);
  void update();

  std::unique_ptr<GraphicsPipeline> pipeline;
  std::unique_ptr<DescriptorSetLayout> descriptorSetLayout;
  VkDescriptorSet descriptorSet;

  TextSys textSys;
  std::unique_ptr<Buffer> vertexBuffer;
  std::unique_ptr<Buffer> indexBuffer;

  std::shared_ptr<Image> fontAtlas;

  hud::DrawInfo drawInfo;
  const int maxHudRects = 1000;
  const int maxVertexCount = 4 * maxHudRects; // 4 vertices = 1 quad = 1 glyph
  VkDeviceSize maxVertexSize = sizeof(hud::SolidColorVertex) * maxVertexCount;

  std::vector<VkVertexInputAttributeDescription> attributeDescriptions = {
      {0, 0, VK_FORMAT_R32G32_SFLOAT,
       offsetof(hud::SolidColorVertex, position)},
      {1, 0, VK_FORMAT_R32G32B32_SFLOAT,
       offsetof(hud::SolidColorVertex, color)}};
  std::vector<VkVertexInputBindingDescription> bindingDescriptions = {
      {0, sizeof(hud::SolidColorVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
  hud::View *view{};
}; // namespace hudSys
} // namespace vkh
