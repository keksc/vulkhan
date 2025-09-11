#pragma once

#include "../text.hpp"
#include "../solidColor.hpp"

#include <vector>

namespace vkh {
namespace hud {
struct DrawInfo {
  std::vector<TextSys::Vertex> textVertices;
  std::vector<uint32_t> textIndices;
  std::vector<SolidColorSys::LineVertex> solidColorLineVertices;
  std::vector<SolidColorSys::TriangleVertex> solidColorTriangleVertices;
  std::vector<uint32_t> solidColorTriangleIndices;
};
} // namespace hud
} // namespace vkh
