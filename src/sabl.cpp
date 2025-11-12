#include "sabl.hpp"

#include <glm/ext.hpp>

#include <print>

glm::vec2 leafRelativeDirection(glm::vec3 otherRelativePos) {
  return {glm::quarter_pi<float>(), 0.f};
}
glm::vec2 kekwRelativeDirection(glm::vec3 otherRelativePos) {
  return {0.f, glm::quarter_pi<float>()};
}
enum Sandworm { Head = 0, Body = 1, Tail = 2 };
Sabl::Sabl(vkh::EngineContext &context, vkh::EntitySys &entitySys)
    : context{context}, entitySys{entitySys} {
  auto leafSandwormScene = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
      context, "models/leafsandworm.glb");
  auto kekwSandwormScene = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
      context, "models/kekwsandworm.glb");
  for (size_t i = 0; i < 20; i++) {
    leaf.sandwormPieces[i] = entitySys.entities.emplace_back(
        std::make_shared<vkh::EntitySys::Entity>(
            vkh::EntitySys::Transform{
                .position = {-5.f, 0.f, static_cast<float>(i) * 2.f}},
            vkh::EntitySys::RigidBody{}, leafSandwormScene, Sandworm::Head));
    kekw.sandwormPieces[i] = entitySys.entities.emplace_back(
        std::make_shared<vkh::EntitySys::Entity>(
            vkh::EntitySys::Transform{
                .position = {5.f, 0.f, static_cast<float>(i) * 2.f}},
            vkh::EntitySys::RigidBody{}, kekwSandwormScene, Sandworm::Head));
  }
  for (size_t i = 0; i < 19; i++) {
    leaf.positionHistory[i] = leaf.sandwormPieces[i]->transform.position;
    kekw.positionHistory[i] = kekw.sandwormPieces[i]->transform.position;
  }
  leaf.relativeDirection = leafRelativeDirection;
  kekw.relativeDirection = kekwRelativeDirection;
}
/**
 *  @brief  Make a glm::vec3 from a yaw and a pitch.
 *  @param  A glm::vec2 that is under the form {yaw, pitch}.
 *  @return A glm::vec3 representing the direction calculated from the yaw and
 * pitch
 */
glm::vec3 dirFromYawPitch(glm::vec2 direction) {
  return {glm::cos(direction.y) * glm::cos(direction.x), glm::sin(direction.y),
          glm::cos(direction.y) * glm::sin(direction.x)};
}
Sabl::~Sabl() {
  for (auto &piece : leaf.sandwormPieces) {
    entitySys.entities.erase(
        std::find(entitySys.entities.begin(), entitySys.entities.end(), piece));
  }
  for (auto &piece : kekw.sandwormPieces) {
    entitySys.entities.erase(
        std::find(entitySys.entities.begin(), entitySys.entities.end(), piece));
  }
}
void Sabl::update() {
  glm::vec3 kekwRelativeToLeaf = kekw.sandwormPieces[0]->transform.position -
                                 leaf.sandwormPieces[0]->transform.position;
  leaf.headVelocity +=
      dirFromYawPitch(leaf.headDirection +=
                      leaf.relativeDirection(kekwRelativeToLeaf)) *
      context.frameInfo.dt * snakeAccelStrength;
  kekw.headVelocity +=
      dirFromYawPitch(kekw.headDirection +=
                      leaf.relativeDirection(-kekwRelativeToLeaf)) *
      context.frameInfo.dt * snakeAccelStrength;

  leaf.sandwormPieces[0]->transform.position +=
      leaf.headVelocity * snakeSpeed;
  kekw.sandwormPieces[0]->transform.position +=
      kekw.headVelocity * snakeSpeed;

  for (size_t i = leaf.sandwormPieces.size() - 1; i > 0; i--) {
    leaf.sandwormPieces[i]->transform.position = leaf.positionHistory[i - 1];
    leaf.positionHistory[i - 1] =
        leaf.sandwormPieces[i - 1]->transform.position;
  }
  for (size_t i = kekw.sandwormPieces.size() - 1; i > 0; i--) {
    kekw.sandwormPieces[i]->transform.position = kekw.positionHistory[i - 1];
    kekw.positionHistory[i - 1] =
        kekw.sandwormPieces[i - 1]->transform.position;
  }
}
