#pragma once

#include <string>

namespace fv {
void openLog(const std::string& path);
void slog(const std::string& msg);
void closeLog();
}  // namespace fv
