#include "../src/vkh/systems/wolf/wolfPacks.hpp"

#include <glm/gtx/quaternion.hpp>

#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>

using vkh::wildlife::WolfNavigation;
using vkh::wildlife::WolfPacks;

int main() {
  // A direct line crosses this lake. A* must produce a clear detour, and
  // each smoothed segment must respect body clearance rather than cut it.
  auto lake = [](glm::vec2 p) {
    return p.x > 12 && p.x < 32 && p.y > -10 && p.y < 10;
  };
  WolfNavigation nav([](glm::vec2) { return 0.f; }, lake);
  std::vector<glm::vec2> path;
  assert(!nav.canTraverse({0, 0}, {45, 0}));
  assert(nav.findPath({0, 0}, {45, 0}, path));
  assert(path.size() > 2);
  for (size_t i = 1; i < path.size(); ++i)
    assert(nav.canTraverse(path[i - 1], path[i]));
  assert(glm::distance(path.front(), glm::vec2(0)) < .01f);
  assert(glm::distance(path.back(), glm::vec2(45, 0)) < .01f);
  // Safe points at a shore must connect to reachable grid cells even when
  // rounding their coordinates would put the nearest centre in the lake.
  assert(nav.findPath({11.2f, 5}, {35, 5}, path));
  for (size_t i = 1; i < path.size(); ++i)
    assert(nav.canTraverse(path[i - 1], path[i]));
  // A cliff / enclosed target must return failure, never a fake direct path.
  WolfNavigation cliff([](glm::vec2 p) { return p.x > 5 ? 30.f : 0.f; }, {});
  assert(!cliff.findPath({0, 0}, {20, 0}, path));
  WolfNavigation enclosed([](glm::vec2) { return 0.f; },
                          [](glm::vec2 p) {
                            const float r = glm::length(p - glm::vec2(30, 0));
                            return r > 5 && r < 10;
                          });
  assert(!enclosed.findPath({0, 0}, {30, 0}, path));

  // All failed searches in a frame share a terrain-query budget. Node
  // limits alone allow hundreds of thousands of height evaluations.
  int queries = 0;
  WolfNavigation bounded(
      [&](glm::vec2) {
        ++queries;
        return 0.f;
      },
      [](glm::vec2 p) {
        const float r = glm::length(p - glm::vec2(30, 0));
        return r > 5 && r < 10;
      });
  for (int i = 0; i < 20; ++i)
    assert(!bounded.findPath({0, 0}, {30, 0}, path));
  assert(queries <= WolfNavigation::kFrameSampleBudget);
  bounded.beginFrame();
  assert(bounded.findPath({0, 0}, {-10, 0}, path));

  // Fast travel can outrun streamed terrain. Unavailable heights must never
  // enter visible transforms; packs return after the terrain becomes ready.
  bool loaded = false;
  WolfPacks streaming(
      [&](glm::vec2) {
        return loaded ? 0.f : std::numeric_limits<float>::quiet_NaN();
      },
      {}, {0, 1.8f, 0});
  for (const auto &wolf : streaming.interpolated())
    assert(!wolf.active);
  loaded = true;
  for (int frame = 0; frame < 360; ++frame)
    streaming.update(1.f / 60.f, {0, 1.8f, 0}, {0, 0, -1}, false);
  for (const auto &wolf : streaming.interpolated()) {
    assert(wolf.active && std::isfinite(wolf.position.y));
    assert(std::isfinite(wolf.orientation.w));
  }
  loaded = false;
  streaming.update(.1f, {0, 1.8f, 0}, {0, 0, -1}, false);
  for (const auto &wolf : streaming.interpolated()) {
    assert(!wolf.active && std::isfinite(wolf.position.y));
    assert(std::isfinite(wolf.orientation.w));
  }

  // A small streamed area contains the animals but none of their full
  // patrol destinations. They must select a reachable local site, rather
  // than repeatedly accept a zero-length route home and remain at rest.
  auto limitedHeight = [](glm::vec2 p) {
    return glm::length(p) < 85.f ? 0.f
                                 : std::numeric_limits<float>::quiet_NaN();
  };
  WolfPacks limited(limitedHeight, {}, {0, 1.8f, 0});
  auto lastLimited = limited.interpolated()[0].position;
  assert(limited.interpolated()[0].active);
  float localTravel = 0;
  for (int frame = 0; frame < 3600; ++frame) {
    limited.update(1.f / 60.f, {0, 1.8f, 0}, {0, 0, -1}, false);
    const auto wolves = limited.interpolated();
    assert(wolves[0].active);
    localTravel += glm::distance(wolves[0].position, lastLimited);
    lastLimited = wolves[0].position;
    for (size_t i = 0; i < WolfPacks::kPackSize; ++i)
      assert(std::isfinite(
          limitedHeight({wolves[i].position.x, wolves[i].position.z})));
    const auto status = limited.status()[0];
    if (status.state == WolfPacks::State::Travel)
      assert(glm::distance(
                 status.destination,
                 glm::vec2(wolves[0].position.x, wolves[0].position.z)) > .5f);
  }
  assert(localTravel > 30.f);

  // Close observation in a partially streamed world must still produce
  // escape movement; failed 65 m routes cannot starve shorter safe routes.
  WolfPacks localEscape(limitedHeight, {}, {0, 1.8f, 0});
  const auto escapeStart = localEscape.interpolated()[0].position;
  const auto escapeCamera = escapeStart + glm::vec3(0, 1.8f, 5);
  for (int frame = 0; frame < 600; ++frame)
    localEscape.update(1.f / 60.f, escapeCamera);
  assert(glm::distance(localEscape.interpolated()[0].position, escapeStart) >
         15.f);

  // A lake arriving from streaming must trigger a detour to the original
  // destination, not a random replacement goal or a walk through water.
  bool streamedLake = false;
  glm::vec2 lakeCentre{}, along{}, across{};
  auto dynamicLake = [&](glm::vec2 p) {
    const auto delta = p - lakeCentre;
    return streamedLake && std::abs(glm::dot(delta, along)) < 7.f &&
           std::abs(glm::dot(delta, across)) < 13.f;
  };
  WolfPacks detour([](glm::vec2) { return 0.f; }, dynamicLake, {0, 1.8f, 0});
  const auto detourStart = detour.interpolated()[0].position;
  const auto detourGoal = detour.status()[0].destination;
  const glm::vec2 startXZ(detourStart.x, detourStart.z);
  along = glm::normalize(detourGoal - startXZ);
  across = {-along.y, along.x};
  lakeCentre = (startXZ + detourGoal) * .5f;
  streamedLake = true;
  bool routedAround = false, arrived = false;
  for (int frame = 0; frame < 3600; ++frame) {
    detour.update(1.f / 60.f, {0, 1.8f, 0}, {0, 0, -1}, false);
    const auto wolves = detour.interpolated();
    const auto state = detour.status()[0];
    for (size_t i = 0; i < WolfPacks::kPackSize; ++i)
      assert(!dynamicLake({wolves[i].position.x, wolves[i].position.z}));
    if (!arrived && state.routePoints > 2) {
      assert(glm::distance(state.destination, detourGoal) < .01f);
      routedAround = true;
    }
    if (glm::distance(glm::vec2(wolves[0].position.x, wolves[0].position.z),
                      detourGoal) < 3.f)
      arrived = true;
  }
  assert(routedAround && arrived);

  WolfPacks patrol([](glm::vec2) { return 0.f; }, {}, {0, 1.8f, 0});
  auto initial = patrol.interpolated();
  auto status = patrol.status();
  const auto firstDestination = status[0].destination;
  int restFrames = 0, movingFrames = 0;
  float maxSpread = 0, closestPair = 100;
  auto previous = initial;
  const auto start = std::chrono::steady_clock::now();
  for (int frame = 0; frame < 10800; ++frame) {
    // Disabled perception lets a spectator observe a complete territory
    // circuit, rather than becoming a threat merely by following its camera.
    patrol.update(1.f / 60.f, {0, 1.8f, 0}, {0, 0, -1}, false);
    auto wolves = patrol.interpolated();
    status = patrol.status();
    if (frame < 300)
      assert(glm::distance(status[0].destination, firstDestination) < .01f);
    if (status[0].state == WolfPacks::State::Rest) {
      ++restFrames;
      if (status[0].stateTime > 2) {
        assert(wolves[0].speed < .05f);
        assert(wolves[0].motionBlend < .05f);
      }
    } else if (status[0].state == WolfPacks::State::Travel) {
      ++movingFrames;
    }
    for (size_t p = 0; p < WolfPacks::kPackCount; ++p) {
      const size_t first = p * WolfPacks::kPackSize;
      for (size_t j = 1; j < WolfPacks::kPackSize; ++j)
        maxSpread =
            std::max(maxSpread, glm::distance(wolves[first].position,
                                              wolves[first + j].position));
      for (size_t j = 0; j < WolfPacks::kPackSize; ++j)
        for (size_t k = j + 1; k < WolfPacks::kPackSize; ++k)
          closestPair =
              std::min(closestPair, glm::distance(wolves[first + j].position,
                                                  wolves[first + k].position));
    }
    for (size_t i = 0; i < wolves.size(); ++i) {
      assert(std::abs(wolves[i].speed - previous[i].speed) < .2f);
      // Bounded yaw rules out the old instant 90-degree obstacle turns.
      const auto a = previous[i].orientation * glm::vec3(0, 0, -1);
      const auto b = wolves[i].orientation * glm::vec3(0, 0, -1);
      assert(glm::dot(a, b) > .995f);
    }
    previous = wolves;
  }
  const auto cpuTime = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - start)
                           .count();
  assert(restFrames > 600 && movingFrames > 3000);
  assert(status[0].routePlans >= 4 && status[0].routePlans < 20);
  assert(maxSpread < 32.f);
  assert(closestPair > .7f);

  // An approaching player is a shared pack threat, not independent random
  // motion. The group must flee, increase distance, then calm down later.
  WolfPacks reaction([](glm::vec2) { return 0.f; }, {}, {0, 1.8f, 0});
  auto wolves = reaction.interpolated();
  const glm::vec3 threat = wolves[0].position + glm::vec3(0, 1.8f, 8);
  for (int frame = 0; frame < 60; ++frame)
    reaction.update(1.f / 60.f, threat);
  assert(reaction.status()[0].state == WolfPacks::State::Flee);
  const float near = glm::distance(reaction.interpolated()[0].position, threat);
  for (int frame = 0; frame < 600; ++frame)
    reaction.update(1.f / 60.f, threat);
  const float far = glm::distance(reaction.interpolated()[0].position, threat);
  assert(far > near + 25);
  for (int frame = 0; frame < 1200; ++frame)
    reaction.update(1.f / 60.f, threat);
  assert(reaction.status()[0].state != WolfPacks::State::Flee);

  // A stationary observer at moderate range elicits watching, and small
  // movements around the detection boundary don't cause state thrashing.
  WolfPacks watching([](glm::vec2) { return 0.f; }, {}, {0, 1.8f, 0});
  const auto leader = watching.interpolated()[0].position;
  const glm::vec3 observer = leader + glm::vec3(0, 1.8f, 29);
  watching.update(1.f / 60.f, observer, {0, 0, -1}, false);
  for (int frame = 0; frame < 120; ++frame)
    watching.update(1.f / 60.f, observer);
  assert(watching.status()[0].state == WolfPacks::State::Alert);
  assert(watching.interpolated()[0].speed < .1f);
  for (int frame = 0; frame < 45; ++frame)
    watching.update(1.f / 60.f,
                    observer + glm::vec3(std::sin(frame * .1f), 0, 0));
  assert(watching.status()[0].state == WolfPacks::State::Alert);

  // Watching a stationary player must not freeze the pack indefinitely.
  const auto alertPosition = watching.interpolated()[0].position;
  for (int frame = 0; frame < 600; ++frame)
    watching.update(1.f / 60.f, observer);
  assert(glm::distance(watching.interpolated()[0].position, alertPosition) >
         20.f);
  assert(watching.status()[0].state != WolfPacks::State::Alert);

  // A ridge occludes every member of the pack from a stationary observer.
  bool occlude = false;
  float wallZ = 0;
  auto terrain = [&](glm::vec2 p) {
    return occlude && std::abs(p.y - wallZ) < 1.8f ? 15.f : 0.f;
  };
  WolfPacks hidden(terrain, {}, {0, 1.8f, 0});
  const auto hiddenLeader = hidden.interpolated()[0].position;
  const glm::vec3 hiddenObserver = hiddenLeader + glm::vec3(0, 1.8f, 38);
  wallZ = hiddenLeader.z + 26.f;
  hidden.update(1.f / 60.f, hiddenObserver, {0, 0, -1}, false);
  occlude = true;
  for (int frame = 0; frame < 120; ++frame)
    hidden.update(1.f / 60.f, hiddenObserver);
  assert(hidden.status()[0].state != WolfPacks::State::Alert);
  assert(hidden.status()[0].state != WolfPacks::State::Flee);

  // A spectator following the pack must never become a perceived threat.
  WolfPacks spectator([](glm::vec2) { return 0.f; }, {}, {0, 1.8f, 0});
  for (int frame = 0; frame < 300; ++frame)
    spectator.update(1.f / 60.f,
                     spectator.interpolated()[0].position + glm::vec3(0, 2, 5),
                     {0, 0, -1}, false);
  assert(spectator.status()[0].state != WolfPacks::State::Alert);
  assert(spectator.status()[0].state != WolfPacks::State::Flee);

  std::cout
      << "Wolf AI: patrol/rest, formations, turning limits, escape, alert "
         "hysteresis and navigation passed; spread="
      << maxSpread << " min pair=" << closestPair
      << " average CPU update=" << cpuTime / 10800
      << "ms (synthetic terrain)\n";
}
