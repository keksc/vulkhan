#pragma once
#include <algorithm>
#include <functional>
#include <numeric>
#include <vector>
namespace vkh {
template <typename Entity>
void buildEntityRenderOrder(const std::vector<Entity> &entities,
                            std::vector<size_t> &order) {
  order.resize(entities.size());
  std::iota(order.begin(), order.end(), size_t{0});
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    auto left = entities[a].scene.get(), right = entities[b].scene.get();
    if (left != right)
      return std::less<decltype(left)>{}(left, right);
    if (entities[a].meshIndex != entities[b].meshIndex)
      return entities[a].meshIndex < entities[b].meshIndex;
    return a < b;
  });
}
} // namespace vkh
