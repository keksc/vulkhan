#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <vector>

#include "../vkh/systems/entity/entities.hpp"

namespace vkh {
class EngineContext;
template <typename T> class Scene;
namespace hud {
class View;
class Text;
} // namespace hud
} // namespace vkh

class FeatherDuckGuard {
public:
  FeatherDuckGuard(vkh::EngineContext &context, vkh::EntitySys &entitySys,
                   vkh::hud::View &view);

  void flee();
  void aggress();
  void update();

private:
  enum AnimationIndex {
    Swoosh = 0,
    DashBackwards = 1,
  };
  AnimationIndex playingAnimationIndex;
  float playingAnimationTimeOfBeginning;
  bool isAnimationPlaying = false;

  std::shared_ptr<vkh::Scene<vkh::EntitySys::Vertex>> scene;

  std::shared_ptr<vkh::hud::Text> headline;
  std::vector<std::reference_wrapper<vkh::EntitySys::Entity>> sceneEntities;

  vkh::EngineContext &context;

  const glm::vec3 &getPosition() const {
    return sceneEntities[0].get().transform.position;
  }
  void setPosition(glm::vec3 newPosition) {
    for (auto &entity : sceneEntities) {
      entity.get().transform.position = newPosition;
    }
  }

  glm::vec3 targetPosition0;    // To affect static (means non-moving)
                                // positions to the targetPosition
  glm::vec3 *targetPositionPtr; // is a pointer so it can track automatically
                                // the player position for example

  void playAnimation(AnimationIndex index);
};
