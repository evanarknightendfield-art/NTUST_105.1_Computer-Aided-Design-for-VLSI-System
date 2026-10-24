// pool.hpp — v4: a small fixed thread pool (parallel SA and window DP).
#pragma once
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// v4: a fixed set of worker threads; run(n, f) calls f(0..n-1) in parallel
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

