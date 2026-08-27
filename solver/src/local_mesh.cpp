#include "local_mesh.hpp"
#include <limits>

namespace fv {

void LocalMesh::computeGeometry() {
  xc.assign(nAll, 0.0);
  yc.assign(nAll, 0.0);
  vol.assign(nAll, 0.0);
  for (int c = 0; c < nAll; ++c) {
    int nn = cellNNodes[c];
    // polygon centroid + signed area (nodes are CCW in CGNS convention)
    double a = 0.0, cx = 0.0, cy = 0.0;
    for (int k = 0; k < nn; ++k) {
      int i0 = cellNodes[c][k], i1 = cellNodes[c][(k + 1) % nn];
      double x0 = nodeX[i0], y0 = nodeY[i0];
      double x1 = nodeX[i1], y1 = nodeY[i1];
      double cr = x0 * y1 - x1 * y0;
      a += cr;
      cx += (x0 + x1) * cr;
      cy += (y0 + y1) * cr;
    }
    a *= 0.5;
    check(std::fabs(a) > 0.0, "degenerate cell area");
    vol[c] = std::fabs(a);
    xc[c] = cx / (6.0 * a);
    yc[c] = cy / (6.0 * a);
  }
  for (auto& f : faces) {
    double x0 = nodeX[f.n0], y0 = nodeY[f.n0];
    double x1 = nodeX[f.n1], y1 = nodeY[f.n1];
    f.fx = 0.5 * (x0 + x1);
    f.fy = 0.5 * (y0 + y1);
    double dx = x1 - x0, dy = y1 - y0;
    double len = std::hypot(dx, dy);
    check(len > 0.0, "degenerate face length");
    f.area = len;
    // normal candidates: (dy, -dx) or (-dy, dx); orient from c0 towards c1
    double nx = dy / len, ny = -dx / len;
    double tx = (f.c1 >= 0 ? xc[f.c1] : 2.0 * f.fx - xc[f.c0]) - xc[f.c0];
    double ty = (f.c1 >= 0 ? yc[f.c1] : 2.0 * f.fy - yc[f.c0]) - yc[f.c0];
    if (nx * tx + ny * ty < 0.0) { nx = -nx; ny = -ny; }
    f.nx = nx;
    f.ny = ny;
  }
}

void LocalMesh::buildCellFaces() {
  vector<int> count(nAll, 0);
  for (size_t fi = 0; fi < faces.size(); ++fi) {
    count[faces[fi].c0]++;
    if (faces[fi].c1 >= 0) count[faces[fi].c1]++;
  }
  cellFaceOff.assign(nAll + 1, 0);
  for (int c = 0; c < nAll; ++c) cellFaceOff[c + 1] = cellFaceOff[c] + count[c];
  cellFaceIdx.resize(cellFaceOff[nAll]);
  vector<int> pos(cellFaceOff.begin(), cellFaceOff.end() - 1);
  for (size_t fi = 0; fi < faces.size(); ++fi) {
    cellFaceIdx[pos[faces[fi].c0]++] = (int)fi;
    if (faces[fi].c1 >= 0) cellFaceIdx[pos[faces[fi].c1]++] = (int)fi;
  }
}

void LocalMesh::buildLsq() {
  // Weighted least-squares stencil over face neighbors + boundary ghost points
  // (mirror of the cell centroid across the boundary face). Owned cells only.
  lsqOff.assign(nOwn + 1, 0);
  for (int c = 0; c < nOwn; ++c) {
    int cnt = 0;
    for (int k = cellFaceOff[c]; k < cellFaceOff[c + 1]; ++k) {
      const LocalFace& f = faces[cellFaceIdx[k]];
      cnt++;
      (void)f;
    }
    lsqOff[c + 1] = lsqOff[c] + cnt;
  }
  lsqEntries.resize(lsqOff[nOwn]);
  lsqInv00.assign(nOwn, 0.0);
  lsqInv01.assign(nOwn, 0.0);
  lsqInv11.assign(nOwn, 0.0);
  for (int c = 0; c < nOwn; ++c) {
    double m00 = 0, m01 = 0, m11 = 0;
    int out = lsqOff[c];
    for (int k = cellFaceOff[c]; k < cellFaceOff[c + 1]; ++k, ++out) {
      int fi = cellFaceIdx[k];
      const LocalFace& f = faces[fi];
      LsqEntry e;
      double px, py;
      if (f.c1 >= 0) {
        int nbr = (f.c0 == c) ? f.c1 : f.c0;
        e.idx = nbr;
        px = xc[nbr];
        py = yc[nbr];
      } else {
        e.idx = ~fi;  // boundary face: mirrored centroid ghost point
        px = 2.0 * f.fx - xc[c];
        py = 2.0 * f.fy - yc[c];
      }
      e.dx = px - xc[c];
      e.dy = py - yc[c];
      double d = std::hypot(e.dx, e.dy);
      e.w = (d > 0.0) ? 1.0 / d : 0.0;
      lsqEntries[out] = e;
      m00 += e.w * e.dx * e.dx;
      m01 += e.w * e.dx * e.dy;
      m11 += e.w * e.dy * e.dy;
    }
    double det = m00 * m11 - m01 * m01;
    check(det > 0.0, "singular LSQ moment matrix");
    lsqInv00[c] = m11 / det;
    lsqInv01[c] = -m01 / det;
    lsqInv11[c] = m00 / det;
  }
}

double LocalMesh::minCellSize() const {
  double m = std::numeric_limits<double>::max();
  for (int c = 0; c < nOwn; ++c) m = std::min(m, vol[c]);
  return m;
}

}  // namespace fv
