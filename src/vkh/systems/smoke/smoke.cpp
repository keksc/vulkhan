#include "smoke.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vkh {

SmokeSys::SmokeSys(EngineContext &context)
    : System(context), fluidGrid(context, glm::ivec3{64, 64, 64}, 1.f) {
}

void SmokeSys::update() {
  glm::vec3 playerPos = context.camera.position;
  glm::vec3 forward =
      -glm::normalize(glm::vec3(context.camera.inverseViewMatrix[2]));

  fluidGrid.recenter(playerPos);

  bool brushActive =
      glfwGetMouseButton(context.window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  glm::ivec3 brushCenterCell{0};
  if (brushActive) {
    glm::vec3 brushCenter = playerPos + forward * brushDistance;
    brushCenterCell = glm::ivec3(glm::floor(brushCenter / fluidGrid.cellSize));
  }

  float radiusCells = brushRadius / fluidGrid.cellSize;

  bool laserActive = glfwGetMouseButton(context.window,
                                        GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
  float laserRadiusCells = laserRadius / fluidGrid.cellSize;

  // Player-movement push: derive velocity from the change in position since
  // last frame, rather than trusting a physics-system velocity we don't
  // have access to here. fluidGrid.dt is the FIXED sim timestep (1/60) the
  // rest of FluidGrid assumes, not this frame's real wall-clock delta - so
  // this is technically only correct if update() is being called at a
  // matching fixed rate. If SmokeSys::update() is ever called at a variable
  // frame rate, this will over/under-estimate velocity; swap in a real
  // per-frame dt here if one becomes available.
  glm::vec3 playerVelocity{0.0f};
  if (havePreviousPlayerPos) {
    playerVelocity = (playerPos - previousPlayerPos) / fluidGrid.dt;
  }
  previousPlayerPos = playerPos;
  havePreviousPlayerPos = true;

  glm::ivec3 playerCenterCell =
      glm::ivec3(glm::floor(playerPos / fluidGrid.cellSize));
  float playerRadiusCellsF = playerPushRadius / fluidGrid.cellSize;

  fluidGrid.update(brushCenterCell, brushActive, radiusCells,
                   forward * brushStrength, brushSmokeRate, playerCenterCell,
                   /*playerPushActive=*/true, playerRadiusCellsF,
                   playerVelocity * playerPushStrength, laserActive, playerPos,
                   forward, laserRadiusCells, forward * laserStrength);

}

} // namespace vkh
