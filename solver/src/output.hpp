#pragma once
#include "common.hpp"
#include <string>

namespace fv {
void setCommandLine(const std::string& cmd);
const std::string& commandLine();
void setGitRevision(const std::string& rev);
const std::string& gitRevision();
}  // namespace fv
