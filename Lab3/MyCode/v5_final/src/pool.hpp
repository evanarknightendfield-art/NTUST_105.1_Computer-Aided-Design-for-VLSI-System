// pool.hpp — a small fixed thread pool (parallel SA and window DP).
#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// a fixed set of worker threads; run(n, f) calls f(0..n-1) in parallel
// and returns when all are done (cheaper than starting threads every round).
class Pool {
public:
    explicit Pool(int n) {
        try {
            for (int i = 0; i < n; ++i) th_.emplace_back([this, i] { loop(i); });
        } catch (...) {
            // A partially constructed vector of joinable threads must also
            // be stopped before propagating a thread-allocation failure.
            stop();
            throw;
        }
    }
    ~Pool() { stop(); }
    void run(int n, const std::function<void(int)>& f) {
        {
            std::lock_guard<std::mutex> l(m_);
            f_ = &f;
            n_ = n;
            error_ = nullptr;
            left_ = (int)th_.size();
            ++gen_;
        }
        cv_.notify_all();
        std::unique_lock<std::mutex> l(m_);
        done_.wait(l, [&] { return left_ == 0; });
        // Wait for every worker before unwinding its caller's placement and
        // lambda captures. main.cpp can now roll back the optional stage.
        if (error_) std::rethrow_exception(error_);
    }

private:
    void stop() {
        {
            std::lock_guard<std::mutex> l(m_);
            stop_ = true;
            ++gen_;
        }
        cv_.notify_all();
        for (auto& t : th_) t.join();
    }
    void loop(int i) {
        long seen = 0;
        for (;;) {
            const std::function<void(int)>* f;
            int n;
            {
                std::unique_lock<std::mutex> l(m_);
                cv_.wait(l, [&] { return gen_ != seen; });
                seen = gen_;
                if (stop_) return;
                f = f_;
                n = n_;
            }
            std::exception_ptr error;
            try {
                if (i < n) (*f)(i);
            } catch (...) {
                error = std::current_exception();
            }
            {
                std::lock_guard<std::mutex> l(m_);
                if (error && !error_) error_ = error;
                if (--left_ == 0) done_.notify_one();
            }
        }
    }
    std::vector<std::thread> th_;
    std::mutex m_;
    std::condition_variable cv_, done_;
    const std::function<void(int)>* f_ = nullptr;
    std::exception_ptr error_;
    int n_ = 0, left_ = 0;
    long gen_ = 0;
    bool stop_ = false;
};


// a team for many very short parallel steps (the max pass runs one step per move, ~29k moves
// of a few tens of microseconds on the 200k-cell design, where waking a blocking pool cost about as
// much as the step). The caller is member 0; members 1..n-1 spin on a generation counter between
// steps and live only as long as the team (one max pass), so no spinning thread outlasts it.
// main.cpp keeps the default team within the available CPUs; waits yield after a short spin.
class SpinTeam {
public:
    explicit SpinTeam(int n) : n_(std::max(1, n)) {
        try {
            for (int i = 1; i < n_; ++i) th_.emplace_back([this, i] { loop(i); });
        } catch (...) {
            stop();
            throw;
        }
    }
    ~SpinTeam() { stop(); }
    int size() const { return n_; }
    // f(0..n-1) with n <= size; members >= n idle this step.
    void run(int n, const std::function<void(int)>& f) {
        f_ = &f;
        active_ = n;
        left_.store(n_ - 1, std::memory_order_relaxed);
        gen_.fetch_add(1, std::memory_order_release);
        std::exception_ptr error;
        try {
            f(0);
        } catch (...) {
            error = std::current_exception();
        }
        waitUntil([&] { return left_.load(std::memory_order_acquire) == 0; });
        if (!error) error = error_;
        error_ = nullptr;
        if (error) std::rethrow_exception(error);
    }

private:
    // spin about 30 us, then yield on every check. With more threads than free CPUs a
    // member that never yields keeps the CPU the caller or a working member needs: on 8 CPUs
    // (16 members) took 6 ms per max move instead of 0.05 ms. Without other runnable threads yield
    // returns at once. L1 max pass, 16 members (spin_tune/): 4096 / 1024 / 256 / 64 spins took
    // 1.56 / 1.56 / 1.62 / 1.65 s on 16 CPUs, 6.7-16.6 / 3.1-3.9 / 2.2-2.7 / 2.0-2.5 s on 12.
    static constexpr int kSpinsBeforeYield = 1024;
    template <class Done> static void waitUntil(Done done) {
        for (int spins = 0; !done();) {
            if (spins < kSpinsBeforeYield) { ++spins; pause(); }
            else std::this_thread::yield();
        }
    }
    static void pause() {
#if defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause();
#else
        std::this_thread::yield();
#endif
    }
    void stop() {
        stop_.store(true, std::memory_order_release);
        gen_.fetch_add(1, std::memory_order_release);
        for (auto& t : th_) t.join();
    }
    void loop(int i) {
        long seen = 0;
        for (;;) {
            long g = seen;
            waitUntil([&] { return (g = gen_.load(std::memory_order_acquire)) != seen; });
            seen = g;
            if (stop_.load(std::memory_order_acquire)) return;
            if (i < active_) {
                try {
                    (*f_)(i);
                } catch (...) {
                    std::lock_guard<std::mutex> l(m_);
                    if (!error_) error_ = std::current_exception();
                }
            }
            left_.fetch_sub(1, std::memory_order_acq_rel);
        }
    }
    int n_;
    std::vector<std::thread> th_;
    std::atomic<long> gen_{0};
    std::atomic<int> left_{0};
    std::atomic<bool> stop_{false};
    const std::function<void(int)>* f_ = nullptr;   // written before gen_ (release), read after (acquire)
    int active_ = 0;
    std::mutex m_;
    std::exception_ptr error_;
};
