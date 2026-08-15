#include "common.h"

#include <ctime>
#include <cstdarg>

namespace cfd {

Logger g_log;

std::string utc_now() {
  time_t t = time(nullptr);
  struct tm tmv;
  gmtime_r(&t, &tmv);
  char buf[64];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
  return std::string(buf);
}

std::string fmt_duration(double seconds) {
  int s = static_cast<int>(std::lround(seconds));
  int h = s / 3600;
  int m = (s % 3600) / 60;
  int sec = s % 60;
  char buf[64];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, sec);
  return std::string(buf);
}

}  // namespace cfd
