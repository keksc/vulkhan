#pragma once

#include <filesystem>
#include <string>

class Item {
protected:
  std::string name;
  std::filesystem::path modelScenePath;

public:
  Item(std::string name, std::filesystem::path modelScenePath);
  virtual void use() {};
};
