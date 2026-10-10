#include "../src/vkh/systems/entity/instanceSlots.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
struct Model {
  std::string name;
  std::vector<size_t> meshes;
};
int main() {
  vkh::InstanceSlots<Model> registry, other;
  auto first = registry.insert({"multi-mesh", {0, 1, 2}});
  auto second = registry.insert({"single", {3}});
  assert(registry.get(first)->meshes.size() == 3);
  assert(!other.get(first));
  for (int i = 0; i < 10000; ++i)
    registry.insert({"bulk", {size_t(i + 4)}});
  assert(registry.get(first)->name == "multi-mesh");
  assert(registry.get(second)->name == "single");
  assert(registry.erase(first));
  assert(!registry.get(first) && !registry.erase(first));
  auto replacement = registry.insert({"replacement", {0}});
  assert(replacement.slot == first.slot &&
         replacement.generation != first.generation);
  assert(!registry.get(first) && registry.get(replacement));
  assert(registry.get(second));
  assert(!registry.get({}));
  for (int i = 0; i < 10000; ++i) {
    assert(registry.erase(replacement));
    auto stale = replacement;
    replacement = registry.insert({"reused", {0}});
    assert(!registry.get(stale) && registry.get(replacement));
  }
  std::cout << "Stable handles, bulk growth, ownership and stale-handle checks "
               "passed\n";
}
