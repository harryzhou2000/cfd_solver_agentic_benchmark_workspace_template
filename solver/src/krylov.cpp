#include "krylov.hpp"

#include <mpi.h>

#include <algorithm>
#include <cmath>

namespace cfd {

namespace {

double dot(const std::vector<double>& a, const std::vector<double>& b) {
  double s = 0.0;
  for (size_t i = 0; i < a.size(); ++i) s += a[i] * b[i];
  double g = 0.0;
  MPI_Allreduce(&s, &g, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  return g;
}

}  // namespace

// Right-preconditioned restarted GMRES(m): solves A x = b with the
// preconditioner M (Mx applies M^{-1}).  The Krylov basis is
// span{r, AM^{-1}r, (AM^{-1})^2 r, ...} with v1 = r/||r||, so the
// Hessenberg least-squares residual |g[k+1]| is the true residual
// ||b - A x||; the correction is x += M^{-1}(V_m y).  Convergence is
// measured against the original right-hand side norm (bnorm).
int gmres(int n, int m, const std::vector<double>& b,
          const std::function<void(const std::vector<double>&, std::vector<double>&)>& Ax,
          const std::function<void(const std::vector<double>&, std::vector<double>&)>& Mx,
          std::vector<double>& x, double tol, int max_iter, bool* converged) {
  if (converged) *converged = false;
  const double bnorm = std::sqrt(dot(b, b));
  if (bnorm < 1e-300) {
    std::fill(x.begin(), x.end(), 0.0);
    return 0;
  }

  int total = 0;
  std::vector<double> r(n), w(n), z(n);
  std::vector<double> g(static_cast<size_t>(m) + 1), cs(static_cast<size_t>(m) + 1),
      sn(static_cast<size_t>(m) + 1);
  std::vector<std::vector<double>> V(static_cast<size_t>(m) + 1,
                                     std::vector<double>(static_cast<size_t>(n)));
  std::vector<std::vector<double>> H(static_cast<size_t>(m) + 1,
                                     std::vector<double>(static_cast<size_t>(m)));
  std::vector<double> y(static_cast<size_t>(m));
  const bool dbg = std::getenv("CFD_GMRES_DEBUG") != nullptr;
  int dbr = 0;
  if (dbg) MPI_Comm_rank(MPI_COMM_WORLD, &dbr);

  while (total < max_iter) {
    // r = b - A x
    Ax(x, w);
    for (int i = 0; i < n; ++i) r[static_cast<size_t>(i)] = b[static_cast<size_t>(i)] - w[static_cast<size_t>(i)];
    double beta = std::sqrt(dot(r, r));
    if (beta < 1e-300) break;
    for (int i = 0; i < n; ++i) V[0][static_cast<size_t>(i)] = r[static_cast<size_t>(i)] / beta;
    g[0] = beta;

    int k = 0;
    for (k = 0; k < m; ++k) {
      Mx(V[static_cast<size_t>(k)], z);
      Ax(z, w);
      for (int j = 0; j <= k; ++j) {
        const double h = dot(w, V[static_cast<size_t>(j)]);
        H[static_cast<size_t>(j)][static_cast<size_t>(k)] = h;
        for (int i = 0; i < n; ++i)
          w[static_cast<size_t>(i)] -= h * V[static_cast<size_t>(j)][static_cast<size_t>(i)];
      }
      const double hk1 = std::sqrt(dot(w, w));
      H[static_cast<size_t>(k + 1)][static_cast<size_t>(k)] = hk1;
      for (int i = 0; i < n; ++i)
        V[static_cast<size_t>(k + 1)][static_cast<size_t>(i)] =
            (hk1 > 1e-300) ? w[static_cast<size_t>(i)] / hk1 : 0.0;

      for (int j = 0; j < k; ++j) {
        const double tmp =
            cs[static_cast<size_t>(j)] * H[static_cast<size_t>(j)][static_cast<size_t>(k)] +
            sn[static_cast<size_t>(j)] * H[static_cast<size_t>(j + 1)][static_cast<size_t>(k)];
        H[static_cast<size_t>(j + 1)][static_cast<size_t>(k)] =
            -sn[static_cast<size_t>(j)] * H[static_cast<size_t>(j)][static_cast<size_t>(k)] +
            cs[static_cast<size_t>(j)] * H[static_cast<size_t>(j + 1)][static_cast<size_t>(k)];
        H[static_cast<size_t>(j)][static_cast<size_t>(k)] = tmp;
      }
      const double hkk = H[static_cast<size_t>(k)][static_cast<size_t>(k)];
      const double hk1k = H[static_cast<size_t>(k + 1)][static_cast<size_t>(k)];
      const double denom = std::sqrt(hkk * hkk + hk1k * hk1k);
      double c = 1.0, s = 0.0;
      if (denom > 1e-300) {
        c = hkk / denom;
        s = hk1k / denom;
      }
      // write back the rotated column: the back-substitution below solves
      // the rotated upper-triangular system and must use the rotated
      // diagonal (denom) and zeroed subdiagonal.
      H[static_cast<size_t>(k)][static_cast<size_t>(k)] = denom;
      H[static_cast<size_t>(k + 1)][static_cast<size_t>(k)] = 0.0;
      cs[static_cast<size_t>(k)] = c;
      sn[static_cast<size_t>(k)] = s;
      const double gk1 = -s * g[static_cast<size_t>(k)];
      g[static_cast<size_t>(k)] = c * g[static_cast<size_t>(k)];
      g[static_cast<size_t>(k + 1)] = gk1;
      ++total;

      if (std::abs(gk1) <= tol * bnorm || k == m - 1) {
        if (dbg && dbr == 0)
          std::fprintf(stderr,
                       "[gmres] total=%d k=%d |g|/beta=%.3e hkk=%.3e hk1k=%.3e "
                       "beta=%.3e bnorm=%.3e\n",
                       total, k, std::abs(gk1) / beta, hkk, hk1k, beta, bnorm);
        for (int j = k; j >= 0; --j) {
          double ssum = g[static_cast<size_t>(j)];
          for (int jj = j + 1; jj <= k; ++jj)
            ssum -= H[static_cast<size_t>(j)][static_cast<size_t>(jj)] *
                    y[static_cast<size_t>(jj)];
          y[static_cast<size_t>(j)] =
              (std::abs(H[static_cast<size_t>(j)][static_cast<size_t>(j)]) > 1e-300)
                  ? ssum / H[static_cast<size_t>(j)][static_cast<size_t>(j)]
                  : 0.0;
        }
        // Right-preconditioned update: x += M^{-1}(V_m y).  The Krylov
        // vectors V live in the preconditioned space, so the correction must
        // be mapped back with one more application of M^{-1}.
        for (int i = 0; i < n; ++i) {
          double ssum = 0.0;
          for (int j = 0; j <= k; ++j)
            ssum += V[static_cast<size_t>(j)][static_cast<size_t>(i)] * y[static_cast<size_t>(j)];
          w[static_cast<size_t>(i)] = ssum;
        }
        Mx(w, z);
        for (int i = 0; i < n; ++i) x[static_cast<size_t>(i)] += z[static_cast<size_t>(i)];
        if (dbg && dbr == 0) {
          // verify the true residual of the updated x
          Ax(x, r);
          double tr2 = 0.0;
          for (int i = 0; i < n; ++i) {
            const double rr = b[static_cast<size_t>(i)] - r[static_cast<size_t>(i)];
            tr2 += rr * rr;
          }
          std::fprintf(stderr, "[gmres-true] total=%d |g|/bnorm=%.3e true/bnorm=%.3e\n", total,
                       std::abs(gk1) / bnorm, std::sqrt(tr2) / bnorm);
        }
        if (std::abs(gk1) <= tol * bnorm) {
          if (converged) *converged = true;
          return total;
        }
        std::fill(y.begin(), y.end(), 0.0);
        break;
        // the true residual when the Arnoldi basis loses orthogonality).
        {
          Ax(x, r);
          double true_res = 0.0;
          for (int i = 0; i < n; ++i) {
            const double rr = b[static_cast<size_t>(i)] - r[static_cast<size_t>(i)];
            true_res += rr * rr;
          }
          // MPI reduction for the true residual
          double gtrue_res = 0.0;
          MPI_Allreduce(&true_res, &gtrue_res, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
          const double true_res_norm = std::sqrt(gtrue_res);
          if (dbg && dbr == 0) {
            std::fprintf(stderr, "[gmres-true] total=%d |g|/bnorm=%.3e true/bnorm=%.3e\n", total,
                         std::abs(gk1) / bnorm, true_res_norm / bnorm);
          }
          if (true_res_norm <= tol * bnorm) {
            if (converged) *converged = true;
            return total;
          }
          if (k == m - 1 && total >= max_iter) {
            // Reached max iterations without convergence; return what we have
            return total;
          }
        }
        std::fill(y.begin(), y.end(), 0.0);
        break;
      }
    }
  }
  return total;
}

}  // namespace cfd
