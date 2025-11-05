#pragma once

#include "vkh/systems/entity/entities.hpp"

class Sabl {
public:
  Sabl(vkh::EngineContext &context, vkh::EntitySys &entitySys);
  void update();
  std::shared_ptr<vkh::Scene<vkh::EntitySys::Vertex>> sandwormScene;
  std::vector<vkh::EntitySys::Entity *> leafSnakePieces;
  std::vector<vkh::EntitySys::Entity *> kekwSnakePieces;
  glm::vec3 kekwHeadVelocity{};
  glm::vec3 leafHeadVelocity{};
};
