#include "vkh/vulkhan.hpp"

#include <fmt/core.h>

#include <stdexcept>

#if defined(WIN32) && defined(NDEBUG)
#include <windows.h>
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    PSTR lpCmdLine, int nCmdShow) {
#else
int main() {
#endif
  try {
    vkh::run();
  } catch (const std::exception &e) {
    fmt::print("{}\n", e.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
