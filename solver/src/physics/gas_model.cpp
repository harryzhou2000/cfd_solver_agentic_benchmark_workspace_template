#include "physics/gas_model.hpp"

#include <cmath>

namespace cfd {

PrimitiveState cons_to_prim(const double* U, double gamma, double R) {
  const double rho = U[0];
  if (!(rho > 0.0) || !std::isfinite(rho))
    throw std::runtime_error("cons_to_prim: non-positive or non-finite density "
                             "(" + std::to_string(rho) + ")");
  const double u = U[1] / rho;
  const double v = U[2] / rho;
  const double ke = 0.5 * (U[1] * U[1] + U[2] * U[2]) / rho;
  const double p = (gamma - 1.0) * (U[3] - ke);
  if (!(p > 0.0) || !std::isfinite(p))
    throw std::runtime_error("cons_to_prim: non-positive or non-finite pressure "
                             "(" + std::to_string(p) + ")");
  PrimitiveState prim;
  prim.rho = rho;
  prim.u = u;
  prim.v = v;
  prim.p = p;
  prim.T = p / (rho * R);
  prim.a = speed_of_sound(p, rho, gamma);
  return prim;
}

void prim_to_cons(const PrimitiveState& prim, double gamma, double* U) {
  U[0] = prim.rho;
  U[1] = prim.rho * prim.u;
  U[2] = prim.rho * prim.v;
  U[3] = prim.p / (gamma - 1.0) +
         0.5 * prim.rho * (prim.u * prim.u + prim.v * prim.v);
}

PrimitiveState freestream_primitive(const CaseConfig& cfg) {
  const double alpha =
      cfg.freestream.aoa_degrees * std::acos(-1.0) / 180.0;
  const double V = cfg.freestream.velocity_magnitude;
  PrimitiveState prim;
  prim.rho = cfg.freestream.rho;
  prim.u = V * std::cos(alpha);
  prim.v = V * std::sin(alpha);
  prim.p = cfg.freestream.pressure;
  prim.T = temperature(prim.p, prim.rho, cfg.gas.R);
  prim.a = speed_of_sound(prim.p, prim.rho, cfg.gas.gamma);
  return prim;
}

std::vector<double> init_freestream(const DistributedMesh& dmesh,
                                    const CaseConfig& cfg) {
  const double gamma = cfg.gas.gamma;
  const PrimitiveState prim = freestream_primitive(cfg);
  double U_inf[NVARS];
  prim_to_cons(prim, gamma, U_inf);

  const long long nlocal = static_cast<long long>(dmesh.cells.size());
  std::vector<double> U(static_cast<std::size_t>(nlocal) * NVARS);
  for (long long c = 0; c < nlocal; ++c)
    for (int k = 0; k < NVARS; ++k)
      U[static_cast<std::size_t>(c) * NVARS + k] = U_inf[k];
  return U;
}

}  // namespace cfd
