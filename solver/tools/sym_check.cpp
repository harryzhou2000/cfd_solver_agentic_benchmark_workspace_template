// Dump node coordinates from a CGNS file and test reflection symmetry about y=0.
#include <cgnslib.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>
int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: sym_check <file.cgns>\n"); return 2; }
  int fn; if (cg_open(argv[1], CG_MODE_READ, &fn)) { fprintf(stderr, "%s\n", cg_get_error()); return 1; }
  int nb; cg_nbases(fn, &nb);
  std::vector<double> xs, ys;
  for (int b = 1; b <= nb; ++b) {
    int nz; cg_nzones(fn, b, &nz);
    for (int z = 1; z <= nz; ++z) {
      cgsize_t sz[3]; char zname[128]; cg_zone_read(fn, b, z, zname, sz);
      cgsize_t nn = sz[0];
      std::vector<double> X(nn), Y(nn);
      cgsize_t lo = 1, hi = nn;
      cg_coord_read(fn, b, z, "CoordinateX", RealDouble, &lo, &hi, X.data());
      cg_coord_read(fn, b, z, "CoordinateY", RealDouble, &lo, &hi, Y.data());
      for (cgsize_t i = 0; i < nn; ++i) { xs.push_back(X[i]); ys.push_back(Y[i]); }
    }
  }
  cg_close(fn);
  double ymin = 1e300, ymax = -1e300, xmax = -1e300, xmin = 1e300;
  for (size_t i = 0; i < xs.size(); ++i) {
    ymin = std::min(ymin, ys[i]); ymax = std::max(ymax, ys[i]);
    xmin = std::min(xmin, xs[i]); xmax = std::max(xmax, xs[i]);
  }
  printf("nodes=%zu  x in [%.4f, %.4f]  y in [%.4f, %.4f]\n", xs.size(), xmin, xmax, ymin, ymax);
  double tol = 1e-6 * std::max(1.0, xmax - xmin);
  double maxgap = 0.0, sumgap = 0.0; long ntest = 0, nmiss = 0;
  for (size_t i = 0; i < xs.size(); ++i) {
    if (std::fabs(ys[i]) <= tol) continue;
    double tx = xs[i], ty = -ys[i];
    double best = 1e300;
    for (size_t j = 0; j < xs.size(); ++j) {
      double dx = xs[j] - tx; if (std::fabs(dx) > 1e-3) continue;
      double dy = ys[j] - ty; double d = std::hypot(dx, dy);
      if (d < best) best = d;
    }
    if (best > 1e-3) { ++nmiss; } else { sumgap += best; if (best > maxgap) maxgap = best; ++ntest; }
  }
  printf("mirror test: tested=%ld missed=%ld  maxgap=%.3e meangap=%.3e\n", ntest, nmiss, maxgap, ntest ? sumgap/ntest : 0.0);
  return 0;
}
