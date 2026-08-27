#include "core/path_utils.h"

#include <filesystem>
#include <system_error>

#include "core/exceptions.h"

namespace cns2d {
namespace fs = std::filesystem;

bool fileExists(const std::string &path) {
  std::error_code ec;
  return fs::exists(fs::path(path), ec) && !ec;
}

std::string absolutePath(const std::string &path) {
  std::error_code ec;
  const fs::path p = fs::absolute(fs::path(path), ec);
  if (ec) return path;
  return p.lexically_normal().string();
}

std::string parentDirectory(const std::string &path) {
  const fs::path p(path);
  const fs::path parent = p.parent_path();
  return parent.empty() ? std::string(".") : parent.string();
}

std::string resolveRelativeTo(const std::string &base, const std::string &rel) {
  const fs::path r(rel);
  if (r.is_absolute()) return r.lexically_normal().string();
  return (fs::path(base) / r).lexically_normal().string();
}

void makeDirectories(const std::string &path) {
  std::error_code ec;
  fs::create_directories(fs::path(path), ec);
  if (ec && !fs::exists(fs::path(path))) {
    throw CnsError("cannot create directory '" + path + "': " + ec.message());
  }
}

std::string joinPath(const std::string &a, const std::string &b) {
  return (fs::path(a) / fs::path(b)).lexically_normal().string();
}

std::string fileName(const std::string &path) { return fs::path(path).filename().string(); }

}  // namespace cns2d
