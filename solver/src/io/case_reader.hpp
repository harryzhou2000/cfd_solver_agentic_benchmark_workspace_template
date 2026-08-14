#pragma once
#include "types.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace omo {

class CaseReader {
public:
    static CaseConfig read(const std::string& case_path);

private:
    static void parse_freestream(const nlohmann::json& j, CaseConfig& cfg);
    static void parse_run_control(const nlohmann::json& j, CaseConfig& cfg);
    static void parse_outputs(const nlohmann::json& j, CaseConfig& cfg);
};

} // namespace omo
