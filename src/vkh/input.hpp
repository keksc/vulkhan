#pragma once

#include "engineContext.hpp"
#include "systems/entity/entities.hpp"

namespace vkh {
namespace input {
void init(EngineContext &context);
void update(EngineContext &context, EntitySys &entitySys);
extern glm::dvec2 lastPos;
enum Action { MoveForward, MoveBackward, MoveLeft, MoveRight, PlaceRect, PlaceText, PlaceLine };
extern std::unordered_map<Action, unsigned int> keybinds;
}; // namespace input
} // namespace vkh
