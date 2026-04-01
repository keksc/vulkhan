#include "dungeonGenerator.hpp"

#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "rng.hpp"
#include "vkh/scene.hpp"
#include "vkh/systems/entity/entities.hpp"

#include <memory>
#include <random>
#include <vector>

void generateDungeon(vkh::EngineContext &context, vkh::EntitySys &entitySys,
                     std::vector<vkh::EntitySys::Entity> &entities,
                     const DungeonConfig &config) {

  auto westWingAssets = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
      context, "models/westwingassets.glb", entitySys.textureSetLayout);

  std::vector<std::vector<CellType>> grid(
      config.gridSize.x,
      std::vector<CellType>(config.gridSize.y, CellType::Empty));

  std::uniform_int_distribution<> roomPosX(1, config.gridSize.x -
                                                  config.maxRoomSize.x - 1);
  std::uniform_int_distribution<> roomPosY(1, config.gridSize.y -
                                                  config.maxRoomSize.y - 1);
  std::uniform_int_distribution<> roomSizeX(config.minRoomSize.x,
                                            config.maxRoomSize.x);
  std::uniform_int_distribution<> roomSizeY(config.minRoomSize.y,
                                            config.maxRoomSize.y);

  std::vector<Room> rooms;

  // The entrance point to the dungeon
  glm::ivec2 connectorPos{0, 0};
  float distanceToConnector = std::numeric_limits<float>::max();
  int closestRoomToConnectorIndex = -1;

  for (int i = 0; i < config.maxRooms; i++) {
    bool placed = false;
    for (int attempts = 10; attempts > 0; attempts--) {
      glm::ivec2 pos{roomPosX(rng), roomPosY(rng)};
      glm::ivec2 size{roomSizeX(rng), roomSizeY(rng)};

      bool overlap = false;
      for (const auto &room : rooms) {
        if (!(pos.x + size.x + 1 < room.topLeft.x ||
              pos.x > room.topLeft.x + room.size.x + 1 ||
              pos.y + size.y + 1 < room.topLeft.y ||
              pos.y > room.topLeft.y + room.size.y + 1)) {
          overlap = true;
          break;
        }
      }
      if (overlap)
        continue;

      Room newRoom{pos, size, Room::Type::Normal};

      glm::vec2 mid = glm::vec2(newRoom.topLeft) + glm::vec2(newRoom.size) / 2.0f;
      float d = glm::distance(mid, glm::vec2(connectorPos));
      if (d < distanceToConnector) {
        distanceToConnector = d;
        closestRoomToConnectorIndex = static_cast<int>(rooms.size());
      }

      if (i == config.maxRooms - 1) {
        newRoom.type = Room::Type::Boss;
      } else if (i % 3 == 0) {
        newRoom.type = Room::Type::Treasure;
      }

      for (int x = pos.x; x < pos.x + size.x; x++) {
        for (int y = pos.y; y < pos.y + size.y; y++) {
          grid[x][y] = CellType::Room;
        }
      }
      rooms.push_back(newRoom);
      placed = true;
      break;
    }
  }

  if (closestRoomToConnectorIndex != -1) {
    glm::ivec2 p1 = connectorPos;
    glm::ivec2 p2 = rooms[closestRoomToConnectorIndex].topLeft +
                    rooms[closestRoomToConnectorIndex].size / 2;

    int x = p1.x;
    int y = p1.y;
    
    grid[x][y] = CellType::Connector;

    while (x != p2.x) {
      x += (p2.x > x) ? 1 : -1;
      if (grid[x][y] == CellType::Empty)
        grid[x][y] = CellType::Corridor;
    }
    while (y != p2.y) {
      y += (p2.y > y) ? 1 : -1;
      if (grid[x][y] == CellType::Empty)
        grid[x][y] = CellType::Corridor;
    }
  }

  for (size_t i = 0; i < rooms.size() - 1; i++) {
    glm::ivec2 p1 = rooms[i].topLeft + rooms[i].size / 2;
    glm::ivec2 p2 = rooms[i + 1].topLeft + rooms[i + 1].size / 2;

    int x = p1.x;
    int y = p1.y;
    while (x != p2.x) {
      x += (p2.x > x) ? 1 : -1;
      if (grid[x][y] == CellType::Empty)
        grid[x][y] = CellType::Corridor;
    }
    while (y != p2.y) {
      y += (p2.y > y) ? 1 : -1;
      if (grid[x][y] == CellType::Empty)
        grid[x][y] = CellType::Corridor;
    }
  }

  glm::quat rotNorth = glm::quat(glm::vec3(0, 0, 0));
  glm::quat rotSouth = glm::quat(glm::vec3(0, glm::pi<float>(), 0));
  glm::quat rotEast = glm::quat(glm::vec3(0, glm::half_pi<float>(), 0));
  glm::quat rotWest = glm::quat(glm::vec3(0, -glm::half_pi<float>(), 0));
  glm::quat defaultRot = glm::quat(1, 0, 0, 0);

  for (int x = 0; x < config.gridSize.x; x++) {
    for (int y = 0; y < config.gridSize.y; y++) {
      if (grid[x][y] == CellType::Empty)
        continue;

      glm::vec3 tileCenter{static_cast<float>(x), 0.f, static_cast<float>(y)};

      entities.emplace_back(vkh::EntitySys::Transform{.position = tileCenter},
                            vkh::EntitySys::RigidBody{}, westWingAssets,
                            RoomModel::Floor);
      entities.emplace_back(vkh::EntitySys::Transform{.position = tileCenter},
                            vkh::EntitySys::RigidBody{}, westWingAssets,
                            RoomModel::Ceiling);

      auto isWalkable = [&](int nx, int ny) {
          if (nx < 0 || nx >= config.gridSize.x || ny < 0 || ny >= config.gridSize.y) 
              return false;
          return grid[nx][ny] != CellType::Empty;
      };

      // FIX: Only skip the West wall (x-1) for the entrance hole. 
      // This prevents the South wall from also being skipped at (0,0).
      bool isEntranceHole = (grid[x][y] == CellType::Connector);

      // Left Wall (West) - Pierced if this is the entrance
      if (!isWalkable(x - 1, y) && !isEntranceHole) {
        entities.emplace_back(vkh::EntitySys::Transform{.position = tileCenter + glm::vec3(-0.5f, 0.f, 0.f),
                                                        .orientation = rotWest},
                              vkh::EntitySys::RigidBody{}, westWingAssets, RoomModel::Wall);
      }
      
      // Right Wall (East)
      if (!isWalkable(x + 1, y)) {
        entities.emplace_back(vkh::EntitySys::Transform{.position = tileCenter + glm::vec3(0.5f, 0.f, 0.f),
                                                        .orientation = rotEast},
                              vkh::EntitySys::RigidBody{}, westWingAssets, RoomModel::Wall);
      }

      // Bottom Wall (South) - No longer pierced by isEntranceHole
      if (!isWalkable(x, y - 1)) {
        entities.emplace_back(vkh::EntitySys::Transform{.position = tileCenter + glm::vec3(0.f, 0.f, -0.5f),
                                                        .orientation = rotSouth},
            vkh::EntitySys::RigidBody{}, westWingAssets, RoomModel::Wall);
      }

      // Top Wall (North)
      if (!isWalkable(x, y + 1)) {
        entities.emplace_back(vkh::EntitySys::Transform{.position = tileCenter + glm::vec3(0.f, 0.f, 0.5f),
                                                        .orientation = rotNorth},
            vkh::EntitySys::RigidBody{}, westWingAssets, RoomModel::Wall);
      }
    }
  }

  for (const auto &room : rooms) {
    glm::vec3 center{room.topLeft.x + room.size.x / 2.0f, 0.f,
                     room.topLeft.y + room.size.y / 2.0f};

    if (room.type == Room::Type::Treasure) {
      entities.emplace_back(vkh::EntitySys::Transform{.position = center, .orientation = defaultRot},
          vkh::EntitySys::RigidBody{}, westWingAssets, RoomModel::PropChest);
    } else if (room.type == Room::Type::Boss) {
      entities.emplace_back(vkh::EntitySys::Transform{.position = center, .orientation = defaultRot},
          vkh::EntitySys::RigidBody{}, westWingAssets, RoomModel::PropAltar);
    }
  }

  // Position camera outside the West entrance, looking East (+X)
  context.camera.position = glm::vec3(-1.5f, 0.5f, 0.0f);
}
