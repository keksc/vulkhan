#pragma once

#include "../vkh/scene.hpp"
#include "../vkh/systems/entity/entities.hpp"

class FeatherDuckGuard {
public:
  FeatherDuckGuard(vkh::EngineContext &context, vkh::EntitySys &entitySys);

private:
  vkh::Scene<vkh::EntitySys::Vertex> scene;
  enum AnimationIndexes {};
};
