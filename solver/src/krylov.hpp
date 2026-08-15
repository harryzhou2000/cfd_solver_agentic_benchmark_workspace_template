#pragma once

#include <functional>
#include <vector>

namespace cfd {

// Restarted GMRES(m) for the linear system A x = b, right-preconditioned with
// M.  Both operators are provided as callables:
//   Ax(v, out):  out = A * v
//   Mx(v, out):  out = M^{-1} * v   (preconditioner apply)
// Returns the number of iterations used; convergence is checked on the
// preconditioned residual norm relative to the initial one.
int gmres(int n, int m, const std::vector<double>& b,
          const std::function<void(const std::vector<double>&, std::vector<double>&)>& Ax,
          const std::function<void(const std::vector<double>&, std::vector<double>&)>& Mx,
          std::vector<double>& x, double tol, int max_iter, bool* converged = nullptr);

}  // namespace cfd
