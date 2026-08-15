#pragma once

#include <map>
#include <string>

#include "types.hpp"

namespace cfd {

// Case configuration parsed from the benchmark JSON case file.
struct CaseConfig {
    std::string case_id;
    std::string description;
    std::string mesh_file;  // from mesh.file
    std::string mesh_format;
    std::string output_dir;

    // physics section
    std::string equations = "compressible_navier_stokes";
    std::string mode = "inviscid";  // inviscid | laminar | turbulent
    bool viscous() const { return mode != "inviscid"; }
    double reynolds = 0.0;                // physics.reynolds (0 = inviscid)
    std::string viscosity_model = "constant";  // physics.viscosity_model

    // numerics_required section
    int spatial_order = 2;
    std::string inviscid_flux = "approximate_riemann";
    std::string main_time_method = "implicit";

    FreestreamParams freestream;
    RunControlParams control;
    OutputControlParams outputs;
    ReferenceParams reference;
    GasParams gas;

    // boundary_conditions section: CGNS BC index/name -> type string
    // e.g. {"bc-2": "farfield", "bc-4": "no_slip_adiabatic_wall"}
    std::map<std::string, std::string> boundary_conditions;
};

// Parses the JSON case file into a CaseConfig.
// Throws std::runtime_error on missing/invalid fields or unsupported
// schema_version.
CaseConfig parse_case_file(const std::string& filename);

// Parses the JSON case file from an already-loaded JSON string.
CaseConfig parse_case_string(const std::string& json_text);

}  // namespace cfd
