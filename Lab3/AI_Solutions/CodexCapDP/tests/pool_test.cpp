#include "../src/pool.hpp"
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
    std::cout << "PASS: worker exceptions, completion barrier, idle workers, pool reuse\n";
}
