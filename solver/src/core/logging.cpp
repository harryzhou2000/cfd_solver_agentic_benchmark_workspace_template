#include "core/logging.h"

#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

#include "core/exceptions.h"

namespace cns2d {

Logger &Logger::instance() {
  static Logger logger;
  return logger;
}

void Logger::configure(int rank, int size) {
  rank_ = rank;
  size_ = size;
}

void Logger::openLogFile(const std::string &path) {
  if (!isRoot()) return;
  closeLogFile();
  auto *os = new std::ofstream(path, std::ios::out | std::ios::trunc);
  if (!os->good()) {
    delete os;
    throw CnsError("cannot open log file for writing: " + path);
  }
  file_ = os;
}

void Logger::closeLogFile() {
  if (file_ != nullptr) {
    auto *os = static_cast<std::ofstream *>(file_);
    os->flush();
    os->close();
    delete os;
    file_ = nullptr;
  }
}

void Logger::emit(const std::string &line, bool all_ranks) {
  if (!all_ranks && !isRoot()) return;
  std::ostream &sink = std::cout;
  sink << line << '\n';
  sink.flush();
  if (file_ != nullptr) {
    auto *os = static_cast<std::ofstream *>(file_);
    (*os) << line << '\n';
    os->flush();
  }
}

void Logger::info(const std::string &msg) { emit("[cns2d] " + msg, false); }

void Logger::warn(const std::string &msg) { emit("[cns2d][warn] " + msg, false); }

void Logger::error(const std::string &msg) {
  emit("[cns2d][error][rank " + std::to_string(rank_) + "] " + msg, true);
}

void Logger::raw(const std::string &msg) { emit(msg, false); }

void logInfo(const std::string &msg) { Logger::instance().info(msg); }
void logWarn(const std::string &msg) { Logger::instance().warn(msg); }
void logError(const std::string &msg) { Logger::instance().error(msg); }
void logRaw(const std::string &msg) { Logger::instance().raw(msg); }

std::string formatString(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  va_list probe;
  va_copy(probe, args);
  const int needed = std::vsnprintf(nullptr, 0, fmt, probe);
  va_end(probe);
  if (needed < 0) {
    va_end(args);
    return std::string();
  }
  std::vector<char> buffer(static_cast<std::size_t>(needed) + 1);
  std::vsnprintf(buffer.data(), buffer.size(), fmt, args);
  va_end(args);
  return std::string(buffer.data(), static_cast<std::size_t>(needed));
}

}  // namespace cns2d
