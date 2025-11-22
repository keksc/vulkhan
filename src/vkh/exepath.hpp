#pragma once
#include <filesystem>
#include <string>

#if defined(_WIN32)
    #include <windows.h>

#elif defined(__APPLE__)
    #include <mach-o/dyld.h>

#elif defined(__linux__)
    #include <unistd.h>
    #include <limits.h>

#else
    #error "Unsupported platform"
#endif

namespace execpath {

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

} // namespace execpath
