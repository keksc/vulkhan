#pragma once

#include "hudElements.hpp"
#include "../../engineContext.hpp"

namespace vkh {
namespace hudSys {
void init(EngineContext &context);
void cleanup(EngineContext &context);
void setView(View& newView);
View& getView();
void render(EngineContext &context);
}; // namespace hudSys
} // namespace vkh
