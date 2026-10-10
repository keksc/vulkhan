#include "text.hpp"

#include "../vkh/systems/hud/hud.hpp"
#include "../vkh/systems/hud/view.hpp"

namespace UI {
float Text::renderedScale() const {
  return sizeMultiplicator * view.layoutScale;
}
Text::Text(vkh::hud::View &view, Element *parent, glm::vec2 position,
           const std::string &content, glm::vec3 color, float sizeMultiplicator)
    : Element(view, parent, position, {}), content{this, content}, color{color},
      sizeMultiplicator{sizeMultiplicator} {
  flushSize();
}
void Text::addToDrawInfo(vkh::hud::DrawInfo &drawInfo, float depth) {
  glm::vec2 cursor = absPos;
  float maxX = cursor.x;

  float maxSizeY = vkh::TextSys::glyphRange.maxSizeY;
  for (auto &c : content) {
    if (c == '\n') {
      cursor.x = absPos.x;
      cursor.y += maxSizeY * renderedScale();
      continue;
    }
    if (!vkh::TextSys::glyphRange.glyphs.count(c))
      continue;
    uint32_t baseIndex = static_cast<uint32_t>(drawInfo.textVertices.size());
    auto &ch = vkh::TextSys::glyphRange.glyphs[c];

    // Calculate vertex positions for this character
    float x0 = cursor.x + ch.offset.x * renderedScale();
    float x1 = x0 + ch.size.x * renderedScale();
    float y0 = cursor.y + (maxSizeY + ch.offset.y) * renderedScale();
    float y1 = y0 + ch.size.y * renderedScale();

    // Add four vertices for this character
    drawInfo.textVertices.emplace_back(glm::vec3{x0, y0, depth}, color,
                                       glm::vec2{
                                           ch.uvOffset.x,
                                           ch.uvOffset.y,
                                       });
    drawInfo.textVertices.emplace_back(glm::vec3{x1, y0, depth}, color,
                                       glm::vec2{
                                           ch.uvOffset.x + ch.uvExtent.x,
                                           ch.uvOffset.y,
                                       });
    drawInfo.textVertices.emplace_back(glm::vec3{x1, y1, depth}, color,
                                       glm::vec2{
                                           ch.uvOffset.x + ch.uvExtent.x,
                                           ch.uvOffset.y + ch.uvExtent.y,
                                       });
    drawInfo.textVertices.emplace_back(glm::vec3{x0, y1, depth}, color,
                                       glm::vec2{
                                           ch.uvOffset.x,
                                           ch.uvOffset.y + ch.uvExtent.y,
                                       });

    // Add indices for this character's quad
    drawInfo.textIndices.emplace_back(baseIndex + 0);
    drawInfo.textIndices.emplace_back(baseIndex + 1);
    drawInfo.textIndices.emplace_back(baseIndex + 2);
    drawInfo.textIndices.emplace_back(baseIndex + 0);
    drawInfo.textIndices.emplace_back(baseIndex + 2);
    drawInfo.textIndices.emplace_back(baseIndex + 3);

    // Move cursor to next character position
    cursor.x += ch.advance * renderedScale();
    maxX = glm::max(cursor.x, maxX);
  }
  setAbsoluteSize(glm::vec2{maxX, cursor.y + maxSizeY * renderedScale()} -
                  absPos);
}
void Text::update() { flushSize(); }
void Text::flushSize() {
  float maxSizeY = vkh::TextSys::glyphRange.maxSizeY;
  glm::vec2 newSize = glm::vec2{0.f, maxSizeY * renderedScale()};
  float maxX = 0.f;

  for (auto &c : content) {
    if (c == '\n') {
      maxX = glm::max(newSize.x, maxX);
      newSize.x = 0.f;
      newSize.y += maxSizeY * renderedScale();
      continue;
    }
    if (!vkh::TextSys::glyphRange.glyphs.count(c))
      continue;
    auto &ch = vkh::TextSys::glyphRange.glyphs[c];

    newSize.x += ch.advance * renderedScale();
  }
  newSize.x = glm::max(newSize.x, maxX);
  setAbsoluteSize(newSize);
}
} // namespace UI
