#ifndef IPC_HPP
#define IPC_HPP

#include <cstdint>
#include <mutex>
#include <string>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

struct SharedState {
    std::int64_t counter;
};

class IpcContext {
public:
    IpcContext();
    ~IpcContext();

    IpcContext(const IpcContext&) = delete;
    IpcContext& operator=(const IpcContext&) = delete;

    bool init(std::string& errorMsg);
    void cleanup();

    std::int64_t getCounter();
    void setCounter(std::int64_t value);
    std::int64_t addCounter(std::int64_t delta);
    std::int64_t multiplyCounter(std::int64_t factor);
    std::int64_t divideCounter(std::int64_t divisor);

    void writeLogLine(const std::string& logPath, const std::string& line);

    bool tryAcquireLeader();
    void releaseLeader();
    bool isLeader() const { return leader_; }

private:
    void lockCounter();
    void unlockCounter();
    void lockLog();
    void unlockLog();

    SharedState* shared_;
    bool leader_;
    bool initialized_;

    std::mutex localCounterMutex_;
    std::mutex localLogMutex_;
    std::mutex localLeaderMutex_;

#ifdef _WIN32
    HANDLE hCounterMutex_;
    HANDLE hLogMutex_;
    HANDLE hLeaderMutex_;
    HANDLE hMapFile_;
#else
    int counterLockFd_;
    int logLockFd_;
    int leaderLockFd_;
    int activeLockFd_;
    int shmFd_;
#endif
};

#endif