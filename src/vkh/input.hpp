#pragma once

#include "systems/entity/entities.hpp"

#include <glm/glm.hpp>
#include <map>

#include <vector>

namespace vkh {
class EngineContext;
namespace input {
void init(EngineContext &context);
void update(EngineContext &context, std::vector<EntitySys::Entity> entities);
extern glm::dvec2 lastPos;
enum class Action {
  MoveForward,
  MoveBackward,
  MoveLeft,
  MoveRight,
  MoveUp,
  MoveDown,
  PlaceRect,
  PlaceText,
  PlaceLine,
  PlaceFreehand,
};

extern std::map<Action, unsigned int> keybinds;

std::string getKeyName(int key);

enum class EventType {
  MouseButton,
  Key,
  CursorPosition,
  Character,
  Drop,
  WindowFocus,
  Scroll,
};

template <EventType E> struct EventTraits;

template <> struct EventTraits<EventType::MouseButton> {
  using CallbackType = std::function<bool(int button, int action, int mods)>;
};

template <> struct EventTraits<EventType::Key> {
  using CallbackType =
      std::function<bool(int key, int scancode, int action, int mods)>;
};

template <> struct EventTraits<EventType::CursorPosition> {
  using CallbackType = std::function<bool(double xpos, double ypos)>;
};

template <> struct EventTraits<EventType::Character> {
  using CallbackType = std::function<bool(unsigned int codepoint)>;
};

template <> struct EventTraits<EventType::Drop> {
  using CallbackType = std::function<bool(int count, const char **paths)>;
};
template <> struct EventTraits<EventType::WindowFocus> {
  using CallbackType = std::function<bool(int focused)>;
};
template <> struct EventTraits<EventType::Scroll> {
  using CallbackType = std::function<bool(double xoffset, double yoffset)>;
};
}; // namespace input
} // namespace vkh
