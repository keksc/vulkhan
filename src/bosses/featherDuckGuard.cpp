#include "featherDuckGuard.hpp"

FeatherDuckGuard::FeatherDuckGuard(vkh::EngineContext &context,
                                   vkh::EntitySys &entitySys)
    : scene(context, "models/featherDuckGuard.glb", entitySys.textureSetLayout) {

    }
