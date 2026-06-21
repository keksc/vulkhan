#pragma once

#include "solidColor.hpp"
#include "text.hpp"

#include <vector>

namespace vkh {
namespace hud {
struct DrawInfo {
  std::vector<TextSys::Vertex> textVertices;
  std::vector<uint32_t> textIndices;
  std::vector<SolidColorSys::LineVertex> solidColorLineVertices;
  std::vector<uint32_t> solidColorLineIndices;
  std::vector<SolidColorSys::TriangleVertex> solidColorTriangleVertices;
  std::vector<uint32_t> solidColorTriangleIndices;
  void add(DrawInfo other) {
    textVertices.insert(textVertices.end(), other.textVertices.begin(),
                        other.textVertices.end());
    textIndices.insert(textIndices.end(), other.textIndices.begin(),
                       other.textIndices.end());
    solidColorLineVertices.insert(solidColorLineVertices.end(),
                                  other.solidColorLineVertices.begin(),
                                  other.solidColorLineVertices.end());
    solidColorLineIndices.insert(solidColorLineIndices.end(),
                                 other.solidColorLineIndices.begin(),
                                 other.solidColorLineIndices.end());
    solidColorTriangleVertices.insert(solidColorTriangleVertices.end(),
                                      other.solidColorTriangleVertices.begin(),
                                      other.solidColorTriangleVertices.end());
    solidColorTriangleIndices.insert(solidColorTriangleIndices.end(),
                                     other.solidColorTriangleIndices.begin(),
                                     other.solidColorTriangleIndices.end());
  }
};
} // namespace hud
} // namespace vkh
