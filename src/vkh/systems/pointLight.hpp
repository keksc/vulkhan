#pragma once

#include "../engineContext.hpp"
#include "../pipeline.hpp"

#include <memory>
#include <vector>

namespace vkh {
namespace pointLightSys {
void init(EngineContext &context, VkDescriptorSetLayout globalSetLayout);
void cleanup(EngineContext& context);

void update(EngineContext& context, GlobalUbo &ubo);
void render(EngineContext& context);
}; // namespace pointLightSys
} // namespace vkh
