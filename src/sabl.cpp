#include "sabl.hpp"
#include <glm/gtc/constants.hpp>

enum Sandworm { Head = 0, Body = 1, Tail = 2 };
Sabl::Sabl(vkh::EngineContext &context, vkh::EntitySys &entitySys) {
  auto sandwormScene = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
      context, "models/sandworm.glb");
  leafSnakePieces.emplace_back(entitySys.entities.emplace_back(
      vkh::EntitySys::Transform{.position = {-5.f, 0.f, 0.f}},
      vkh::EntitySys::RigidBody{}, sandwormScene, Sandworm::Head));
  kekwSnakePieces.emplace_back(entitySys.entities.emplace_back(
      vkh::EntitySys::Transform{.position = {5.f, 0.f, 0.f}},
      vkh::EntitySys::RigidBody{}, sandwormScene, Sandworm::Head));
}
glm::vec2 leafHeadAccelDirection() { return {0.f, 0.f}; }
glm::vec2 kekwHeadAccelDirection() {
  return {glm::half_pi<float>() * .5f, 0.f};
}
void Sabl::update() {
  glm::vec2 accelDirLeaf = leafHeadAccelDirection();
  glm::vec3 accelLeaf{glm::cos(accelDirLeaf.x) * cos(accelDirLeaf.y),
                      glm::sin(accelDirLeaf.x) * glm::cos(accelDirLeaf.y),
                      glm::sin(accelDirLeaf.y)};
  glm::vec2 accelDirKekw = kekwHeadAccelDirection();
  glm::vec3 accelKekw{glm::cos(accelDirKekw.x) * cos(accelDirKekw.y),
                      glm::sin(accelDirKekw.x) * glm::cos(accelDirKekw.y),
                      glm::sin(accelDirKekw.y)};
  leafHeadVelocity += accelLeaf;
  kekwHeadVelocity += accelKekw;
  leafSnakePieces.begin()->transform.position += leafHeadVelocity;
  kekwSnakePieces.begin()->transform.position += kekwHeadVelocity;
}
