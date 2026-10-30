#pragma once
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

// Linux high-water RSS is cumulative for the process, including all workers.
// Call only at phase boundaries; -1 means the OS did not expose the value.
inline long readProcMemoryKiB(const char* key) {
    FILE* file = std::fopen("/proc/self/status", "r");
    if (!file) return -1;
    char line[256];
    long value = -1;
    while (std::fgets(line, sizeof(line), file)) {
        const size_t length = std::strlen(key);
        if (std::strncmp(line, key, length) == 0) {
            if (std::sscanf(line + length, "%ld", &value) != 1) value = -1;
            break;
        }
    }
    std::fclose(file);
    return value;
}
inline long readVmHwmKiB() { return readProcMemoryKiB("VmHWM:"); }
inline long readVmRssKiB() { return readProcMemoryKiB("VmRSS:"); }

// M1: call only after all Stage 1 workers have joined and their scratch is dead.
// Other allocators keep the vector-release behavior without this optional hint.
inline bool releasedMemoryTrimSupported() {
#if defined(__GLIBC__)
    return true;
#else
    return false;
#endif
}
inline int trimReleasedMemory() {
#if defined(__GLIBC__)
    return malloc_trim(0);
#else
    return 0;
#endif
}

enum class WindowThreadMode { automatic, followThreads, explicitCount };
struct WindowThreadSetting {
    WindowThreadMode mode = WindowThreadMode::automatic;
    int count = 0;
    void parse(const std::string& text) {
        if (text == "auto" || text == "0") { mode = WindowThreadMode::automatic; count = 0; }
        else if (text == "threads") { mode = WindowThreadMode::followThreads; count = 0; }
        else {
            size_t end = 0;
            int n = std::stoi(text, &end);
            if (end != text.size() || n < 1 || n > 64)
                throw std::invalid_argument("--window-threads expects auto, threads, or 1..64");
            mode = WindowThreadMode::explicitCount; count = n;
        }
    }
    const char* name() const {
        return mode == WindowThreadMode::automatic ? "auto" :
            mode == WindowThreadMode::followThreads ? "threads" : "explicit";
    }
    int resolve(int inherited, int cpus, bool rateScored) const {
        if (mode == WindowThreadMode::explicitCount) return count;
        if (mode == WindowThreadMode::followThreads || !rateScored) return inherited;
        return std::max(inherited, std::min(16, cpus));
    }
};

// Constructed only for --print-options. One atomic stdio write per scope keeps
// diagnostics separate from the four numerical stdout lines.
class OptionLine {
    std::ostringstream out_;
public:
    explicit OptionLine(const char* scope) { out_ << "[options] scope=" << scope << std::setprecision(17); }
    template<class T> void add(const char* key, const T& value) { out_ << ' ' << key << '=' << value; }
    void add(const char* key, const std::string& value) { out_ << ' ' << key << '=' << std::quoted(value); }
    ~OptionLine() { std::fprintf(stderr, "%s\n", out_.str().c_str()); }
};
