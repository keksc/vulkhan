#include "hud.hpp"

#include <vulkan/vulkan_core.h>

#include <memory>
#include <vector>

#include "../../buffer.hpp"
#include "../../swapChain.hpp"

namespace vkh {
HudSys::HudSys(EngineContext &context)
    : System(context), textSys(context), solidColorSys(context) {}

void HudSys::setView(hud::View *newView) {
  view = newView;
  newView->setCurrent();
}

hud::View *HudSys::getView() { return view; }

void HudSys::addToDraw(std::vector<std::shared_ptr<hud::Element>> &elements,
                       float &depth, float oneOverElementCount) {
  for (const auto &element : elements) {
    element->addToDrawInfo(drawInfo, depth);
    float child_depth = depth + oneOverElementCount;
    addToDraw(element->children, child_depth, oneOverElementCount);
    depth += oneOverElementCount;
  }
}

void HudSys::update() {
  drawInfo.solidColorTriangleVertices.clear();
  drawInfo.solidColorTriangleIndices.clear();
  drawInfo.textVertices.clear();
  drawInfo.textIndices.clear();
  drawInfo.solidColorLineVertices.clear();
  drawInfo.solidColorLineIndices.clear();
  // float oneOverViewSize =
  //     view->elementCount > 0 ? 1.f / view->elementCount : 0.1f;
  float depth = 0.f;
  addToDraw(view->elements, depth, 1.f / view->elementCount);
  textSys.vertexBuffer->write(drawInfo.textVertices.data(),
                              drawInfo.textVertices.size() *
                                  sizeof(TextSys::Vertex));
  textSys.indexBuffer->write(drawInfo.textIndices.data(),
                             drawInfo.textIndices.size() * sizeof(uint32_t));
  solidColorSys.linesVertexBuffer->write(
      drawInfo.solidColorLineVertices.data(),
      drawInfo.solidColorLineVertices.size() *
          sizeof(SolidColorSys::LineVertex));
  solidColorSys.linesIndexBuffer->write(drawInfo.solidColorLineIndices.data(),
                                        drawInfo.solidColorLineIndices.size() *
                                            sizeof(uint32_t));
  solidColorSys.trianglesVertexBuffer->write(
      drawInfo.solidColorTriangleVertices.data(),
      drawInfo.solidColorTriangleVertices.size() *
          sizeof(SolidColorSys::TriangleVertex));
  solidColorSys.trianglesIndexBuffer->write(
      drawInfo.solidColorTriangleIndices.data(),
      drawInfo.solidColorTriangleIndices.size() * sizeof(uint32_t));
}

void HudSys::render() {
  if (!view)
    return;
  if (view->empty())
    return;
  if (forceUpdate) {
    update();
    forceUpdate = false;
  }

  debug::beginLabel(context, context.frameInfo.cmd, "HudSys rendering",
                    glm::vec4{1.f, 1.f, 1.f, 1.f});
  VkClearAttachment clearAttachment{};
  clearAttachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  clearAttachment.clearValue.depthStencil = {0.0f, 0};

  // clear depth buffer
  VkClearRect clearRect{};
  clearRect.rect.offset = {0, 0};
  clearRect.rect.extent = context.vulkan.swapChain->swapChainExtent;
  clearRect.layerCount = 1;

  vkCmdClearAttachments(context.frameInfo.cmd, 1, &clearAttachment, 1,
                        &clearRect);

  solidColorSys.render(drawInfo.solidColorLineIndices.size(),
                       drawInfo.solidColorTriangleIndices.size());

  textSys.render(drawInfo.textIndices.size());
  debug::endLabel(context, context.frameInfo.cmd);
}
} // namespace vkh
