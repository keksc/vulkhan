#include "../src/vkh/systems/terrain/terrainChunks.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <unordered_set>
using namespace vkh::terrain;
int main() {
  ChunkParams p;
  p.lod0CellSize = 1;
  p.quadsPerChunk = 16;
  p.lodCount = 5;
  p.viewDistance = 550;
  size_t comparisons = 0, chunks = 0;
  double stitchTime = 0;
  std::array<IndexLod, kIndexLodCount> indexLods;
  auto indices = buildChunkIndices(p, indexLods);
  for (auto index : indices)
    assert(index < verticesPerChunk(p));
  assert(indexLods[0].count == p.quadsPerChunk * p.quadsPerChunk * 6);
  for (uint32_t budget : {0u, 96u}) {
    p.leafBudget = budget;
    for (glm::vec3 cam : {glm::vec3(10, 30, 10), glm::vec3(-250, 15, 87),
                          glm::vec3(64, 80, -64)}) {
      std::vector<ChunkKey> keys;
      selectLeaves(p, cam, keys);
      assert(budget == 0 || keys.size() <= budget);
      // A fully resident selection must draw exactly that cut, even if older
      // parent chunks remain cached during a streaming transition.
      std::unordered_set<ChunkKey, ChunkKeyHash> resident(keys.begin(),
                                                          keys.end());
      for (auto k : keys) {
        while (k.lod + 1 < p.lodCount) {
          k.x = static_cast<int32_t>(std::floor(k.x / 2.0));
          k.z = static_cast<int32_t>(std::floor(k.z / 2.0));
          ++k.lod;
          resident.insert(k);
        }
      }
      std::vector<ChunkKey> drawn;
      buildDrawList(
          p, cam, [&](const ChunkKey &k) { return resident.contains(k); },
          drawn);
      assert(drawn.size() == keys.size());
      for (const auto &k : drawn)
        assert(std::find(keys.begin(), keys.end(), k) != keys.end());
      chunks += keys.size();
      std::vector<EdgeSamples> samples;
      for (const auto &k : keys) {
        std::vector<TerrainVertex> verts;
        Box box;
        generateChunk(
            p,
            [](glm::vec2 xz, float cell) {
              return 30.f +
                     12.f * std::sin(xz.x * .025f) * std::cos(xz.y * .032f) +
                     cell * .3f;
            },
            k, verts, box);
        assert(verts.size() ==
               size_t(p.quadsPerChunk + 1) * (p.quadsPerChunk + 1));
        samples.push_back(extractEdges(p, verts));
      }
      std::vector<const EdgeSamples *> refs;
      for (auto &s : samples)
        refs.push_back(&s);
      const uint32_t G = p.quadsPerChunk + 1;
      for (uint32_t step : {1u, 2u, 4u}) {
        std::vector<glm::vec4> out;
        auto start = std::chrono::steady_clock::now();
        stitchEdges(p, keys, refs, step, out);
        stitchTime += std::chrono::duration<double>(
                          std::chrono::steady_clock::now() - start)
                          .count();
        for (size_t i = 0; i < keys.size(); ++i) {
          auto mn = chunkMin(p, keys[i]);
          float size = chunkSize(p, keys[i].lod),
                cell = chunkCellSize(p, keys[i].lod);
          for (size_t j = i + 1; j < keys.size(); ++j) {
            auto bn = chunkMin(p, keys[j]);
            float bs = chunkSize(p, keys[j].lod),
                  bc = chunkCellSize(p, keys[j].lod);
            for (uint32_t e = 0; e < 4; ++e)
              for (uint32_t n = 0; n < G; n += step) {
                glm::vec2 pt =
                    mn + (e < 2 ? glm::vec2(n * cell, e == 0 ? 0 : size)
                                : glm::vec2(e == 2 ? 0 : size, n * cell));
                if (pt.x < bn.x || pt.y < bn.y || pt.x > bn.x + bs ||
                    pt.y > bn.y + bs)
                  continue;
                uint32_t be;
                float off;
                if (pt.y == bn.y || pt.y == bn.y + bs) {
                  be = pt.y == bn.y ? 0 : 1;
                  off = (pt.x - bn.x) / bc;
                } else if (pt.x == bn.x || pt.x == bn.x + bs) {
                  be = pt.x == bn.x ? 2 : 3;
                  off = (pt.y - bn.y) / bc;
                } else
                  continue;
                uint32_t lo = std::min(uint32_t(off / step) * step,
                                       p.quadsPerChunk),
                         hi = std::min(lo + step, p.quadsPerChunk);
                float t = lo == hi ? 0 : (off - lo) / float(hi - lo);
                auto other = glm::mix(out[(j * 4 + be) * G + lo],
                                      out[(j * 4 + be) * G + hi], t);
                auto actual = out[(i * 4 + e) * G + n];
                if (glm::length(actual - other) > 1e-4) {
                  std::cerr << "seam mismatch " << i << ' ' << j << " step "
                            << step << " error " << glm::length(actual - other)
                            << '\n';
                  return 1;
                }
                ++comparisons;
              }
          }
        }
      }
    }
  }
  // Many lake zones used to overfill the fixed GPU pool at startup.
  ChunkParams startup;
  startup.leafBudget = 384;
  RefineZones zones{{glm::vec2(-1500), glm::vec2(1500)}};
  std::vector<ChunkKey> selected;
  selectLeaves(startup, glm::vec3(0, 30, 0), selected, &zones);
  assert(!selected.empty() && selected.size() <= startup.leafBudget);
  assert(comparisons > 0);
  std::cout << "Validated " << comparisons << " seam samples across " << chunks
            << " chunks at all three render resolutions. Stitching: "
            << stitchTime << "s total.\n";
}
