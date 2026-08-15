#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <algorithm>

namespace fv {

// Conservative state index
enum { IRHO = 0, IRHOU = 1, IRHOV = 2, IRHOE = 3 };
constexpr int NVAR = 4;

using Vec4 = std::array<double, NVAR>;

inline double wall_time() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Simple dual-destination logger (stdout + stdout.log mirror), rank 0 only.
class Logger {
public:
    void open(const std::string& path, int rank) {
        rank_ = rank;
        if (rank_ == 0) {
            file_.open(path, std::ios::out | std::ios::trunc);
            if (!file_) throw std::runtime_error("cannot open log file: " + path);
        }
    }
    void log(const std::string& msg) {
        if (rank_ != 0) return;
        std::cout << msg << std::endl;
        if (file_) { file_ << msg << "\n"; file_.flush(); }
    }
    void logf(const char* fmt, ...) {
        if (rank_ != 0) return;
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        log(std::string(buf));
    }
private:
    std::ofstream file_;
    int rank_ = 0;
};

extern Logger g_log;

} // namespace fv
