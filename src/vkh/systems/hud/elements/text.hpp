#pragma once

#include "element.hpp"

namespace vkh {
namespace hud {
class Text : public Element {
public:
  Text(View &view, Element *parent, glm::vec2 position,
       const std::string &content = "")
      : Element(view, parent, position, {}), content{content} {}
  std::string content;

protected:
  void addToDrawInfo(DrawInfo &drawInfo) override {
    glm::vec2 cursor = position;
    float maxX = cursor.x;

    float maxSizeY = TextSys::glyphRange.maxSizeY;
    for (auto &c : content) {
      if (c == '\n') {
        cursor.x = position.x;
        cursor.y += maxSizeY;
        continue;
      }
      if (!TextSys::glyphRange.glyphs.count(c))
        continue;
      uint32_t baseIndex = static_cast<uint32_t>(drawInfo.textVertices.size());
      auto &ch = TextSys::glyphRange.glyphs[c];

      // Calculate vertex positions for this character
      float x0 = cursor.x + ch.offset.x;
      float x1 = x0 + ch.size.x;
      float y0 = maxSizeY + cursor.y + ch.offset.y;
      float y1 = y0 + ch.size.y;

      // Add four vertices for this character
      drawInfo.textVertices.emplace_back(
          glm::vec2{x0, y0}, glm::vec2{ch.uvOffset.x, ch.uvOffset.y});
      drawInfo.textVertices.emplace_back(
          glm::vec2{x1, y0},
          glm::vec2{ch.uvOffset.x + ch.uvExtent.x, ch.uvOffset.y});
      drawInfo.textVertices.emplace_back(
          glm::vec2{x1, y1}, glm::vec2{ch.uvOffset.x + ch.uvExtent.x,
                                       ch.uvOffset.y + ch.uvExtent.y});
      drawInfo.textVertices.emplace_back(
          glm::vec2{x0, y1},
          glm::vec2{ch.uvOffset.x, ch.uvOffset.y + ch.uvExtent.y});

      // Add indices for this character's quad
      drawInfo.textIndices.emplace_back(baseIndex + 0);
      drawInfo.textIndices.emplace_back(baseIndex + 1);
      drawInfo.textIndices.emplace_back(baseIndex + 2);
      drawInfo.textIndices.emplace_back(baseIndex + 0);
      drawInfo.textIndices.emplace_back(baseIndex + 2);
      drawInfo.textIndices.emplace_back(baseIndex + 3);

      // Move cursor to next character position
      cursor.x += ch.advance;
      maxX = glm::max(cursor.x, maxX);
    }
    size = glm::vec2{maxX, cursor.y + maxSizeY} - position;
  }
};
} // namespace hud
} // namespace vkh
