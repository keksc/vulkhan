#include "hud.hpp"

#include <fmt/format.h>
#include <vulkan/vulkan_core.h>

#include <memory>
#include <vector>

#include "../../buffer.hpp"
#include "../../pipeline.hpp"
#include "../../swapChain.hpp"
#include "hudElements.hpp"

namespace vkh {
void HudSys::createBuffers() {
  vertexBuffer = std::make_unique<Buffer<hud::SolidColorVertex>>(
      context, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      maxVertexCount);
  vertexBuffer->map();

  indexBuffer = std::make_unique<Buffer<uint32_t>>(
      context, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      maxIndexCount);
  indexBuffer->map();
}

void HudSys::createPipeline() {
  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  pipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  pipelineInfo.attributeDescriptions = attributeDescriptions;
  pipelineInfo.bindingDescriptions = bindingDescriptions;
  pipelineInfo.vertpath = "shaders/solidColor.vert.spv";
  pipelineInfo.fragpath = "shaders/solidColor.frag.spv";
  pipeline = std::make_unique<GraphicsPipeline>(context, pipelineInfo);
}
HudSys::HudSys(EngineContext &context)
    : System(context), textSys(context), linesSys(context) {
  createBuffers();

  createPipeline();
}
void HudSys::addToDraw(std::shared_ptr<hud::Element> element) {
  for (auto &child : element->children) {
    addToDraw(child);
  }
  element->addToDrawInfo(drawInfo);
}
void HudSys::setView(hud::View *newView) {
  view = newView;
  newView->setCurrent();
}
hud::View *HudSys::getView() { return view; }
void HudSys::update() {
  drawInfo.solidColorVertices.clear();
  drawInfo.solidColorIndices.clear();
  drawInfo.textVertices.clear();
  drawInfo.textIndices.clear();
  drawInfo.lineVertices.clear();
  for (const auto &element : *view) {
    addToDraw(element);
  }
  vertexBuffer->write(drawInfo.solidColorVertices.data(),
                      drawInfo.solidColorVertices.size() *
                          sizeof(hud::SolidColorVertex));
  indexBuffer->write(drawInfo.solidColorIndices.data(),
                     drawInfo.solidColorIndices.size() * sizeof(uint32_t));
  textSys.vertexBuffer->write(drawInfo.textVertices.data(),
                              drawInfo.textVertices.size() *
                                  sizeof(TextSys::Vertex));
  textSys.indexBuffer->write(drawInfo.textIndices.data(),
                             drawInfo.textIndices.size() * sizeof(uint32_t));
  linesSys.vertexBuffer->write(drawInfo.lineVertices.data(),
                               drawInfo.lineVertices.size() *
                                   sizeof(SolidColorSys::Vertex));
}

void HudSys::render() {
  if (!view)
    return;
  update();

  linesSys.render(drawInfo.lineVertices.size());

  pipeline->bind(context.frameInfo.cmd);

  VkBuffer buffers[] = {*vertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(context.frameInfo.cmd, 0, 1, buffers, offsets);
  vkCmdBindIndexBuffer(context.frameInfo.cmd, *indexBuffer, 0,
                       VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(context.frameInfo.cmd,
                   static_cast<uint32_t>(drawInfo.solidColorIndices.size()), 1,
                   0, 0, 0);

  textSys.render(drawInfo.textIndices.size());
} // namespace hudSys
} // namespace vkh
