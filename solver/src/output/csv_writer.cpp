#include "csv_writer.hpp"
#include <fstream>

void write_residuals_csv(const std::string& path,
                         const std::vector<int>& steps,
                         const std::vector<Real>& times,
                         const std::vector<int>& inner_iters,
                         const std::vector<Real>& cfl_vals,
                         const std::vector<Real>& dt_vals,
                         const std::vector<StateVector>& residuals,
                         const std::vector<Real>& l2_norms,
                         const std::vector<Real>& linf_norms) {
    std::ofstream f(path);
    f << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
    size_t n = steps.size();
    for (size_t i = 0; i < n; i++) {
        f << steps[i] << "," << times[i] << "," << inner_iters[i]
          << "," << cfl_vals[i] << "," << dt_vals[i] << ","
          << residuals[i][0] << "," << residuals[i][1] << ","
          << residuals[i][2] << "," << residuals[i][3] << ","
          << l2_norms[i] << "," << linf_norms[i] << "\n";
    }
}

void write_forces_csv(const std::string& path,
                      const std::vector<int>& steps,
                      const std::vector<Real>& times,
                      const std::vector<Real>& cl,
                      const std::vector<Real>& cd,
                      const std::vector<Real>& cmz,
                      const std::vector<Real>& p_drag,
                      const std::vector<Real>& v_drag,
                      const std::vector<Real>& p_lift,
                      const std::vector<Real>& v_lift) {
    std::ofstream f(path);
    f << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
    size_t n = steps.size();
    for (size_t i = 0; i < n; i++) {
        f << steps[i] << "," << times[i] << "," << cl[i] << "," << cd[i]
          << "," << cmz[i] << "," << p_drag[i] << "," << v_drag[i]
          << "," << p_lift[i] << "," << v_lift[i] << "\n";
    }
}

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
                       const std::vector<std::string>& tags) {
    std::ofstream f(path);
    f << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
    size_t n = pts.size();
    for (size_t i = 0; i < n; i++) {
        f << pts[i].x() << "," << pts[i].y() << ","
          << normals[i].x() << "," << normals[i].y() << ","
          << pressures[i] << "," << cp[i] << "," << cf[i] << ","
          << rho[i] << "," << u[i] << "," << v[i] << ","
          << mach[i] << "," << tags[i] << "\n";
    }
}
