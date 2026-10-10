#include "../src/vkh/systems/tree/firGenerator.hpp"
#include <cassert>
#include <iostream>
int main() {
  for (bool broadleaf : {false, true})
    for (uint32_t seed : {1u, 2u, 977u}) {
      vkh::fir::Params p;
      p.seed = seed;
      p.broadleaf = broadleaf;
      if (broadleaf) {
        p.crownStart = .35f;
        p.maxCrownRadius = 3.8f;
        p.tiers = 14;
      }
      auto high = vkh::fir::generate(p);
      auto lowp = p;
      lowp.tiers = 8;
      lowp.fanCards = 1;
      lowp.cardSegments = 2;
      lowp.trunkSides = 5;
      lowp.trunkRings = 3;
      auto low = vkh::fir::generate(lowp);
      assert(high.parts.size() == 2 &&
             high.parts[0].indexCount > p.trunkRings * p.trunkSides * 6);
      assert(low.indices.size() < high.indices.size());
      for (const auto &part : high.parts) {
        assert(part.indexOffset + part.indexCount <= high.indices.size());
        for (uint32_t i = part.indexOffset;
             i < part.indexOffset + part.indexCount; i++) {
          const auto &v = high.vertices[high.indices[i]];
          assert(glm::all(glm::greaterThanEqual(v.pos, part.aabbMin)));
          assert(glm::all(glm::lessThanEqual(v.pos, part.aabbMax)));
          assert(std::isfinite(v.normal.x) && std::isfinite(v.normal.y) &&
                 std::isfinite(v.normal.z));
          assert(std::abs(glm::length(v.normal) - 1) < 1e-4);
        }
      }
      auto &wood = high.parts[0];
      for (uint32_t i = wood.indexOffset;
           i < wood.indexOffset + wood.indexCount; i += 3) {
        auto &a = high.vertices[high.indices[i]],
             &b = high.vertices[high.indices[i + 1]],
             &c = high.vertices[high.indices[i + 2]];
        assert(glm::dot(glm::cross(b.pos - a.pos, c.pos - a.pos),
                        a.normal + b.normal + c.normal) > 0);
      }
    }
  auto needles = vkh::fir::makeAtlas(128, false),
       leaves = vkh::fir::makeAtlas(128, true);
  assert(needles.size() == 128 * 128 * 4 && leaves.size() == needles.size() &&
         needles != leaves);
  std::cout << "Conifer/broadleaf branches, normals, bounds, LOD reduction and "
               "distinct leaf atlases pass.\n";
}
