#pragma once

#include "common.hpp"

namespace cfd {

// Parse a benchmark case JSON file.  Mesh paths are resolved relative to the
// directory containing the case file.
CaseInput load_case(const std::string& path);

} // namespace cfd
