// Quantifies the two implicit-path findings on the REAL cylinder mesh:
//
// (A) Item 3 -- the reset_increment restart.  Emulates the driver's
//     "add more sweeps" loop both ways and reports the linear residual ratio
//     actually achieved:
//       OLD: every ImplicitSolver::solve call restarts from dU = 0
//       NEW: only the first call resets, later blocks continue
//
// (B) The residual high-CFL weakness after (A) is fixed: the SGS spectral
//     radius tends to 1 as CFL grows.  Remedy tested here is diagonal
//     inflation: D = V/dtau + beta*0.5*sum|lambda|*area with beta > 1.
//     This needs no source change to test, because buildDiagonal reads
//     conv_radius through the public solve()/linearResidualRatio() arguments
//     while offDiagonalAction recomputes lambda from the state -- so passing a
//     scaled conv_radius scales ONLY the diagonal, which is exactly beta.
#include <cstdio>
#include <cmath>
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
    opt.output_dir = "/workspace/solver/scratch/o_probe_quant";
    opt.override_spatial_order = 1;
    const CaseInput input = loadCaseInput(opt.case_path);
    SolverContext ctx(input, opt, MPI_COMM_WORLD);
    ctx.initializeState();

    const DistributedMesh &mesh = ctx.mesh();
    const Index n_owned = mesh.numOwned();
    ResidualAssembler &asmb = ctx.assembler();
    ImplicitSolver &imp = ctx.implicit();
    HaloExchange &halo = ctx.halo();
    StateField &U = ctx.state();

    // Take a few real pseudo-time steps so the residual is a realistic RHS
    // rather than the pristine freestream impulse.
    StateField R(mesh.numLocal()), dU(mesh.numLocal());
    std::vector<Real> dtau, ds((std::size_t)n_owned, 0.0), cr_scaled;
    ResidualDiagnostics diag;
    for (int warm = 0; warm < 20; ++warm) {
      ctx.syncState();
      asmb.evaluate(U, R, halo, diag);
      computeLocalTimeSteps(mesh, 0.5, asmb.convectiveSpectralRadius(),
                            asmb.viscousSpectralRadius(), 4.0, dtau);
      for (Index c = 0; c < n_owned; ++c) {
        const Real V = mesh.cells()[(std::size_t)c].volume;
        ds[(std::size_t)c] = V / dtau[(std::size_t)c];
      }
      imp.solve(U, R, ds, asmb.convectiveSpectralRadius(), asmb.viscousSpectralRadius(),
                6, dU, halo, true);
      for (Index c = 0; c < n_owned; ++c) {
        ConsVec u = U.get(c);
        const Real *d = dU.cell(c);
        for (int k = 0; k < kNumVars; ++k) u[k] += d[k];
        U.set(c, u);
      }
      ctx.enforcePositivity(U);
    }
    ctx.syncState();
    asmb.evaluate(U, R, halo, diag);

    const int min_inner = input.run_control.min_inner_iterations;
    const int max_inner = input.run_control.max_inner_iterations;

    // Emulate the driver's inner loop; 'reset_every' reproduces the old bug.
    auto runInner = [&](double cfl, double beta, bool reset_every, int &iters_used) {
      computeLocalTimeSteps(mesh, cfl, asmb.convectiveSpectralRadius(),
                            asmb.viscousSpectralRadius(), 4.0, dtau);
      for (Index c = 0; c < n_owned; ++c) {
        const Real V = mesh.cells()[(std::size_t)c].volume;
        ds[(std::size_t)c] = V / dtau[(std::size_t)c];
      }
      const auto &cr = asmb.convectiveSpectralRadius();
      cr_scaled.assign(cr.begin(), cr.end());
      for (auto &v : cr_scaled) v *= beta;

      int performed = 0;
      Real ratio = 1.0;
      while (performed < max_inner) {
        const int block = (performed == 0) ? min_inner : std::min(2, max_inner - performed);
        imp.solve(U, R, ds, cr_scaled, asmb.viscousSpectralRadius(), block, dU, halo,
                  /*reset_increment=*/reset_every ? true : (performed == 0));
        performed += block;
        ratio = imp.linearResidualRatio(U, R, ds, cr_scaled, asmb.viscousSpectralRadius(),
                                        dU, halo);
        if (ratio <= input.run_control.inner_residual_reduction_target) break;
      }
      iters_used = performed;
      return ratio;
    };

    printf("\n=== (A) effect of the dU.setZero() restart, beta = 1 (as shipped) ===\n");
    printf("inner target = %.1e, max_inner = %d\n\n",
           input.run_control.inner_residual_reduction_target, max_inner);
    printf("     CFL     OLD reset-every-call      NEW accumulate\n");
    printf("              ratio      iters        ratio      iters\n");
    for (double cfl : {0.5, 5.0, 10.0, 30.0, 100.0}) {
      int io = 0, in = 0;
      const Real ro = runInner(cfl, 1.0, true,  io);
      const Real rn = runInner(cfl, 1.0, false, in);
      printf("  %7.1f    %9.3e  %5d      %9.3e  %5d\n", cfl, ro, io, rn, in);
    }

    printf("\n=== (B) diagonal inflation beta, with accumulation fixed ===\n");
    printf("     CFL      beta=1        beta=1.5      beta=2        beta=3\n");
    for (double cfl : {0.5, 5.0, 10.0, 30.0, 100.0}) {
      printf("  %7.1f", cfl);
      for (double beta : {1.0, 1.5, 2.0, 3.0}) {
        int it = 0;
        const Real r = runInner(cfl, beta, false, it);
        printf("   %8.2e(%2d)", r, it);
      }
      printf("\n");
    }
  }
  MPI_Finalize();
  return 0;
}
