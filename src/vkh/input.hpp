#pragma once

#include "engineContext.hpp"
#include "systems/entity/entities.hpp"

namespace vkh {
namespace input {
void init(EngineContext &context);
void update(EngineContext &context, EntitySys &entitySys);
extern glm::dvec2 lastPos;
enum Action {
  MoveForward,
  MoveBackward,
  MoveLeft,
  MoveRight,
  PlaceRect,
  PlaceText,
  PlaceLine
};
extern std::unordered_map<Action, unsigned int> keybinds;

enum class EventType { MouseButton, Key, CursorPosition, Character, Drop };

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
}; // namespace input
} // namespace vkh
