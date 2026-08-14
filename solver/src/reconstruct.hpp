#pragma once

#include "mesh_local.hpp"
#include "physics.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace cfd {

struct CellGrad {
    double drho[2] = {0, 0};
    double du[2] = {0, 0};
    double dv[2] = {0, 0};
    double dp[2] = {0, 0};
};

struct LSMatrix {
    double a = 0, b = 0, c = 0;
    bool valid = false;
};

// Inverse least-squares matrix per owned cell (inverse-distance weights).
inline std::vector<LSMatrix> build_ls_matrices(const LocalMesh& m) {
    std::vector<LSMatrix> M(m.n_owned);
    for (int i = 0; i < m.n_owned; ++i) {
        double sxx = 0.0, sxy = 0.0, syy = 0.0;
        for (int j : m.cell_neighbors[i]) {
            double cxj = (j < m.n_owned) ? m.cell_cx[j] : m.ghost_cx[j - m.n_owned];
            double cyj = (j < m.n_owned) ? m.cell_cy[j] : m.ghost_cy[j - m.n_owned];
            double dx = cxj - m.cell_cx[i];
            double dy = cyj - m.cell_cy[i];
            double w = 1.0 / std::max(dx * dx + dy * dy, 1e-30);
            sxx += w * dx * dx;
            sxy += w * dx * dy;
            syy += w * dy * dy;
        }
        double det = sxx * syy - sxy * sxy;
        if (std::abs(det) > 1e-30) {
            M[i] = {syy / det, -sxy / det, sxx / det, true};
        }
    }
    return M;
}

// Primitive gradients from neighbor values.
inline void compute_gradients(const LocalMesh& m, const std::vector<LSMatrix>& M,
                              const double* rho, const double* u, const double* v,
                              const double* p, std::vector<CellGrad>& g) {
    g.assign(m.n_owned, CellGrad{});
    for (int i = 0; i < m.n_owned; ++i) {
        if (!M[i].valid) continue;
        double srho[2] = {0, 0}, su[2] = {0, 0}, sv[2] = {0, 0}, sp[2] = {0, 0};
        for (int j : m.cell_neighbors[i]) {
            double cxj = (j < m.n_owned) ? m.cell_cx[j] : m.ghost_cx[j - m.n_owned];
            double cyj = (j < m.n_owned) ? m.cell_cy[j] : m.ghost_cy[j - m.n_owned];
            double dx = cxj - m.cell_cx[i];
            double dy = cyj - m.cell_cy[i];
            double w = 1.0 / std::max(dx * dx + dy * dy, 1e-30);
            srho[0] += w * (rho[j] - rho[i]) * dx;
            srho[1] += w * (rho[j] - rho[i]) * dy;
            su[0] += w * (u[j] - u[i]) * dx;
            su[1] += w * (u[j] - u[i]) * dy;
            sv[0] += w * (v[j] - v[i]) * dx;
            sv[1] += w * (v[j] - v[i]) * dy;
            sp[0] += w * (p[j] - p[i]) * dx;
            sp[1] += w * (p[j] - p[i]) * dy;
        }
        const LSMatrix& A = M[i];
        g[i].drho[0] = A.a * srho[0] + A.b * srho[1];
        g[i].drho[1] = A.b * srho[0] + A.c * srho[1];
        g[i].du[0] = A.a * su[0] + A.b * su[1];
        g[i].du[1] = A.b * su[0] + A.c * su[1];
        g[i].dv[0] = A.a * sv[0] + A.b * sv[1];
        g[i].dv[1] = A.b * sv[0] + A.c * sv[1];
        g[i].dp[0] = A.a * sp[0] + A.b * sp[1];
        g[i].dp[1] = A.b * sp[0] + A.c * sp[1];
    }
}

// Barth-Jespersen limiter for a single variable.  qmin/qmax over the stencil.
inline double barth_limit_component(const LocalMesh& m, int i, double q_i,
                                    double qmin, double qmax, double gx, double gy) {
    double ph = 1.0;
    for (int fid : m.cell_faces[i]) {
        double fx, fy;
        if (fid < m.n_faces_internal()) {
            fx = m.faces[fid].fx;
            fy = m.faces[fid].fy;
        } else {
            auto& b = m.bfaces[fid - m.n_faces_internal()];
            fx = b.fx;
            fy = b.fy;
        }
        double q_rec = q_i + gx * (fx - m.cell_cx[i]) + gy * (fy - m.cell_cy[i]);
        if (q_rec > q_i + 1e-30) {
            ph = std::min(ph, (qmax - q_i) / (q_rec - q_i + 1e-30));
        } else if (q_rec < q_i - 1e-30) {
            ph = std::min(ph, (qmin - q_i) / (q_rec - q_i - 1e-30));
        }
    }
    return std::max(0.0, std::min(1.0, ph));
}

}  // namespace cfd
