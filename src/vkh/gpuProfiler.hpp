#pragma once

#include "deviceHelpers.hpp"
#include "engineContext.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <print>
#include <vector>

namespace vkh {

// Opt-in, fence-delayed timestamps: no query waits or queue-idle stalls.
class GpuProfiler {
public:
  explicit GpuProfiler(EngineContext &context) : context(context) {
    const char *enabled = std::getenv("VULKHAN_GPU_TIMINGS");
    if (!enabled || enabled[0] != '1')
      return;
    const auto families =
        context.vulkan.physicalDevice.getQueueFamilyProperties();
    validBits = families[findPhysicalQueueFamilies(context).graphicsFamily]
                    .timestampValidBits;
    if (!validBits)
      return;
    vk::QueryPoolCreateInfo info{};
    info.queryType = vk::QueryType::eTimestamp;
    info.queryCount = kCount * context.vulkan.maxFramesInFlight;
    pool = context.vulkan.device.createQueryPool(info);
    pending.resize(context.vulkan.maxFramesInFlight, false);
  }

  ~GpuProfiler() {
    if (pool)
      context.vulkan.device.destroyQueryPool(pool);
  }
  GpuProfiler(const GpuProfiler &) = delete;
  GpuProfiler &operator=(const GpuProfiler &) = delete;

  // Call only after beginFrame has waited for this frame slot's fence.
  void begin(vk::CommandBuffer cmd, uint32_t frame) {
    if (!pool)
      return;
    base = frame * kCount;
    if (pending[frame]) {
      std::array<uint64_t, kCount> ticks{};
      auto result = context.vulkan.device.getQueryPoolResults(
          pool, base, kCount, sizeof(ticks), ticks.data(), sizeof(uint64_t),
          vk::QueryResultFlagBits::e64);
      if (result == vk::Result::eSuccess) {
        const uint64_t mask =
            validBits == 64 ? ~uint64_t{0} : (uint64_t{1} << validBits) - 1;
        const double scale =
            context.vulkan.physicalDeviceProperties.limits.timestampPeriod /
            1e6;
        for (uint32_t i = 0; i < kCount - 1; ++i)
          sums[i] += ((ticks[i + 1] - ticks[i]) & mask) * scale;
        ++samples;
        const auto now = std::chrono::steady_clock::now();
        if (now - lastReport >= std::chrono::seconds(1)) {
          double total = 0;
          std::print("GPU ms:");
          for (uint32_t i = 0; i < kCount - 1; ++i) {
            const double mean = sums[i] / samples;
            total += mean;
            std::print(" {}={:.2f}", names[i], mean);
          }
          std::println(" total={:.2f} (144fps budget=6.94)", total);
          sums.fill(0);
          samples = 0;
          lastReport = now;
        }
      }
    }
    pending[frame] = true;
    cmd.resetQueryPool(pool, base, kCount);
    mark(cmd, 0);
  }

  void mark(vk::CommandBuffer cmd, uint32_t point) {
    if (pool)
      cmd.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, pool,
                         base + point);
  }

private:
  static constexpr uint32_t kCount = 8;
  static constexpr std::array<const char *, kCount - 1> names{
      "simulation",    "reflections/waves", "scene", "strand-fur",
      "fur/particles", "postprocess",       "HUD"};
  EngineContext &context;
  vk::QueryPool pool{};
  std::vector<bool> pending;
  uint32_t base = 0;
  uint32_t validBits = 0;
  uint32_t samples = 0;
  std::array<double, kCount - 1> sums{};
  std::chrono::steady_clock::time_point lastReport =
      std::chrono::steady_clock::now();
};

} // namespace vkh
