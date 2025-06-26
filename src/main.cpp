#include "vkh/vulkhan.hpp"

// #include <GameAnalytics/GameAnalytics.h>
#include <fmt/format.h>

#include <stdexcept>

/*void setupAnalytics() {
  gameanalytics::GameAnalytics::setEnabledInfoLog(false);
  gameanalytics::GameAnalytics::setEnabledVerboseLog(false);

  gameanalytics::GameAnalytics::configureBuild("alpha");

  gameanalytics::GameAnalytics::configureAvailableResourceCurrencies(
      {"sanity"});
  gameanalytics::GameAnalytics::initialize(
      "99cf79ca1f6f9a87f9438ac39067640b",
      "7974f4f0c8f62b2f947aa5bf6a67aa81bbec784e");
}*/

#if defined(WIN32) && defined(NDEBUG)
#include <windows.h>
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine,
                   int nCmdShow) {
#else
int main() {
#endif
  // setupAnalytics();
  try {
    vkh::run();
  } catch (const std::exception &e) {
    fmt::print("{}\n", e.what());
    // gameanalytics::GameAnalytics::addErrorEvent(
    //     gameanalytics::EGAErrorSeverity::Critical, e.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
