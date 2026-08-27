// Measure near-wall resolution: for each WALL boundary edge, find the adjacent
// cell and report the normal distance from cell centroid to the wall face.
#include <cgnslib.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <map>
#include <algorithm>
#include <string>
struct P { double x, y; };
int main(int argc, char** argv) {
  int fn; if (cg_open(argv[1], CG_MODE_READ, &fn)) { fprintf(stderr, "%s\n", cg_get_error()); return 1; }
  int nb; cg_nbases(fn, &nb);
  std::vector<P> node; // global merged not needed: per-zone wall edges + cells
  double minH = 1e30, maxH = 0, sumH = 0; long nH = 0;
  double minEdge = 1e30, maxEdge = 0;
  for (int b = 1; b <= nb; ++b) {
    int nz; cg_nzones(fn, b, &nz);
    for (int z = 1; z <= nz; ++z) {
      char zn[128]; cgsize_t sz[3]; cg_zone_read(fn, b, z, zn, sz);
      cgsize_t nn = sz[0];
      std::vector<double> X(nn), Y(nn); cgsize_t lo = 1, hi = nn;
      cg_coord_read(fn, b, z, "CoordinateX", RealDouble, &lo, &hi, X.data());
      cg_coord_read(fn, b, z, "CoordinateY", RealDouble, &lo, &hi, Y.data());
      // read cells (tri+quad) centroid and node set
      int nsec; cg_nsections(fn, b, z, &nsec);
      std::vector<std::vector<cgsize_t>> cells; std::vector<P> cent;
      std::vector<std::pair<cgsize_t,cgsize_t>> wallEdges;
      for (int s = 1; s <= nsec; ++s) {
        char sn[128]; ElementType_t et; cgsize_t e0, e1; int nbd, pf;
        cg_section_read(fn, b, z, s, sn, &et, &e0, &e1, &nbd, &pf);
        cgsize_t nelt = e1 - e0 + 1;
        int npe = 0; if (et != MIXED) cg_npe(et, &npe);
        if (et == BAR_2 || npe == 2) { // boundary bar elements
          std::vector<cgsize_t> cn(nelt * 2);
          cg_elements_read(fn, b, z, s, cn.data(), nullptr);
          const char* pat = argc > 2 ? argv[2] : "WALL";
          bool isWall = std::string(sn).find(pat) != std::string::npos;
          if (isWall) for (cgsize_t i = 0; i < nelt; ++i) wallEdges.push_back({cn[2*i]-1, cn[2*i+1]-1});
          continue;
        }
        if (npe < 3) continue;
        std::vector<cgsize_t> cn(nelt * npe);
        cg_elements_read(fn, b, z, s, cn.data(), nullptr);
        for (cgsize_t i = 0; i < nelt; ++i) {
          std::vector<cgsize_t> cv; P c{0,0};
          for (int k = 0; k < npe; ++k) { cgsize_t g = cn[i*npe+k]-1; cv.push_back(g); c.x+=X[g]; c.y+=Y[g]; }
          c.x/=npe; c.y/=npe; cells.push_back(cv); cent.push_back(c);
        }
      }
      // map node -> cells
      std::map<cgsize_t, std::vector<int>> n2c;
      for (size_t ci = 0; ci < cells.size(); ++ci) for (auto g : cells[ci]) n2c[g].push_back((int)ci);
      for (auto& e : wallEdges) {
        P a{X[e.first], Y[e.first]}, bP{X[e.second], Y[e.second]};
        double ex = bP.x-a.x, ey = bP.y-a.y; double el = std::hypot(ex,ey);
        minEdge = std::min(minEdge, el); maxEdge = std::max(maxEdge, el);
        // find cell containing both nodes
        int found = -1;
        for (int ci : n2c[e.first]) { auto& cv = cells[ci];
          if (std::find(cv.begin(), cv.end(), e.second) != cv.end()) { found = ci; break; } }
        if (found < 0) continue;
        P c = cent[found];
        // distance from centroid to edge line
        double d = std::fabs((c.x-a.x)*ey - (c.y-a.y)*ex) / (el>0?el:1);
        minH = std::min(minH, d); maxH = std::max(maxH, d); sumH += d; ++nH;
      }
    }
  }
  cg_close(fn);
  printf("wall edges: n=%ld  first-cell normal height min=%.4e max=%.4e mean=%.4e\n", nH, minH, maxH, nH?sumH/nH:0.0);
  printf("wall edge length min=%.4e max=%.4e\n", minEdge, maxEdge);
  printf("diameter=1, Re=20 -> BL scale ~ D/sqrt(Re) = %.3f\n", 1.0/std::sqrt(20.0));
  return 0;
}
