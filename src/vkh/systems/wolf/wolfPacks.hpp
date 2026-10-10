#pragma once

#include "wolfNavigation.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <random>
#include <string_view>
#include <vector>

namespace vkh::wildlife {

// Wildlife AI and locomotion only. The renderer consumes immutable snapshots.
class WolfPacks {
public:
  static constexpr size_t kPackCount = 3, kPackSize = 6;
  static constexpr size_t kWolfCount = kPackCount * kPackSize;
  static constexpr float kStep = 1.f / 30.f;
  using HeightFn = WolfNavigation::HeightFn;
  using BlockedFn = WolfNavigation::BlockedFn;
  enum class State { Travel, Rest, Alert, Flee, Regroup };
  static std::string_view stateName(State state);
  struct Wolf {
    glm::vec3 position{};
    glm::quat orientation{1, 0, 0, 0};
    float scale = 1, phase = 0, speed = 0, motionBlend = 0;
    bool active = false;
    // GLB forward is +Z; steering orientations map canonical -Z forward.
    glm::mat4 modelMatrix() const;
  };
  struct PackStatus {
    State state = State::Rest;
    glm::vec2 destination{};
    float stateTime = 0, stamina = 1, threat = 0;
    size_t routePoints = 0;
    uint32_t routePlans = 0;
  };
  WolfPacks(HeightFn height, BlockedFn blocked, glm::vec3 camera,
            glm::vec3 forward = {0, 0, -1});
  void update(float dt, glm::vec3 camera, glm::vec3 forward = {0, 0, -1},
              bool perceivePlayer = true);
  std::array<Wolf, kWolfCount> interpolated() const;
  std::array<PackStatus, kPackCount> status() const;

private:
  struct Pack {
    glm::vec2 home{}, heading{0, -1}, lastThreat{};
    std::array<glm::vec2, 4> sites{};
    std::vector<glm::vec2> route;
    std::vector<glm::vec2> trail; // newest first, arc-length sampled
    State state = State::Travel;
    size_t patrol = 0, waypoint = 1;
    float stateTime = 0, decisionTimer = 0, planCooldown = 0;
    float stamina = 1, threat = 0, stalled = 0, lostPlayer = 0;
    uint32_t routePlans = 0;
    bool preferLocalRoute = false;
  };
  void spawn(size_t pack, glm::vec3 camera);
  void tick(glm::vec3 camera, glm::vec2 playerVelocity, bool perceivePlayer);
  void decide(size_t pack, glm::vec3 camera, glm::vec2 playerVelocity,
              bool perceivePlayer);
  void enter(Pack &pack, State state);
  void resolveContacts(size_t pack);
  bool plan(size_t pack, glm::vec2 destination, bool allowDetour = true);
  bool patrolRoute(size_t pack);
  bool fleeRoute(size_t pack);
  glm::vec2 routeTarget(Pack &pack, glm::vec2 leader);
  struct FormationSlot {
    glm::vec2 position, tangent;
  };
  FormationSlot formationTarget(const Pack &pack, size_t slot) const;
  void move(size_t wolf, glm::vec2 target, float targetSpeed,
            glm::vec2 faceDirection, const std::array<Wolf, kWolfCount> &old);
  glm::vec3 ground(glm::vec2 p) const;
  bool sightClear(glm::vec3 camera, glm::vec3 target) const;
  bool inSight(glm::vec3 camera) const;
  void setForward(glm::vec3 forward);
  float random(float lo, float hi);
  WolfNavigation navigation;
  std::mt19937 rng{4701};
  std::array<Pack, kPackCount> packs{};
  std::array<Wolf, kWolfCount> previous{}, current{};
  std::array<glm::vec2, kWolfCount> velocity{};
  std::array<glm::vec2, kWolfCount> headings{};
  std::array<int, kWolfCount> avoidanceSide{};
  std::array<float, kWolfCount> followerStalled{}, recoveryCooldown{},
      previousSlotError{};
  std::array<std::vector<glm::vec2>, kWolfCount> recoveryRoutes;
  std::array<size_t, kWolfCount> recoveryWaypoint{};
  float accumulator = 0, encounterCheck = 0, encounterHidden = 0;
  glm::vec2 viewForward{0, -1};
  glm::vec3 previousObserver{};
};

} // namespace vkh::wildlife
