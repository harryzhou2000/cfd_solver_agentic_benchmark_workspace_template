#pragma once
// Common types, logging, and serialization helpers for cfd2d.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cfd2d {

// Conservative state indices.
enum { IRHO = 0, IRHOU = 1, IRHOV = 2, IRHOE = 3 };

using Vec4 = std::array<double, 4>;   // one conserved/primitive state
using Vec2 = std::array<double, 2>;   // 2-D vector

struct FatalError : public std::runtime_error {
  explicit FatalError(const std::string& msg) : std::runtime_error(msg) {}
};

// Logger: mirrors messages to stdout (rank 0) and to <outdir>/stdout.log so
// the output contract's stdout.log exists regardless of shell redirection.
class Logger {
 public:
  Logger() = default;
  void init(const std::string& path, bool echo) {
    echo_ = echo;
    if (!path.empty()) {
      file_.open(path, std::ios::out | std::ios::trunc);
    }
  }
  void log(const std::string& msg) {
    if (echo_) {
      std::cout << msg << std::endl;
    }
    if (file_.is_open()) {
      file_ << msg << "\n";
      file_.flush();
    }
  }
  template <typename... Args>
  void logf(const char* fmt, Args... args) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf), fmt, args...);
    log(std::string(buf));
  }
 private:
  bool echo_ = true;
  std::ofstream file_;
};

// Byte buffer for packing/unpacking rank-local partition data.
class Buffer {
 public:
  template <typename T>
  void put(const T& v) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
    data_.insert(data_.end(), p, p + sizeof(T));
  }
  void putBytes(const void* p, size_t n) {
    const uint8_t* b = reinterpret_cast<const uint8_t*>(p);
    data_.insert(data_.end(), b, b + n);
  }
  void putString(const std::string& s) {
    uint64_t n = s.size();
    put(n);
    putBytes(s.data(), n);
  }
  template <typename T>
  void putVec(const std::vector<T>& v) {
    uint64_t n = v.size();
    put(n);
    if (n) putBytes(v.data(), n * sizeof(T));
  }

  template <typename T>
  T get() {
    T v;
    need(sizeof(T));
    std::memcpy(&v, data_.data() + pos_, sizeof(T));
    pos_ += sizeof(T);
    return v;
  }
  std::string getString() {
    uint64_t n = get<uint64_t>();
    need(n);
    std::string s(reinterpret_cast<const char*>(data_.data() + pos_), n);
    pos_ += n;
    return s;
  }
  template <typename T>
  std::vector<T> getVec() {
    uint64_t n = get<uint64_t>();
    need(n * sizeof(T));
    std::vector<T> v(n);
    if (n) std::memcpy(v.data(), data_.data() + pos_, n * sizeof(T));
    pos_ += n * sizeof(T);
    return v;
  }

  const std::vector<uint8_t>& data() const { return data_; }
  std::vector<uint8_t>& data() { return data_; }
  void setData(std::vector<uint8_t> d) { data_ = std::move(d); pos_ = 0; }
  size_t size() const { return data_.size(); }

 private:
  void need(size_t n) const {
    if (pos_ + n > data_.size()) throw FatalError("Buffer underflow during unpack");
  }
  std::vector<uint8_t> data_;
  size_t pos_ = 0;
};

// Wall-clock helper (MPI_Wtime equivalent used without dragging mpi.h here).
double wallTime();

}  // namespace cfd2d
