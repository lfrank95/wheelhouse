#pragma once
// Wheelhouse - small persistent worker pool for splitting the room rendering across CPU cores.
//
// setJob(fn) once, then run(jobs) executes fn(job) for job = 0..jobs-1 on the workers and the calling thread and returns when all
// are done. Workers spin briefly after a job (the next sub-block usually follows within a millisecond) and then
// sleep on a condition variable, so an idle pool costs nothing. No allocation happens in run().

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <chrono>
#include <cstdint>

namespace leslie {

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
        if (threads == 1 || jobs <= 1) { for (int j = 0; j < jobs; j++) job(j); return; }
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
    static constexpr uint64_t NEXT_MASK = (1ull << COUNT_SHIFT) - 1, COUNT_MASK = (1ull << (GEN_SHIFT - COUNT_SHIFT)) - 1;

    void workOnJobs() {
        for (;;) {
            uint64_t v = state.fetch_add(1, std::memory_order_acq_rel);
            uint64_t j = v & NEXT_MASK, count = (v >> COUNT_SHIFT) & COUNT_MASK;
            if (j >= count) break;
            job((int) j);
            done.fetch_add(1, std::memory_order_acq_rel);
        }
    }
    void workerLoop() {
        uint64_t seen = 0;
        for (;;) {
            // spin for a short while: sub-blocks arrive every ~1.3 ms, a sleeping thread would add wake-up latency
            auto t0 = std::chrono::steady_clock::now();
            while ((state.load(std::memory_order_acquire) >> GEN_SHIFT) == seen && !quit.load(std::memory_order_acquire)) {
                if (std::chrono::steady_clock::now() - t0 > std::chrono::microseconds(300)) {
                    std::unique_lock<std::mutex> lock(mtx);
                    cv.wait_for(lock, std::chrono::milliseconds(2), [&] { return (state.load(std::memory_order_acquire) >> GEN_SHIFT) != seen || quit.load(); });
                    t0 = std::chrono::steady_clock::now();
                }
            }
            if (quit.load(std::memory_order_acquire)) return;
            seen = state.load(std::memory_order_acquire) >> GEN_SHIFT;
            workOnJobs();
        }
    }
    void stop() {
        if (workers.empty()) return;
        { std::lock_guard<std::mutex> lock(mtx); quit = true; }
        cv.notify_all();
        for (auto& w : workers) w.join();
        workers.clear();
    }

    int threads = 1;
    std::vector<std::thread> workers;
    std::function<void(int)> job;
    std::atomic<uint64_t> state { 0 };
    std::atomic<int> done { 0 };
    std::atomic<bool> quit { false };
    std::mutex mtx;
    std::condition_variable cv;
};

} // namespace leslie
