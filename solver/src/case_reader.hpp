#pragma once

/// @file case_reader.hpp
/// Parse JSON case files into a `cfd::CaseConfig`.

#include "common.hpp"

#include <string>

namespace cfd {

/// Parse a benchmark case JSON file into a fully-populated CaseConfig.
///
/// Derives freestream quantities (AoA in radians, velocity components,
/// temperature, speed of sound, laminar viscosity), resolves relative
/// file paths against the case file directory, and validates the schema
/// version (only version 1 is accepted).
///
/// @throws std::runtime_error on unreadable files, parse errors, unsupported
///         schema versions, or unknown enum strings.
CaseConfig read_case(const std::string& case_file_path);

} // namespace cfd
