#pragma once

#include "../../buffer.hpp"
#include "../system.hpp"

#include "fluidGrid.hpp"
#include <memory>

namespace vkh {

class SmokeSys : public System {
public:
  SmokeSys(EngineContext &context);
  void update();

  // Rendering happens at post processing

  FluidGrid fluidGrid;

  float brushDistance = 6.0f;  // how far in front of the camera smoke spawns
  float brushRadius = 3.0f;    // world units
  float brushStrength = 6.0f;  // velocity imparted along the camera's forward
  float brushSmokeRate = 0.6f; // smoke density added per frame at brush center

  // Player-movement push tuning: the player's own motion injects velocity
  // into the grid at their position (no dye), so walking through smoke
  // actually disturbs it instead of it being static wallpaper.
  float playerPushRadius = 0.5f;   // world units around the player
  float playerPushStrength = 1.0f; // multiplier on the player's actual
                                   // world-space velocity (1.0 = 1:1)

  // Velocity laser (right mouse button): a beam of velocity impulses fired
  // from the camera along its forward vector, out to the far side of the
  // grid's local window. No dye - it's meant to stir the smoke, not add to
  // it.
  float laserRadius = 1.5f;    // world units, per-step sphere radius
  float laserStrength = 20.0f; // velocity imparted along the camera's forward

private:
  // Previous frame's player position, used to derive an instantaneous
  // world-space velocity (playerPos - previousPlayerPos) / fluidGrid.dt.
  // Starts uninitialized-but-marked so the very first frame doesn't inject
  // a huge spurious velocity from an uninitialized/zero previous position.
  glm::vec3 previousPlayerPos{};
  bool havePreviousPlayerPos = false;
};

} // namespace vkh
