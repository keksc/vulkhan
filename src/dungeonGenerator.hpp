#pragma once

#include "vkh/engineContext.hpp"
#include "vkh/systems/entity/entities.hpp"
#include <glm/glm.hpp>

#include <vector>

enum class CellType {
  Empty,
  Room,
  Corridor,
  Connector, // Will have an open side
};

enum RoomModel {
  Wall = 0,
  Ceiling = 1,
  Floor = 2,
  PropChest = 3,
  PropAltar = 4,
};

struct Room {
  glm::ivec2 topLeft;
  glm::ivec2 size;
  enum class Type {
    Normal,
    Boss,
    Treasure,
  };
  Type type = Type::Normal;
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
