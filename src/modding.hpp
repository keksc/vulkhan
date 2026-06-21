#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace wasmtime {
class Engine;
class Store;
class Linker;
class Module;
class Instance;
} // namespace wasmtime

class ModManager;
class Mod {
public:
  Mod(const Mod &) = delete;
  Mod &operator=(const Mod &) = delete;

  Mod(Mod &&) noexcept = default;
  Mod &operator=(Mod &&) noexcept = default;

  Mod(ModManager &mgr, std::filesystem::path path);
  ~Mod();

  void instantiate();
  void init();

  std::unique_ptr<wasmtime::Module> module;
  std::unique_ptr<wasmtime::Instance> instance;

  bool isDead() const;

private:
  void crash();

  ModManager &mgr;

  bool dead{false};

  std::string name;
};

class ModManager {
public:
  ModManager();
  ~ModManager();

  wasmtime::Engine &getEngine();
  wasmtime::Store &getStore();
  wasmtime::Linker &getLinker();

private:
  std::unique_ptr<wasmtime::Engine> engine;
  std::unique_ptr<wasmtime::Store> store;
  std::unique_ptr<wasmtime::Linker> linker;
  std::map<std::string, Mod> mods;
};
