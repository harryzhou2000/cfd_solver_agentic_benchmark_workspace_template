// Rank-aware logging.  Rank 0 writes to stdout and, once a case output
// directory is known, tees the same stream into <output-dir>/stdout.log so the
// log required by OUTPUT_CONTRACT.md is a faithful copy of the console output.
#pragma once

#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

namespace cfd {

class Logger {
 public:
  static Logger& instance();

  void setRank(int rank, int size) { rank_ = rank; size_ = size; }
  int rank() const { return rank_; }
  int size() const { return size_; }

  // Open the tee file. Only rank 0 writes.
  void openFile(const std::string& path);
  void close();

  void write(const std::string& text);
  void flush();

 private:
  Logger() = default;
  int rank_ = 0;
  int size_ = 1;
  std::unique_ptr<std::ofstream> file_;
};

// Stream helper: LOG() << ... << '\n';  (rank 0 only)
class LogStream {
 public:
  LogStream() = default;
  ~LogStream() {
    Logger& lg = Logger::instance();
    if (lg.rank() == 0) lg.write(oss_.str());
  }
  template <typename T>
  LogStream& operator<<(const T& v) {
    oss_ << v;
    return *this;
  }
  LogStream& operator<<(std::ostream& (*fn)(std::ostream&)) {
    fn(oss_);
    return *this;
  }

 private:
  std::ostringstream oss_;
};

#define LOG() ::cfd::LogStream()

}  // namespace cfd
