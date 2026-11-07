#pragma once
// Wheelhouse - small persistent worker pool for splitting the room rendering across CPU cores.
//
// setJob(fn) once, then run(jobs) executes fn(job) for job = 0..jobs-1 on the workers and the calling thread and
// returns when all are done. Workers spin briefly after a job (the next sub-block usually follows within a millisecond)
// and then sleep on a condition variable, so an idle pool costs nothing. No allocation happens in run().

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <chrono>
#include <cstdint>
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif
#if defined(_WIN32)
// Declared by hand (identical to the Windows headers) so this header does not pull <windows.h> into JUCE code.
extern "C" __declspec(dllimport) void* __stdcall GetCurrentThread();
extern "C" __declspec(dllimport) int __stdcall SetThreadPriority(void* thread, int priority);
extern "C"
    __declspec(dllimport) void* __stdcall AvSetMmThreadCharacteristicsW(const wchar_t* task, unsigned long* index);
extern "C" __declspec(dllimport) int __stdcall AvSetMmThreadPriority(void* handle, int priority);
#if defined(_MSC_VER)
#pragma comment(lib, "avrt.lib")
#endif
#elif defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#include <sched.h>
#endif

namespace rotary {

class WorkerPool {
public:
    WorkerPool() = default;
    ~WorkerPool() { stop(); }

    /** Number of threads including the caller; 1 = no workers (everything runs on the calling thread). */
    void setThreads(int total) {
        total = total < 1 ? 1 : total > 64 ? 64 : total;
        if (total == threads) return;
        stop();
        threads = total;
        if (threads > 1) {
            quit = false;
            for (int i = 1; i < threads; i++) workers.emplace_back([this] { workerLoop(); });
        }
    }
    int getThreads() const { return threads; }

    /** The job function is fixed for the pool's lifetime (set once, so run() never touches it). */
    void setJob(std::function<void(int)> fn) { job = std::move(fn); }

    /** Runs job(0..jobs-1) on all threads and returns when every job has finished. */
    void run(int jobs) {
        if (threads == 1 || jobs <= 1) {
            for (int j = 0; j < jobs; j++) job(j);
            return;
        }
        // One atomic word describes the batch: generation | job count | next job index. A worker claims a job with
        // fetch_add and sees generation and count together, so a straggler from the previous batch can never run a
        // job of the next one twice or early.
        const uint64_t gen = (state.load(std::memory_order_relaxed) >> GEN_SHIFT) + 1;
        done.store(0, std::memory_order_relaxed);
        state.store((gen << GEN_SHIFT) | ((uint64_t) jobs << COUNT_SHIFT), std::memory_order_release);
        { std::lock_guard<std::mutex> lock(mtx); }
        cv.notify_all();
        workOnJobs();
        while (done.load(std::memory_order_acquire) < jobs) std::this_thread::yield();
    }

private:
    static constexpr int GEN_SHIFT = 40, COUNT_SHIFT = 24;
    static constexpr uint64_t NEXT_MASK = (1ull << COUNT_SHIFT) - 1,
                              COUNT_MASK = (1ull << (GEN_SHIFT - COUNT_SHIFT)) - 1;

    void workOnJobs() {
        for (;;) {
            uint64_t v = state.fetch_add(1, std::memory_order_acq_rel);
            uint64_t j = v & NEXT_MASK, count = (v >> COUNT_SHIFT) & COUNT_MASK;
            if (j >= count) break;
            job((int) j);
            done.fetch_add(1, std::memory_order_acq_rel);
        }
    }
    // Workers do audio work the driver is waiting for: they must not lose the CPU to the UI or the browser view.
    // Same treatment as the audio thread: real-time priority (Windows: MMCSS "Pro Audio") and denormals flushed
    // to zero (decaying filters would otherwise slow down by a factor of 100 on x86).
    static void prepareThread() {
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
        _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
        _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#elif defined(__aarch64__) || defined(__arm64__)
        uint64_t fpcr; // Apple Silicon / ARM64: FZ bit of the floating-point control register
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        asm volatile("msr fpcr, %0" : : "r"(fpcr | (uint64_t(1) << 24)));
#endif
#if defined(_WIN32)
        unsigned long index = 0;
        void* task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &index);
        if (task) AvSetMmThreadPriority(task, 1 /* AVRT_PRIORITY_HIGH */);
        SetThreadPriority(GetCurrentThread(), 15 /* THREAD_PRIORITY_TIME_CRITICAL */);
#elif defined(__unix__) || defined(__APPLE__)
        sched_param sp{};
        sp.sched_priority = sched_get_priority_max(SCHED_FIFO) - 2;
        pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp); // needs permission; stays normal otherwise
#endif
    }
    void workerLoop() {
        prepareThread();
        uint64_t seen = 0;
        for (;;) {
            // spin for a short while: sub-blocks arrive every ~1.3 ms, a sleeping thread would add wake-up latency
            // (only right after a job: an idle pool sleeps and costs nothing, even at real-time priority)
            const auto t0 = std::chrono::steady_clock::now();
            while ((state.load(std::memory_order_acquire) >> GEN_SHIFT) == seen &&
                   !quit.load(std::memory_order_acquire)) {
                if (std::chrono::steady_clock::now() - t0 > std::chrono::microseconds(300)) {
                    std::unique_lock<std::mutex> lock(mtx);
                    cv.wait(lock, [&] {
                        return (state.load(std::memory_order_acquire) >> GEN_SHIFT) != seen || quit.load();
                    });
                }
            }
            if (quit.load(std::memory_order_acquire)) return;
            seen = state.load(std::memory_order_acquire) >> GEN_SHIFT;
            workOnJobs();
        }
    }
    void stop() {
        if (workers.empty()) return;
        {
            std::lock_guard<std::mutex> lock(mtx);
            quit = true;
        }
        cv.notify_all();
        for (auto& w : workers) w.join();
        workers.clear();
    }

    int threads = 1;
    std::vector<std::thread> workers;
    std::function<void(int)> job;
    std::atomic<uint64_t> state{0};
    std::atomic<int> done{0};
    std::atomic<bool> quit{false};
    std::mutex mtx;
    std::condition_variable cv;
};

} // namespace rotary
