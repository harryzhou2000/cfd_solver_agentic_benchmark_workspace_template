#pragma once
// Case JSON reader: parses a benchmark case file into a RunConfig struct.

#include <string>

#include "types.h"

namespace cfd {

// Load a case JSON file and return a fully populated RunConfig.
// The mesh file path is resolved relative to the case file's directory.
// Validates all BC type strings, the spatial order, and the mesh dimension;
// throws std::runtime_error on any parse/validation failure.
RunConfig load_case(const std::string& case_path);

// Print a human-readable summary of the case configuration.
// Called by the driver (main.cpp) after load_case returns.
void print_case_summary(const RunConfig& cfg);

}  // namespace cfd
