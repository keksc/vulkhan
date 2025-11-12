#pragma once

#include "vkh/engineContext.hpp"
#include "vkh/systems/entity/entities.hpp"

// this class contains Sabl(), the constructor
// the Scene (uninteresting),
// The snake pieces for both players,
// the velocities for both players' heads.
//
// this is a bad implementation, much worse than my friends' that is not an
// approximation and can be smoothed in replay.
class Sabl {
public:
  Sabl(vkh::EngineContext &context, vkh::EntitySys &entitySys);
  ~Sabl();
  void update();

private:
  vkh::EntitySys &entitySys;
  vkh::EngineContext &context;

  std::shared_ptr<vkh::Scene<vkh::EntitySys::Vertex>> sandwormScene;

  const float snakeAccelStrength = 3.f;
  const float snakeSpeed = .1f;

  struct Player {
    std::array<std::shared_ptr<vkh::EntitySys::Entity>, 20> sandwormPieces;
    std::array<glm::vec3, 19> positionHistory;
    glm::vec3 headVelocity{};
    glm::vec2 headDirection{};
    std::function<glm::vec2(glm::vec3 otherRelativePos)> relativeDirection;
  };
  Player kekw, leaf;
};
