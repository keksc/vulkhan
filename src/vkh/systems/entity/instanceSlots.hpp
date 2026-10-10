#pragma once
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>
namespace vkh {
struct InstanceHandle {
  uint32_t slot = std::numeric_limits<uint32_t>::max();
  uint64_t generation = 0;
  const void *owner = nullptr;
  bool operator==(const InstanceHandle &) const = default;
};
// Dense rendering remains separate from stable public identity.
template <typename T> class InstanceSlots {
public:
  InstanceSlots() = default;
  InstanceSlots(const InstanceSlots &) = delete;
  InstanceSlots &operator=(const InstanceSlots &) = delete;
  InstanceHandle insert(T value) {
    uint32_t index;
    if (free.empty()) {
      index = slots.size();
      slots.emplace_back();
    } else {
      index = free.back();
      free.pop_back();
    }
    slots[index].value.emplace(std::move(value));
    return handle(index);
  }
  InstanceHandle handle(uint32_t index) const {
    if (index >= slots.size() || !slots[index].value)
      return {};
    return {index, slots[index].generation, this};
  }
  T *get(InstanceHandle id) {
    return const_cast<T *>(std::as_const(*this).get(id));
  }
  const T *get(InstanceHandle id) const {
    if (id.owner != this || id.slot >= slots.size())
      return nullptr;
    const auto &slot = slots[id.slot];
    return slot.generation == id.generation && slot.value ? &*slot.value
                                                          : nullptr;
  }
  bool erase(InstanceHandle id) {
    if (!get(id))
      return false;
    slots[id.slot].value.reset();
    ++slots[id.slot].generation;
    free.push_back(id.slot);
    return true;
  }

private:
  struct Slot {
    std::optional<T> value;
    uint64_t generation = 1;
  };
  std::vector<Slot> slots;
  std::vector<uint32_t> free;
};
} // namespace vkh
