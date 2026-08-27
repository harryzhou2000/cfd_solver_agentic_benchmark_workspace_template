// Real-mesh confirmation: build the actual cylinder case through SolverContext,
// then measure the spectral radius of the shipped ImplicitSolver::solve sweep
// operator as a function of CFL, plus the linear residual ratio it achieves.
//
// Method: with rhs = 0 the exact solution is dU = 0, so iterating the sweeps on
// a random nonzero dU measures the error-propagation factor directly.  We use
// the public solve() entry point with reset_increment=false so it continues from
// the current dU, exactly as the driver's "add more sweeps" loop does.
#include <cstdio>
#include <cmath>
#include <random>
#include <vector>
#include "core/case_input.h"
#include "core/options.h"
#include "solve/solver_context.h"
#include "solve/local_time_step.h"

using namespace cns2d;

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);
  {
    CommandLineOptions opt;
    opt.case_path = "/workspace/solver/scratch/dbg_cyl_inviscid.json";
    opt.output_dir = "/workspace/solver/scratch/o_probe_spec";
    opt.override_spatial_order = 1;   // isolate the implicit operator
    const CaseInput input = loadCaseInput(opt.case_path);
    SolverContext ctx(input, opt, MPI_COMM_WORLD);
    ctx.initializeState();

    const DistributedMesh &mesh = ctx.mesh();
    const Index n_owned = mesh.numOwned();
    ResidualAssembler &asmb = ctx.assembler();
    ImplicitSolver &imp = ctx.implicit();
    HaloExchange &halo = ctx.halo();
    StateField &U = ctx.state();

    // One residual evaluation to populate the spectral radii.
    StateField R(mesh.numLocal());
    ResidualDiagnostics diag;
    asmb.evaluate(U, R, halo, diag);

    std::vector<Real> dtau, diag_scale(static_cast<std::size_t>(n_owned), 0.0);
    StateField zero_rhs(mesh.numLocal());
    zero_rhs.setZero();
    StateField dU(mesh.numLocal());

    printf("Real cylinder mesh (%d owned cells), first order, M=0.1\n", (int)n_owned);
    printf("Spectral radius of the SHIPPED LU-SGS sweep operator vs CFL:\n\n");
    printf("     CFL     rho(1 SGS sweep)   sweeps needed for 1e-2   verdict\n");

    const double cfls[] = {0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 30.0, 50.0, 100.0};
    for (double cfl : cfls) {
      computeLocalTimeSteps(mesh, cfl, asmb.convectiveSpectralRadius(),
                            asmb.viscousSpectralRadius(), 4.0, dtau);
      for (Index c = 0; c < n_owned; ++c) {
        const Real dt = dtau[(std::size_t)c];
        const Real V = mesh.cells()[(std::size_t)c].volume;
        diag_scale[(std::size_t)c] = (dt > 0.0) ? V / dt : V;
      }

      // Random initial error.
      std::mt19937 rng(2024);
      std::uniform_real_distribution<double> d(-1.0, 1.0);
      for (Index c = 0; c < mesh.numLocal(); ++c) {
        ConsVec v{};
        for (int k = 0; k < kNumVars; ++k) v[k] = d(rng);
        dU.set(c, v);
      }

      auto nrm = [&]() {
        Real s = 0.0;
        for (Index c = 0; c < n_owned; ++c) {
          const Real *p = dU.cell(c);
          for (int k = 0; k < kNumVars; ++k) s += p[k] * p[k];
        }
        return std::sqrt(s);
      };

      Real rho = 0.0;
      for (int it = 0; it < 60; ++it) {
        const Real before = nrm();
        if (!(before > 0.0) || !std::isfinite(before)) break;
        for (Index c = 0; c < n_owned; ++c) {
          ConsVec v = dU.get(c);
          for (int k = 0; k < kNumVars; ++k) v[k] /= before;
          dU.set(c, v);
        }
        // ONE symmetric Gauss-Seidel iteration, continuing from current dU.
        imp.solve(U, zero_rhs, diag_scale, asmb.convectiveSpectralRadius(),
                  asmb.viscousSpectralRadius(), 1, dU, halo, /*reset_increment=*/false);
        rho = nrm();
      }

      const bool conv = rho < 1.0;
      const double need = conv ? std::log(1.0e-2) / std::log(std::max(rho, 1e-300)) : -1.0;
      printf("  %7.1f        %10.5f          ", cfl, rho);
      if (conv) printf("%8.1f", need); else printf("   never");
      printf("          %s\n", conv ? "converges" : "DIVERGES");
    }
  }
  MPI_Finalize();
  return 0;
}
