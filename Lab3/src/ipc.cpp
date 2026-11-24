#include "ipc.hpp"

#include <fstream>

#ifndef _WIN32
    #include <cerrno>
    #include <fcntl.h>
    #include <sys/file.h>
    #include <sys/mman.h>
    #include <unistd.h>

namespace {
int flockRetry(int fd, int op) {
    int rc;
    do {
        rc = flock(fd, op);
    } while (rc == -1 && errno == EINTR);
    return rc;
}
}
#endif

IpcContext::IpcContext()
    : shared_(nullptr),
      leader_(false),
      initialized_(false)
#ifdef _WIN32
      , hCounterMutex_(NULL),
      hLogMutex_(NULL),
      hLeaderMutex_(NULL),
      hMapFile_(NULL)
#else
      , counterLockFd_(-1),
      logLockFd_(-1),
      leaderLockFd_(-1),
      activeLockFd_(-1),
      shmFd_(-1)
#endif
{}

IpcContext::~IpcContext() {
    cleanup();
}

bool IpcContext::init() {
    if (initialized_) {
        return true;
    }

#ifdef _WIN32
    hCounterMutex_ = CreateMutexA(NULL, FALSE, "Local\\LabCounterMutex_v1");
    hLogMutex_ = CreateMutexA(NULL, FALSE, "Local\\LabLogMutex_v1");
    hLeaderMutex_ = CreateMutexA(NULL, FALSE, "Local\\LabLeaderMutex_v1");
    hMapFile_ = CreateFileMappingA(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
        0, sizeof(SharedState), "Local\\LabCounterShm_v1");

    if (!hCounterMutex_ || !hLogMutex_ || !hLeaderMutex_ || !hMapFile_) {
        cleanup();
        return false;
    }

    shared_ = static_cast<SharedState*>(
        MapViewOfFile(hMapFile_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState)));
    if (!shared_) {
        cleanup();
        return false;
    }
#else
    counterLockFd_ = open("/tmp/lab_counter_mutex_v1.lock", O_CREAT | O_RDWR, 0666);
    logLockFd_ = open("/tmp/lab_log_mutex_v1.lock", O_CREAT | O_RDWR, 0666);
    leaderLockFd_ = open("/tmp/lab_leader_v1.lock", O_CREAT | O_RDWR, 0666);
    activeLockFd_ = open("/tmp/lab_active_v1.lock", O_CREAT | O_RDWR, 0666);
    shmFd_ = open("/tmp/lab_counter_v1.shm", O_CREAT | O_RDWR, 0666);

    if (counterLockFd_ < 0 || logLockFd_ < 0 || leaderLockFd_ < 0 ||
        activeLockFd_ < 0 || shmFd_ < 0) {
        cleanup();
        return false;
    }

    if (ftruncate(shmFd_, sizeof(SharedState)) != 0) {
        cleanup();
        return false;
    }

    void* ptr = mmap(NULL, sizeof(SharedState), PROT_READ | PROT_WRITE, MAP_SHARED, shmFd_, 0);
    if (ptr == MAP_FAILED) {
        cleanup();
        return false;
    }
    shared_ = static_cast<SharedState*>(ptr);

    lockCounter();
    if (flockRetry(activeLockFd_, LOCK_EX | LOCK_NB) == 0) {
        shared_->counter = 0;
    }
    flockRetry(activeLockFd_, LOCK_SH);
    unlockCounter();
#endif

    initialized_ = true;
    return true;
}

void IpcContext::cleanup() {
    releaseLeader();

#ifdef _WIN32
    if (shared_) { UnmapViewOfFile(shared_); shared_ = nullptr; }
    if (hMapFile_) { CloseHandle(hMapFile_); hMapFile_ = NULL; }
    if (hLeaderMutex_) { CloseHandle(hLeaderMutex_); hLeaderMutex_ = NULL; }
    if (hLogMutex_) { CloseHandle(hLogMutex_); hLogMutex_ = NULL; }
    if (hCounterMutex_) { CloseHandle(hCounterMutex_); hCounterMutex_ = NULL; }
#else
    if (shared_) { munmap(shared_, sizeof(SharedState)); shared_ = nullptr; }
    if (shmFd_ >= 0) { close(shmFd_); shmFd_ = -1; }
    if (activeLockFd_ >= 0) { flockRetry(activeLockFd_, LOCK_UN); close(activeLockFd_); activeLockFd_ = -1; }
    if (leaderLockFd_ >= 0) { close(leaderLockFd_); leaderLockFd_ = -1; }
    if (logLockFd_ >= 0) { close(logLockFd_); logLockFd_ = -1; }
    if (counterLockFd_ >= 0) { close(counterLockFd_); counterLockFd_ = -1; }
#endif

    initialized_ = false;
}

void IpcContext::lockCounter() {
    localCounterMutex_.lock();
#ifdef _WIN32
    WaitForSingleObject(hCounterMutex_, INFINITE);
#else
    flockRetry(counterLockFd_, LOCK_EX);
#endif
}

void IpcContext::unlockCounter() {
#ifdef _WIN32
    ReleaseMutex(hCounterMutex_);
#else
    flockRetry(counterLockFd_, LOCK_UN);
#endif
    localCounterMutex_.unlock();
}

void IpcContext::lockLog() {
    localLogMutex_.lock();
#ifdef _WIN32
    WaitForSingleObject(hLogMutex_, INFINITE);
#else
    flockRetry(logLockFd_, LOCK_EX);
#endif
}

void IpcContext::unlockLog() {
#ifdef _WIN32
    ReleaseMutex(hLogMutex_);
#else
    flockRetry(logLockFd_, LOCK_UN);
#endif
    localLogMutex_.unlock();
}

std::int64_t IpcContext::getCounter() {
    lockCounter();
    std::int64_t val = shared_->counter;
    unlockCounter();
    return val;
}

void IpcContext::setCounter(std::int64_t value) {
    lockCounter();
    shared_->counter = value;
    unlockCounter();
}

std::int64_t IpcContext::addCounter(std::int64_t delta) {
    lockCounter();
    shared_->counter += delta;
    std::int64_t val = shared_->counter;
    unlockCounter();
    return val;
}

std::int64_t IpcContext::multiplyCounter(std::int64_t factor) {
    lockCounter();
    shared_->counter *= factor;
    std::int64_t val = shared_->counter;
    unlockCounter();
    return val;
}

std::int64_t IpcContext::divideCounter(std::int64_t divisor) {
    lockCounter();
    if (divisor != 0) {
        shared_->counter /= divisor;
    }
    std::int64_t val = shared_->counter;
    unlockCounter();
    return val;
}

void IpcContext::writeLogLine(const std::string& logPath, const std::string& line) {
    lockLog();
    std::ofstream ofs(logPath.c_str(), std::ios::out | std::ios::app);
    if (ofs.is_open()) {
        ofs << line << '\n';
        ofs.flush();
    }
    unlockLog();
}

bool IpcContext::tryAcquireLeader() {
    if (leader_) {
        return true;
    }
#ifdef _WIN32
    DWORD res = WaitForSingleObject(hLeaderMutex_, 0);
    if (res == WAIT_OBJECT_0 || res == WAIT_ABANDONED) {
        leader_ = true;
        return true;
    }
    return false;
#else
    if (flockRetry(leaderLockFd_, LOCK_EX | LOCK_NB) == 0) {
        leader_ = true;
        return true;
    }
    return false;
#endif
}

void IpcContext::releaseLeader() {
    if (!leader_) {
        return;
    }
#ifdef _WIN32
    if (hLeaderMutex_) {
        ReleaseMutex(hLeaderMutex_);
    }
#else
    if (leaderLockFd_ >= 0) {
        flockRetry(leaderLockFd_, LOCK_UN);
    }
#endif
    leader_ = false;
}