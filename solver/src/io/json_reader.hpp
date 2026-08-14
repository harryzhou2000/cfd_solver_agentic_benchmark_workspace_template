#pragma once

#include "common/types.hpp"
#include <string>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

CaseConfig parse_case_json(const std::string& filepath);

// Helper: get optional field with default
template<typename T>
T json_opt(const json& j, const std::string& key, const T& default_val) {
    if (j.contains(key)) return j[key].get<T>();
    return default_val;
}
