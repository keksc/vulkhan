#pragma once
#include "../vkh/systems/hud/hud.hpp"
#include "../vkh/systems/hud/view.hpp"

namespace UI::theme {
inline size_t color(vkh::hud::View &view, uint32_t rgb, uint8_t alpha = 255) {
  const uint32_t rgba = ((rgb >> 16) & 255) | ((rgb >> 8) & 255) << 8 |
                        (rgb & 255) << 16 | uint32_t(alpha) << 24;
  return view.hudSys.solidColorSys.addColor(rgba);
}
constexpr uint32_t background = 0x070707, panel = 0x151515, border = 0x4a4a4a;
constexpr uint32_t button = 0x202020, hover = 0x441414, pressed = 0x741818,
                   accent = 0xe63b3b;
inline void initialize(vkh::hud::View &view) {
  for (auto c : {background, panel, border, button, hover, pressed, accent})
    color(view, c);
}
} // namespace UI::theme
