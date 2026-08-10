#pragma once
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <string>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <limits.h>
#include <unistd.h>
#else
#error "Unsupported platform"
#endif
namespace paths {
inline std::filesystem::path getExecutablePath() {
#if defined(_WIN32)
  char buffer[MAX_PATH];
  DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
  return std::filesystem::path(buffer, buffer + len);
#elif defined(__APPLE__)
  char buffer[1024];
  uint32_t size = sizeof(buffer);
  _NSGetExecutablePath(buffer, &size);
  return std::filesystem::canonical(buffer);
#elif defined(__linux__)
  char buffer[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  buffer[len] = '\0';
  return std::filesystem::path(buffer);
#endif
}
inline std::filesystem::path getExecutableDir() {
  return getExecutablePath().parent_path();
}
inline void setWorkingDirectoryToExecutable() {
  std::filesystem::current_path(getExecutableDir());
}
// Returns the OS-appropriate cache directory for appName, e.g.:
//   Linux:   $XDG_CACHE_HOME/<appName>  (falls back to ~/.cache/<appName>)
//   macOS:   ~/Library/Caches/<appName>
//   Windows: %LOCALAPPDATA%\<appName>\cache
// Creates the directory (and any missing parents) if it doesn't already
// exist.
inline std::filesystem::path getCacheDir(const std::string &appName) {
  std::filesystem::path dir;
#if defined(_WIN32)
  if (const char *localAppData = std::getenv("LOCALAPPDATA")) {
    dir = std::filesystem::path(localAppData) / appName / "cache";
  } else if (const char *userProfile = std::getenv("USERPROFILE")) {
    dir = std::filesystem::path(userProfile) / "AppData" / "Local" / appName /
          "cache";
  } else {
    dir = std::filesystem::path(".") / "cache";
  }
#elif defined(__APPLE__)
  if (const char *home = std::getenv("HOME")) {
    dir = std::filesystem::path(home) / "Library" / "Caches" / appName;
  } else {
    dir = std::filesystem::path(".") / "cache";
  }
#elif defined(__linux__)
  if (const char *xdgCache = std::getenv("XDG_CACHE_HOME");
      xdgCache && *xdgCache != '\0') {
    dir = std::filesystem::path(xdgCache) / appName;
  } else if (const char *home = std::getenv("HOME")) {
    dir = std::filesystem::path(home) / ".cache" / appName;
  } else {
    dir = std::filesystem::path(".") / "cache";
  }
#endif

  std::error_code ec;
  std::filesystem::create_directories(dir, ec);

  return dir;
}

namespace detail {
inline std::filesystem::path &cacheDirStorage() {
  static std::filesystem::path dir;
  return dir;
}
} // namespace detail

// Call once at startup with your app's name. Resolves and creates the cache
// directory immediately, so later cacheDir() calls are just a lookup.
inline void initCacheDir(const std::string &appName) {
  detail::cacheDirStorage() = getCacheDir(appName);
}

// Returns the path resolved by initCacheDir(). Must be called after
// initCacheDir(); asserts otherwise.
inline const std::filesystem::path &cacheDir() {
  assert(!detail::cacheDirStorage().empty() &&
         "paths::cacheDir() called before paths::initCacheDir()");
  return detail::cacheDirStorage();
}
} // namespace paths
