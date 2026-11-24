#include "process.hpp"

#include <vector>

#ifndef _WIN32
    #include <cerrno>
    #include <fcntl.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

ChildProcess::ChildProcess()
    : active_(false)
#ifdef _WIN32
      , hProcess_(NULL)
#else
      , pid_(-1)
#endif
{}

ChildProcess::~ChildProcess() {
#ifdef _WIN32
    if (hProcess_) {
        CloseHandle(hProcess_);
    }
#endif
}

bool ChildProcess::spawn(const std::string& exePath, const std::string& arg) {
    if (isRunning()) {
        return false;
    }

#ifdef _WIN32
    std::string cmdLine = "\"" + exePath + "\" " + arg;
    std::vector<char> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back('\0');

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    BOOL ok = CreateProcessA(
        NULL, mutableCmd.data(), NULL, NULL, FALSE,
        DETACHED_PROCESS, NULL, NULL, &si, &pi);

    if (!ok) {
        return false;
    }

    CloseHandle(pi.hThread);
    hProcess_ = pi.hProcess;
    active_ = true;
    return true;
#else
    pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }

        char* argv[] = {
            const_cast<char*>(exePath.c_str()),
            const_cast<char*>(arg.c_str()),
            nullptr
        };
        execv(exePath.c_str(), argv);
        _exit(127);
    }

    pid_ = pid;
    active_ = true;
    return true;
#endif
}

bool ChildProcess::isRunning() {
    if (!active_) {
        return false;
    }

#ifdef _WIN32
    if (WaitForSingleObject(hProcess_, 0) == WAIT_TIMEOUT) {
        return true;
    }
    CloseHandle(hProcess_);
    hProcess_ = NULL;
    active_ = false;
    return false;
#else
    int status = 0;
    pid_t res;
    do {
        res = waitpid(pid_, &status, WNOHANG);
    } while (res == -1 && errno == EINTR);

    if (res == 0) {
        return true;
    }
    pid_ = -1;
    active_ = false;
    return false;
#endif
}