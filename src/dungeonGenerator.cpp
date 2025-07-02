#include "dungeonGenerator.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <random>
#include <vector>

#include "vkh/systems/entity/entities.hpp"

enum class CellType { Empty, Room, Hallway };

void generateDungeon(vkh::EntitySys &entitySys) {
  std::mt19937 rng{std::random_device{}()};
  // glm::ivec2 maxRoomSize{10, 10};
  // int maxRooms = 10;
  // glm::ivec2 gridSize{50, 50};
  // std::vector<std::vector<CellType>> grid(
  //     gridSize.x, std::vector<CellType>(gridSize.y, CellType::Empty));
  //
  // std::random_device rd;
  // std::mt19937 rng(rd());
  // std::uniform_int_distribution<> roomPosX(0, gridSize.x);
  // std::uniform_int_distribution<> roomPosY(0, gridSize.y);
  // std::uniform_int_distribution<> roomSizeX(2, maxRoomSize.x);
  // std::uniform_int_distribution<> roomSizeY(2, maxRoomSize.y);
  //
  // for (int i = 0; i < maxRooms; i++) {
  //   glm::ivec2 pos = {roomPosX(rng), roomPosY(rng)};
  // }
  //
  // for (auto &row : grid) {
  //   for (auto &cell : row) {
  //     // cell = static_cast<CellType>(cellType(rng));
  //   }
  // }
  // for (const auto &row : grid) {
  //   for (const auto &cell : row) {
  //     char c;
  //     switch (cell) {
  //     case CellType::Empty:
  //       c = '.';
  //       break;
  //     case CellType::Room:
  //       c = 'R';
  //       break;
  //     case CellType::Hallway:
  //       c = 'H';
  //       break;
  //     }
  //     std::print("{}", c);
  //   }
  //   std::print("\n");
  // }

  auto westWingAssets = entitySys.createScene("models/westwingassets.glb");
  entitySys.entities.emplace_back(
      vkh::EntitySys::Transform{.position = {0.f, 0.f, 0.f},
                                .scale = glm::vec3(1.f)},
      vkh::EntitySys::RigidBody{}, westWingAssets);
}
