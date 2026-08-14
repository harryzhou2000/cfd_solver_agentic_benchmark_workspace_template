#include "solver.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <cstring>

namespace cfd2d {

void Solver::initialize() {
  int n = lm->numLocal;
  U.assign(n, ConsState{});
  P.assign(n, PrimState{});
  // Freestream state
  ConsState Uinf = gas.primToCons({gas.rhoInf, gas.uInf, gas.vInf, gas.pInf});
  for (int i = 0; i < n; ++i) {
    U[i] = Uinf;
    P[i] = gas.consToPrim(U[i]);
  }
}

void Solver::exchangeHalo() {
  // Use non-blocking send/recv with neighbors
  // A rank may have send-only or recv-only neighbors
  std::vector<MPI_Request> reqs;
  std::vector<std::vector<double>> recvBufs;
  std::vector<std::vector<double>> sendBufs;

  // Separate recv and send neighbor lists
  std::vector<int> recvNeighbors, sendNeighbors;
  for (int rk : lm->neighborRanks) {
    if (lm->recvFromNeighbor.count(rk)) recvNeighbors.push_back(rk);
    if (lm->sendToNeighbor.count(rk)) sendNeighbors.push_back(rk);
  }

  // Post receives
  recvBufs.resize(recvNeighbors.size());
  for (size_t i = 0; i < recvNeighbors.size(); ++i) {
    int rk = recvNeighbors[i];
    auto& cells = lm->recvFromNeighbor.at(rk);
    recvBufs[i].resize(cells.size() * NEQ);
    reqs.push_back(MPI_REQUEST_NULL);
    MPI_Irecv(recvBufs[i].data(), (int)recvBufs[i].size(), MPI_DOUBLE,
              rk, 100, MPI_COMM_WORLD, &reqs.back());
  }

  // Post sends
  sendBufs.resize(sendNeighbors.size());
  for (size_t i = 0; i < sendNeighbors.size(); ++i) {
    int rk = sendNeighbors[i];
    auto& cells = lm->sendToNeighbor.at(rk);
    sendBufs[i].resize(cells.size() * NEQ);
    for (size_t j = 0; j < cells.size(); ++j) {
      int li = cells[j];
      for (int k = 0; k < NEQ; ++k)
        sendBufs[i][j * NEQ + k] = U[li][k];
    }
    reqs.push_back(MPI_REQUEST_NULL);
    MPI_Isend(sendBufs[i].data(), (int)sendBufs[i].size(), MPI_DOUBLE,
              rk, 100, MPI_COMM_WORLD, &reqs.back());
  }

  // Wait for receives, then unpack
  std::vector<MPI_Request> recvReqs(reqs.begin(), reqs.begin() + recvNeighbors.size());
  std::vector<MPI_Status> recvStats(recvReqs.size());
  MPI_Waitall((int)recvReqs.size(), recvReqs.data(), recvStats.data());

  for (size_t i = 0; i < recvNeighbors.size(); ++i) {
    int rk = recvNeighbors[i];
    auto& cells = lm->recvFromNeighbor.at(rk);
    for (size_t j = 0; j < cells.size(); ++j) {
      int li = cells[j];
      for (int k = 0; k < NEQ; ++k)
        U[li][k] = recvBufs[i][j * NEQ + k];
      P[li] = gas.consToPrim(U[li]);
    }
  }

  // Wait for sends
  std::vector<MPI_Request> sendReqs(reqs.begin() + recvNeighbors.size(), reqs.end());
  std::vector<MPI_Status> sendStats(sendReqs.size());
  MPI_Waitall((int)sendReqs.size(), sendReqs.data(), sendStats.data());
}

void Solver::applyBCs() {
  // For boundary faces, set ghost cell state based on BC type
  for (int fi = 0; fi < (int)lm->faces.size(); ++fi) {
    const Face& f = lm->faces[fi];
    if (f.bcType == (int)BCType::Interior) continue;

    // Boundary face: one side is real cell, other is BC ghost
    int c, g;
    if (f.l >= 0 && f.l < lm->numOwned) { c = f.l; g = f.r; }
    else if (f.r >= 0 && f.r < lm->numOwned) { c = f.r; g = f.l; }
    else continue;
    if (g < 0) continue;

    const PrimState& Pc = P[c];
    PrimState Pg = Pc; // ghost state

    if (f.bcType == (int)BCType::Farfield) {
      // Characteristic-based farfield: use freestream for inflow, cell for outflow
      double un = Pc[1] * f.nx + Pc[2] * f.ny;
      if (un < 0) {
        // Inflow: use freestream
        Pg[0] = gas.rhoInf;
        Pg[1] = gas.uInf;
        Pg[2] = gas.vInf;
        Pg[3] = gas.pInf;
      } else {
        // Outflow: zero gradient
        Pg = Pc;
      }
    } else if (f.bcType == (int)BCType::SlipWall) {
      // Reflect normal velocity: u_ghost = u_cell - 2*un*n
      double un = Pc[1] * f.nx + Pc[2] * f.ny;
      Pg[1] = Pc[1] - 2.0 * un * f.nx;
      Pg[2] = Pc[2] - 2.0 * un * f.ny;
      Pg[0] = Pc[0];
      Pg[3] = Pc[3];
    } else if (f.bcType == (int)BCType::NoSlipAdiabaticWall) {
      // No-slip: u_ghost = -u_cell (mirror), adiabatic: dT/dn = 0
      Pg[1] = -Pc[1];
      Pg[2] = -Pc[2];
      Pg[0] = Pc[0];
      Pg[3] = Pc[3]; // adiabatic: p ghost = p cell
    }

    U[g] = gas.primToCons(Pg);
    P[g] = Pg;
  }
}

void Solver::computeResidual(double dt, bool transient, double physTime) {
  int n = lm->numLocal;
  R.assign(n, ConsState{0,0,0,0});

  // Compute gradients (for viscous flux and limiter)
  computeGradients(*lm, P, gradX, gradY, gas);

  // Reconstruct face states (second-order with Barth limiter)
  // Check environment for first-order fallback
  if (getenv("CFD2D_FIRST_ORDER")) {
    // First-order: use cell-center values directly
    recon.resize(lm->faces.size());
    for (int fi = 0; fi < (int)lm->faces.size(); ++fi) {
      const Face& f = lm->faces[fi];
      if (f.l >= 0) recon[fi].left = P[f.l]; else recon[fi].left = P[f.r];
      if (f.r >= 0) recon[fi].right = P[f.r]; else recon[fi].right = P[f.l];
    }
  } else {
    reconstructFaces(*lm, P, gradX, gradY, recon, gas);
  }

  // Accumulate fluxes
  double maxFlux = 0;
  int maxFluxFace = -1;
  for (int fi = 0; fi < (int)lm->faces.size(); ++fi) {
    const Face& f = lm->faces[fi];
    int l = f.l, r = f.r;
    if (l < 0 && r < 0) continue;

    // Get reconstructed states (or BC-applied states)
    ConsState UL, UR;
    if (l >= 0) UL = gas.primToCons(recon[fi].left);
    if (r >= 0) UR = gas.primToCons(recon[fi].right);

    // For boundary faces, use the BC ghost state (set by applyBCs) directly,
    // not the reconstructed state (which would be the cell's own value).
    if (f.bcType != (int)BCType::Interior) {
      int g = (f.l >= 0) ? f.r : f.l;
      int c = (f.l >= 0) ? f.l : f.r;
      if (g >= 0 && c >= 0) {
        UR = U[g]; // BC ghost state
        UL = U[c]; // cell state (not reconstructed, to match BC)
      }
    }

    // Inviscid flux (Rusanov)
    ConsState Finv = gas.rusanovFlux(UL, UR, f.nx, f.ny);

    // Viscous flux
    ConsState Fvisc = {0,0,0,0};
    if (gas.viscous) {
      PrimState PL = (l >= 0) ? recon[fi].left : P[r];
      PrimState PR = (r >= 0) ? recon[fi].right : P[l];
      // Gradients at face (average of left/right cell gradients)
      std::array<double,4> dWdx = {0,0,0,0}, dWdy = {0,0,0,0};
      if (l >= 0 && r >= 0) {
        for (int k = 0; k < 4; ++k) {
          dWdx[k] = 0.5 * (gradX[l][k] + gradX[r][k]);
          dWdy[k] = 0.5 * (gradY[l][k] + gradY[r][k]);
        }
      } else if (l >= 0) {
        dWdx = gradX[l]; dWdy = gradY[l];
      } else if (r >= 0) {
        dWdx = gradX[r]; dWdy = gradY[r];
      }
      // Convert p gradient to T gradient: T = p/(rho*R)
      // dT = (dp*rho - p*drho)/(rho^2 * R)
      // For simplicity, compute T gradient from p and rho gradients
      double rhoAvg = 0.5 * (PL[0] + PR[0]);
      double pAvg = 0.5 * (PL[3] + PR[3]);
      double TgradX = (dWdx[3] * rhoAvg - pAvg * dWdx[0]) / (rhoAvg * rhoAvg * gas.R);
      double TgradY = (dWdy[3] * rhoAvg - pAvg * dWdy[0]) / (rhoAvg * rhoAvg * gas.R);
      std::array<double,4> dWdxT = {dWdx[0], dWdx[1], dWdx[2], TgradX};
      std::array<double,4> dWdyT = {dWdy[0], dWdy[1], dWdy[2], TgradY};
      gas.viscousFlux(PL, PR, dWdxT, dWdyT, f.nx, f.ny, Fvisc);
    }

    // Total flux
    ConsState F;
    for (int k = 0; k < NEQ; ++k) F[k] = Finv[k] - Fvisc[k];

    // Add to residual: R += F * len / vol (only for owned cells)
    if (l >= 0 && l < lm->numOwned) {
      double vol = lm->area[l];
      for (int k = 0; k < NEQ; ++k) {
        double contrib = F[k] * f.len / vol;
        R[l][k] += contrib;
        if (std::abs(contrib) > maxFlux) { maxFlux = std::abs(contrib); maxFluxFace = fi; }
      }
    }
    if (r >= 0 && r < lm->numOwned) {
      double vol = lm->area[r];
      for (int k = 0; k < NEQ; ++k) {
        double contrib = F[k] * f.len / vol;
        R[r][k] -= contrib;
        if (std::abs(contrib) > maxFlux) { maxFlux = std::abs(contrib); maxFluxFace = fi; }
      }
    }
  }

  if (getenv("CFD2D_DEBUG") && rank == 0) {
    std::cerr << "  maxFluxContrib=" << maxFlux << " at face " << maxFluxFace << "\n";
    if (maxFluxFace >= 0) {
      const Face& df = lm->faces[maxFluxFace];
      std::cerr << "    face l=" << df.l << " r=" << df.r << " bc=" << df.bcType
                << " len=" << df.len << " nx=" << df.nx << " ny=" << df.ny
                << " n0=" << df.n0 << " n1=" << df.n1
                << " fam=" << df.family << "\n";
      if (df.l >= 0) {
        std::cerr << "    L cell area=" << lm->area[df.l] << " U=" << U[df.l][0] << "," << U[df.l][1]
                  << "," << U[df.l][2] << "," << U[df.l][3] << " P=" << P[df.l][0] << "," << P[df.l][1]
                  << "," << P[df.l][2] << "," << P[df.l][3] << "\n";
      }
      if (df.r >= 0) {
        std::cerr << "    R ghost area=" << lm->area[df.r] << " U=" << U[df.r][0] << "," << U[df.r][1]
                  << "," << U[df.r][2] << "," << U[df.r][3] << " P=" << P[df.r][0] << "," << P[df.r][1]
                  << "," << P[df.r][2] << "," << P[df.r][3] << "\n";
      }
    }
  }

  // Debug: check for NaN in residual
  if (getenv("CFD2D_DEBUG")) {
    double rmax = 0;
    int imax = -1;
    for (int i = 0; i < lm->numOwned; ++i) {
      for (int k = 0; k < NEQ; ++k) {
        if (std::isnan(R[i][k]) || std::isinf(R[i][k])) {
          std::cerr << "  NaN/Inf in R[" << i << "][" << k << "]=" << R[i][k]
                    << " U=" << U[i][0] << "," << U[i][1] << "," << U[i][2] << "," << U[i][3]
                    << " P=" << P[i][0] << "," << P[i][1] << "," << P[i][2] << "," << P[i][3] << "\n";
        }
        if (std::abs(R[i][k]) > rmax) { rmax = std::abs(R[i][k]); imax = i; }
      }
    }
    if (rank == 0) std::cerr << "  maxResComp=" << rmax << " at cell " << imax << "\n";
  }

  // For transient BDF2, add physical time derivative term
  if (transient) {
    // BDF2: (3U^{n+1} - 4U^n + U^{n-1}) / (2*dt)
    // Residual += (3U - 4Un + Unm1) / (2*dt)
    double coef = 1.0 / (2.0 * dt);
    for (int i = 0; i < lm->numOwned; ++i) {
      for (int k = 0; k < NEQ; ++k) {
        R[i][k] += coef * (3.0 * U[i][k] - 4.0 * Un[i][k] + Unm1[i][k]);
      }
    }
  }
}

double Solver::computeDt(double cfl) const {
  // Local time step based on spectral radius
  double dtmin = 1e30;
  for (int i = 0; i < lm->numOwned; ++i) {
    double a = gas.soundSpeed(P[i]);
    double un = std::abs(P[i][1]) + std::abs(P[i][2]);
    double spec = un + a;
    // Estimate max face length around cell (use sqrt(area) as characteristic)
    double h = std::sqrt(lm->area[i]);
    double dt = cfl * h / std::max(spec, 1e-12);
    if (gas.viscous) {
      // Viscous spectral radius: use more conservative limit
      // dt_visc < cfl * h^2 / (4 * mu/rho) but h should be based on cell spacing
      // Use h = 2*area/perimeter ~ area/face_length for better estimate
      double dtv = cfl * 0.1 * gas.rhoInf * h * h / std::max(gas.mu, 1e-30);
      dt = std::min(dt, dtv);
    }
    dtmin = std::min(dtmin, dt);
  }
  double dtglobal;
  MPI_Allreduce(&dtmin, &dtglobal, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
  return dtglobal;
}

double Solver::residualNorm() const {
  double l2 = 0;
  double lmax = 0;
  for (int i = 0; i < lm->numOwned; ++i) {
    for (int k = 0; k < NEQ; ++k) {
      l2 += R[i][k] * R[i][k];
      lmax = std::max(lmax, std::abs(R[i][k]));
    }
  }
  double l2global, lmaxglobal;
  MPI_Allreduce(&l2, &l2global, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&lmax, &lmaxglobal, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  int ntot;
  MPI_Allreduce(&lm->numOwned, &ntot, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  if (rank == 0 && getenv("CFD2D_DEBUG")) {
    std::cerr << "  resNorm l2=" << std::sqrt(l2global / std::max(ntot*NEQ,1))
              << " lmax=" << lmaxglobal << "\n";
  }
  return std::sqrt(l2global / std::max(ntot * NEQ, 1));
}

void Solver::computeForces(double& cl, double& cd, double& cmz,
                            double& pDrag, double& vDrag,
                            double& pLift, double& vLift) {
  // Integrate pressure and viscous forces on wall boundaries
  double fx_p = 0, fy_p = 0, fx_v = 0, fy_v = 0;
  double mx_p = 0, my_p = 0; // moment

  for (int fi = 0; fi < (int)lm->faces.size(); ++fi) {
    const Face& f = lm->faces[fi];
    if (f.bcType != (int)BCType::SlipWall &&
        f.bcType != (int)BCType::NoSlipAdiabaticWall) continue;
    // Boundary face: find the real (owned) cell
    int c = -1;
    if (f.l >= 0 && f.l < lm->numOwned) c = f.l;
    else if (f.r >= 0 && f.r < lm->numOwned) c = f.r;
    if (c < 0) continue;

    const PrimState& Pc = P[c];
    double p = Pc[3];
    // Pressure force: F = -p * n * len (n points outward from cell)
    double fpx = -p * f.nx * f.len;
    double fpy = -p * f.ny * f.len;
    fx_p += fpx; fy_p += fpy;
    // Moment about reference center
    double rx = f.mx - ci->momentCx, ry = f.my - ci->momentCy;
    mx_p += rx * fpy - ry * fpx;

    // Viscous force (only for no-slip wall)
    if (f.bcType == (int)BCType::NoSlipAdiabaticWall && gas.viscous) {
      // Shear stress from velocity gradient
      double dudx = gradX[c][1], dudy = gradY[c][1];
      double dvdx = gradX[c][2], dvdy = gradY[c][2];
      double div = dudx + dvdy;
      double txx = gas.mu * (2.0 * dudx - 2.0/3.0 * div);
      double tyy = gas.mu * (2.0 * dvdy - 2.0/3.0 * div);
      double txy = gas.mu * (dudy + dvdx);
      // Force = tau . n * len
      double fvx = (txx * f.nx + txy * f.ny) * f.len;
      double fvy = (txy * f.nx + tyy * f.ny) * f.len;
      fx_v += fvx; fy_v += fvy;
      mx_p += rx * fvy - ry * fvx;
    }
  }

  // Global reduce
  double local[6] = {fx_p, fy_p, fx_v, fy_v, mx_p, 0};
  double global[6];
  MPI_Allreduce(local, global, 6, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  fx_p = global[0]; fy_p = global[1]; fx_v = global[2]; fy_v = global[3];
  double mz = global[4];

  if (getenv("CFD2D_DEBUG") && rank == 0) {
    std::cerr << "  Forces: fx_p=" << fx_p << " fy_p=" << fy_p
              << " fx_v=" << fx_v << " fy_v=" << fy_v << "\n";
  }

  // Nondimensionalize: F_coeff = F / (0.5 * rho_inf * U_inf^2 * area)
  // The force on the body from the fluid is F = -integral(p * n_fluid) dS
  // where n_fluid points from fluid into body. Our fpx = -p * nx is the
  // force on the body in the +x direction. Drag is the force in the
  // freestream direction, so cd = (fx_tot * ca + fy_tot * sa) / q.
  // But the pressure force on the body is actually F_body = +integral(p * n_body) dS
  // where n_body points from body into fluid = -n_fluid.
  // So F_body_x = +p * (-n_fluid_x) = -p * n_fluid_x = fpx. Correct.
  // The drag is the force on the body in the freestream direction.
  // For flow in +x, the stagnation pressure at the upstream side (x<0)
  // has n_fluid pointing in +x, so fpx = -p*(+1) = -p < 0 (backward).
  // This means the body is pushed backward (upstream), which is drag.
  // So cd = -fpx / q (drag opposes the flow).
  // Actually, the standard convention: drag = force on body in freestream direction.
  // The freestream is in +x. The force on the body in +x is fpx.
  // At the front (upstream, x<0), the high pressure pushes the body in -x (fpx<0).
  // This means the drag force (in +x) is negative, which means the body
  // experiences a net force opposing the flow. The drag coefficient is
  // defined as positive for this opposing force: cd = -fpx/q.
  double q = 0.5 * gas.rhoInf * gas.velMag * gas.velMag * ci->refArea;
  double aoa = ci->aoaDeg * M_PI / 180.0;
  double ca = std::cos(aoa), sa = std::sin(aoa);
  double fx_tot = fx_p + fx_v, fy_tot = fy_p + fy_v;
  // Drag = force component in freestream direction (positive = downstream)
  // The force on the body in the freestream direction is:
  // F_drag = fx_tot * cos(aoa) + fy_tot * sin(aoa)
  // But our fpx = -p * n_fluid_x is the force on the body in +x.
  // The drag coefficient is positive when the body is pushed downstream.
  // For a body in flow, the net force is downstream (positive drag).
  // The stagnation pressure at the front pushes the body upstream (negative fpx),
  // but the low pressure at the back pulls it downstream (positive fpx).
  // The net should be positive (drag). If our fpx is negative, the sign is wrong.
  // The issue: fpx = -p * n_fluid_x. The force on the body is actually
  // F = +integral(p * n_body) dS where n_body = -n_fluid.
  // So F_x = +p * (-n_fluid_x) = -p * n_fluid_x = fpx. This is correct.
  // But the drag is defined as the force on the body in the flow direction.
  // For flow in +x, drag = F_x = fpx. If fpx < 0, drag is negative.
  // This happens when the upstream pressure dominates (which it should for drag).
  // The drag coefficient should be positive, so cd = -fpx / q.
  cd = -(fx_tot * ca + fy_tot * sa) / q;
  cl = -(-fx_tot * sa + fy_tot * ca) / q;
  cmz = mz / (q * ci->refLength);
  pDrag = -(fx_p * ca + fy_p * sa) / q;
  vDrag = -(fx_v * ca + fy_v * sa) / q;
  pLift = -(-fx_p * sa + fy_p * ca) / q;
  vLift = -(-fx_v * sa + fy_v * ca) / q;
}

} // namespace cfd2d
