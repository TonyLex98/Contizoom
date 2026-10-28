#ifndef IC_CLOCK_H
#define IC_CLOCK_H

#include <chrono>
#include <cstdint>

namespace ic::clock {

// Wall-clock time as Unix seconds (used for createdAt/modifiedAt stamps).
inline uint64_t unixSeconds() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

// Monotonic milliseconds (used for double-click detection).
inline uint64_t monotonicMillis() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

}  // namespace ic::clock

#endif  // IC_CLOCK_H
