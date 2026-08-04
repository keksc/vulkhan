#pragma once

#include <functional>
#include <memory>

namespace vkh {
struct EngineContext;
} // namespace vkh

class GameUI {
public:
  explicit GameUI(vkh::EngineContext &context);
  ~GameUI();

  GameUI(const GameUI &) = delete;
  GameUI &operator=(const GameUI &) = delete;

  void update(vkh::EngineContext &context, float frameTime);

  void render();

  bool isWorldViewActive() const;
  bool isSmokeViewActive() const;

  using KeyHandler =
      std::function<bool(int key, int scancode, int action, int mods)>;
  using FocusHandler = std::function<bool(int focused)>;
  void addWorldViewKeyHandler(KeyHandler handler);
  void addWorldViewFocusHandler(FocusHandler handler);

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
