#pragma once

#include "vkh/engineContext.hpp"
#include "vkh/systems/entity/entities.hpp"

void generateDungeon(vkh::EngineContext &context, vkh::EntitySys &entitySys,
                     std::vector<vkh::EntitySys::Entity> &entities);
