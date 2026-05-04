#include "dungeonGenerator.hpp"

#include <glm/glm.hpp>
#include <magic_enum/magic_enum.hpp>

#include "rng.hpp"
#include "vkh/scene.hpp"
#include "vkh/systems/entity/entities.hpp"

#include <bitset>
#include <iostream>
#include <memory>
#include <stack>

enum RoomModel {
  Crate = 0,
  Pedestal = 1,
  Horse = 2,
  Brick = 3,
  Trap_empty = 4,
  Skull = 5,
  Stairs_SideCover = 6,
  Fence_90_Modular = 7,
  Barrel = 8,
  Fence_Straight_Modular = 9,
  Bag_Standing = 10,
  Chest_Top = 11,
  Chest_Base = 12,
  WallCover_Modular = 13,
  Arch_Door_bottompivot = 14,
  Wall_Modular = 15,
  Floor_BricksSeparate = 16,
  Trap_spikes = 17,
  Barrel_1 = 18,
  Banner_wall = 19,
  Trapdoor = 20,
  Chair = 21,
  Arch = 22,
  Chest_Top_1 = 23,
  Chest_Base_1 = 24,
  Spikes = 25,
  Bag_Coins = 26,
  Fence_End_Modular = 27,
  WoodFire = 28,
  Table_Big = 29,
  Coin_Pile = 30,
  Vase = 31,
  Trapdoor_open = 32,
  Column = 33,
  Bucket = 34,
  Table_Small = 35,
  Arch_Door = 36,
  Arch_bars = 37,
  Torch = 38,
  Floor_BricksSeparate2 = 39,
  Stairs_SideCoverWall = 40,
  Floor_Modular = 41,
  Decorative_Wall = 42,
  Stairs_Modular = 43,
  Pedestal2 = 44,
  Column2 = 45,
  Sword_WallMount = 46,
  Sword_big = 47,
  Sword = 48,
  Cobweb2 = 49,
  Pedestal_1 = 50,
  Cobweb = 51,
  Banner = 52,
};

enum Tile {
  Empty = 0, // Solid rock
  Floor = 1, // Open room
  Wall_N = 2,
  Wall_E = 3,
  Wall_S = 4,
  Wall_W = 5,
};

enum Direction { North, East, South, West };

struct WFC {
  static constexpr size_t TilePossibilitiesCount =
      magic_enum::enum_count<Tile>();
  using TilePossibilities = std::bitset<TilePossibilitiesCount>;
  std::vector<TilePossibilities> grid;
  std::stack<size_t> stack;
  size_t N{};
  size_t NSquared{};

  WFC(size_t N) : N{N}, NSquared{N * N} {
    grid.resize(NSquared);
    reset();
  }

  void reset() {
    for (auto &cell : grid)
      cell.set();
    while (!stack.empty())
      stack.pop();
  }

  size_t getIdx(glm::ivec2 pos) { return pos.y * N + pos.x; }

  glm::ivec2 getPos(size_t idx) {
    return glm::ivec2(static_cast<int>(idx % N), static_cast<int>(idx / N));
  }

  bool adjacencyConstraint(Tile self, Tile neighbor, Direction dir) {
    auto getEdge = [](Tile t, Direction d) -> bool {
      if (t == Floor)
        return true;
      if (t == Empty)
        return false;

      if (t == Wall_N)
        return (d != North);
      if (t == Wall_E)
        return (d != East);
      if (t == Wall_S)
        return (d != South);
      if (t == Wall_W)
        return (d != West);
      return false;
    };

    Direction opposite;
    if (dir == North)
      opposite = South;
    else if (dir == South)
      opposite = North;
    else if (dir == East)
      opposite = West;
    else if (dir == West)
      opposite = East;

    return getEdge(self, dir) == getEdge(neighbor, opposite);
  }

  std::optional<size_t> observe() {
    std::optional<size_t> bestCellIdx{};
    size_t minEntropy = TilePossibilitiesCount + 1;

    for (size_t i = 0; i < NSquared; i++) {
      size_t entropy = grid[i].count();
      if (entropy > 1) {
        if (entropy < minEntropy ||
            (entropy == minEntropy &&
             std::uniform_int_distribution<>(0, 1)(rng) == 0)) {
          minEntropy = entropy;
          bestCellIdx = i;
        }
      }
    }

    if (bestCellIdx.has_value()) {
      TilePossibilities &cell = grid[bestCellIdx.value()];
      std::vector<size_t> validIndices;

      for (int i = 0; i < TilePossibilitiesCount; ++i) {
        if (cell.test(i)) {
          int weight = 1;
          if (i == Tile::Floor)
            weight = 5;
          else if (i != Tile::Empty)
            weight = 2;

          for (int w = 0; w < weight; ++w)
            validIndices.push_back(i);
        }
      }

      int chosenIndex = validIndices[std::uniform_int_distribution<>(
          0, validIndices.size() - 1)(rng)];
      cell.reset();
      cell.set(chosenIndex);

      stack.push(bestCellIdx.value());
    }
    return bestCellIdx;
  }

  // false if contradiction is detected
  bool propagatePossibilities() {
    constexpr std::array<glm::ivec2, 4> dirs = {
        glm::ivec2{0, 1}, glm::ivec2{1, 0}, glm::ivec2{0, -1},
        glm::ivec2{-1, 0}};
    const std::array<Direction, 4> enumDirs = {North, East, South, West};

    while (!stack.empty()) {
      size_t idx = stack.top();
      stack.pop();

      glm::ivec2 pos = getPos(idx);
      TilePossibilities &currentPossibilities = grid[idx];

      for (size_t i = 0; i < 4; i++) {
        glm::ivec2 neighborPos = pos + dirs[i];
        if (neighborPos.x < 0 || neighborPos.x >= (int)N || neighborPos.y < 0 ||
            neighborPos.y >= (int)N) {
          continue;
        }

        size_t neighborIdx = getIdx(neighborPos);
        TilePossibilities &neighborPossibilities = grid[neighborIdx];

        if (neighborPossibilities.count() <= 1)
          continue;

        bool neighborChanged = false;
        for (size_t next = 0; next < TilePossibilitiesCount; next++) {
          if (!neighborPossibilities.test(next))
            continue;

          bool possible = false;
          for (size_t curr = 0; curr < TilePossibilitiesCount; curr++) {
            if (currentPossibilities.test(curr)) {
              if (adjacencyConstraint(static_cast<Tile>(curr),
                                      static_cast<Tile>(next), enumDirs[i])) {
                possible = true;
                break;
              }
            }
          }
          if (!possible) {
            neighborPossibilities.reset(next);
            neighborChanged = true;

            // contradiction
            if (neighborPossibilities.none()) {
              return false;
            }
          }
        }
        if (neighborChanged)
          stack.push(neighborIdx);
      }
    }
    return true;
  }

  bool runAttempt() {
    // size_t center = getIdx({(int)N / 2, (int)N / 2});
    // grid[center].reset();
    // grid[center].set(Tile::Floor);
    // stack.push(center);

    if (!propagatePossibilities())
      return false;

    while (true) {
      auto nextCell = observe();
      if (!nextCell.has_value()) {
        // Double check to make sure no cells are left uncollapsed with 0
        // possibilities
        for (const auto &cell : grid) {
          if (cell.none())
            return false;
        }
        return true; // We successfully collapsed the whole grid!
      }

      if (!propagatePossibilities()) {
        return false; // Contradiction during generation
      }
    }
  }

  void runWithRetries(int maxRetries = 50) {
    for (int i = 0; i < maxRetries; ++i) {
      reset();
      if (runAttempt()) {
        std::cout << "WFC Success after " << i << " retries.\n";
        return;
      }
    }
    std::cerr
        << "WFC FAILED: Reached max retries. Map will be empty or broken.\n";
  }
};

void generateDungeon(vkh::EngineContext &context, vkh::EntitySys &entitySys) {
  auto assets = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
      context, "models/dungeonAssets.glb", entitySys.texturesSetLayout);

  WFC wfc(15);
  wfc.runWithRetries(50);

  for (size_t i = 0; i < wfc.NSquared; ++i) {
    Tile t = Empty;
    for (size_t b = 0; b < wfc.TilePossibilitiesCount; ++b) {
      if (wfc.grid[i].test(b)) {
        t = static_cast<Tile>(b);
        break;
      }
    }

    if (t == Empty)
      continue;

    vkh::EntitySys::Entity ent{};
    ent.scene = assets;
    glm::ivec2 p = wfc.getPos(i);
    ent.transform.position = glm::vec3(p.x * 2.0f, 0.0f, p.y * 2.0f);

    if (t == Floor) {
      // 1. Spawn the Floor
      ent.meshIndex = RoomModel::Floor_Modular;
      entitySys.entities.push_back(ent);

      // 2. Random Prop Spawner (20% chance to spawn a prop on this floor)
      if (std::uniform_int_distribution<>(1, 100)(rng) <= 20) {
        vkh::EntitySys::Entity prop{};
        prop.scene = assets;
        prop.transform.position = ent.transform.position;

        // Add a random rotation to the prop so they don't all face the same way
        float randomRot =
            std::uniform_real_distribution<float>(0.0f, 360.0f)(rng);
        prop.transform.orientation = glm::rotate(
            glm::quat(1, 0, 0, 0), glm::radians(randomRot), {0, 1, 0});

        // Pick a random prop from your enum
        const std::array<RoomModel, 6> propTypes = {
            RoomModel::Crate, RoomModel::Barrel,      RoomModel::Coin_Pile,
            RoomModel::Chair, RoomModel::Table_Small, RoomModel::Skull};
        prop.meshIndex = propTypes[std::uniform_int_distribution<size_t>(
            0, propTypes.size() - 1)(rng)];

        entitySys.entities.push_back(prop);
      }

    } else {
      // Spawn Walls
      ent.meshIndex = RoomModel::Wall_Modular;
      if (t == Wall_E)
        ent.transform.orientation =
            glm::rotate(glm::quat(1, 0, 0, 0), glm::radians(90.0f), {0, 1, 0});
      if (t == Wall_S)
        ent.transform.orientation =
            glm::rotate(glm::quat(1, 0, 0, 0), glm::radians(180.0f), {0, 1, 0});
      if (t == Wall_W)
        ent.transform.orientation =
            glm::rotate(glm::quat(1, 0, 0, 0), glm::radians(270.0f), {0, 1, 0});

      entitySys.entities.push_back(ent);
    }
  }
  entitySys.markStructuralDirty();
}
