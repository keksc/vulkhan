#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <utility>

namespace vkh {

// Opt-in CPU hitch diagnostics, including time blocked on frame fences.
class CpuScope {
public:
  explicit CpuScope(const char *name) : name(name) {
    if (enabled())
      start = Clock::now();
  }
  ~CpuScope() {
    if (!enabled())
      return;
    const double ms =
        std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    if (ms >= 8.)
      std::fprintf(stderr, "[CPU hitch] %s: %.2f ms\n", name, ms);
  }
  CpuScope(const CpuScope &) = delete;
  CpuScope &operator=(const CpuScope &) = delete;

private:
  using Clock = std::chrono::steady_clock;
  static bool enabled() {
    static const bool value = [] {
      const char *flag = std::getenv("VULKHAN_CPU_PROFILE");
      return flag && flag[0] == '1';
    }();
    return value;
  }
  const char *name;
  Clock::time_point start{};
};

template <typename Fn> decltype(auto) profileCpu(const char *name, Fn &&fn) {
  CpuScope scope(name);
  return std::invoke(std::forward<Fn>(fn));
}

} // namespace vkh
