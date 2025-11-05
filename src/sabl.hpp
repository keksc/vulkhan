#pragma once

#include "vkh/systems/entity/entities.hpp"
#include "vkh/engineContext.hpp"

// this class contains Sabl(), the constructor 
// the Scene (uninteresting),
// The snake pieces for both players,
// the velocities for both players' heads.
//
// this is a bad implementation, much worse than my friends' that is not an approximation
// and can be smoothed in replay.
class Sabl {
public:
  Sabl(vkh::EngineContext &context, vkh::EntitySys &entitySys);
  ~Sabl();
  void update();
private:
  vkh::EntitySys& entitySys;
  vkh::EngineContext& context;

  std::shared_ptr<vkh::Scene<vkh::EntitySys::Vertex>> sandwormScene;
  std::vector<std::shared_ptr<vkh::EntitySys::Entity>> leafSnakePieces;
  std::vector<std::shared_ptr<vkh::EntitySys::Entity>> kekwSnakePieces;
  glm::vec3 kekwHeadVelocity{};
  glm::vec3 leafHeadVelocity{};
};
