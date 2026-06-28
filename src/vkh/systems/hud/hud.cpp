#include "hud.hpp"

#include "../../debug.hpp"
#include "../../pipeline.hpp"
#include "../../swapChain.hpp"
#include "element.hpp"
#include "view.hpp"

namespace vkh {

HudSys::HudSys(EngineContext &context)
    : System(context), textSys(context), solidColorSys(context) {}

void HudSys::setView(hud::View *newView) {
  view = newView;
  newView->setCurrent();
}

hud::View *HudSys::getView() { return view; }

void HudSys::addToDraw(hud::Element &element, float &depth,
                       float oneOverElementCount) {
  for (const auto &child : element.children) {
    child->addToDrawInfo(drawInfo, depth);
    float child_depth = depth + oneOverElementCount;
    addToDraw(*child, child_depth, oneOverElementCount);
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

  float depth = 0.f;
  addToDraw(view->container, depth, 1.f / view->elementCount);

  solidColorSys.ensureLineCapacity(drawInfo.solidColorLineVertices.size(),
                                   drawInfo.solidColorLineIndices.size());
  solidColorSys.ensureTriangleCapacity(
      drawInfo.solidColorTriangleVertices.size(),
      drawInfo.solidColorTriangleIndices.size());

  textSys.ensureCapacity(drawInfo.textVertices.size(),
                         drawInfo.textIndices.size());

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
  if (!view || view->container.children.empty())
    return;
  update();

  auto cmd = context.frameInfo.cmd;

  debug::beginLabel(context, cmd, "HudSys rendering",
                    glm::vec4{1.f, 1.f, 1.f, 1.f});

  vk::ClearAttachment clearAttachment{};
  clearAttachment.aspectMask = vk::ImageAspectFlagBits::eDepth;
  clearAttachment.clearValue.depthStencil = vk::ClearDepthStencilValue{0.0f, 0};

  vk::ClearRect clearRect{};
  clearRect.rect.offset = vk::Offset2D{0, 0};
  clearRect.rect.extent = context.vulkan.swapChain->swapChainExtent;
  clearRect.layerCount = 1;

  cmd.clearAttachments(1, &clearAttachment, 1, &clearRect);

  solidColorSys.render(drawInfo.solidColorLineIndices.size(),
                       drawInfo.solidColorTriangleIndices.size());

  textSys.render(drawInfo.textIndices.size());

  debug::endLabel(context, cmd);
}

} // namespace vkh
