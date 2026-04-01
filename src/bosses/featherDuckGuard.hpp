#pragma once

#include "../vkh/scene.hpp"
#include "../vkh/systems/entity/entities.hpp"
#include "../vkh/systems/hud/elements/text.hpp"
#include "../vkh/systems/hud/elements/view.hpp"

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

  std::shared_ptr<vkh::Scene<vkh::EntitySys::Vertex>> scene;

  AnimationIndex currentlyPlayingAnimation;

  std::shared_ptr<vkh::hud::Text> headline;

  vkh::EngineContext &context;
};
