#ifndef PROCESS_HPP
#define PROCESS_HPP

#include <string>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <sys/types.h>
#endif

class ChildProcess {
public:
    ChildProcess();
    ~ChildProcess();

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    bool spawn(const std::string& exePath, const std::string& arg);
    bool isRunning();

private:
    bool active_;
#ifdef _WIN32
    HANDLE hProcess_;
#else
    pid_t pid_;
#endif
};

#endif