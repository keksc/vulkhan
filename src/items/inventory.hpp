#pragma once

#include "item.hpp"

#include <memory>
#include <vector>

class Inventory {
  std::vector<std::unique_ptr<Item>> items;
  size_t selectedItem = 0;
};
