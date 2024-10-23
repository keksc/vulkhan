#pragma once

#include "../engineContext.hpp"
#include "../frameInfo.hpp"
#include "../pipeline.hpp"

#include <memory>
#include <vector>

namespace vkh {
namespace pointLightSys {
void init(EngineContext &context, VkDescriptorSetLayout globalSetLayout);
void cleanup(EngineContext& context);

void update(EngineContext& context, FrameInfo &frameInfo, GlobalUbo &ubo);
void render(EngineContext& context, FrameInfo &frameInfo);
}; // namespace pointLightSys
} // namespace vkh
