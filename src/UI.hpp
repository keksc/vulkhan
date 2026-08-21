#pragma once

#include <cstdint>
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

  using KeyHandler =
      std::function<bool(int key, int scancode, int action, int mods)>;
  using FocusHandler = std::function<bool(int focused)>;
  void addWorldViewKeyHandler(KeyHandler handler);
  void addWorldViewFocusHandler(FocusHandler handler);

  // Called whenever the settings-menu sky resolution button is pressed,
  // with the newly selected (milkyWayFaceSize, discTurbulenceSize) pair.
  // main.cpp wires this to SkySys::rebake() so the change takes effect
  // live rather than only on next launch.
  using SkyResolutionHandler = std::function<void(uint32_t milkyWayFaceSize,
                                                    uint32_t discTurbulenceSize)>;
  void setSkyResolutionHandler(SkyResolutionHandler handler);

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
