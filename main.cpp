#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include <gperftools/profiler.h>

namespace {

std::atomic<bool> g_stop{false};
std::atomic<bool> g_profile_active{false};
std::atomic<uint64_t> g_progress{0};
std::atomic<uint64_t> g_exceptions{0};

__attribute__((noinline)) void ThrowLikeBusinessRefresh() {
    // 只制造普通 C++ 异常展开；这里不直接调用 dl_iterate_phdr。
    const std::string empty;
    std::stoll(empty);
}

void WorkerMain() {
    // 注册当前线程，让 profiler 采样覆盖这个异常展开热点。
    ProfilerRegisterThread();

    while (!g_profile_active.load(std::memory_order_acquire) &&
           !g_stop.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    while (!g_stop.load(std::memory_order_relaxed)) {
        try {
            ThrowLikeBusinessRefresh();
        } catch (const std::exception&) {
            g_exceptions.fetch_add(1, std::memory_order_relaxed);
        }
        g_progress.fetch_add(1, std::memory_order_release);
    }
}

void WatchdogMain() {
    // watchdog 只在 worker 停滞后 abort，方便留下 core。
    uint64_t last = g_progress.load(std::memory_order_acquire);
    int stale_seconds = 0;

    while (!g_stop.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!g_profile_active.load(std::memory_order_acquire)) {
            last = g_progress.load(std::memory_order_acquire);
            stale_seconds = 0;
            continue;
        }

        const uint64_t current = g_progress.load(std::memory_order_acquire);
        if (current == last) {
            ++stale_seconds;
        } else {
            last = current;
            stale_seconds = 0;
        }

        if (stale_seconds >= 5) {
            std::cerr << "watchdog: worker progress stalled for "
                      << stale_seconds << " seconds; aborting" << std::endl;
            std::raise(SIGABRT);
        }
    }
}

}  // namespace

int main() {
    std::thread worker(WorkerMain);
    std::thread watchdog(WatchdogMain);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    const char* profile_path = "cpu_profile.out";
    std::cout << "starting gperftools CPU profiler: " << profile_path << std::endl;
    if (!ProfilerStart(profile_path)) {
        std::cerr << "ProfilerStart failed" << std::endl;
        g_stop.store(true, std::memory_order_relaxed);
        worker.join();
        watchdog.join();
        return 1;
    }
    g_profile_active.store(true, std::memory_order_release);

    uint64_t last = g_progress.load(std::memory_order_acquire);
    for (int sec = 1; sec <= 20; ++sec) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const uint64_t current = g_progress.load(std::memory_order_acquire);
        std::cout << "tick " << sec
                  << " progress_delta=" << (current - last)
                  << " progress=" << current
                  << " exceptions=" << g_exceptions.load(std::memory_order_relaxed)
                  << std::endl;
        last = current;
    }

    g_profile_active.store(false, std::memory_order_release);
    ProfilerStop();

    g_stop.store(true, std::memory_order_relaxed);
    worker.join();
    watchdog.join();

    std::cout << "done: progress=" << g_progress.load(std::memory_order_relaxed)
              << " exceptions=" << g_exceptions.load(std::memory_order_relaxed)
              << std::endl;
    return 0;
}
