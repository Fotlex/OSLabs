#include "process.hpp"

#ifndef _WIN32
    #include <cerrno>
    #include <fcntl.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

namespace {
#ifdef _WIN32
std::string quoteWindowsArg(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) {
        return arg;
    }
    std::string out = "\"";
    for (std::size_t i = 0; i < arg.size(); ++i) {
        std::size_t backslashes = 0;
        while (i < arg.size() && arg[i] == '\\') {
            ++backslashes;
            ++i;
        }
        if (i == arg.size()) {
            out.append(backslashes * 2, '\\');
            break;
        } else if (arg[i] == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
        } else {
            out.append(backslashes, '\\');
            out.push_back(arg[i]);
        }
    }
    out.push_back('"');
    return out;
}
#endif
}

ChildProcess::ChildProcess()
    : active_(false)
#ifdef _WIN32
      , hProcess_(NULL)
#else
      , pid_(-1)
#endif
{}

ChildProcess::~ChildProcess() {
    detach();
}

bool ChildProcess::spawn(const std::string& exePath, const std::vector<std::string>& args) {
    if (isRunning()) {
        return false;
    }

#ifdef _WIN32
    std::string cmdLine = quoteWindowsArg(exePath);
    for (const auto& a : args) {
        cmdLine += " " + quoteWindowsArg(a);
    }

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::vector<char> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back('\0');

    BOOL ok = CreateProcessA(
        NULL,
        mutableCmd.data(),
        NULL,
        NULL,
        FALSE,
        DETACHED_PROCESS,
        NULL,
        NULL,
        &si,
        &pi);

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

        std::vector<char*> argv;
        argv.reserve(args.size() + 2);
        argv.push_back(const_cast<char*>(exePath.c_str()));
        for (const auto& a : args) {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);

        execv(exePath.c_str(), argv.data());
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
    DWORD res = WaitForSingleObject(hProcess_, 0);
    if (res == WAIT_TIMEOUT) {
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

void ChildProcess::detach() {
    if (!active_) {
        return;
    }
#ifdef _WIN32
    if (hProcess_) {
        CloseHandle(hProcess_);
        hProcess_ = NULL;
    }
#else
    pid_ = -1;
#endif
    active_ = false;
}