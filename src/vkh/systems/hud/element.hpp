#pragma once

#include "../../input.hpp"
#include "drawInfo.hpp"
#include <any>
#include <functional>
#include <magic_enum/magic_enum.hpp>
#include <unordered_map>
#include <vector>

namespace vkh {
namespace hud {
class View;
class Element {
public:
  Element(View &view, Element *parent, glm::vec2 position, glm::vec2 size);
  Element(const Element &) = delete;
  Element &operator=(const Element &) = delete;

  virtual ~Element();

  template <typename T, typename... Args>
  std::shared_ptr<T> addChild(Args &&...args) {
    auto element = std::make_shared<T>(view, this, std::forward<Args>(args)...);
    children.emplace_back(element);
    return element;
  }

  template <typename T, typename... Args>
  std::shared_ptr<T>
  insertChild(std::vector<std::shared_ptr<Element>>::iterator pos,
              Args &&...args) {
    auto element = std::make_shared<T>(view, this, std::forward<Args>(args)...);
    children.insert(pos, element);
    return element;
  }

  virtual bool isPositionInside(const glm::vec2 &pos);

  bool isCursorInside();

  virtual void addToDrawInfo(DrawInfo &drawInfo, float depth);

  template <input::EventType E>
  void addEventHandler(typename input::EventTraits<E>::CallbackType handler) {
    eventHandlers[E].push_back(std::any(std::move(handler)));
  }

  template <input::EventType E, typename... Args>
  bool dispatchEvent(Args &&...args) {
    for (auto &child : children) {
      if (child->dispatchEvent<E>(std::forward<Args>(args)...)) {
        return true;
      }
    }
    return handleEvent<E>(std::forward<Args>(args)...);
  }

  // Virtual handlers for subclasses to override
  virtual bool handleMouseButton(int button, int action, int mods);
  virtual bool handleKey(int key, int scancode, int action, int mods);
  virtual bool handleCursorPosition(double xpos, double ypos);
  virtual bool handleCharacter(unsigned int codepoint);
  virtual bool handleDrop(int count, const char **paths);
  virtual bool handleScroll(double xoffset, double yoffset);

  std::vector<std::shared_ptr<Element>> children;

  View &view;

  virtual void setPosition(glm::vec2 newPosition);
  virtual void setAbsolutePosition(glm::vec2 newPosition);
  glm::vec2 getPosition() const;
  glm::vec2 getAbsolutePosition() const;

  virtual void setSize(glm::vec2 newSize);
  virtual void setAbsoluteSize(glm::vec2 newSize);
  glm::vec2 getSize() const;
  glm::vec2 getAbsoluteSize() const;

  virtual void updateAbsolute();

protected:
  Element *parent;

  glm::vec2 relPosition{};
  glm::vec2 relSize{};
  glm::vec2 absPos{};
  glm::vec2 absSize{};

private:
  // Store handlers using std::any for type safety
  std::unordered_map<input::EventType, std::vector<std::any>,
                     std::hash<input::EventType>>
      eventHandlers;

  // Handle an event by calling virtual method and registered handlers
  template <input::EventType E, typename... Args>
  bool handleEvent(Args &&...args) {
    bool handled = false;
    // Call virtual handler
    if constexpr (E == input::EventType::MouseButton) {
      handled = handleMouseButton(std::forward<Args>(args)...);
    } else if constexpr (E == input::EventType::Key) {
      handled = handleKey(std::forward<Args>(args)...);
    } else if constexpr (E == input::EventType::CursorPosition) {
      handled = handleCursorPosition(std::forward<Args>(args)...);
    } else if constexpr (E == input::EventType::Character) {
      handled = handleCharacter(std::forward<Args>(args)...);
    } else if constexpr (E == input::EventType::Drop) {
      handled = handleDrop(std::forward<Args>(args)...);
    } else if constexpr (E == input::EventType::Scroll) {
      handled = handleScroll(std::forward<Args>(args)...);
    }

    // Call registered handlers
    auto it = eventHandlers.find(E);
    if (it != eventHandlers.end()) {
      for (const auto &handler : it->second) {
        // Cast to the correct callback type
        if (auto *cb =
                std::any_cast<typename input::EventTraits<E>::CallbackType>(
                    &handler)) {
          if ((*cb)(std::forward<Args>(args)...)) {
            handled = true;
          }
        }
      }
    }
    return handled;
  }
};

} // namespace hud
} // namespace vkh
