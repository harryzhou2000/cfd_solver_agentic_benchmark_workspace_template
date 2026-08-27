// Item 2: is buildDiagonal consistent with offDiagonalAction?
//
// Backward Euler on  V dU/dt = R,  R_i = -sum_f F.n area  gives
//     (V/dtau I - dR/dU) dU = R.
// With the upwind split F_f = 0.5(F(U_i)+F(U_j)).n - 0.5 lambda (U_j - U_i):
//     -dR_i/dU_i = sum_f [ 0.5 A(U_i).n + 0.5 lambda I ] area      (diagonal)
//     -dR_i/dU_j =        0.5 A(U_j).n - 0.5 lambda I   area       (off-diagonal)
// offDiagonalAction implements the second line verbatim.  buildDiagonal keeps
// only the 0.5*lambda*area part and DROPS the central term
//     C_i = sum_f 0.5 * A(U_i).n_if * area_f .
// Claim: C_i vanishes identically because A(U_i) does not depend on the face,
// so C_i = 0.5 * A(U_i) . (sum_f n_if area_f) = 0 by face closure.
// Verify that on a real cell of the actual cylinder mesh.
#include <cstdio>
#include <cmath>
#include <vector>
#include "core/case_input.h"
#include "core/options.h"
#include "solve/solver_context.h"

using namespace cns2d;

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);
  {
    CommandLineOptions opt;
    opt.case_path = "/workspace/solver/scratch/dbg_cyl_inviscid.json";
    opt.output_dir = "/workspace/solver/scratch/o_probe_diag";
    opt.override_spatial_order = 1;
    const CaseInput input = loadCaseInput(opt.case_path);
    SolverContext ctx(input, opt, MPI_COMM_WORLD);
    ctx.initializeState();
    const DistributedMesh &mesh = ctx.mesh();
    const PerfectGas &gas = ctx.flow().gas;

    // A non-uniform test state so the check is nontrivial.
    PrimVec W{1.17, 0.83, -0.41, 65.3};
    ConsVec U = gas.consFromPrim(W);

    const auto &ranges = mesh.cellFaceRanges();
    const auto &cfaces = mesh.cellFaces();
    const auto &signs  = mesh.cellFaceSign();
    const auto &faces  = mesh.faces();

    double worst_rel = 0.0; int worst_cell = -1;
    double worst_closure = 0.0;
    for (Index c = 0; c < mesh.numOwned(); ++c) {
      const auto rg = ranges[(std::size_t)c];
      // C_i applied to four independent unit directions.
      double maxC = 0.0, scale = 0.0;
      Vec2 closure{0,0};
      for (int j = 0; j < kNumVars; ++j) {
        ConsVec e{}; e[j] = 1.0;
        ConsVec C{};
        for (Index k = 0; k < rg.count; ++k) {
          const Index fid = cfaces[(std::size_t)(rg.begin + k)];
          const LocalFace &f = faces[(std::size_t)fid];
          const Real sg = (Real)signs[(std::size_t)(rg.begin + k)];
          const Vec2 n = sg * f.geom.normal;
          if (j == 0) closure = closure + f.geom.area * n;
          const ConsVec jac = fluxJacobianTimesVector(gas, U, n, e);
          for (int i = 0; i < kNumVars; ++i) C[i] += 0.5 * jac[i] * f.geom.area;
          for (int i = 0; i < kNumVars; ++i)
            scale = std::max(scale, std::fabs(0.5 * jac[i] * f.geom.area));
        }
        for (int i = 0; i < kNumVars; ++i) maxC = std::max(maxC, std::fabs(C[i]));
      }
      const double rel = maxC / std::max(scale, 1e-300);
      if (rel > worst_rel) { worst_rel = rel; worst_cell = c; }
      worst_closure = std::max(worst_closure, norm(closure));
    }
    printf("\n=== item 2: the central term dropped by buildDiagonal ===\n");
    printf("cells checked                       : %d\n", (int)mesh.numOwned());
    printf("worst |sum_f 0.5 A.n_if area| / scale: %.3e  (cell %d)\n", worst_rel, worst_cell);
    printf("worst |sum_f n_if area_f| (closure)  : %.3e\n", worst_closure);
    printf("VERDICT: %s\n", worst_rel < 1e-12
      ? "central term vanishes identically -> buildDiagonal IS consistent with offDiagonalAction"
      : "central term does NOT vanish -> diagonal is inconsistent");
  }
  MPI_Finalize();
  return 0;
}
