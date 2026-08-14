#pragma once
#include "common/types.hpp"
#include <string>
#include <vector>

// Write residuals CSV
void write_residuals_csv(const std::string& path,
                         const std::vector<int>& steps,
                         const std::vector<Real>& times,
                         const std::vector<int>& inner_iters,
                         const std::vector<Real>& cfl_vals,
                         const std::vector<Real>& dt_vals,
                         const std::vector<StateVector>& residuals,
                         const std::vector<Real>& l2_norms,
                         const std::vector<Real>& linf_norms);

// Write forces CSV
void write_forces_csv(const std::string& path,
                      const std::vector<int>& steps,
                      const std::vector<Real>& times,
                      const std::vector<Real>& cl,
                      const std::vector<Real>& cd,
                      const std::vector<Real>& cmz,
                      const std::vector<Real>& p_drag,
                      const std::vector<Real>& v_drag,
                      const std::vector<Real>& p_lift,
                      const std::vector<Real>& v_lift);

// Write surface CSV
void write_surface_csv(const std::string& path,
                       const std::vector<Vec2>& pts,
                       const std::vector<Vec2>& normals,
                       const std::vector<Real>& pressures,
                       const std::vector<Real>& cp,
                       const std::vector<Real>& cf,
                       const std::vector<Real>& rho,
                       const std::vector<Real>& u,
                       const std::vector<Real>& v,
                       const std::vector<Real>& mach,
                       const std::vector<std::string>& tags);
