// Caveat check for the diagonal-inflation remedy.
//
// Inflating the diagonal changes the MATRIX, so the sweeps' fixed point is the
// inflated system, not the true linearization.  If the reported inner ratio is
// also measured with the inflated diagonal it will look excellent while the
// step actually solved a different system.  Here we measure both:
//   ratio_inflated : vs the inflated operator (what a naive change would report)
//   ratio_true     : the SAME dU vs the TRUE (beta=1) operator
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
    opt.output_dir = "/workspace/solver/scratch/o_probe_caveat";
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

    StateField R(mesh.numLocal()), dU(mesh.numLocal());
    std::vector<Real> dtau, ds((std::size_t)n_owned, 0.0), cr_scaled;
    ResidualDiagnostics diag;
    for (int warm = 0; warm < 20; ++warm) {
      ctx.syncState();
      asmb.evaluate(U, R, halo, diag);
      computeLocalTimeSteps(mesh, 0.5, asmb.convectiveSpectralRadius(),
                            asmb.viscousSpectralRadius(), 4.0, dtau);
      for (Index c = 0; c < n_owned; ++c)
        ds[(std::size_t)c] = mesh.cells()[(std::size_t)c].volume / dtau[(std::size_t)c];
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

    printf("\n=== diagonal inflation: reported vs TRUE linear residual ===\n");
    printf("(30 SGS sweeps, accumulate; ratio_true uses the beta=1 operator)\n\n");
    printf("     CFL   beta   ratio_inflated   ratio_TRUE\n");
    for (double cfl : {10.0, 30.0, 100.0}) {
      computeLocalTimeSteps(mesh, cfl, asmb.convectiveSpectralRadius(),
                            asmb.viscousSpectralRadius(), 4.0, dtau);
      for (Index c = 0; c < n_owned; ++c)
        ds[(std::size_t)c] = mesh.cells()[(std::size_t)c].volume / dtau[(std::size_t)c];
      const auto &cr = asmb.convectiveSpectralRadius();
      for (double beta : {1.0, 2.0}) {
        cr_scaled.assign(cr.begin(), cr.end());
        for (auto &v : cr_scaled) v *= beta;
        imp.solve(U, R, ds, cr_scaled, asmb.viscousSpectralRadius(), 30, dU, halo, true);
        const Real r_inf = imp.linearResidualRatio(U, R, ds, cr_scaled,
                                                   asmb.viscousSpectralRadius(), dU, halo);
        const Real r_true = imp.linearResidualRatio(U, R, ds, cr,
                                                    asmb.viscousSpectralRadius(), dU, halo);
        printf("  %7.1f  %4.1f      %9.2e     %9.2e\n", cfl, beta, r_inf, r_true);
      }
    }
  }
  MPI_Finalize();
  return 0;
}
