#pragma once
#include "../../sceneBuilder.hpp"
#include "rockGeometry.hpp"
#include "treeSys.hpp"
namespace vkh::rocks {
inline std::shared_ptr<Scene<TreeSys::Vertex>>
makeScene(EngineContext &context, vk::DescriptorSetLayout layout,
          uint32_t seed) {
  auto rock = generate(seed);
  std::vector<TreeSys::Vertex> vertices;
  vertices.reserve(rock.vertices.size());
  for (const auto &v : rock.vertices) {
    TreeSys::Vertex vertex{};
    vertex.pos = v.pos;
    vertex.normal = v.normal;
    vertex.color = {0.f, 1.f, 0.f, 1.f}; // rigid, no wind
    vertices.push_back(vertex);
  }
  SceneCreateInfo<TreeSys::Vertex> info{vertices, rock.indices};
  auto scene = std::make_shared<Scene<TreeSys::Vertex>>(context, info, layout);
  scene->materials[0].baseColorTextureIndex.reset();
  scene->materials[0].roughnessFactor = 0.8f;
  scene->materials[0].metallicFactor = glm::vec4(0.f);
  for (auto &mesh : scene->meshes) {
    mesh.aabb.min = rock.min;
    mesh.aabb.max = rock.max;
  }
  return scene;
}
} // namespace vkh::rocks
