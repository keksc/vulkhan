#pragma once

#include "vkh/engineContext.hpp"
#include "vkh/systems/entity/entities.hpp"
#include <glm/glm.hpp>

#include <vector>

enum class RoomType {
  Normal,
  Spawn,
  Boss,
  Treasure,
};

struct DungeonConfig {
  int maxRooms = 15;
  glm::ivec2 gridSize = {50, 50};
  glm::ivec2 minRoomSize = {4, 4};
  glm::ivec2 maxRoomSize = {10, 10};
};

void generateDungeon(vkh::EngineContext &context, vkh::EntitySys &entitySys,
                     std::vector<vkh::EntitySys::Entity> &entities,
                     const DungeonConfig &config = DungeonConfig{});
