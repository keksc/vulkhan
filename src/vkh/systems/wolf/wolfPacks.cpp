#include "wolfPacks.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace vkh::wildlife {
namespace {
glm::vec2 xz(glm::vec3 p) { return {p.x, p.z}; }
glm::vec2 direction(glm::vec2 v, glm::vec2 fallback = {0, -1}) {
  const float length = glm::length(v);
  return length > .001f ? v / length : fallback;
}
float angle(glm::vec2 v) { return std::atan2(v.y, v.x); }
glm::vec2 turn(glm::vec2 from, glm::vec2 to, float maximum) {
  const float delta =
      std::remainder(angle(to) - angle(from), glm::two_pi<float>());
  const float a = angle(from) + std::clamp(delta, -maximum, maximum);
  return {std::cos(a), std::sin(a)};
}
} // namespace

glm::mat4 WolfPacks::Wolf::modelMatrix() const {
  return glm::translate(glm::mat4(1), position) * glm::mat4_cast(orientation) *
         glm::scale(glm::mat4(1), glm::vec3(-scale, scale, -scale));
}

std::string_view WolfPacks::stateName(State state) {
  switch (state) {
  case State::Travel:
    return "travel";
  case State::Rest:
    return "rest";
  case State::Alert:
    return "alert";
  case State::Flee:
    return "flee";
  case State::Regroup:
    return "regroup";
  }
  return "unknown";
}

WolfPacks::WolfPacks(HeightFn height, BlockedFn blocked, glm::vec3 camera,
                     glm::vec3 forward)
    : navigation(std::move(height), std::move(blocked)),
      previousObserver(camera) {
  setForward(forward);
  for (size_t p = 0; p < kPackCount; ++p)
    spawn(p, camera);
  previous = current;
}

float WolfPacks::random(float lo, float hi) {
  return std::uniform_real_distribution<float>(lo, hi)(rng);
}

glm::vec3 WolfPacks::ground(glm::vec2 p) const {
  return {p.x, navigation.height(p), p.y};
}

void WolfPacks::setForward(glm::vec3 forward) {
  const glm::vec2 horizontal = xz(forward);
  const float length = glm::length(horizontal);
  if (std::isfinite(length) && length > .001f)
    viewForward = horizontal / length;
}

bool WolfPacks::sightClear(glm::vec3 from, glm::vec3 to) const {
  from.y = std::max(from.y, navigation.height(xz(from)) + 1.3f);
  const int steps =
      std::clamp(int(glm::distance(xz(from), xz(to)) / 3.f), 4, 48);
  for (int i = 1; i < steps; ++i) {
    const auto p = glm::mix(from, to, float(i) / steps);
    if (navigation.height(xz(p)) > p.y - .1f)
      return false;
  }
  return true;
}

bool WolfPacks::inSight(glm::vec3 camera) const {
  for (size_t i = 0; i < kPackSize; ++i) {
    const auto &wolf = current[i];
    if (!wolf.active)
      continue;
    const auto delta = xz(wolf.position) - xz(camera);
    const float distance = glm::length(delta);
    if (distance < 10 ||
        (distance < 500 && glm::dot(delta, viewForward) > distance * .5f &&
         sightClear(camera, wolf.position + glm::vec3(0, 1.3f, 0))))
      return true;
  }
  return false;
}

void WolfPacks::enter(Pack &pack, State state) {
  if (pack.state == state)
    return;
  pack.state = state;
  pack.stateTime = 0;
  pack.stalled = 0;
}

bool WolfPacks::plan(size_t p, glm::vec2 destination, bool allowDetour) {
  auto &pack = packs[p];
  std::vector<glm::vec2> route;
  ++pack.routePlans;
  pack.planCooldown = 1.5f;
  if (!navigation.findPath(xz(current[p * kPackSize].position), destination,
                           route, allowDetour))
    return false;
  pack.route = std::move(route);
  pack.waypoint = 1;
  pack.stalled = 0;
  return true;
}

bool WolfPacks::patrolRoute(size_t p) {
  auto &pack = packs[p];
  // Four persistent territory destinations, visited in order. No new random
  // target every few seconds, and failures don't strand the pack forever.
  const auto leader = xz(current[p * kPackSize].position);
  for (size_t attempt = 0;
       !pack.preferLocalRoute && attempt < pack.sites.size(); ++attempt) {
    pack.patrol = (pack.patrol + 1) % pack.sites.size();
    // Returning "success" for home while already standing there traps a
    // pack in repeated rest cycles when the other sites aren't streamed.
    if (glm::distance(leader, pack.sites[pack.patrol]) > 2.5f &&
        plan(p, pack.sites[pack.patrol]))
      return true;
  }
  // Adapt the next persistent territory site to reachable local ground.
  // Keep the terrain-query budget and never regenerate unloaded chunks.
  // Deterministic directions favour the intended route, then its flanks,
  // finally a retreat into known ground; this isn't a random walk.
  pack.preferLocalRoute = true;
  const size_t nextSite = (pack.patrol + 1) % pack.sites.size();
  const auto preferred = direction(pack.sites[nextSite] - leader, pack.heading);
  for (float distance : {32.f, 16.f, 8.f})
    for (float offset : {0.f, -.65f, .65f, -1.3f, 1.3f, glm::pi<float>()}) {
      const float a = angle(preferred) + offset;
      const auto destination =
          leader + glm::vec2(std::cos(a), std::sin(a)) * distance;
      if (plan(p, destination, false)) {
        pack.preferLocalRoute = false;
        pack.patrol = nextSite;
        pack.sites[nextSite] = destination;
        return true;
      }
    }
  pack.route.clear();
  pack.planCooldown = 5;
  return false;
}

bool WolfPacks::fleeRoute(size_t p) {
  auto &pack = packs[p];
  const auto leader = xz(current[p * kPackSize].position);
  const auto away = direction(leader - pack.lastThreat, pack.heading);
  // Try the direct escape route first, then wider alternatives. This is a
  // deliberate escape decision, not a random walk or per-wolf panic vector.
  if (!pack.preferLocalRoute)
    for (float offset : {0.f, -.45f, .45f, -.9f, .9f}) {
      const float a = angle(away) + offset;
      const auto goal = leader + glm::vec2(std::cos(a), std::sin(a)) * 65.f;
      if (plan(p, goal))
        return true;
    }
  pack.preferLocalRoute = true;
  for (float distance : {32.f, 16.f, 8.f})
    for (float offset : {0.f, -.45f, .45f, -.9f, .9f, -1.55f, 1.55f}) {
      const float a = angle(away) + offset;
      const auto goal = leader + glm::vec2(std::cos(a), std::sin(a)) * distance;
      if (plan(p, goal, false))
        return true;
    }
  pack.route.clear();
  pack.planCooldown = 2;
  return false;
}

void WolfPacks::spawn(size_t p, glm::vec3 cameraPosition) {
  const auto camera = xz(cameraPosition);
  const float facing = std::atan2(viewForward.x, -viewForward.y);
  const float preferred = facing + (p == 0 ? 0.f : p == 1 ? 1.2f : -1.2f);
  float best = -std::numeric_limits<float>::infinity();
  glm::vec2 centre{};
  for (int attempt = 0; attempt < 48; ++attempt) {
    const float a =
        preferred + random(p == 0 ? -.65f : -1.1f, p == 0 ? .65f : 1.1f);
    const float distance = p == 0 ? random(35, 75) : random(100, 220);
    const auto candidate =
        camera + glm::vec2(std::sin(a), -std::cos(a)) * distance;
    const float h = navigation.height(candidate);
    if (!navigation.canStand(candidate) ||
        (p == 0 && !sightClear(cameraPosition,
                               ground(candidate) + glm::vec3(0, 1.3f, 0))))
      continue;
    const std::array taps{navigation.height(candidate + glm::vec2(12, 0)),
                          navigation.height(candidate - glm::vec2(12, 0)),
                          navigation.height(candidate + glm::vec2(0, 12)),
                          navigation.height(candidate - glm::vec2(0, 12))};
    const float dx = (taps[0] - taps[1]) / 24, dz = (taps[2] - taps[3]) / 24;
    if (dx * dx + dz * dz > .85f * .85f)
      continue;
    const auto heading = direction(candidate - camera);
    const glm::vec2 right(-heading.y, heading.x);
    bool room = true;
    for (size_t j = 1; j < kPackSize; ++j) {
      const auto offset = -heading * (4.5f + float((j - 1) / 2) * 4.8f) +
                          right * (j % 2 ? 1.8f : -1.8f);
      room &= navigation.canTraverse(candidate, candidate + offset);
    }
    if (!room)
      continue;
    const auto [lo, hi] = std::minmax_element(taps.begin(), taps.end());
    const float score = *hi - *lo +
                        std::max(h - navigation.height(camera), 0.f) * .1f -
                        distance * .006f - std::abs(a - preferred) * 2;
    if (score > best) {
      best = score;
      centre = candidate;
    }
  }
  if (!std::isfinite(best))
    return;
  auto &pack = packs[p];
  pack = Pack{};
  pack.home = centre;
  pack.heading = direction(centre - camera);
  pack.decisionTimer = float(p) * .06f;
  const glm::vec2 right(-pack.heading.y, pack.heading.x);
  pack.sites = {centre, centre + pack.heading * 65.f + right * 25.f,
                centre + pack.heading * 115.f - right * 25.f,
                centre + right * 75.f};
  for (int i = 0; i < 48; ++i)
    pack.trail.push_back(centre - pack.heading * (float(i) * .45f));
  for (size_t j = 0; j < kPackSize; ++j) {
    const size_t i = p * kPackSize + j;
    const auto offset =
        j == 0 ? glm::vec2(0)
               : -pack.heading * (4.5f + float((j - 1) / 2) * 4.8f) +
                     right * (j % 2 ? 1.8f : -1.8f);
    auto &wolf = current[i];
    wolf.position = ground(centre + offset);
    wolf.scale = random(1.2f, 1.45f);
    wolf.phase = random(0, 1);
    wolf.speed = 0;
    wolf.motionBlend = 0;
    wolf.active = true;
    wolf.orientation = glm::quatLookAt(
        glm::vec3(pack.heading.x, 0, pack.heading.y), glm::vec3(0, 1, 0));
    headings[i] = pack.heading;
    velocity[i] = {0, 0};
    avoidanceSide[i] = j % 2 ? 1 : -1;
    followerStalled[i] = 0;
    previousSlotError[i] = 0;
    recoveryCooldown[i] = float(j) * .25f;
    recoveryRoutes[i].clear();
    recoveryWaypoint[i] = 1;
    previous[i] = wolf;
  }
  if (!patrolRoute(p))
    enter(pack, State::Rest);
}

void WolfPacks::decide(size_t p, glm::vec3 camera, glm::vec2 playerVelocity,
                       bool perceivePlayer) {
  auto &pack = packs[p];
  const size_t first = p * kPackSize;
  float distance = std::numeric_limits<float>::infinity();
  for (size_t j = 0; j < kPackSize; ++j)
    distance = std::min(distance, glm::distance(current[first + j].position +
                                                    glm::vec3(0, 1.3f, 0),
                                                camera));
  bool seen = false;
  if (perceivePlayer && distance < 38)
    for (size_t j = 0; j < kPackSize; ++j)
      if (glm::distance(current[first + j].position + glm::vec3(0, 1.3f, 0),
                        camera) < 38 &&
          sightClear(current[first + j].position + glm::vec3(0, 1.3f, 0),
                     camera)) {
        seen = true;
        break;
      }
  const bool heard =
      perceivePlayer && distance < 22 && glm::length(playerVelocity) > 3;
  if (seen || heard) {
    pack.lastThreat = xz(camera);
    pack.threat = std::min(1.f, pack.threat + .3f);
    pack.lostPlayer = 0;
  } else {
    pack.threat = std::max(0.f, pack.threat - .06f);
    pack.lostPlayer += .2f;
  }
  const auto toPack = direction(xz(current[first].position) - xz(camera));
  const bool approaching = glm::dot(playerVelocity, toPack) > 2.5f;
  if (perceivePlayer && ((distance < 13 && (seen || heard)) ||
                         (distance < 26 && approaching && (seen || heard)))) {
    if (pack.state != State::Flee) {
      enter(pack, State::Flee);
      pack.planCooldown = 0;
    }
  } else if (pack.state != State::Flee && seen && distance < 32 &&
             pack.threat > .35f) {
    enter(pack, State::Alert);
  }

  float spread = 0;
  for (size_t j = 1; j < kPackSize; ++j)
    spread = std::max(spread, glm::distance(xz(current[first + j].position),
                                            xz(current[first].position)));
  switch (pack.state) {
  case State::Flee:
    if (pack.stateTime > 6 && distance > 55 && pack.lostPlayer > 3) {
      enter(pack, State::Rest);
      break;
    }
    if (pack.planCooldown <= 0 &&
        (pack.route.empty() || pack.waypoint >= pack.route.size() ||
         pack.stalled > 1.2f ||
         glm::dot(direction(pack.route.back() - xz(current[first].position)),
                  direction(xz(current[first].position) - pack.lastThreat)) <
             .25f))
      fleeRoute(p);
    break;
  case State::Alert:
    // A wild pack assesses the observer briefly, then creates distance.
    // Previously continuing visibility held every animal still forever.
    if (pack.stateTime > 3 && (seen || heard)) {
      enter(pack, State::Flee);
      pack.planCooldown = 0;
      fleeRoute(p);
    } else if (pack.lostPlayer > 4 || distance > 46)
      enter(pack, spread > 25 ? State::Regroup : State::Travel);
    break;
  case State::Regroup:
    if (spread < 17 || pack.stateTime > 8)
      enter(pack, State::Travel);
    break;
  case State::Rest:
    if (pack.stateTime > 6 + float(p) * 2 && pack.stamina > .55f &&
        pack.planCooldown <= 0) {
      if ((!pack.route.empty() && glm::distance(xz(current[first].position),
                                                pack.route.back()) > 2.5f) ||
          patrolRoute(p))
        enter(pack, State::Travel);
    }
    break;
  case State::Travel:
    if (pack.stamina < .3f) {
      enter(pack, State::Rest);
    } else if (spread > 28) {
      enter(pack, State::Regroup);
    } else if (!pack.route.empty() && glm::distance(xz(current[first].position),
                                                    pack.route.back()) < 2.5f) {
      enter(pack, State::Rest);
    } else if (pack.planCooldown <= 0) {
      if (pack.route.empty()) {
        if (!patrolRoute(p))
          enter(pack, State::Rest);
      } else if (pack.stalled > 1.5f ||
                 (pack.waypoint < pack.route.size() &&
                  !navigation.canTraverse(xz(current[first].position),
                                          pack.route[pack.waypoint]))) {
        // A newly streamed exclusion changes the path, not the pack's
        // purpose. Try a detour to the SAME destination before giving up.
        const auto goal = pack.route.back();
        if (!plan(p, goal) && !patrolRoute(p))
          enter(pack, State::Rest);
      }
    }
    break;
  }
}

glm::vec2 WolfPacks::routeTarget(Pack &pack, glm::vec2 leader) {
  while (pack.waypoint < pack.route.size() &&
         glm::distance(leader, pack.route[pack.waypoint]) < 2.f)
    ++pack.waypoint;
  if (pack.waypoint >= pack.route.size())
    return leader;
  return pack.route[pack.waypoint];
}

WolfPacks::FormationSlot WolfPacks::formationTarget(const Pack &pack,
                                                    size_t slot) const {
  const float behind = 4.5f + float((slot - 1) / 2) * 4.8f;
  float distance = 0;
  glm::vec2 centre = pack.trail.back(), tangent = pack.heading;
  for (size_t i = 1; i < pack.trail.size(); ++i) {
    const float segment = glm::distance(pack.trail[i - 1], pack.trail[i]);
    if (distance + segment >= behind && segment > .001f) {
      centre = glm::mix(pack.trail[i - 1], pack.trail[i],
                        (behind - distance) / segment);
      tangent = direction(pack.trail[i - 1] - pack.trail[i], pack.heading);
      break;
    }
    distance += segment;
  }
  const glm::vec2 right(-tangent.y, tangent.x);
  const auto lateral = centre + right * (slot % 2 ? 1.8f : -1.8f);
  // Narrow terrain naturally turns the pair formation into single file.
  return {navigation.canStand(lateral) ? lateral : centre, tangent};
}

void WolfPacks::move(size_t i, glm::vec2 target, float desiredSpeed,
                     glm::vec2 faceDirection,
                     const std::array<Wolf, kWolfCount> &old) {
  const auto position = xz(old[i].position);
  glm::vec2 desired = direction(target - position, headings[i]);
  glm::vec2 separation(0);
  const size_t first = (i / kPackSize) * kPackSize;
  for (size_t j = first; j < first + kPackSize; ++j) {
    if (j == i)
      continue;
    const auto away = position - xz(old[j].position);
    const float d2 = glm::dot(away, away);
    if (d2 > .001f && d2 < 3.f * 3.f) {
      const float distance = std::sqrt(d2);
      separation += away / distance * (1.f - distance / 3.f) * 3.f;
    }
  }
  if (desiredSpeed > .05f)
    desired = direction(desired + separation * .35f, desired);
  else if (glm::length(faceDirection) > .001f)
    desired = direction(faceDirection);
  else
    desired = headings[i];
  const float lookahead = std::max(1.f, old[i].speed * .8f);
  if (desiredSpeed > .05f &&
      !navigation.canTraverse(position, position + desired * lookahead)) {
    float best = -10;
    glm::vec2 safe = headings[i];
    bool found = false;
    for (float offset : {0.35f, -0.35f, .7f, -.7f, 1.05f, -1.05f, 1.4f, -1.4f,
                         1.9f, -1.9f, 2.4f, -2.4f}) {
      const float a = angle(desired) + offset;
      const glm::vec2 probe(std::cos(a), std::sin(a));
      if (!navigation.canTraverse(position, position + probe * lookahead))
        continue;
      const float score = glm::dot(probe, desired) +
                          (offset * avoidanceSide[i] > 0 ? .12f : 0.f);
      if (score > best) {
        best = score;
        safe = probe;
        found = true;
        avoidanceSide[i] = offset > 0 ? 1 : -1;
      }
    }
    if (found)
      desired = safe;
    else
      desiredSpeed = 0;
  }
  const float turning = std::abs(std::remainder(
      angle(desired) - angle(headings[i]), glm::two_pi<float>()));
  desiredSpeed *= std::clamp(std::cos(turning), .15f, 1.f);
  const float yawRate = desiredSpeed > 1 ? 1.35f : .8f;
  headings[i] = turn(headings[i], desired, yawRate * kStep);
  const float acceleration = desiredSpeed < old[i].speed ? 4.5f : 2.4f;
  float speed =
      old[i].speed + std::clamp(desiredSpeed - old[i].speed,
                                -acceleration * kStep, acceleration * kStep);
  auto next = position + headings[i] * speed * kStep;
  if (!navigation.canTraverse(position, next)) {
    speed = 0;
    next = position;
  }
  velocity[i] = headings[i] * speed;
  auto &wolf = current[i];
  wolf.position = ground(next);
  wolf.speed = speed;
  const float horizontal = glm::distance(next, position);
  const float dy = std::clamp(wolf.position.y - old[i].position.y,
                              -horizontal * .85f, horizontal * .85f);
  wolf.phase +=
      std::sqrt(horizontal * horizontal + dy * dy) / (2.3f * wolf.scale);
  if (wolf.phase > 1024) {
    wolf.phase -= 1024;
    previous[i].phase -= 1024;
  }
  const float blend = glm::smoothstep(.15f, 2.f, speed);
  wolf.motionBlend =
      glm::mix(old[i].motionBlend, blend, std::min(1.f, kStep * 5));
  const float tap = .65f;
  const float dx = navigation.height(next + glm::vec2(tap, 0)) -
                   navigation.height(next - glm::vec2(tap, 0));
  const float dz = navigation.height(next + glm::vec2(0, tap)) -
                   navigation.height(next - glm::vec2(0, tap));
  const glm::vec3 normal = std::isfinite(dx) && std::isfinite(dz)
                               ? glm::normalize(glm::vec3(-dx, 2 * tap, -dz))
                               : glm::vec3(0, 1, 0);
  glm::vec3 forward(headings[i].x, 0, headings[i].y);
  forward = glm::normalize(forward - normal * glm::dot(normal, forward));
  const auto right = glm::normalize(glm::cross(forward, normal));
  wolf.orientation =
      glm::quat_cast(glm::mat3(right, glm::cross(right, forward), -forward));
}

void WolfPacks::resolveContacts(size_t p) {
  // A body-clearance constraint handles occasional converging paths during
  // a turn. Navigation still decides where to go; contacts never push an
  // animal through an exclusion or onto an unwalkable slope.
  const size_t first = p * kPackSize;
  for (int pass = 0; pass < 3; ++pass)
    for (size_t a = first; a < first + kPackSize; ++a)
      for (size_t b = a + 1; b < first + kPackSize; ++b) {
        const auto pa = xz(current[a].position), pb = xz(current[b].position);
        const float distance = glm::distance(pa, pb);
        if (distance >= 1.3f)
          continue;
        const auto normal = direction(pa - pb, {1, 0});
        const auto correction = normal * ((1.3f - distance) * .5f);
        const bool moveA = navigation.canTraverse(pa, pa + correction);
        const bool moveB = navigation.canTraverse(pb, pb - correction);
        const auto nextA = pa + correction * (moveB ? 1.f : 2.f);
        const auto nextB = pb - correction * (moveA ? 1.f : 2.f);
        if (moveA && navigation.canTraverse(pa, nextA))
          current[a].position = ground(nextA);
        if (moveB && navigation.canTraverse(pb, nextB))
          current[b].position = ground(nextB);
      }
}

void WolfPacks::tick(glm::vec3 camera, glm::vec2 playerVelocity,
                     bool perceivePlayer) {
  previous = current;
  for (size_t p = 0; p < kPackCount; ++p) {
    auto &pack = packs[p];
    const size_t first = p * kPackSize;
    // Streamed terrain can disappear during fast travel. Do not regenerate
    // erosion here, or let unavailable heights enter transforms/animation.
    if (current[first].active) {
      bool loaded = true;
      for (size_t j = 0; j < kPackSize; ++j)
        loaded &=
            std::isfinite(navigation.height(xz(current[first + j].position)));
      if (!loaded) {
        for (size_t j = 0; j < kPackSize; ++j)
          current[first + j].active = false;
        pack.stateTime = 0;
      }
    }
    if (!current[first].active) {
      pack.stateTime += kStep;
      if (pack.stateTime > 5) {
        pack.stateTime = 0;
        spawn(p, camera);
      }
      continue;
    }
    if (glm::distance(xz(current[first].position), xz(camera)) >
            (p == 0 ? 200 : 650) &&
        (p != 0 || !inSight(camera))) {
      spawn(p, camera);
      continue;
    }
    pack.stateTime += kStep;
    pack.planCooldown -= kStep;
    pack.decisionTimer -= kStep;
    if (pack.decisionTimer <= 0) {
      decide(p, camera, playerVelocity, perceivePlayer);
      pack.decisionTimer = .2f;
    }
    pack.stamina =
        std::clamp(pack.stamina + (pack.state == State::Rest   ? .025f
                                   : pack.state == State::Flee ? -.015f
                                                               : -.002f) *
                                      kStep,
                   .2f, 1.f);
    const auto old = current;
    const auto leader = xz(old[first].position);
    const auto target = routeTarget(pack, leader);
    const bool moving =
        pack.state == State::Travel || pack.state == State::Flee;
    float speed =
        moving ? (pack.state == State::Flee ? 6.2f
                                            : 3.8f * (.8f + .2f * pack.stamina))
               : 0;
    if (moving && !pack.route.empty()) {
      const float remaining = glm::distance(leader, pack.route.back());
      speed = std::min(speed, std::sqrt(std::max(0.f, remaining - 1.f) * 5.f));
    } else if (pack.route.empty()) {
      speed = 0;
    }
    const glm::vec2 watching =
        pack.state == State::Alert ? xz(camera) - leader : glm::vec2(0);
    move(first, target, speed, watching, old);
    if (moving && current[first].speed < .2f)
      pack.stalled += kStep;
    else
      pack.stalled = 0;
    pack.heading = headings[first];
    if (glm::distance(pack.trail.front(), xz(current[first].position)) > .4f) {
      pack.trail.insert(pack.trail.begin(), xz(current[first].position));
      if (pack.trail.size() > 150)
        pack.trail.pop_back();
    }
    for (size_t j = 1; j < kPackSize; ++j) {
      const auto slot = formationTarget(pack, j);
      const auto position = xz(old[first + j].position);
      const auto error = slot.position - position;
      // Feed-forward the leader's speed along the historical trail tangent,
      // then correct slot error. Pure pursuit made followers orbit a nearly
      // reached slot, brake abruptly, and accelerate again every few steps.
      const auto desiredVelocity =
          slot.tangent * current[first].speed + error * .85f;
      const float maximum = pack.state == State::Flee ? 6.8f : 5.2f;
      float follow = std::min(maximum, glm::length(desiredVelocity));
      if (current[first].speed < .1f && glm::length(error) < .2f)
        follow = 0;
      const auto target =
          position + direction(desiredVelocity, headings[first + j]) *
                         std::max(3.f, glm::length(error));
      const size_t follower = first + j;
      recoveryCooldown[follower] -= kStep;
      const float slotError = glm::length(error);
      const bool noProgress =
          old[follower].speed < .3f ||
          previousSlotError[follower] - slotError < kStep * .1f;
      followerStalled[follower] = noProgress && slotError > 5.f
                                      ? followerStalled[follower] + kStep
                                      : 0.f;
      previousSlotError[follower] = slotError;
      if (followerStalled[follower] > 1.5f && recoveryCooldown[follower] <= 0) {
        navigation.findPath(position, slot.position, recoveryRoutes[follower]);
        recoveryWaypoint[follower] = 1;
        recoveryCooldown[follower] = 4.f;
      }
      auto &recovery = recoveryRoutes[follower];
      auto &waypoint = recoveryWaypoint[follower];
      while (waypoint < recovery.size() &&
             glm::distance(position, recovery[waypoint]) < 1.5f)
        ++waypoint;
      if (glm::length(error) < 2.f || waypoint >= recovery.size())
        recovery.clear();
      const auto movementTarget =
          recovery.empty() ? target : recovery[waypoint];
      if (!recovery.empty())
        follow = std::min(maximum, 4.f);
      move(follower, movementTarget, follow,
           pack.state == State::Alert ? xz(camera) - position : glm::vec2(0),
           old);
    }
    resolveContacts(p);
  }
}

void WolfPacks::update(float dt, glm::vec3 camera, glm::vec3 forward,
                       bool perceivePlayer) {
  if (!std::isfinite(dt) || dt <= 0)
    return;
  navigation.beginFrame();
  setForward(forward);
  const float elapsed = std::min(dt, kStep * 4);
  const auto playerVelocity = glm::distance(camera, previousObserver) > 15.f
                                  ? glm::vec2(0)
                                  : (xz(camera) - xz(previousObserver)) / dt;
  previousObserver = camera;
  encounterCheck += elapsed;
  if (encounterCheck >= 1) {
    encounterHidden = inSight(camera) ? 0 : encounterHidden + encounterCheck;
    encounterCheck = 0;
    if (encounterHidden >= 12 &&
        (!current[0].active ||
         glm::distance(xz(current[0].position), xz(camera)) > 110)) {
      spawn(0, camera);
      encounterHidden = 0;
    }
  }
  accumulator += elapsed;
  while (accumulator >= kStep) {
    tick(camera, playerVelocity, perceivePlayer);
    accumulator -= kStep;
  }
}

std::array<WolfPacks::Wolf, WolfPacks::kWolfCount>
WolfPacks::interpolated() const {
  auto result = current;
  const float t = accumulator / kStep;
  for (size_t i = 0; i < kWolfCount; ++i) {
    result[i].position = glm::mix(previous[i].position, current[i].position, t);
    if (result[i].active) {
      const float height = navigation.height(xz(result[i].position));
      if (std::isfinite(height))
        result[i].position.y = height;
      else
        result[i].active = false;
    }
    result[i].orientation =
        glm::slerp(previous[i].orientation, current[i].orientation, t);
    result[i].phase = glm::mix(previous[i].phase, current[i].phase, t);
    result[i].speed = glm::mix(previous[i].speed, current[i].speed, t);
    result[i].motionBlend =
        glm::mix(previous[i].motionBlend, current[i].motionBlend, t);
  }
  return result;
}

std::array<WolfPacks::PackStatus, WolfPacks::kPackCount>
WolfPacks::status() const {
  std::array<PackStatus, kPackCount> result{};
  for (size_t p = 0; p < kPackCount; ++p) {
    const auto &pack = packs[p];
    result[p] = {
        pack.state,     pack.route.empty() ? pack.home : pack.route.back(),
        pack.stateTime, pack.stamina,
        pack.threat,    pack.route.size(),
        pack.routePlans};
  }
  return result;
}

} // namespace vkh::wildlife
