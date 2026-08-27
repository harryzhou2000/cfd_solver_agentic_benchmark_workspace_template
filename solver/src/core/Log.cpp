#include "core/Log.hpp"

namespace cfd {

Logger& Logger::instance() {
  static Logger inst;
  return inst;
}

void Logger::openFile(const std::string& path) {
  if (rank_ != 0) return;
  file_ = std::make_unique<std::ofstream>(path, std::ios::out | std::ios::trunc);
  if (!file_->good()) file_.reset();
}

void Logger::close() {
  if (file_) {
    file_->flush();
    file_->close();
    file_.reset();
  }
}

void Logger::write(const std::string& text) {
  if (rank_ != 0) return;
  std::cout << text;
  std::cout.flush();
  if (file_) {
    (*file_) << text;
    file_->flush();
  }
}

void Logger::flush() {
  std::cout.flush();
  if (file_) file_->flush();
}

}  // namespace cfd
