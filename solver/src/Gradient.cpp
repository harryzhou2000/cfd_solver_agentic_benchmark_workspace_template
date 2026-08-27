#include "Gradient.hpp"
#include "Physics.hpp"
#include <cmath>

void computeGradients(const LocalMesh& lm,
                      const std::vector<StateVec>& states,
                      std::vector<StateGrad>& grads) {
    int n_owned = lm.n_owned;
    int n_total = n_owned + lm.n_ghost;
    grads.resize(n_total);
    // Only zero owned-cell gradients; ghost cell gradients are maintained by haloExchangeGrads
    // and must NOT be reset here (they provide frozen boundary conditions during inner iterations).
    for (int i = 0; i < n_owned; i++) grads[i] = {GradVec{0,0}, GradVec{0,0}, GradVec{0,0}, GradVec{0,0}};

    for (int i = 0; i < n_owned; i++) {
        double xi = lm.cell_cx[i], yi = lm.cell_cy[i];
        double a11=0, a12=0, a22=0;
        std::array<double,4> b1={}, b2={};

        // Use pre-built neighbor list
        for (int j : lm.cell_nbrs_local_int[i]) {
            double dx = lm.cell_cx[j] - xi;
            double dy = lm.cell_cy[j] - yi;
            double d2 = dx*dx + dy*dy;
            if (d2 < 1e-30) continue;
            double w2 = 1.0/d2;
            a11 += w2*dx*dx; a12 += w2*dx*dy; a22 += w2*dy*dy;
            for (int k=0; k<4; k++) {
                double dU = states[j][k] - states[i][k];
                b1[k] += w2*dx*dU; b2[k] += w2*dy*dU;
            }
        }
        double det = a11*a22 - a12*a12;
        if (std::abs(det) < 1e-30) continue;
        double inv_det = 1.0/det;
        for (int k=0; k<4; k++) {
            grads[i][k][0] = inv_det*(a22*b1[k] - a12*b2[k]);
            grads[i][k][1] = inv_det*(a11*b2[k] - a12*b1[k]);
        }
    }
}

void computePrimGradients(const LocalMesh& lm,
                          const std::vector<StateVec>& states,
                          double gamma, double R,
                          std::vector<PrimGrads>& prim_grads) {
    int n_total = lm.n_owned + lm.n_ghost;
    prim_grads.resize(n_total);
    // Only zero owned-cell prim_grads; ghost gradients preserved from haloExchangePrimGrads.
    int n_owned_pg = lm.n_owned;
    for (int i = 0; i < n_owned_pg; i++) prim_grads[i] = {GradVec{0,0}, GradVec{0,0}, GradVec{0,0}};

    std::vector<std::array<double,3>> prims(n_total);
    for (int i = 0; i < n_total; i++) {
        const auto& U = states[i];
        double rho = U[0], u = U[1]/rho, v = U[2]/rho;
        double p = pressure(U, gamma);
        double T = p/(rho*R);
        prims[i] = {u, v, T};
    }

    for (int i = 0; i < lm.n_owned; i++) {
        double xi = lm.cell_cx[i], yi = lm.cell_cy[i];
        double a11=0, a12=0, a22=0;
        std::array<double,3> b1={}, b2={};
        for (int j : lm.cell_nbrs_local_int[i]) {
            double dx = lm.cell_cx[j] - xi;
            double dy = lm.cell_cy[j] - yi;
            double d2 = dx*dx + dy*dy;
            if (d2 < 1e-30) continue;
            double w2 = 1.0/d2;
            a11 += w2*dx*dx; a12 += w2*dx*dy; a22 += w2*dy*dy;
            for (int k=0; k<3; k++) {
                double dV = prims[j][k] - prims[i][k];
                b1[k] += w2*dx*dV; b2[k] += w2*dy*dV;
            }
        }
        double det = a11*a22 - a12*a12;
        if (std::abs(det) < 1e-30) continue;
        double inv_det = 1.0/det;
        for (int k=0; k<3; k++) {
            prim_grads[i][k][0] = inv_det*(a22*b1[k] - a12*b2[k]);
            prim_grads[i][k][1] = inv_det*(a11*b2[k] - a12*b1[k]);
        }
    }
}
