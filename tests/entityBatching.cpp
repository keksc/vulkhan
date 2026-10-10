#include "../src/vkh/systems/entity/batchOrder.hpp"
#include <cassert>
#include <iostream>
#include <memory>
struct Entity {
  std::shared_ptr<int> scene;
  size_t meshIndex;
  int id;
};
int main() {
  auto a = std::make_shared<int>(1), b = std::make_shared<int>(2);
  std::vector<Entity> entities;
  for (int i = 0; i < 100; ++i) {
    entities.push_back({a, 0, i * 3});
    entities.push_back({b, 1, i * 3 + 1});
    entities.push_back({a, 1, i * 3 + 2});
  }
  std::vector<size_t> order;
  vkh::buildEntityRenderOrder(entities, order);
  size_t groups = 0;
  std::vector<bool> seen(entities.size());
  for (size_t i = 0; i < order.size(); ++i) {
    assert(!seen[order[i]]);
    seen[order[i]] = true;
    assert(entities[order[i]].id == int(order[i]));
    if (!i || entities[order[i]].scene != entities[order[i - 1]].scene ||
        entities[order[i]].meshIndex != entities[order[i - 1]].meshIndex)
      ++groups;
  }
  assert(groups == 3);
  for (size_t i = 0; i < entities.size(); ++i)
    assert(entities[i].id == int(i));
  auto first = order;
  vkh::buildEntityRenderOrder(entities, order);
  assert(first == order);
  entities.clear();
  vkh::buildEntityRenderOrder(entities, order);
  assert(order.empty());
  std::cout << "Interleaved models batch into three groups without moving "
               "entity identities\n";
}
