#include "wolfNavigation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>

namespace vkh::wildlife {

WolfNavigation::WolfNavigation(HeightFn height, BlockedFn blocked)
    : heightFn(std::move(height)), blockedFn(std::move(blocked)) {
  if (!heightFn)
    throw std::invalid_argument("Wolf navigation needs a height function");
}

float WolfNavigation::height(glm::vec2 p) const {
  if (planning) {
    if (searchSamplesRemaining <= 0 || frameSamplesRemaining <= 0)
      return std::numeric_limits<float>::quiet_NaN();
    --searchSamplesRemaining;
    --frameSamplesRemaining;
  }
  return heightFn(p);
}

bool WolfNavigation::canStand(glm::vec2 p) const {
  if (!std::isfinite(height(p)))
    return false;
  // Clearance for the body, not just a point at its centre.
  if (blockedFn)
    for (glm::vec2 offset :
         {glm::vec2(0), glm::vec2(.65f, 0), glm::vec2(-.65f, 0),
          glm::vec2(0, .65f), glm::vec2(0, -.65f)})
      if (blockedFn(p + offset))
        return false;
  return true;
}

bool WolfNavigation::canTraverse(glm::vec2 from, glm::vec2 to) const {
  const float distance = glm::distance(from, to);
  const int steps = std::max(1, int(std::ceil(distance / .25f)));
  float previousHeight = height(from);
  if (!std::isfinite(previousHeight))
    return false;
  for (int i = 1; i <= steps; ++i) {
    const auto p = glm::mix(from, to, float(i) / steps);
    if (!canStand(p))
      return false;
    const float h = height(p);
    if (std::abs(h - previousHeight) > distance / steps * .85f + .015f)
      return false;
    previousHeight = h;
  }
  return true;
}

bool WolfNavigation::findPath(glm::vec2 from, glm::vec2 to,
                              std::vector<glm::vec2> &path,
                              bool allowDetour) const {
  path.clear();
  // Node count alone doesn't bound terrain queries: every edge and smoothed
  // segment samples at body-clearance resolution. Share a query budget across
  // all route attempts this frame, including follower recovery.
  planning = true;
  searchSamplesRemaining = kSearchSampleBudget;
  struct EndPlanning {
    bool &flag;
    ~EndPlanning() { flag = false; }
  } endPlanning{planning};
  if (!canStand(from) || !canStand(to))
    return false;
  if (canTraverse(from, to)) {
    path = {from, to};
    return true;
  }
  if (!allowDetour)
    return false;
  constexpr int side = 49, centre = side / 2, count = side * side;
  constexpr float cell = 4.f;
  const auto extent = glm::abs(to - from);
  if (std::max(extent.x, extent.y) > (side - 4) * cell)
    return false;
  const auto origin = (from + to) * .5f - glm::vec2(centre * cell);
  auto id = [](int x, int z) { return z * side + x; };
  auto position = [&](int i) {
    return origin + glm::vec2(i % side, i / side) * cell;
  };
  const glm::ivec2 target = glm::ivec2(glm::round((to - origin) / cell));
  if (target.x < 1 || target.y < 1 || target.x >= side - 1 ||
      target.y >= side - 1)
    return false;
  const glm::ivec2 source = glm::ivec2(glm::round((from - origin) / cell));
  // A point can be safely beside a shore while its nearest grid centre is
  // in water. Connect to a reachable neighbour instead of declaring the
  // whole path impossible (this matters for stragglers at obstacle edges).
  auto endpoint = [&](glm::vec2 point, glm::ivec2 nominal) {
    int selected = -1;
    float nearest = std::numeric_limits<float>::infinity();
    for (int dz = -1; dz <= 1; ++dz)
      for (int dx = -1; dx <= 1; ++dx) {
        const int x = nominal.x + dx, z = nominal.y + dz;
        if (x < 0 || z < 0 || x >= side || z >= side)
          continue;
        const int candidate = id(x, z);
        const float distance = glm::distance(point, position(candidate));
        if (distance < nearest && canStand(position(candidate)) &&
            canTraverse(point, position(candidate))) {
          selected = candidate;
          nearest = distance;
        }
      }
    return selected;
  };
  const int start = endpoint(from, source), end = endpoint(to, target);
  if (start < 0 || end < 0)
    return false;
  std::array<float, count> cost;
  cost.fill(std::numeric_limits<float>::infinity());
  std::array<int, count> parent;
  parent.fill(-1);
  std::array<bool, count> closed{};
  struct Open {
    float score;
    int id;
    bool operator>(const Open &other) const {
      return score == other.score ? id > other.id : score > other.score;
    }
  };
  std::priority_queue<Open, std::vector<Open>, std::greater<Open>> open;
  cost[start] = 0;
  open.push({glm::distance(from, to), start});
  int expanded = 0;
  bool reached = false;
  while (!open.empty() && expanded < kExpansionBudget) {
    if (searchSamplesRemaining <= 0 || frameSamplesRemaining <= 0)
      return false;
    const int current = open.top().id;
    open.pop();
    if (closed[current])
      continue;
    closed[current] = true;
    ++expanded;
    if (current == end) {
      reached = true;
      break;
    }
    const auto here = position(current);
    for (int dz = -1; dz <= 1; ++dz)
      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dz == 0)
          continue;
        const int x = current % side + dx, z = current / side + dz;
        if (x < 0 || z < 0 || x >= side || z >= side)
          continue;
        const int next = id(x, z);
        if (closed[next])
          continue;
        const auto there = position(next);
        if (!canTraverse(here, there))
          continue;
        if (dx && dz &&
            (!canStand(here + glm::vec2(dx * cell, 0)) ||
             !canStand(here + glm::vec2(0, dz * cell))))
          continue;
        // Prefer gentler terrain without ruling out uphill routes.
        const float candidate = cost[current] + glm::distance(here, there) +
                                std::abs(height(there) - height(here)) * 1.5f;
        if (candidate < cost[next]) {
          cost[next] = candidate;
          parent[next] = current;
          open.push({candidate + glm::distance(there, to), next});
        }
      }
  }
  if (!reached || !canTraverse(position(end), to))
    return false;
  std::vector<glm::vec2> raw{to};
  for (int i = end; i != -1; i = parent[i])
    raw.push_back(position(i));
  std::reverse(raw.begin(), raw.end());
  raw.insert(raw.begin(), from);
  path.push_back(from);
  for (size_t i = 0; i + 1 < raw.size();) {
    size_t farthest = i + 1;
    for (size_t j = i + 2; j < raw.size(); ++j)
      if (canTraverse(raw[i], raw[j]))
        farthest = j;
      else
        break;
    path.push_back(raw[farthest]);
    i = farthest;
  }
  return true;
}

} // namespace vkh::wildlife
