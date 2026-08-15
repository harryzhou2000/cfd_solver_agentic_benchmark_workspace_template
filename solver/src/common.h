#pragma once

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <set>
#include <memory>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <functional>

#include <mpi.h>

namespace cfd {

// ---------------------------------------------------------------------------
// Logging: writes to stdout and optionally to a log file (rank 0 only).
// ---------------------------------------------------------------------------
class Logger {
 public:
  Logger() = default;
  bool enabled = true;
  void open(const std::string& path) {
    if (file_.is_open()) file_.close();
    file_.open(path, std::ios::out | std::ios::trunc);
  }
  void close() { if (file_.is_open()) file_.close(); }
  void log(const std::string& msg) {
    if (!enabled) return;
    std::cout << msg << std::flush;
    if (file_.is_open()) {
      file_ << msg << std::flush;
    }
  }
  void logf(const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log(std::string(buf));
  }
 private:
  std::ofstream file_;
};

extern Logger g_log;

// ---------------------------------------------------------------------------
// Small 2-D helpers.
// ---------------------------------------------------------------------------
struct Vec2 {
  double x = 0.0, y = 0.0;
};

inline double det(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }

// ISO-8601 UTC timestamp.
std::string utc_now();

// Human readable duration.
std::string fmt_duration(double seconds);

}  // namespace cfd
