#pragma once

#include "../engineContext.hpp"

namespace vkh {
namespace fontSys {
void init(EngineContext &context);
void cleanup(EngineContext &context);

void updateText(EngineContext &context, std::string text);
void render(EngineContext &context);
}; // namespace fontSys
} // namespace vkh
