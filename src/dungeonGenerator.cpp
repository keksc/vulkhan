#include "dungeonGenerator.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <random>
#include <vector>

#include "vkh/systems/entity/entities.hpp"

#include "rng.hpp"

enum CellType { Empty, Room, Corridor };
enum RoomModel {
  Bottom = 5,
  Front = 0,
  Left = 1,
  Back = 2,
  Right = 3,
  Top = 4
};

void generateDungeon(vkh::EngineContext &context, vkh::EntitySys &entitySys,
                     std::vector<vkh::EntitySys::Entity> &entities) {
  auto westWingAssets = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
      context, "models/westwingassets.glb", entitySys.textureSetLayout);

  glm::ivec2 maxRoomSize{10, 10};
  int maxRooms = 10;
  glm::ivec2 gridSize{50, 50};
  std::vector<std::vector<CellType>> grid(
      gridSize.x, std::vector<CellType>(gridSize.y, CellType::Empty));

  std::uniform_int_distribution<> roomPosX(0, gridSize.x - 2);
  std::uniform_int_distribution<> roomPosY(0, gridSize.y - 2);
  std::uniform_int_distribution<> roomSizeX(3, maxRoomSize.x);
  std::uniform_int_distribution<> roomSizeY(3, maxRoomSize.y);

  struct Room {
    glm::ivec2 topLeft;
    glm::ivec2 size;
  };
  std::vector<Room> rooms;

  for (int i = 0; i < maxRooms; i++) {
    bool placed = false;
    for (int attempts = 10; attempts > 0; attempts--) {
      glm::ivec2 pos{roomPosX(rng), roomPosY(rng)};
      glm::ivec2 size{roomSizeX(rng), roomSizeY(rng)};

      if (pos.x + size.x >= gridSize.x || pos.y + size.y >= gridSize.y) {
        continue;
      }

      bool overlap = false;
      for (const auto &room : rooms) {
        if (!(pos.x + size.x < room.topLeft.x ||
              pos.x > room.topLeft.x + room.size.x ||
              pos.y + size.y < room.topLeft.y ||
              pos.y > room.topLeft.y + room.size.y)) {
          overlap = true;
          break;
        }
      }
      if (overlap) {
        continue;
      }

      // Place room in grid
      for (int x = pos.x; x < pos.x + size.x; x++) {
        for (int y = pos.y; y < pos.y + size.y; y++) {
          grid[x][y] = CellType::Room;
        }
      }
      rooms.emplace_back(pos, size);
      placed = true;
      break;
    }
    if (!placed) {
      break;
    }
  }

  // Connect rooms with corridors
  for (size_t i = 0; i < rooms.size() - 1; i++) {
    auto &room1 = rooms[i];
    auto &room2 = rooms[i + 1];

    // Get center points of rooms
    glm::ivec2 p1 = room1.topLeft + room1.size / 2;
    glm::ivec2 p2 = room2.topLeft + room2.size / 2;

    // Add corridor (horizontal then vertical)
    int x = p1.x;
    int y = p1.y;
    while (x != p2.x) {
      if (x >= 0 && x < gridSize.x && y >= 0 && y < gridSize.y) {
        if (grid[x][y] == CellType::Empty) {
          grid[x][y] = CellType::Corridor;
        }
      }
      x += (p2.x > x) ? 1 : -1;
    }
    while (y != p2.y) {
      if (x >= 0 && x < gridSize.x && y >= 0 && y < gridSize.y) {
        if (grid[x][y] == CellType::Empty) {
          grid[x][y] = CellType::Corridor;
        }
      }
      y += (p2.y > y) ? 1 : -1;
    }
  }

  for (int x = 0; x < gridSize.x; x++) {
    for (int y = 0; y < gridSize.y; y++) {
      if (grid[x][y] == CellType::Empty)
        continue;
      glm::vec3 pos{x, 0.f, y};
      entities.emplace_back(
          vkh::EntitySys::Transform{.position = pos},
          vkh::EntitySys::RigidBody{}, westWingAssets, RoomModel::Bottom);

      entities.emplace_back(
          vkh::EntitySys::Transform{.position = pos},
          vkh::EntitySys::RigidBody{}, westWingAssets, RoomModel::Top);

      if (x == 0 || grid[x - 1][y] == CellType::Empty) {
        entities.emplace_back(

            vkh::EntitySys::Transform{.position = pos},
            vkh::EntitySys::RigidBody{}, westWingAssets, RoomModel::Front);
      }
      if (x == gridSize.x - 1 || grid[x + 1][y] == CellType::Empty) {
        entities.emplace_back(

            vkh::EntitySys::Transform{.position = pos},
            vkh::EntitySys::RigidBody{}, westWingAssets, RoomModel::Back);
      }
      if (y == 0 || grid[x][y - 1] == CellType::Empty) {
        entities.emplace_back(

            vkh::EntitySys::Transform{.position = pos},
            vkh::EntitySys::RigidBody{}, westWingAssets, RoomModel::Right);
      }
      if (y == gridSize.y - 1 || grid[x][y + 1] == CellType::Empty) {
        entities.emplace_back(
            vkh::EntitySys::Transform{.position = pos},
            vkh::EntitySys::RigidBody{}, westWingAssets, RoomModel::Left);
      }
    }
  }
}
