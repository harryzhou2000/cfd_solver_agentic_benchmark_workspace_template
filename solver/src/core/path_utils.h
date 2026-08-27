// cns2d -- small filesystem helpers shared by input parsing and output writing.
#pragma once

#include <string>

namespace cns2d {

bool fileExists(const std::string &path);
std::string absolutePath(const std::string &path);
std::string parentDirectory(const std::string &path);
// Resolve 'rel' against 'base' unless 'rel' is already absolute.
std::string resolveRelativeTo(const std::string &base, const std::string &rel);
// Create the directory (and parents).  Throws CnsError on failure.
void makeDirectories(const std::string &path);
std::string joinPath(const std::string &a, const std::string &b);
std::string fileName(const std::string &path);

}  // namespace cns2d
