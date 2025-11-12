#include "sabl.hpp"

#include <glm/gtc/constants.hpp>

/**
 *  @brief  Make a glm::vec3 from a yaw and a pitch.
 *  @param  A glm::vec2 that is under the form {yaw, pitch}.
 *  @return A glm::vec3 representing the direction calculated from the yaw and
 * pitch
 */
glm::vec3 dirFromYawPitch(glm::vec2 direction) {
  return {glm::sin(direction.x) * glm::cos(direction.y), glm::sin(direction.y),
          glm::cos(direction.x) * glm::cos(direction.y)};
}
glm::vec2 leafRelativeDirection(Sabl::Player &self,
                                glm::vec3 otherRelativePos) {
  // Flatten to XZ plane
  otherRelativePos.y = 0.0f;
  if (glm::length(otherRelativePos) < 1e-5f)
    return {0.f, 0.f};

  // Leaf's current forward direction
  glm::vec3 forward = dirFromYawPitch(self.headDirection);
  forward.y = 0.0f;
  forward = glm::normalize(forward);

  // Flee = opposite direction
  glm::vec3 awayDir = glm::normalize(-otherRelativePos);

  // Determine turn direction
  float crossY = glm::cross(forward, awayDir).y;
  float dot = glm::dot(forward, awayDir);

  constexpr float turnSpeed = 0.02f;
  float yawDelta = -crossY * turnSpeed;

  static bool once = true;
  if (once) {
    once = false;
    return {yawDelta, 1.f};
  }
  return {yawDelta, 0.f};
}
glm::vec2 kekwRelativeDirection(Sabl::Player &self,
                                glm::vec3 otherRelativePos) {
  // Flatten to XZ plane
  otherRelativePos.y = 0.0f;
  if (glm::length(otherRelativePos) < 1e-5f)
    return {0.f, 0.f};

  // Kekw's current forward direction
  glm::vec3 forward = dirFromYawPitch(self.headDirection);
  forward.y = 0.0f;
  forward = glm::normalize(forward);

  // Chase = toward target
  glm::vec3 targetDir = glm::normalize(otherRelativePos);

  // Determine turn direction
  float crossY = glm::cross(forward, targetDir).y;
  float dot = glm::dot(forward, targetDir);

  constexpr float turnSpeed = 0.05f;
  float yawDelta = -crossY * turnSpeed;

  return {yawDelta, 0.0f};
}
enum Sandworm { Head = 0, Body = 1, Tail = 2 };
Sabl::Sabl(vkh::EngineContext &context, vkh::EntitySys &entitySys)
    : context{context}, entitySys{entitySys} {
  auto leafSandwormScene = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
      context, "models/leafsandworm.glb");
  auto kekwSandwormScene = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
      context, "models/kekwsandworm.glb");
  for (size_t i = 0; i < baseSnakeSize; i++) {
    leaf.sandwormPieces[i] = entitySys.entities.emplace_back(
        std::make_shared<vkh::EntitySys::Entity>(
            vkh::EntitySys::Transform{.position = {-5.f, 0.f, 0.f}},
            vkh::EntitySys::RigidBody{}, leafSandwormScene, Sandworm::Head));
    kekw.sandwormPieces[i] = entitySys.entities.emplace_back(
        std::make_shared<vkh::EntitySys::Entity>(
            vkh::EntitySys::Transform{.position = {5.f, 0.f, 0.f}},
            vkh::EntitySys::RigidBody{}, kekwSandwormScene, Sandworm::Head));
  }
  for (size_t i = 0; i < baseSnakeSize - 1; i++) {
    leaf.positionHistory[i] = leaf.sandwormPieces[i]->transform.position;
    kekw.positionHistory[i] = kekw.sandwormPieces[i]->transform.position;
  }
  leaf.relativeDirection = leafRelativeDirection;
  kekw.relativeDirection = kekwRelativeDirection;
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
  leaf.headDirection += leaf.relativeDirection(leaf, kekwRelativeToLeaf);
  leaf.headVelocity += dirFromYawPitch(leaf.headDirection) * snakeAccelStrength;
  kekw.headDirection += kekw.relativeDirection(kekw, -kekwRelativeToLeaf);
  kekw.headVelocity += dirFromYawPitch(kekw.headDirection) * snakeAccelStrength;

  leaf.sandwormPieces[0]->transform.position += leaf.headVelocity * snakeSpeed;
  kekw.sandwormPieces[0]->transform.position += kekw.headVelocity * snakeSpeed;

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
