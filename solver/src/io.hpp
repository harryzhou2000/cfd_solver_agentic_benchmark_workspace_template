#pragma once
#include "types.hpp"
#include <string>

namespace cfd {

// Parse a benchmark case JSON file into CaseInput. Throws std::runtime_error
// with a clear message on malformed or unsupported input.
CaseInput load_case(const std::string& case_json_path);

// Resolve a possibly relative path against the directory containing base_file.
std::string resolve_path(const std::string& path, const std::string& base_file);

} // namespace cfd
