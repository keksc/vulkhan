#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <print>
#include <type_traits>

#include "vkh/paths.hpp"

namespace vkh {

// Plain-old-data settings blob, written and read as a raw byte dump. Rules
// that keep that safe:
//  - keep every field POD (bools/ints/floats -- no pointers, std::string,
//    std::vector, etc; the static_assert below enforces this at compile
//    time)
//  - prefer *appending* new fields at the end over reordering/removing one:
//    load() below tolerantly reads however many bytes exist into a
//    default-initialized struct, so an old, shorter settings.bin still
//    loads fine and any new fields just come back at their defaults
//  - bump kVersion only when you reorder or change the type of an existing
//    field -- that's the one kind of change old bytes would silently
//    misinterpret rather than merely miss
struct Settings {
  static constexpr uint32_t kVersion = 1;

  uint32_t version = kVersion;

  bool vsync = true; // TODO: not wired up yet -- needs the swapchain's
                     // VkPresentModeKHR selection, which lives in code I
                     // don't have visibility into. Share swapChain.hpp/
                     // renderer.hpp if you want this one wired too.
  bool showFps = true;    // applied live in UI.cpp (GameUI::Impl::update)
  bool invertYAxis = false; // TODO: not wired up yet -- needs the mouse-look
                            // delta code, likely in input.cpp/camera.cpp,
                            // which I don't have visibility into.
  bool fullscreen = false; // applied live in UI.cpp via glfwSetWindowMonitor
  // Whether SkySys should be allowed to load its baked environment (milky
  // way cubemap + accretion disc turbulence) from disk instead of
  // recomputing it on the GPU every launch. SkySys itself doesn't know
  // about Settings -- whoever constructs it reads this and passes it in
  // as a plain bool. See sky.hpp/sky.cpp and main.cpp.
  bool useCachedSkyBake = true;
};

static_assert(std::is_trivially_copyable_v<Settings>,
             "Settings is dumped/loaded as raw bytes -- every field must "
             "stay trivially copyable");

namespace settings {

inline Settings &current() {
  static Settings s{};
  return s;
}

inline std::filesystem::path filePath() {
  return paths::cacheDir() / "settings.bin";
}

// Loads settings.bin from the cache directory into current(), if present
// and its version matches. Tolerates a size mismatch from appended
// fields: reads min(file size, sizeof(Settings)) bytes into a
// default-initialized struct, so fields not present in an older file just
// keep their defaults instead of the whole file being rejected. Call once
// at startup, after paths::initCacheDir().
inline void load() {
  std::ifstream file(filePath(), std::ios::binary);
  if (!file)
    return; // no settings file yet -- first launch, keep defaults

  // Peek the version field first so a genuinely incompatible (reordered/
  // retyped) file gets discarded outright rather than partially applied.
  uint32_t version = 0;
  file.read(reinterpret_cast<char *>(&version), sizeof(version));
  if (!file || version != Settings::kVersion) {
    std::println("settings.bin is missing or from a different version, "
                 "using defaults");
    return;
  }

  file.seekg(0, std::ios::end);
  auto fileSize = static_cast<size_t>(file.tellg());
  file.seekg(0);

  Settings loaded{}; // defaults for any bytes not present in the file
  file.read(reinterpret_cast<char *>(&loaded),
           std::min(fileSize, sizeof(Settings)));
  if (!file && !file.eof()) {
    std::println("settings.bin could not be read, using defaults");
    return;
  }

  current() = loaded;
}

// Dumps current() as a raw byte blob to settings.bin. Cheap enough to call
// after every single toggle, which is what the settings UI does -- that
// way settings survive a crash, not just a clean exit.
inline void save() {
  std::error_code ec;
  std::filesystem::create_directories(paths::cacheDir(), ec);

  std::ofstream file(filePath(), std::ios::binary | std::ios::trunc);
  if (!file) {
    std::println("failed to open {} for writing", filePath().string());
    return;
  }

  current().version = Settings::kVersion;
  file.write(reinterpret_cast<const char *>(&current()), sizeof(Settings));
}

} // namespace settings
} // namespace vkh
