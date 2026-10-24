#include "../src/pool.hpp"
#include "../src/placedb.hpp"
#ifdef __linux__
#include <sched.h>
#endif
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    Pool pool(4);
    std::atomic<int> completed{0};
    bool caught = false;
    try {
        pool.run(4, [&](int i) {
            if (i == 1) throw std::runtime_error("worker failed");
            ++completed;
        });
    } catch (const std::runtime_error& error) {
        caught = std::string(error.what()) == "worker failed";
    }
    require(caught, "worker exception did not reach the caller");
    require(completed == 3, "run returned before every worker completed");
    completed = 0;
    pool.run(2, [&](int) { ++completed; });
    require(completed == 2, "pool could not be reused after an exception");
    pool.run(0, [&](int) { throw std::runtime_error("idle worker ran"); });
    caught = false;
    try {
        pool.run(4, [&](int) { throw 42; });
    } catch (int value) {
        caught = value == 42;
    }
    require(caught, "non-standard worker exception did not propagate");
    completed = 0;
    pool.run(4, [&](int) { ++completed; });
    require(completed == 4, "pool was poisoned by multiple exceptions");
#ifdef __linux__
    // v7: availableCpus() follows the affinity mask (taskset / cpusets), unlike hardware_concurrency()
    cpu_set_t all, one;
    require(sched_getaffinity(0, sizeof(all), &all) == 0, "sched_getaffinity failed");
    require(availableCpus() == CPU_COUNT(&all), "availableCpus differs from the affinity mask");
    int first = 0;
    while (!CPU_ISSET(first, &all)) ++first;
    CPU_ZERO(&one);
    CPU_SET(first, &one);
    require(sched_setaffinity(0, sizeof(one), &one) == 0, "sched_setaffinity failed");
    require(availableCpus() == 1, "availableCpus ignores a one-CPU mask");
    require(sched_setaffinity(0, sizeof(all), &all) == 0, "affinity not restored");
#endif
    std::cout << "PASS: worker exceptions, completion barrier, idle workers, pool reuse, availableCpus\n";
}
