#include "numerics/Verification.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <random>

#include <nlohmann/json.hpp>

#include "core/Exception.hpp"
#include "core/Log.hpp"
#include "mesh/CgnsReader.hpp"
#include "mesh/GlobalMesh.hpp"
#include "mesh/MeshDistributor.hpp"
#include "mesh/Partitioner.hpp"
#include "numerics/ImplicitSolver.hpp"
#include "numerics/SpatialOperator.hpp"
#include "parallel/Comm.hpp"

namespace cfd {
namespace {

using json = nlohmann::json;

constexpr Real kDomain = 1.0;
constexpr Real kAmp = 0.10;
constexpr Real kWave = 2.0 * M_PI / kDomain;   // one full period across the domain
constexpr Real kBumpRadius = 0.36;             // support of the manufactured perturbation
constexpr Real kU0 = 0.5;                      // background velocity
constexpr Real kP0 = 1.0;
constexpr Real kRho0 = 1.0;

// C-infinity bump, identically zero (with all derivatives) outside the disk of
// radius kBumpRadius centred in the domain.  The manufactured state therefore
// equals the uniform freestream on the whole boundary, so the ordinary
// characteristic farfield condition is exact there and the measured error is
// purely the interior discretisation error.
Real bump(Real x, Real y) {
  const Real dx = x - 0.5 * kDomain, dy = y - 0.5 * kDomain;
  const Real r2 = (dx * dx + dy * dy) / (kBumpRadius * kBumpRadius);
  if (r2 >= 1.0) return 0.0;
  return std::exp(1.0 - 1.0 / (1.0 - r2));
}

// Smooth manufactured primitive state with compact support.
PrimVec manufactured(Real x, Real y) {
  const Real sx = std::sin(kWave * x), cx = std::cos(kWave * x);
  const Real sy = std::sin(kWave * y), cy = std::cos(kWave * y);
  const Real b = kAmp * bump(x, y);
  PrimVec w{};
  w[0] = kRho0 * (1.0 + b * sx * cy);
  w[1] = kU0 * (1.0 + b * cx * sy);
  w[2] = kU0 * b * sx * sy;
  w[3] = kP0 * (1.0 + b * cx * cy);
  return w;
}

ConsVec manufacturedFluxX(Real x, Real y, Real gamma) {
  const PrimVec w = manufactured(x, y);
  const Real E = w[3] / ((gamma - 1.0) * w[0]) + 0.5 * (w[1] * w[1] + w[2] * w[2]);
  return {w[0] * w[1], w[0] * w[1] * w[1] + w[3], w[0] * w[1] * w[2],
          w[1] * (w[0] * E + w[3])};
}

ConsVec manufacturedFluxY(Real x, Real y, Real gamma) {
  const PrimVec w = manufactured(x, y);
  const Real E = w[3] / ((gamma - 1.0) * w[0]) + 0.5 * (w[1] * w[1] + w[2] * w[2]);
  return {w[0] * w[2], w[0] * w[1] * w[2], w[0] * w[2] * w[2] + w[3],
          w[2] * (w[0] * E + w[3])};
}

// Analytic divergence of the inviscid flux, evaluated by high-order-accurate
// central differences of the closed-form flux (step chosen so that truncation
// and round-off are both far below the discretisation error being measured).
ConsVec analyticDivergence(Real x, Real y, Real gamma) {
  const Real h = 1.0e-5;
  const ConsVec fxp = manufacturedFluxX(x + h, y, gamma);
  const ConsVec fxm = manufacturedFluxX(x - h, y, gamma);
  const ConsVec gyp = manufacturedFluxY(x, y + h, gamma);
  const ConsVec gym = manufacturedFluxY(x, y - h, gamma);
  ConsVec d{};
  for (int k = 0; k < kNVar; ++k)
    d[k] = (fxp[k] - fxm[k]) / (2.0 * h) + (gyp[k] - gym[k]) / (2.0 * h);
  return d;
}

// Mixed triangle/quadrilateral mesh of [0,1]^2 with randomly perturbed interior
// nodes, so the study is run on a genuinely unstructured, non-uniform grid.
// The mesh is a smooth analytic distortion of a Cartesian grid: non-uniform,
// with mixed quadrilaterals and triangles, but with smoothly varying metrics,
// which is what a formal order study requires.  `jitter_fraction` > 0 adds
// random node displacement on top of it (an *irregular* mesh, exposed through
// `cfd2d verify --mesh-jitter`), for which finite-volume schemes are known to
// lose truncation-error order while keeping their solution-error order.
void buildPerturbedMesh(int n, GlobalMesh& mesh, Real jitter_fraction = 0.0) {
  const int np = n + 1;
  const Real h = kDomain / n;
  std::mt19937 rng(20240517u + static_cast<unsigned>(n));
  std::uniform_real_distribution<Real> jitter(-jitter_fraction * h, jitter_fraction * h);

  mesh.x.assign(static_cast<std::size_t>(np) * np, 0.0);
  mesh.y.assign(static_cast<std::size_t>(np) * np, 0.0);
  for (int j = 0; j < np; ++j) {
    for (int i = 0; i < np; ++i) {
      const Index id = j * np + i;
      const Real xi = static_cast<Real>(i) / n, eta = static_cast<Real>(j) / n;
      // Smooth, boundary-preserving distortion of the Cartesian grid.
      Real x = kDomain * (xi + 0.07 * std::sin(M_PI * xi) * std::sin(2.0 * M_PI * eta));
      Real y = kDomain * (eta + 0.07 * std::sin(2.0 * M_PI * xi) * std::sin(M_PI * eta));
      if (jitter_fraction > 0.0 && i > 0 && i < n && j > 0 && j < n) {
        x += jitter(rng);
        y += jitter(rng);
      }
      mesh.x[id] = x;
      mesh.y[id] = y;
    }
  }

  mesh.cell_node_offset.clear();
  mesh.cell_nodes.clear();
  mesh.cell_zone.clear();
  mesh.cell_node_offset.push_back(0);
  auto push = [&](std::initializer_list<Index> nodes) {
    for (Index v : nodes) mesh.cell_nodes.push_back(v);
    mesh.cell_node_offset.push_back(static_cast<Index>(mesh.cell_nodes.size()));
    mesh.cell_zone.push_back(0);
  };
  for (int j = 0; j < n; ++j) {
    for (int i = 0; i < n; ++i) {
      const Index a = j * np + i, b = a + 1, c = a + np + 1, d = a + np;
      if (((i + j) % 2) == 0) {
        push({a, b, c, d});                       // quadrilateral
      } else {
        push({a, b, c});                          // two triangles
        push({a, c, d});
      }
    }
  }
  mesh.orientCells();

  BoundaryPatch bp;
  bp.name = "verification_farfield";
  bp.bc_name = "verification_farfield";
  mesh.patches.assign(1, bp);
  std::vector<std::vector<std::pair<Index, Index>>> edges(1);
  for (int i = 0; i < n; ++i) {
    edges[0].emplace_back(i, i + 1);                                       // south
    edges[0].emplace_back(n * np + i, n * np + i + 1);                     // north
    edges[0].emplace_back(i * np, (i + 1) * np);                           // west
    edges[0].emplace_back(i * np + n, (i + 1) * np + n);                   // east
  }
  mesh.buildTopology(edges);
}

CaseConfig verificationCase() {
  CaseConfig c;
  c.case_id = "manufactured_solution_order_study";
  c.description = "smooth manufactured Euler state on a perturbed mixed mesh";
  c.mode = PhysicsMode::kInviscid;
  c.gas.gamma = 1.4;
  c.gas.R = 1.0;
  c.gas.prandtl = 0.72;
  c.freestream.rho = kRho0;
  c.freestream.velocity_magnitude = kU0;
  c.freestream.pressure = kP0;
  c.freestream.mach = kU0 / std::sqrt(1.4 * kP0 / kRho0);
  c.reference.length = 1.0;
  c.reference.area = 1.0;
  c.reference.reynolds_length = 1.0;
  c.boundary_conditions["verification_farfield"] = BcType::kFarfield;
  c.run.type = RunType::kSteady;
  c.run.max_steps = 1;
  return c;
}

OrderLevel measureLevel(int n, const SolverOptions& opts, MPI_Comm comm, Real jitter) {
  int rank = 0;
  MPI_Comm_rank(comm, &rank);
  GlobalMesh gmesh;
  PartitionResult part;
  int size = 1;
  MPI_Comm_size(comm, &size);
  if (rank == 0) {
    buildPerturbedMesh(n, gmesh, jitter);
    part = partitionMesh(gmesh, size);
  }
  LocalMesh lmesh;
  distributeMesh(gmesh, part, comm, lmesh);

  CaseConfig cfg = verificationCase();
  SpatialOperator op(lmesh, cfg, opts, comm);

  // Initialise from the manufactured state (cell-centre values).
  for (Index c = 0; c < lmesh.numTotalCells(); ++c) {
    const PrimVec w = manufactured(lmesh.cell_center[c][0], lmesh.cell_center[c][1]);
    const ConsVec u = op.gas().toConservative(w);
    for (int k = 0; k < kNVar; ++k) op.U()[c * kNVar + k] = u[k];
  }
  op.evaluateResidual();

  // Sample only cells that are far enough from the boundary that the boundary
  // treatment cannot pollute the interior truncation error.
  const Real h = kDomain / n;
  const Real margin = std::min(4.0 * h, 0.2 * kDomain);
  Real l1 = 0.0, l2 = 0.0, linf = 0.0, hsum = 0.0;
  long long count = 0;
  for (Index c = 0; c < lmesh.num_owned; ++c) {
    const Real x = lmesh.cell_center[c][0], y = lmesh.cell_center[c][1];
    if (x < margin || x > kDomain - margin || y < margin || y > kDomain - margin) continue;
    const ConsVec exact = analyticDivergence(x, y, cfg.gas.gamma);
    Real e2 = 0.0;
    for (int k = 0; k < kNVar; ++k) {
      const Real num = op.residual()[c * kNVar + k] / lmesh.cell_volume[c];
      const Real d = num - exact[k];
      e2 += d * d;
    }
    const Real e = std::sqrt(e2);
    l1 += e;
    l2 += e * e;
    linf = std::max(linf, e);
    hsum += std::sqrt(lmesh.cell_volume[c]);
    ++count;
  }
  Real red[3] = {l1, l2, hsum};
  globalSumArray(red, 3, comm);
  const long long gcount = globalSumLL(count, comm);
  const Real glinf = globalMax(linf, comm);

  OrderLevel lv;
  lv.n = n;
  lv.num_cells = static_cast<Index>(lmesh.global_num_cells);
  lv.num_sampled = static_cast<Index>(gcount);
  if (gcount > 0) {
    lv.err_l1 = red[0] / gcount;
    lv.err_l2 = std::sqrt(red[1] / gcount);
    lv.h = red[2] / gcount;
  }
  lv.err_linf = glinf;
  return lv;
}


// Exact cell average of the manufactured state, by a 3-point quadrature on each
// triangle of the polygon (degree-2 exact, so the quadrature error is far below
// the discretisation error being measured).
PrimVec exactCellAverage(const LocalMesh& m, Index c) {
  const Index* nodes = m.cellNodePtr(c);
  const int n = m.cellSize(c);
  PrimVec acc{};
  Real area_tot = 0.0;
  for (int k = 1; k < n - 1; ++k) {
    const Index a = nodes[0], b = nodes[k], d = nodes[k + 1];
    const Real ax = m.x[a], ay = m.y[a];
    const Real bx = m.x[b], by = m.y[b];
    const Real dx = m.x[d], dy = m.y[d];
    const Real area = 0.5 * std::abs((bx - ax) * (dy - ay) - (dx - ax) * (by - ay));
    // Midpoints of the triangle edges: exact for quadratics.
    const Real qx[3] = {0.5 * (ax + bx), 0.5 * (bx + dx), 0.5 * (dx + ax)};
    const Real qy[3] = {0.5 * (ay + by), 0.5 * (by + dy), 0.5 * (dy + ay)};
    for (int q = 0; q < 3; ++q) {
      const PrimVec w = manufactured(qx[q], qy[q]);
      for (int v = 0; v < kNVar; ++v) acc[v] += (area / 3.0) * w[v];
    }
    area_tot += area;
  }
  for (int v = 0; v < kNVar; ++v) acc[v] /= std::max(area_tot, 1e-300);
  return acc;
}

// Solve the steady manufactured problem
//     sum_f Phi_f . n S - V * div F(W_exact) = 0
// to convergence and return the discretisation error of the primitive state.
MmsLevel mmsLevel(int n, const SolverOptions& opts, MPI_Comm comm, Real jitter) {
  int rank = 0, size = 1;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);
  GlobalMesh gmesh;
  PartitionResult part;
  if (rank == 0) {
    buildPerturbedMesh(n, gmesh, jitter);
    part = partitionMesh(gmesh, size);
  }
  LocalMesh lmesh;
  distributeMesh(gmesh, part, comm, lmesh);

  CaseConfig cfg = verificationCase();
  cfg.run.max_steps = 4000;
  cfg.run.cfl_initial = 1.0;
  cfg.run.cfl_max = 200.0;
  cfg.run.pseudo_cfl_ramp_steps = 150;
  cfg.run.min_inner_iterations = 4;
  cfg.run.max_inner_iterations = 60;
  cfg.run.inner_residual_reduction_target = 1.0e-3;

  SpatialOperator op(lmesh, cfg, opts, comm);
  ImplicitSolver solver(op);
  op.initializeFreestream();

  const std::size_t nt = static_cast<std::size_t>(lmesh.numTotalCells()) * kNVar;
  std::vector<Real> du(nt, 0.0), rhs(nt, 0.0), source(nt, 0.0);
  for (Index c = 0; c < lmesh.num_owned; ++c) {
    const ConsVec d = analyticDivergence(lmesh.cell_center[c][0], lmesh.cell_center[c][1],
                                         cfg.gas.gamma);
    for (int k = 0; k < kNVar; ++k) source[c * kNVar + k] = lmesh.cell_volume[c] * d[k];
  }

  Real res0 = 0.0, res = 0.0;
  long long steps = 0;
  for (long long step = 0; step <= cfg.run.max_steps; ++step) {
    op.evaluateResidual();
    for (std::size_t i = 0; i < static_cast<std::size_t>(lmesh.num_owned) * kNVar; ++i)
      rhs[i] = op.residual()[i] - source[i];
    res = op.computeNorms(rhs).l2;
    if (step == 0) res0 = std::max(res, 1e-300);
    steps = step;
    if (res <= res0 * 1.0e-11) break;
    const Real f = std::min(1.0, static_cast<Real>(step + 1) / cfg.run.pseudo_cfl_ramp_steps);
    const Real cfl = cfg.run.cfl_initial *
                     std::pow(cfg.run.cfl_max / cfg.run.cfl_initial, f);
    op.computeTimeStep(cfl, 0.0);
    solver.solve(rhs, cfg.run.min_inner_iterations, cfg.run.max_inner_iterations,
                 cfg.run.inner_residual_reduction_target, du);
    op.applyUpdate(du, 1.0);
  }

  Real l1 = 0.0, l2 = 0.0, linf = 0.0, hsum = 0.0, vsum = 0.0;
  for (Index c = 0; c < lmesh.num_owned; ++c) {
    const PrimVec ex = exactCellAverage(lmesh, c);
    Real e2 = 0.0;
    // Normalised primitive-variable error (density, both velocities, pressure).
    const Real sc[kNVar] = {kRho0, kU0, kU0, kP0};
    for (int k = 0; k < kNVar; ++k) {
      const Real d = (op.W()[c * kNVar + k] - ex[k]) / sc[k];
      e2 += d * d;
    }
    const Real e = std::sqrt(e2);
    const Real v = lmesh.cell_volume[c];
    l1 += e * v;
    l2 += e * e * v;
    linf = std::max(linf, e);
    hsum += std::sqrt(v) * v;
    vsum += v;
  }
  Real red[4] = {l1, l2, hsum, vsum};
  globalSumArray(red, 4, comm);
  const Real glinf = globalMax(linf, comm);

  MmsLevel lv;
  lv.n = n;
  lv.num_cells = static_cast<Index>(lmesh.global_num_cells);
  lv.err_l1 = red[0] / red[3];
  lv.err_l2 = std::sqrt(red[1] / red[3]);
  lv.err_linf = glinf;
  lv.h = red[2] / red[3];
  lv.steps = steps;
  lv.residual_orders = std::log10(res0 / std::max(res, 1e-300));
  return lv;
}

Real fitOrderMms(const std::vector<MmsLevel>& lv, Real MmsLevel::*field) {
  Real sx = 0, sy = 0, sxx = 0, sxy = 0;
  int n = 0;
  for (const auto& l : lv) {
    if (l.h <= 0.0 || l.*field <= 0.0) continue;
    const Real x = std::log(l.h), y = std::log(l.*field);
    sx += x; sy += y; sxx += x * x; sxy += x * y; ++n;
  }
  CFD_CHECK(n >= 2, "MMS order fit needs at least two levels with a positive error");
  return (n * sxy - sx * sy) / (n * sxx - sx * sx);
}

Real fitOrder(const std::vector<OrderLevel>& lv, Real OrderLevel::*field) {
  CFD_CHECK(lv.size() >= 2, "order fit needs at least two refinement levels");
  // Least-squares slope of log(err) against log(h).
  Real sx = 0, sy = 0, sxx = 0, sxy = 0;
  int n = 0;
  for (const auto& l : lv) {
    if (l.h <= 0.0 || l.*field <= 0.0) continue;
    const Real x = std::log(l.h), y = std::log(l.*field);
    sx += x; sy += y; sxx += x * x; sxy += x * y; ++n;
  }
  CFD_CHECK(n >= 2, "order fit needs at least two levels with a positive error");
  return (n * sxy - sx * sy) / (n * sxx - sx * sx);
}

}  // namespace

VerificationReport runVerification(int levels, int base, const std::string* case_path,
                                   const SolverOptions& base_opts, MPI_Comm comm, Real jitter) {
  VerificationReport rep;
  rep.mesh_jitter = jitter;
  LOG() << "order-of-accuracy study on generated perturbed mixed meshes\n"
        << "  level      cells    mean h       L1 error       L2 error     Linf error\n";
  for (int l = 0; l < levels; ++l) {
    const int n = base << l;
    SolverOptions o = base_opts;
    o.second_order = true;
    const OrderLevel lv = measureLevel(n, o, comm, jitter);
    rep.second_order.push_back(lv);
    LOG() << "  2nd  " << std::setw(4) << n << " " << std::setw(9) << lv.num_cells << "  "
          << std::scientific << std::setprecision(3) << lv.h << "  " << lv.err_l1 << "  "
          << lv.err_l2 << "  " << lv.err_linf << "\n";
  }
  for (int l = 0; l < levels; ++l) {
    const int n = base << l;
    SolverOptions o = base_opts;
    o.second_order = false;
    const OrderLevel lv = measureLevel(n, o, comm, jitter);
    rep.first_order.push_back(lv);
    LOG() << "  1st  " << std::setw(4) << n << " " << std::setw(9) << lv.num_cells << "  "
          << std::scientific << std::setprecision(3) << lv.h << "  " << lv.err_l1 << "  "
          << lv.err_l2 << "  " << lv.err_linf << "\n";
  }
  {
    struct Cfg { const char* name; bool second; LimiterType lim; std::vector<MmsLevel>* out; };
    const Cfg cfgs[3] = {
        {"2nd order, unlimited", true, LimiterType::kNone, &rep.mms_second_order},
        {"2nd order, Venkatakrishnan", true, base_opts.limiter, &rep.mms_second_order_limited},
        {"1st order", false, LimiterType::kNone, &rep.mms_first_order}};
    LOG() << "manufactured-solution study (discretisation error of the converged steady state)\n"
          << "  scheme                      n     cells    mean h      L1 error      L2 error"
             "    Linf error  steps  res.orders\n";
    for (const auto& cf : cfgs) {
      for (int l = 0; l < levels; ++l) {
        const int n = base << l;
        SolverOptions o = base_opts;
        o.second_order = cf.second;
        o.limiter = cf.lim;
        const MmsLevel lv = mmsLevel(n, o, comm, jitter);
        cf.out->push_back(lv);
        LOG() << "  " << std::left << std::setw(26) << cf.name << std::right << std::setw(4)
              << n << std::setw(10) << lv.num_cells << "  " << std::scientific
              << std::setprecision(3) << lv.h << "  " << lv.err_l1 << "  " << lv.err_l2 << "  "
              << lv.err_linf << std::setw(7) << lv.steps << std::fixed << std::setprecision(1)
              << std::setw(9) << lv.residual_orders << "\n";
      }
    }
    rep.mms_order_second = fitOrderMms(rep.mms_second_order, &MmsLevel::err_l1);
    rep.mms_order_second_limited = fitOrderMms(rep.mms_second_order_limited, &MmsLevel::err_l1);
    rep.mms_order_first = fitOrderMms(rep.mms_first_order, &MmsLevel::err_l1);
    LOG() << "  observed L1 solution-error order: unlimited 2nd " << std::fixed
          << std::setprecision(3) << rep.mms_order_second << ", limited 2nd "
          << rep.mms_order_second_limited << ", 1st " << rep.mms_order_first << "\n";
  }

  rep.observed_order_l1_second = fitOrder(rep.second_order, &OrderLevel::err_l1);
  rep.observed_order_l2_second = fitOrder(rep.second_order, &OrderLevel::err_l2);
  rep.observed_order_l1_first = fitOrder(rep.first_order, &OrderLevel::err_l1);
  LOG() << "  observed order (second-order scheme): L1 " << std::fixed << std::setprecision(3)
        << rep.observed_order_l1_second << ", L2 " << rep.observed_order_l2_second << "\n"
        << "  observed order (first-order scheme) : L1 " << rep.observed_order_l1_first << "\n";

  if (case_path != nullptr) {
    CaseConfig cfg = CaseConfig::loadFromFile(*case_path);
    int rank = 0, size = 1;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);
    GlobalMesh gmesh;
    PartitionResult part;
    if (rank == 0) {
      CgnsReadOptions ro;
      ro.verbose = false;
      readCgnsMesh(cfg.mesh_file, ro, gmesh);
      part = partitionMesh(gmesh, size);
    }
    LocalMesh lmesh;
    distributeMesh(gmesh, part, comm, lmesh);

    // Cells that touch no boundary face: their stencil is entirely interior, so
    // both checks below measure the interior scheme alone.
    // A uniform freestream is not a solution of the problem *with a body*, so
    // the wall boundary condition legitimately produces a residual in the wall
    // cells, and their non-zero gradients reach one cell further through the
    // reconstruction.  Excluding two layers therefore isolates the interior
    // scheme, which is what freestream preservation is about.  The mask is
    // grown through the halo so that it is identical for every rank count.
    std::vector<Real> mask(static_cast<std::size_t>(lmesh.numTotalCells()), 0.0);
    for (Index b = 0; b < lmesh.numBoundaryFaces(); ++b) mask[lmesh.bface_cell[b]] = 1.0;
    {
      HaloExchanger halo;
      halo.setup(lmesh, comm);
      halo.exchange(mask.data(), 1);
      std::vector<Real> grown = mask;
      for (Index f = 0; f < lmesh.numFaces(); ++f) {
        const Index l = lmesh.face_l[f], r = lmesh.face_r[f];
        if (mask[l] > 0.0) grown[r] = 1.0;
        if (mask[r] > 0.0) grown[l] = 1.0;
      }
      mask.swap(grown);
      halo.exchange(mask.data(), 1);
    }
    std::vector<char> touches_boundary(static_cast<std::size_t>(lmesh.numTotalCells()), 0);
    for (Index c = 0; c < lmesh.numTotalCells(); ++c) touches_boundary[c] = mask[c] > 0.0;

    // (1) Freestream preservation.
    {
      SpatialOperator op(lmesh, cfg, base_opts, comm);
      op.initializeFreestream();
      op.evaluateResidual();
      Real worst = 0.0;
      const Real scale = cfg.freestream.rho * cfg.freestreamSoundSpeed();
      for (Index c = 0; c < lmesh.num_owned; ++c) {
        if (touches_boundary[c]) continue;
        for (int k = 0; k < kNVar; ++k)
          worst = std::max(worst, std::abs(op.residual()[c * kNVar + k]) /
                                      (lmesh.cell_volume[c] * scale));
      }
      rep.freestream_residual_linf = globalMax(worst, comm);
    }

    // (2) Exactness of the gradient and of the reconstruction for a linear
    //     primitive field: a scheme that is second-order accurate in smooth
    //     regions must reproduce linear data exactly.  The slopes are scaled by
    //     the mesh extent so that density and pressure stay positive over the
    //     whole domain (the aerofoil mesh reaches |x| ~ 80), because otherwise
    //     the positivity floors would clip the field and the test would measure
    //     the clipping instead of the reconstruction.
    {
      SolverOptions o = base_opts;
      o.limiter = LimiterType::kNone;
      o.second_order = true;
      SpatialOperator op(lmesh, cfg, o, comm);
      Real ext = 0.0;
      for (Index i = 0; i < lmesh.numNodes(); ++i)
        ext = std::max(ext, std::max(std::abs(lmesh.x[i]), std::abs(lmesh.y[i])));
      ext = std::max(globalMax(ext, comm), 1.0);
      const Real r0 = cfg.freestream.rho, p0 = cfg.freestream.pressure;
      const Real ref[kNVar] = {r0, cfg.freestreamSoundSpeed(), cfg.freestreamSoundSpeed(), p0};
      // Total variation of each variable over the domain is 5% of its reference.
      const Real a = 0.05 / (2.0 * ext);
      const Real gx[kNVar] = {0.6 * a * ref[0], 1.0 * a * ref[1], -0.8 * a * ref[2],
                              0.5 * a * ref[3]};
      const Real gy[kNVar] = {-0.4 * a * ref[0], 0.3 * a * ref[1], 0.9 * a * ref[2],
                              -1.0 * a * ref[3]};
      const Real w0[kNVar] = {r0, 0.31 * ref[1], -0.12 * ref[2], p0};
      auto exact = [&](Real x, Real y, int k) { return w0[k] + gx[k] * x + gy[k] * y; };
      for (Index c = 0; c < lmesh.numTotalCells(); ++c) {
        PrimVec w{};
        for (int k = 0; k < kNVar; ++k)
          w[k] = exact(lmesh.cell_center[c][0], lmesh.cell_center[c][1], k);
        CFD_CHECK(w[0] > 0.0 && w[3] > 0.0, "verification linear field went non-physical");
        const ConsVec u = op.gas().toConservative(w);
        for (int k = 0; k < kNVar; ++k) op.U()[c * kNVar + k] = u[k];
      }
      op.evaluateResidual();
      Real gerr = 0.0, rerr = 0.0;
      for (Index c = 0; c < lmesh.num_owned; ++c) {
        if (touches_boundary[c]) continue;
        for (int k = 0; k < kNVar; ++k) {
          const Real sc = std::max(std::abs(gx[k]) + std::abs(gy[k]), 1e-30);
          gerr = std::max(gerr, std::abs(op.gradients()[(c * kNVar + k) * kDim + 0] - gx[k]) / sc);
          gerr = std::max(gerr, std::abs(op.gradients()[(c * kNVar + k) * kDim + 1] - gy[k]) / sc);
        }
      }
      for (Index f = 0; f < lmesh.numFaces(); ++f) {
        for (Index c : {lmesh.face_l[f], lmesh.face_r[f]}) {
          if (c >= lmesh.num_owned || touches_boundary[c]) continue;
          const Vec2 d = lmesh.face_center[f] - lmesh.cell_center[c];
          for (int k = 0; k < kNVar; ++k) {
            const Real rec = op.W()[c * kNVar + k] +
                             op.limiters()[c * kNVar + k] *
                                 (op.gradients()[(c * kNVar + k) * kDim + 0] * d[0] +
                                  op.gradients()[(c * kNVar + k) * kDim + 1] * d[1]);
            const Real ex = exact(lmesh.face_center[f][0], lmesh.face_center[f][1], k);
            // Normalised by the reference magnitude, not by the local value,
            // which would be arbitrarily small where the field crosses a level.
            rerr = std::max(rerr, std::abs(rec - ex) / ref[k]);
          }
        }
      }
      rep.linear_gradient_max_error = globalMax(gerr, comm);
      rep.linear_reconstruction_max_error = globalMax(rerr, comm);
    }
    rep.case_id = cfg.case_id;
    rep.have_case_checks = true;
    LOG() << "checks on mesh '" << cfg.mesh_file << "' (case " << cfg.case_id << "):\n"
          << "  freestream preservation, max |R|/(V rho a) over interior cells : "
          << std::scientific << std::setprecision(3) << rep.freestream_residual_linf << "\n"
          << "  linear-field gradient relative error (interior cells)          : "
          << rep.linear_gradient_max_error << "\n"
          << "  linear-field reconstruction relative error at face centres     : "
          << rep.linear_reconstruction_max_error << "\n";
  }
  return rep;
}

void writeVerificationJson(const VerificationReport& r, const std::string& path) {
  json j;
  auto levels = [](const std::vector<OrderLevel>& v) {
    json a = json::array();
    for (const auto& l : v) {
      a.push_back({{"n", l.n}, {"num_cells", l.num_cells}, {"num_sampled", l.num_sampled},
                   {"mean_h", l.h}, {"err_l1", l.err_l1}, {"err_l2", l.err_l2},
                   {"err_linf", l.err_linf}});
    }
    return a;
  };
  j["description"] =
      "Truncation error of the finite-volume residual against the analytic flux divergence of a "
      "smooth manufactured Euler state, on internally generated mixed triangle/quadrilateral "
      "meshes of [0,1]^2 obtained by a smooth analytic distortion of a Cartesian grid (optional "
      "extra random node jitter through --mesh-jitter; the runs reported here use jitter 0). "
      "Cells near the boundary are excluded so the interior scheme is measured in isolation.";
  j["mesh_jitter_fraction"] = r.mesh_jitter;
  j["second_order_levels"] = levels(r.second_order);
  j["first_order_levels"] = levels(r.first_order);
  auto mms = [](const std::vector<MmsLevel>& v) {
    json a = json::array();
    for (const auto& l : v) {
      a.push_back({{"n", l.n}, {"num_cells", l.num_cells}, {"mean_h", l.h},
                   {"err_l1", l.err_l1}, {"err_l2", l.err_l2}, {"err_linf", l.err_linf},
                   {"steps", l.steps}, {"residual_reduction_orders", l.residual_orders}});
    }
    return a;
  };
  j["mms_description"] =
      "Method of manufactured solutions: a C-infinity, compactly supported perturbation of the "
      "uniform state is used, so the manufactured state coincides with the freestream on the "
      "whole boundary and the ordinary characteristic farfield condition is exact. The steady "
      "problem with the analytic source term is solved to machine convergence and the error is "
      "measured against exact cell averages.";
  j["mms_second_order_unlimited"] = mms(r.mms_second_order);
  j["mms_second_order_limited"] = mms(r.mms_second_order_limited);
  j["mms_first_order"] = mms(r.mms_first_order);
  j["mms_observed_order_second_order_unlimited"] = r.mms_order_second;
  j["mms_observed_order_second_order_limited"] = r.mms_order_second_limited;
  j["mms_observed_order_first_order"] = r.mms_order_first;
  j["observed_order_second_order_l1"] = r.observed_order_l1_second;
  j["observed_order_second_order_l2"] = r.observed_order_l2_second;
  j["observed_order_first_order_l1"] = r.observed_order_l1_first;
  if (r.have_case_checks) {
    j["case_id"] = r.case_id;
    j["freestream_residual_linf"] = r.freestream_residual_linf;
    j["linear_gradient_max_error"] = r.linear_gradient_max_error;
    j["linear_reconstruction_max_error"] = r.linear_reconstruction_max_error;
  }
  std::ofstream(path) << j.dump(2) << "\n";
}

}  // namespace cfd
