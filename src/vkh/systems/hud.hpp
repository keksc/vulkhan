#pragma once

#include "../engineContext.hpp"

namespace vkh {
namespace hudSys {
void init(EngineContext &context);
void cleanup(EngineContext &context);

struct Vertex {
  glm::vec2 position{};
  glm::vec3 color{};
};
struct DrawInfo {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
};
struct Element {
  virtual DrawInfo &getDrawInfo(uint32_t baseIndex) = 0;
};

struct Rect : public Element {
  Rect(glm::vec2 position, glm::vec2 size, glm::vec3 color)
      : position{position}, size{size}, color{color} {};
  glm::vec2 position{};
  glm::vec2 size{};
  glm::vec3 color{};
  std::vector<Rect> children{};
  DrawInfo drawInfo{};
  DrawInfo &getDrawInfo(uint32_t baseIndex) override {
    // TODO: this might be optimizable when nothing changes, maybe add a
    // "changed" flag
    drawInfo.vertices.clear();
    float x0 = position.x;
    float x1 = x0 + size.x;
    float y0 = position.y;
    float y1 = y0 + size.y;
    drawInfo.vertices.push_back({{x0, y0}, color});
    drawInfo.vertices.push_back({{x1, y0}, color});
    drawInfo.vertices.push_back({{x1, y1}, color});
    drawInfo.vertices.push_back({{x0, y1}, color});

    drawInfo.indices.clear();
    drawInfo.indices.push_back(baseIndex + 0);
    drawInfo.indices.push_back(baseIndex + 1);
    drawInfo.indices.push_back(baseIndex + 2);
    drawInfo.indices.push_back(baseIndex + 0);
    drawInfo.indices.push_back(baseIndex + 2);
    drawInfo.indices.push_back(baseIndex + 3);

    return drawInfo;
  };
};
struct Text : public Element {
  Text(const std::string &content) : content{content} {}
  std::string content;
};

void update(EngineContext &context,
            std::vector<std::unique_ptr<Element>> &content);
void render(EngineContext &context);
}; // namespace hudSys
} // namespace vkh
