#include "featherDuckGuard.hpp"
#include "../vkh/scene.hpp"
#include "../vkh/systems/hud/elements/text.hpp"

FeatherDuckGuard::FeatherDuckGuard(vkh::EngineContext &context,
                                   vkh::EntitySys &entitySys,
                                   vkh::hud::View &view)
    : context{context} {
  headline = view.addElement<vkh::hud::Text>(
      glm::vec2{0.f, .5f}, "Feather Duck Guard", glm::vec3{0.f}, 3.f);

  scene = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
      context, "models/featherDuckGuard.glb", entitySys.textureSetLayout);

  for (size_t i = 0; i < scene->meshes.size(); i++)
    entitySys.entities.emplace_back(
        vkh::EntitySys::Transform{.position{25.f}, .scale{1.f}},
        vkh::EntitySys::RigidBody{}, scene, i);
}

void FeatherDuckGuard::flee() {}
void FeatherDuckGuard::aggress() {}
void FeatherDuckGuard::update() {}
