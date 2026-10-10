#include "../src/vkh/sceneBuilder.hpp"
#include "../src/vkh/swapChain.hpp"
#include "../src/vkh/systems/entity/entities.hpp"
#include "../src/vkh/systems/wolf/furGroom.hpp"

#include <cassert>
#include <print>

int main() {
  vkh::EngineContext context{};
  vkh::Scene<vkh::EntitySys::Vertex> scene(context, "models/wolf.glb");
  assert(scene.lodMeshGroups.size() == 3);
  uint32_t previous = UINT32_MAX;
  for (size_t level = 0; level < 3; ++level) {
    const auto &mesh = scene.meshes[scene.lodMeshGroups[level].at(0)];
    assert(mesh.primitives.size() == 1);
    const auto &skin = mesh.primitives[0];
    assert(skin.indexCount < previous);
    previous = skin.indexCount;
    assert(!skin.furParameters.empty() && !skin.furDirections.empty());
    const auto groom = vkh::buildSurfaceFurGroom<vkh::EntitySys::Vertex>(
        scene.vertices,
        std::span(scene.indices).subspan(skin.indexOffset, skin.indexCount),
        skin.furParameters, skin.furDirections);
    assert(groom.size() > 1000);
    for (const auto &bundle : groom)
      for (const auto &guide : bundle.guides) {
        assert(guide.normal.w >= 0 && guide.normal.w <= 1);
        assert(guide.position.w >= .2f && guide.position.w <= 1);
      }
    std::println("Engine loader LOD{}: {} skin triangles, {} surface patches",
                 level, skin.indexCount / 3, groom.size());
  }
  std::println("Engine surface-fur integration checks passed");
}
