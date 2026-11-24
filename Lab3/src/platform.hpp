#ifndef PLATFORM_HPP
#define PLATFORM_HPP

#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <direct.h>
    #include <io.h>
    #include <stdio.h>
#else
    #include <limits.h>
    #include <stdlib.h>
    #include <unistd.h>
    #ifdef __APPLE__
        #include <mach-o/dyld.h>
    #endif
#endif

namespace platform {

inline long long currentPid() {
#ifdef _WIN32
    return static_cast<long long>(GetCurrentProcessId());
#else
    return static_cast<long long>(getpid());
#endif
}

inline std::string currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf;
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
                  tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec,
                  static_cast<int>(ms.count()));
    return std::string(buf);
}

inline bool isStdinTty() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

inline std::string getExecutablePath(const char* argv0) {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::string(buf, len);
    }
    return argv0 ? std::string(argv0) : std::string();
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        char resolved[PATH_MAX];
        if (realpath(buf, resolved)) {
            return std::string(resolved);
        }
        return std::string(buf);
    }
    return argv0 ? std::string(argv0) : std::string();
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return std::string(buf);
    }
    if (argv0) {
        char resolved[PATH_MAX];
        if (realpath(argv0, resolved)) {
            return std::string(resolved);
        }
        return std::string(argv0);
    }
    return std::string();
#endif
}

inline std::string makeAbsolutePath(const std::string& path) {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetFullPathNameA(path.c_str(), MAX_PATH, buf, NULL);
    if (len > 0 && len < MAX_PATH) {
        return std::string(buf, len);
    }
    return path;
#else
    if (!path.empty() && path[0] == '/') {
        return path;
    }
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) {
        return std::string(cwd) + "/" + path;
    }
    return path;
#endif
}

} namespace platform

#endif