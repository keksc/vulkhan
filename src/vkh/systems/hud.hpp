#pragma once

#include <fmt/format.h>
#include <glm/ext/quaternion_common.hpp>

#include <functional>
#include <memory>
#include <unordered_map>

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
class Element {
public:
  virtual ~Element() = default;
  virtual void updateDrawInfo(uint32_t baseIndex) = 0;
  DrawInfo drawInfo{};
  virtual void addChild(std::shared_ptr<Element> element) = 0;

protected:
  std::vector<std::shared_ptr<Element>> children;
  friend void addElementToDraw(std::shared_ptr<Element> element);
};

class Rect : public Element {
public:
  Rect(glm::vec2 position, glm::vec2 size, glm::vec3 color)
      : position{position}, size{size}, color{color} {};
  glm::vec2 position{};
  glm::vec2 size{};
  glm::vec3 color{};
  void updateDrawInfo(uint32_t baseIndex) override {
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
  };
  void addChild(std::shared_ptr<Element> element) override {
    auto child = std::dynamic_pointer_cast<Rect>(element);
    child->position = position + child->position * size;
    child->size = child->size * size;
    children.push_back(child);
  }
};
class EventListener {
public:
  virtual void onMouseButton(int button, int action, int mods) {};

  EventListener() {
    mouseButtonCallbacks[this] = [this](int button, int action, int mods) {
      onMouseButton(button, action, mods);
    };
  }
  ~EventListener() { mouseButtonCallbacks.erase(this); }
  static std::unordered_map<EventListener *, std::function<void(int, int, int)>>
      mouseButtonCallbacks;
  // maybe friend the input's functions so that it doesnt bloat the
  // autocompletion
};
class Button : public Rect, public EventListener {
public:
  Button(glm::vec2 position, glm::vec2 size, glm::vec3 color)
      : Rect(position, size, color), EventListener() {}
  void addChild(std::shared_ptr<Element> element) override {
    Rect::addChild(element);
    auto child = std::dynamic_pointer_cast<Button>(children.back());
  }
  void onMouseButton(int button, int action, int mods) override {
    fmt::println("mouse btn pressed from callback !!");
  }
  ~Button() {}
};
class Text : public Element {
public:
  Text(const std::string &content) : content{content} {}
  std::string content;
  void updateDrawInfo(uint32_t baseIndex) override {}
};

void update(EngineContext &context,
            std::vector<std::shared_ptr<Element>> &content);
void render(EngineContext &context);
}; // namespace hudSys
} // namespace vkh
