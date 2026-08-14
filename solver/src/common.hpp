#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cfd {

// Conservative state / flux vector in 2-D: [rho, rho*u, rho*v, rho*E]
using Vec4 = std::array<double, 4>;

inline Vec4 operator+(const Vec4& a, const Vec4& b) {
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2], a[3] + b[3]};
}
inline Vec4 operator-(const Vec4& a, const Vec4& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2], a[3] - b[3]};
}
inline Vec4 operator*(const Vec4& a, double s) {
    return {a[0] * s, a[1] * s, a[2] * s, a[3] * s};
}
inline Vec4 operator*(double s, const Vec4& a) { return a * s; }
inline Vec4 operator/(const Vec4& a, double s) {
    return {a[0] / s, a[1] / s, a[2] / s, a[3] / s};
}

// Boundary condition tags used internally.
enum class BCType { Farfield, SlipWall, NoSlipAdiabaticWall, Internal, Invalid };

// Physics / case configuration (subset of case JSON needed by the solver).
struct CaseConfig {
    std::string case_id;
    std::string mesh_file;      // absolute or case-relative path
    std::string mesh_format;
    bool viscous = false;
    double reynolds = 0.0;
    std::string viscosity_model = "constant";
    double gamma = 1.4;
    double gas_R = 1.0;
    double prandtl = 0.72;
    double mach = 0.1;
    double aoa_degrees = 0.0;
    double rho_inf = 1.0;
    double vel_mag = 1.0;
    double p_inf = 1.0;
    double ref_length = 1.0;
    double ref_area = 1.0;
    double ref_reynolds_length = 1.0;
    std::array<double, 2> moment_center{0.0, 0.0};
    // boundary family -> BC tag mapping from JSON
    std::vector<std::pair<std::string, std::string>> bc_map;

    // run control
    std::string run_type = "steady";  // steady | transient
    std::string time_integrator = "bdf2";
    bool first_order = false;
    int max_steps = 20000;
    double residual_reduction_target = 4.0;
    double cfl_initial = 1.0;
    double cfl_max = 100.0;
    int pseudo_cfl_ramp_steps = 2000;
    int min_inner_iterations = 3;
    int max_inner_iterations = 50;
    double inner_residual_reduction_target = 0.01;
    double time_step = 0.01;
    double final_time = 300.0;
    double rusanov_dissipation_scale = 1.0;

    // outputs
    int write_forces_every = 1;
    int write_residuals_every = 1;
    double write_field_every_time = 1.0;
};

}  // namespace cfd
