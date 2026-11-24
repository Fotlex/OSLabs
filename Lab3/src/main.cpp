#include "ipc.hpp"
#include "platform.hpp"
#include "process.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace {

void writeLog(IpcContext& ipc, const std::string& logPath, const std::string& msg) {
    ipc.writeLogLine(
        logPath,
        "[" + platform::currentTimestamp() + "] [PID=" +
            std::to_string(platform::currentPid()) + "] " + msg);
}

int runCopy1(IpcContext& ipc, const std::string& logPath) {
    writeLog(ipc, logPath, "Copy 1 started");
    ipc.addCounter(10);
    writeLog(ipc, logPath, "Copy 1 exiting");
    return 0;
}

int runCopy2(IpcContext& ipc, const std::string& logPath) {
    writeLog(ipc, logPath, "Copy 2 started");
    ipc.multiplyCounter(2);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    ipc.divideCounter(2);
    writeLog(ipc, logPath, "Copy 2 exiting");
    return 0;
}

}

int main(int argc, char* argv[]) {
    const std::string logPath = "counter.log";
    const std::string exePath = platform::getExecutablePath(argc > 0 ? argv[0] : nullptr);

    IpcContext ipc;
    if (!ipc.init()) {
        std::cerr << "IPC initialization error\n";
        return 1;
    }

    if (argc > 1) {
        std::string mode = argv[1];
        if (mode == "--copy1") {
            return runCopy1(ipc, logPath);
        }
        if (mode == "--copy2") {
            return runCopy2(ipc, logPath);
        }
    }

    writeLog(ipc, logPath, "Process started");

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
                    writeLog(ipc, logPath, "counter=" + std::to_string(ipc.getCounter()));
                }
                while (next1s <= now) {
                    next1s += std::chrono::seconds(1);
                }
            }

            if (now >= next3s) {
                if (isLeader) {
                    if (copy1.isRunning()) {
                        writeLog(ipc, logPath, "Copy 1 is still running, skipping launch");
                    } else {
                        copy1.spawn(exePath, "--copy1");
                    }

                    if (copy2.isRunning()) {
                        writeLog(ipc, logPath, "Copy 2 is still running, skipping launch");
                    } else {
                        copy2.spawn(exePath, "--copy2");
                    }
                }
                while (next3s <= now) {
                    next3s += std::chrono::seconds(3);
                }
            }
        }

        ipc.releaseLeader();
    });

    std::cout << "Started PID=" << platform::currentPid()
              << ". Enter a number to set counter, or 'exit' to quit.\n";

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string token;
        if (!(iss >> token)) {
            continue;
        }
        if (token == "exit" || token == "quit") {
            break;
        }

        std::istringstream numIss(line);
        std::int64_t val = 0;
        char extra;
        if (numIss >> val && !(numIss >> extra)) {
            ipc.setCounter(val);
            std::cout << "Counter = " << val << "\n";
        } else {
            std::cout << "Invalid input.\n";
        }
    }

    running.store(false);
    cv.notify_all();
    if (timerThread.joinable()) {
        timerThread.join();
    }

    return 0;
}