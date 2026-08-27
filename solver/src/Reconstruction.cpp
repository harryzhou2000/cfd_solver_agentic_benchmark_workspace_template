#include "Reconstruction.hpp"
#include "Physics.hpp"
#include <algorithm>
#include <cmath>

void computeLimiters(const LocalMesh& lm,
                     const std::vector<StateVec>& states,
                     const std::vector<StateGrad>& grads,
                     std::vector<std::array<double,4>>& limiters) {
    int n_owned = lm.n_owned;
    int n_total = n_owned + lm.n_ghost;
    limiters.resize(n_total);
    for (auto& l : limiters) l = {1.0, 1.0, 1.0, 1.0};

    for (int i = 0; i < n_owned; i++) {
        // Min/max over neighbors
        std::array<double,4> Umin, Umax;
        for (int k=0; k<4; k++) { Umin[k] = states[i][k]; Umax[k] = states[i][k]; }
        for (int j : lm.cell_nbrs_local_int[i]) {
            for (int k=0; k<4; k++) {
                Umin[k] = std::min(Umin[k], states[j][k]);
                Umax[k] = std::max(Umax[k], states[j][k]);
            }
        }

        // Barth-Jespersen
        std::array<double,4> phi = {1.0, 1.0, 1.0, 1.0};
        double cx = lm.cell_cx[i], cy = lm.cell_cy[i];
        for (int fidx : lm.cell_face_ids_local[i]) {
            double dx = lm.face_cx[fidx] - cx;
            double dy = lm.face_cy[fidx] - cy;
            for (int k=0; k<4; k++) {
                double dU = grads[i][k][0]*dx + grads[i][k][1]*dy;
                double Ui = states[i][k];
                if (dU > 1e-14)
                    phi[k] = std::min(phi[k], (Umax[k]-Ui)/dU);
                else if (dU < -1e-14)
                    phi[k] = std::min(phi[k], (Umin[k]-Ui)/dU);
            }
        }
        for (int k=0; k<4; k++) limiters[i][k] = std::max(0.0, std::min(1.0, phi[k]));
    }
}

StateVec reconstruct(int cell, double fx, double fy,
                     const LocalMesh& lm,
                     const std::vector<StateVec>& states,
                     const std::vector<StateGrad>& grads,
                     const std::vector<std::array<double,4>>& limiters) {
    double cx = lm.cell_cx[cell], cy = lm.cell_cy[cell];
    double dx = fx - cx, dy = fy - cy;
    StateVec U = states[cell];
    const auto& g = grads[cell];
    const auto& phi = limiters[cell];
    for (int k=0; k<4; k++)
        U[k] += phi[k] * (g[k][0]*dx + g[k][1]*dy);
    return U;
}
