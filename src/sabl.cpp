#include "sabl.hpp"

enum Sandworm { Head = 0, Body = 1, Tail = 2 };
Sabl::Sabl(vkh::EngineContext &context, vkh::EntitySys &entitySys) {
  auto sandwormScene = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
      context, "models/sandworm.glb");
  leafSnakePieces.emplace_back(
      entitySys.entities.emplace_back(std::make_unique<vkh::EntitySys::Entity>(
          vkh::EntitySys::Transform{.position = {-5.f, 0.f, 0.f}},
          vkh::EntitySys::RigidBody{}, sandwormScene, Sandworm::Head)));
  kekwSnakePieces.emplace_back(
      entitySys.entities.emplace_back(std::make_shared<vkh::EntitySys::Entity>(
          vkh::EntitySys::Transform{.position = {5.f, 0.f, 0.f}},
          vkh::EntitySys::RigidBody{}, sandwormScene, Sandworm::Head)));
}
glm::vec2 leafHeadAccelYawPitch() { return {0.f, 0.f}; }
glm::vec2 kekwHeadAccelYawPitch() { return {glm::half_pi<float>() * .5f, 0.f}; }
glm::vec3 yawPitchToAccel(glm::vec2 yawPitch) {
  return {glm::cos(yawPitch.x), glm::sin(yawPitch.y), glm::sin(yawPitch.x)};
}
void Sabl::update() {
  glm::vec2 accelDirLeaf = leafHeadAccelYawPitch();
  glm::vec3 accelLeaf = yawPitchToAccel(accelDirLeaf);
  glm::vec2 accelDirKekw = kekwHeadAccelYawPitch();
  glm::vec3 accelKekw = yawPitchToAccel(accelDirKekw);
  leafHeadVelocity += accelLeaf;
  kekwHeadVelocity += accelKekw;
  leafSnakePieces[0]->transform.position.x += 1.f;
  // kekwSnakePieces[0]->transform.position += 1.f;
  // entitySys.entities[0].transform.position.x += .2f;
}
