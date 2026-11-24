#include "ipc.hpp"
#include "platform.hpp"
#include "process.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

enum class RunMode {
    Normal,
    Copy1,
    Copy2
};

bool parseInt64(const std::string& s, std::int64_t& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    long long val = std::strtoll(s.c_str(), &end, 10);
    if (errno != 0 || end == s.c_str() || *end != '\0') {
        return false;
    }
    out = static_cast<std::int64_t>(val);
    return true;
}

std::string trim(const std::string& s) {
    std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    std::size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

void printHelp() {
    std::cout << "Commands:\n"
              << "  <number>       Set counter to <number>\n"
              << "  set <number>   Set counter to <number>\n"
              << "  get            Show current counter and process role\n"
              << "  help           Show this help\n"
              << "  exit / quit    Exit program\n";
}

int runCopy1(IpcContext& ipc, const std::string& logPath) {
    const long long pid = platform::currentPid();
    ipc.writeLogLine(logPath,
        "[" + platform::currentTimestamp() + "] [PID=" + std::to_string(pid) + "] Copy 1 started");

    ipc.addCounter(10);

    ipc.writeLogLine(logPath,
        "[" + platform::currentTimestamp() + "] [PID=" + std::to_string(pid) + "] Copy 1 exiting");
    return 0;
}

int runCopy2(IpcContext& ipc, const std::string& logPath) {
    const long long pid = platform::currentPid();
    ipc.writeLogLine(logPath,
        "[" + platform::currentTimestamp() + "] [PID=" + std::to_string(pid) + "] Copy 2 started");

    ipc.multiplyCounter(2);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    ipc.divideCounter(2);

    ipc.writeLogLine(logPath,
        "[" + platform::currentTimestamp() + "] [PID=" + std::to_string(pid) + "] Copy 2 exiting");
    return 0;
}

}

int main(int argc, char* argv[]) {
    RunMode mode = RunMode::Normal;
    std::string logPath = "counter.log";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--copy1") {
            mode = RunMode::Copy1;
        } else if (arg == "--copy2") {
            mode = RunMode::Copy2;
        } else if (arg == "--log" && i + 1 < argc) {
            logPath = argv[++i];
        }
    }

    logPath = platform::makeAbsolutePath(logPath);
    const std::string exePath = platform::getExecutablePath(argc > 0 ? argv[0] : nullptr);

    IpcContext ipc;
    std::string err;
    if (!ipc.init(err)) {
        std::cerr << "IPC initialization error: " << err << std::endl;
        return 1;
    }

    if (mode == RunMode::Copy1) {
        return runCopy1(ipc, logPath);
    }
    if (mode == RunMode::Copy2) {
        return runCopy2(ipc, logPath);
    }

    const long long pid = platform::currentPid();
    ipc.writeLogLine(logPath,
        "[" + platform::currentTimestamp() + "] [PID=" + std::to_string(pid) + "] Process started");

    std::atomic<bool> running(true);
    std::mutex cvMutex;
    std::condition_variable cv;

    std::thread timerThread([&]() {
        using clock = std::chrono::steady_clock;
        auto now = clock::now();
        auto next300ms = now + std::chrono::milliseconds(300);
        auto next1s = now + std::chrono::seconds(1);
        auto next3s = now + std::chrono::seconds(3);

        ChildProcess copy1;
        ChildProcess copy2;

        ipc.tryAcquireLeader();

        while (running.load()) {
            auto wakeup = std::min(next300ms, std::min(next1s, next3s));
            {
                std::unique_lock<std::mutex> lock(cvMutex);
                cv.wait_until(lock, wakeup, [&]() { return !running.load(); });
            }
            if (!running.load()) {
                break;
            }

            now = clock::now();

            bool isLeader = ipc.tryAcquireLeader();

            if (now >= next300ms) {
                ipc.addCounter(1);
                while (next300ms <= now) {
                    next300ms += std::chrono::milliseconds(300);
                }
            }

            if (now >= next1s) {
                if (isLeader) {
                    std::int64_t val = ipc.getCounter();
                    ipc.writeLogLine(logPath,
                        "[" + platform::currentTimestamp() + "] [PID=" +
                        std::to_string(pid) + "] counter=" + std::to_string(val));
                }
                while (next1s <= now) {
                    next1s += std::chrono::seconds(1);
                }
            }

            if (now >= next3s) {
                if (isLeader) {
                    if (copy1.isRunning()) {
                        ipc.writeLogLine(logPath,
                            "[" + platform::currentTimestamp() + "] [PID=" +
                            std::to_string(pid) + "] Copy 1 is still running, skipping launch");
                    } else {
                        copy1.spawn(exePath, {"--copy1", "--log", logPath});
                    }

                    if (copy2.isRunning()) {
                        ipc.writeLogLine(logPath,
                            "[" + platform::currentTimestamp() + "] [PID=" +
                            std::to_string(pid) + "] Copy 2 is still running, skipping launch");
                    } else {
                        copy2.spawn(exePath, {"--copy2", "--log", logPath});
                    }
                }
                while (next3s <= now) {
                    next3s += std::chrono::seconds(3);
                }
            }
        }

        ipc.releaseLeader();
    });

    std::cout << "Started PID=" << pid << ", log=" << logPath << "\n";
    printHelp();

    std::string line;
    while (running.load()) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) {
            if (platform::isStdinTty()) {
                break;
            }
            while (running.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            break;
        }

        line = trim(line);
        if (line.empty()) {
            continue;
        }

        if (line == "exit" || line == "quit") {
            break;
        } else if (line == "help") {
            printHelp();
        } else if (line == "get") {
            std::cout << "counter=" << ipc.getCounter()
                      << " (" << (ipc.isLeader() ? "leader" : "worker")
                      << ", PID=" << pid << ")\n";
        } else {
            std::string numStr = line;
            if (line.rfind("set ", 0) == 0) {
                numStr = trim(line.substr(4));
            }
            std::int64_t newVal = 0;
            if (parseInt64(numStr, newVal)) {
                ipc.setCounter(newVal);
                std::cout << "Counter set to " << newVal << "\n";
            } else {
                std::cout << "Unknown command or invalid integer. Type 'help'.\n";
            }
        }
    }

    running.store(false);
    cv.notify_all();
    if (timerThread.joinable()) {
        timerThread.join();
    }

    return 0;
}