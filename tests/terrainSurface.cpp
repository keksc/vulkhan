#include "../src/vkh/systems/terrain/terrainChunks.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <random>

using namespace vkh::terrain;

int main() {
  ChunkParams params;
  params.quadsPerChunk = 4;
  params.lod0CellSize = 1.5f;
  std::array<IndexLod, kIndexLodCount> ranges;
  auto indices = buildChunkIndices(params, ranges);
  std::mt19937 rng(4701);
  std::uniform_real_distribution<float> random(0, 1);
  size_t checked = 0;
  for (uint32_t lod = 0; lod < 4; ++lod) {
    const ChunkKey key{-2, 3, lod};
    std::vector<TerrainVertex> verts;
    Box bounds;
    generateChunk(
        params,
        [](glm::vec2 p, float) {
          return std::sin(p.x * .3f) * 3 + std::cos(p.y * .2f) * 5;
        },
        key, verts, bounds);
    std::vector<float> heights;
    for (auto v : verts)
      heights.push_back(v.pos.y);
    for (bool fitted : {false, true}) {
      auto edges = extractEdges(params, verts);
      if (fitted)
        for (auto &edge : edges)
          edge.x += 2.f;
      const uint32_t Q = params.quadsPerChunk, G = Q + 1;
      auto drawn = verts;
      if (fitted)
        for (uint32_t z = 0; z <= Q; ++z)
          for (uint32_t x = 0; x <= Q; ++x) {
            if (z == 0)
              drawn[z * G + x].pos.y = edges[x].x;
            else if (z == Q)
              drawn[z * G + x].pos.y = edges[G + x].x;
            else if (x == 0)
              drawn[z * G + x].pos.y = edges[2 * G + z].x;
            else if (x == Q)
              drawn[z * G + x].pos.y = edges[3 * G + z].x;
          }
      // Independently construct points on the triangles from the actual
      // mesh index buffer and barycentric weights, then query the sampler.
      for (uint32_t i = 0; i < ranges[0].count; i += 3) {
        const auto a = drawn[indices[i]].pos;
        const auto b = drawn[indices[i + 1]].pos;
        const auto c = drawn[indices[i + 2]].pos;
        for (int sample = 0; sample < 100; ++sample) {
          float u = random(rng), v = random(rng);
          if (u + v > 1) {
            u = 1 - u;
            v = 1 - v;
          }
          const auto p = a + (b - a) * u + (c - a) * v;
          const float actual =
              sampleChunkHeight(params, key, heights, {p.x, p.z},
                                fitted ? std::span<const glm::vec4>(edges)
                                       : std::span<const glm::vec4>{});
          assert(std::abs(actual - p.y) < .001f);
          ++checked;
        }
      }
      for (const auto &vertex : drawn) {
        const auto p = vertex.pos;
        const auto actual =
            sampleChunkHeight(params, key, heights, {p.x, p.z},
                              fitted ? std::span<const glm::vec4>(edges)
                                     : std::span<const glm::vec4>{});
        assert(std::abs(actual - p.y) < .001f);
      }
    }
  }
  std::cout << "Terrain surface: " << checked << " triangle samples checked\n";
}
