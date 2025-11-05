#include "sabl.hpp"

enum Sandworm { Head = 0, Body = 1, Tail = 2 };
Sabl::Sabl(vkh::EngineContext &context, vkh::EntitySys &entitySys) : context{context}, entitySys{entitySys} {
  auto sandwormScene = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
      context, "models/sandworm.glb");
  for (size_t i = 0; i < 200; i++) {
  leafSnakePieces.emplace_back(
      entitySys.entities.emplace_back(std::make_shared<vkh::EntitySys::Entity>(
          vkh::EntitySys::Transform{.position = {-5.f, 0.f, 0.f}},
          vkh::EntitySys::RigidBody{}, sandwormScene, Sandworm::Head)));
  }
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
Sabl::~Sabl() {
  for(auto& piece : leafSnakePieces) {
    entitySys.entities.erase(std::find(entitySys.entities.begin(), entitySys.entities.end(), piece));
  }
  for(auto& piece : kekwSnakePieces) {
    entitySys.entities.erase(std::find(entitySys.entities.begin(), entitySys.entities.end(), piece));
  }
}
void Sabl::update() {
  glm::vec2 accelDirLeaf = leafHeadAccelYawPitch();
  glm::vec3 accelLeaf = yawPitchToAccel(accelDirLeaf);
  glm::vec2 accelDirKekw = kekwHeadAccelYawPitch();
  glm::vec3 accelKekw = yawPitchToAccel(accelDirKekw);
  leafHeadVelocity += accelLeaf;
  kekwHeadVelocity += accelKekw;
  // leafSnakePieces[0]->transform.position += leafHeadVelocity * .0001f;
  // kekwSnakePieces[0]->transform.position += kekwHeadVelocity * .0001f;
  for (size_t i = leafSnakePieces.size()-1; i > 0; i--) {
    leafSnakePieces[i]->transform.position.x = leafSnakePieces[i-1]->transform.position.x;
    leafSnakePieces[i]->transform.position.y = leafSnakePieces[i-1]->transform.position.y;
  }
  for (size_t i = kekwSnakePieces.size()-1; i > 0; i--) {
    kekwSnakePieces[i]->transform.position.x = kekwSnakePieces[i-1]->transform.position.x;
    kekwSnakePieces[i]->transform.position.y = kekwSnakePieces[i-1]->transform.position.y;
  }
  leafSnakePieces[0]->transform.position.x = cos(context.time);
  leafSnakePieces[0]->transform.position.y = context.time; 
  // kekwSnakePieces[0]->transform.position.x = cos(-context.time)+1.f;
  // kekwSnakePieces[0]->transform.position.y = sin(-context.time)+1.f;

}
