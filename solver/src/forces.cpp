#include "forces.hpp"

#include <cmath>

namespace cfd {

ForceData reduce_forces(const ForceData& local, MPI_Comm comm) {
  double l[5] = {local.pressure_drag, local.viscous_drag, local.pressure_lift,
                 local.viscous_lift,  local.moment};
  double g[5] = {0};
  MPI_Allreduce(l, g, 5, MPI_DOUBLE, MPI_SUM, comm);
  int nwall = 0;
  MPI_Allreduce(&local.wall_faces, &nwall, 1, MPI_INT, MPI_SUM, comm);
  ForceData out;
  out.pressure_drag = g[0];
  out.viscous_drag = g[1];
  out.pressure_lift = g[2];
  out.viscous_lift = g[3];
  out.moment = g[4];
  out.wall_faces = static_cast<int>(nwall);
  return out;
}

void normalize_forces(ForceData& f, const FreeStream& fs, const Case& c) {
  const double q = fs.qinf * c.ref_area;
  f.pressure_drag /= q;
  f.viscous_drag /= q;
  f.pressure_lift /= q;
  f.viscous_lift /= q;
  f.cd = f.pressure_drag + f.viscous_drag;
  f.cl = f.pressure_lift + f.viscous_lift;
  f.cmz = f.moment / (fs.qinf * c.ref_area * c.ref_length);
}

}  // namespace cfd
