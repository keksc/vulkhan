#include "hud.hpp"

#include <vulkan/vulkan_core.h>

#include <memory>
#include <vector>

#include "../../buffer.hpp"
#include "../../pipeline.hpp"
#include "../../swapChain.hpp"
#include "hudElements.hpp"

namespace vkh {
HudSys::HudSys(EngineContext &context)
    : System(context), textSys(context), solidColorSys(context) {}

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
  drawInfo.solidColorTriangleVertices.clear();
  drawInfo.solidColorTriangleIndices.clear();
  drawInfo.textVertices.clear();
  drawInfo.textIndices.clear();
  drawInfo.solidColorLineVertices.clear();
  for (const auto &element : *view) {
    addToDraw(element);
  }
  textSys.vertexBuffer->write(drawInfo.textVertices.data(),
                              drawInfo.textVertices.size() *
                                  sizeof(TextSys::Vertex));
  textSys.indexBuffer->write(drawInfo.textIndices.data(),
                             drawInfo.textIndices.size() * sizeof(uint32_t));
  solidColorSys.linesVertexBuffer->write(
      drawInfo.solidColorLineVertices.data(),
      drawInfo.solidColorLineVertices.size() * sizeof(SolidColorSys::Vertex));
  solidColorSys.trianglesVertexBuffer->write(
      drawInfo.solidColorTriangleVertices.data(),
      drawInfo.solidColorTriangleVertices.size() * sizeof(SolidColorSys::Vertex));
  solidColorSys.trianglesIndexBuffer->write(
      drawInfo.solidColorTriangleIndices.data(),
      drawInfo.solidColorTriangleIndices.size() * sizeof(uint32_t));
}

void HudSys::render() {
  if (!view)
    return;
  update();

  solidColorSys.render(drawInfo.solidColorLineVertices.size(),
                       drawInfo.solidColorTriangleIndices.size());

  textSys.render(drawInfo.textIndices.size());
} // namespace hudSys
} // namespace vkh
