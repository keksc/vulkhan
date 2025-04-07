#pragma once

#include <glm/glm.hpp>

#include "../../engineContext.hpp"
#include "../../buffer.hpp"

namespace vkh {
namespace textSys {
struct Glyph {
  glm::vec2 size;
  glm::vec2 offset;
  glm::vec2 uvOffset;
  glm::vec2 uvExtent;
  float advance;
};
struct Vertex {
  glm::vec2 position{};
  glm::vec2 uv{};
};
void init(EngineContext &context);
void cleanup(EngineContext &context);
void render(EngineContext& context, size_t indicesSize);
extern std::unique_ptr<Buffer> vertexBuffer;
extern std::unique_ptr<Buffer> indexBuffer;
extern std::unordered_map<char, textSys::Glyph> glyphs;
} // namespace textSys
} // namespace vkh
