#pragma once

/// @file logging.hpp
/// Minimal logging utility: timestamped, rank-prefixed messages on stderr.
///
/// All ranks write to stderr. Rank 0 always prints. Non-zero ranks can be
/// suppressed via `cfd::logging::suppress_nonzero(bool)` (intended for MPI
/// runs; serial builds use rank 0).

#include <fmt/core.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

namespace cfd {
namespace logging {

/// Current process rank (0 for serial builds). Set from MPI when integrated.
inline int& rank() {
    static int r = 0;
    return r;
}

/// Whether non-zero ranks should suppress their log output.
inline bool& suppress_nonzero() {
    static bool s = false;
    return s;
}

namespace detail {

inline std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return buf;
}

inline void emit(const char* level, const std::string& msg) {
    fmt::print(stderr, "[{}] [rank {}] [{}] {}\n", timestamp(), cfd::logging::rank(), level, msg);
}

} // namespace detail
} // namespace logging
} // namespace cfd

/// Log an informational message to stderr.
#define LOG_INFO(...)                                                          \
    do {                                                                       \
        if (cfd::logging::rank() == 0 || !cfd::logging::suppress_nonzero()) {  \
            cfd::logging::detail::emit("INFO", fmt::format(__VA_ARGS__));      \
        }                                                                      \
    } while (0)

/// Log a warning message to stderr.
#define LOG_WARN(...)                                                          \
    do {                                                                       \
        if (cfd::logging::rank() == 0 || !cfd::logging::suppress_nonzero()) {  \
            cfd::logging::detail::emit("WARN", fmt::format(__VA_ARGS__));      \
        }                                                                      \
    } while (0)

/// Log an error message to stderr (never suppressed).
#define LOG_ERROR(...)                                                         \
    do {                                                                       \
        cfd::logging::detail::emit("ERROR", fmt::format(__VA_ARGS__));         \
    } while (0)
