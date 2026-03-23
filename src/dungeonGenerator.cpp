#include "dungeonGenerator.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "rng.hpp"
#include "vkh/systems/entity/entities.hpp"

#include <memory>
#include <random>
#include <vector>

enum class CellType {
  Empty,
  Room,
  Corridor,
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
  RoomType type = RoomType::Normal;
};

void generateDungeon(vkh::EngineContext &context, vkh::EntitySys &entitySys,
                     std::vector<vkh::EntitySys::Entity> &entities,
                     const DungeonConfig &config) {

  auto westWingAssets = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
      context, "models/westwingassets.glb", entitySys.textureSetLayout);

  std::vector<std::vector<CellType>> grid(
      config.gridSize.x,
      std::vector<CellType>(config.gridSize.y, CellType::Empty));

  // Add buffers to prevent rooms from generating directly on the outer boundary
  std::uniform_int_distribution<> roomPosX(1, config.gridSize.x -
                                                  config.maxRoomSize.x - 1);
  std::uniform_int_distribution<> roomPosY(1, config.gridSize.y -
                                                  config.maxRoomSize.y - 1);
  std::uniform_int_distribution<> roomSizeX(config.minRoomSize.x,
                                            config.maxRoomSize.x);
  std::uniform_int_distribution<> roomSizeY(config.minRoomSize.y,
                                            config.maxRoomSize.y);

  std::vector<Room> rooms;

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

      Room newRoom{pos, size, RoomType::Normal};

      if (rooms.empty()) {
        newRoom.type = RoomType::Spawn;
      } else if (i == config.maxRooms - 1) {
        newRoom.type = RoomType::Boss;
      } else if (i % 3 == 0) {
        newRoom.type = RoomType::Treasure;
      }

      // Claim grid cells
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

  for (size_t i = 0; i < rooms.size() - 1; i++) {
    glm::ivec2 p1 = rooms[i].topLeft + rooms[i].size / 2;
    glm::ivec2 p2 = rooms[i + 1].topLeft + rooms[i + 1].size / 2;

    int x = p1.x;
    int y = p1.y;
    while (x != p2.x) {
      if (grid[x][y] == CellType::Empty)
        grid[x][y] = CellType::Corridor;
      x += (p2.x > x) ? 1 : -1;
    }
    while (y != p2.y) {
      if (grid[x][y] == CellType::Empty)
        grid[x][y] = CellType::Corridor;
      y += (p2.y > y) ? 1 : -1;
    }
  }

  // Define Wall Rotations (Note: You may need to swap angles depending on which
  // way your glTF wall faces by default)
  glm::quat rotNorth = glm::quat(glm::vec3(0, 0, 0));
  glm::quat rotSouth = glm::quat(glm::vec3(0, glm::radians(180.f), 0));
  glm::quat rotEast = glm::quat(glm::vec3(0, glm::radians(90.f), 0));
  glm::quat rotWest = glm::quat(glm::vec3(0, glm::radians(-90.f), 0));
  glm::quat defaultRot = glm::quat(1, 0, 0, 0);

  // Spawn entities
  for (int x = 0; x < config.gridSize.x; x++) {
    for (int y = 0; y < config.gridSize.y; y++) {
      if (grid[x][y] == CellType::Empty)
        continue;

      glm::vec3 tileCenter{x, 0.f, y};

      entities.emplace_back(vkh::EntitySys::Transform{.position = tileCenter},
                            vkh::EntitySys::RigidBody{}, westWingAssets,
                            RoomModel::Floor);
      entities.emplace_back(vkh::EntitySys::Transform{.position = tileCenter},
                            vkh::EntitySys::RigidBody{}, westWingAssets,
                            RoomModel::Ceiling);

      if (x == 0 || grid[x - 1][y] == CellType::Empty) {
        glm::vec3 wallPos = tileCenter + glm::vec3(-0.5f, 0.f, 0.f);
        entities.emplace_back(vkh::EntitySys::Transform{.position = wallPos,
                                                        .orientation = rotWest},
                              vkh::EntitySys::RigidBody{}, westWingAssets,
                              RoomModel::Wall);
      }
      if (x == config.gridSize.x - 1 || grid[x + 1][y] == CellType::Empty) {
        glm::vec3 wallPos = tileCenter + glm::vec3(0.5f, 0.f, 0.f);
        entities.emplace_back(vkh::EntitySys::Transform{.position = wallPos,
                                                        .orientation = rotEast},
                              vkh::EntitySys::RigidBody{}, westWingAssets,
                              RoomModel::Wall);
      }
      if (y == 0 || grid[x][y - 1] == CellType::Empty) {
        glm::vec3 wallPos = tileCenter + glm::vec3(0.f, 0.f, -0.5f);
        entities.emplace_back(
            vkh::EntitySys::Transform{.position = wallPos,
                                      .orientation = rotSouth},
            vkh::EntitySys::RigidBody{}, westWingAssets, RoomModel::Wall);
      }
      if (y == config.gridSize.y - 1 || grid[x][y + 1] == CellType::Empty) {
        glm::vec3 wallPos = tileCenter + glm::vec3(0.f, 0.f, 0.5f);
        entities.emplace_back(
            vkh::EntitySys::Transform{.position = wallPos,
                                      .orientation = rotNorth},
            vkh::EntitySys::RigidBody{}, westWingAssets, RoomModel::Wall);
      }
    }
  }

  glm::vec3 playerSpawnPos{0.f, 0.f, 0.f};

  for (const auto &room : rooms) {
    glm::vec3 center{room.topLeft.x + room.size.x / 2.0f, 0.f,
                     room.topLeft.y + room.size.y / 2.0f};

    switch (room.type) {
    case RoomType::Treasure:
      entities.emplace_back(
          vkh::EntitySys::Transform{.position = center,
                                    .orientation = defaultRot},
          vkh::EntitySys::RigidBody{}, westWingAssets, RoomModel::PropChest);
      break;
    case RoomType::Boss:
      entities.emplace_back(
          vkh::EntitySys::Transform{.position = center,
                                    .orientation = defaultRot},
          vkh::EntitySys::RigidBody{}, westWingAssets, RoomModel::PropAltar);
      break;
    case RoomType::Spawn:
      playerSpawnPos = center;
      playerSpawnPos.y = .5f;
      break;
    default:
      break;
    }
  }
  context.camera.position = playerSpawnPos;
}
