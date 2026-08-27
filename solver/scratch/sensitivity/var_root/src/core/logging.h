// cns2d -- rank-aware logging.
//
// Only rank 0 writes to stdout and to the run log file, which keeps
// stdout.log free of interleaved rank output (the output contract requires
// rank-independent output discipline).  Errors are printed by every rank with
// an explicit rank prefix so a single-rank failure is still visible.
#pragma once

#include <string>

namespace cns2d {

class Logger {
 public:
  static Logger &instance();

  void configure(int rank, int size);
  // Tee subsequent log output into 'path' (rank 0 only).
  void openLogFile(const std::string &path);
  void closeLogFile();

  void info(const std::string &msg);
  void warn(const std::string &msg);
  void error(const std::string &msg);
  // Write a line verbatim (no level prefix) -- used for tabular progress logs.
  void raw(const std::string &msg);

  int rank() const { return rank_; }
  int size() const { return size_; }
  bool isRoot() const { return rank_ == 0; }

 private:
  Logger() = default;
  void emit(const std::string &line, bool all_ranks);

  int rank_{0};
  int size_{1};
  void *file_{nullptr};  // std::ofstream*, hidden to keep the header light
};

// Convenience wrappers.
void logInfo(const std::string &msg);
void logWarn(const std::string &msg);
void logError(const std::string &msg);
void logRaw(const std::string &msg);

// printf-style formatting helper (no external dependency required).
std::string formatString(const char *fmt, ...);

}  // namespace cns2d
