#include "solver.hpp"

#include "numerics.hpp"
#include "physics.hpp"
#include "steady_recovery.hpp"

#include <mpi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>

namespace aerofv {
namespace {

constexpr double kTiny = 1.0e-14;
constexpr double kPi = 3.141592653589793238462643383279502884;

void mpi_check(int status, const char *operation) {
  if (status == MPI_SUCCESS) {
    return;
  }
  char message[MPI_MAX_ERROR_STRING]{};
  int length = 0;
  MPI_Error_string(status, message, &length);
  throw std::runtime_error(std::string(operation) + ": " +
                           std::string(message, static_cast<std::size_t>(length)));
}

double wall_seconds() {
  using Clock = std::chrono::steady_clock;
  static const auto origin = Clock::now();
  return std::chrono::duration<double>(Clock::now() - origin).count();
}

double primitive_component(const Primitive &state, std::size_t component) {
  switch (component) {
  case 0:
    return state.rho;
  case 1:
    return state.u;
  case 2:
    return state.v;
  case 3:
    return state.p;
  default:
    throw std::logic_error("primitive component index out of range");
  }
}

void update_extrema(Primitive &minimum, Primitive &maximum,
                    const Primitive &candidate) {
  minimum.rho = std::min(minimum.rho, candidate.rho);
  minimum.u = std::min(minimum.u, candidate.u);
  minimum.v = std::min(minimum.v, candidate.v);
  minimum.p = std::min(minimum.p, candidate.p);
  maximum.rho = std::max(maximum.rho, candidate.rho);
  maximum.u = std::max(maximum.u, candidate.u);
  maximum.v = std::max(maximum.v, candidate.v);
  maximum.p = std::max(maximum.p, candidate.p);
}

Conservative zero_state() { return {0.0, 0.0, 0.0, 0.0}; }

double relative_range(const std::deque<double> &history) {
  if (history.empty()) {
    return std::numeric_limits<double>::infinity();
  }
  const auto [minimum, maximum] = std::minmax_element(history.begin(), history.end());
  const double mean = std::accumulate(history.begin(), history.end(), 0.0) /
                      static_cast<double>(history.size());
  return (*maximum - *minimum) / std::max(1.0, std::abs(mean));
}

} // namespace

FlowSolver::FlowSolver(const CaseConfig &config, const LocalMesh &mesh,
                       MPI_Comm communicator)
    : config_(config), mesh_(mesh), communicator_(communicator) {
  mpi_check(MPI_Comm_rank(communicator_, &rank_), "MPI_Comm_rank");
  mpi_check(MPI_Comm_size(communicator_, &ranks_), "MPI_Comm_size");
  if (mesh_.owned_cell_count <= 0 || mesh_.cells.empty()) {
    throw std::invalid_argument("solver rank has no owned mesh cells");
  }
  if (static_cast<std::size_t>(mesh_.owned_cell_count) > mesh_.cells.size()) {
    throw std::invalid_argument("invalid LocalMesh owned-cell count");
  }
  const double angle = config_.freestream.aoa_degrees * kPi / 180.0;
  drag_direction_ = {std::cos(angle), std::sin(angle)};
  lift_direction_ = {-std::sin(angle), std::cos(angle)};
  freestream_ = {config_.freestream.rho,
                 config_.freestream.velocity_magnitude * drag_direction_.x,
                 config_.freestream.velocity_magnitude * drag_direction_.y,
                 config_.freestream.pressure};
  freestream_state_ = primitive_to_conservative(freestream_, config_.gas);
  dynamic_pressure_ = 0.5 * freestream_.rho *
                      config_.freestream.velocity_magnitude *
                      config_.freestream.velocity_magnitude;
  if (!(dynamic_pressure_ > 0.0)) {
    throw std::invalid_argument("freestream dynamic pressure is not positive");
  }
  if (config_.physics.mode == PhysicsMode::laminar) {
    dynamic_viscosity_ =
        freestream_.rho * config_.freestream.velocity_magnitude *
        config_.reference.reynolds_length / *config_.physics.reynolds;
  }

  for (const Face &face : mesh_.faces) {
    if (face.right_cell < 0 && boundary_type(face) == BoundaryType::unknown) {
      throw std::invalid_argument("mesh boundary family '" + face.boundary_family +
                                  "' is absent from case boundary_conditions");
    }
  }
  states_.assign(mesh_.cells.size(), freestream_state_);
  primitives_.assign(mesh_.cells.size(), freestream_);
  gradients_.resize(mesh_.cells.size());
  limiters_.assign(mesh_.cells.size(), PrimitiveLimiter{1.0, 1.0, 1.0, 1.0});

  method_metadata_.solver_name = "AeroFV";
#ifdef AEROFV_VERSION
  method_metadata_.solver_version = AEROFV_VERSION;
#else
  method_metadata_.solver_version = "development";
#endif
#ifdef AEROFV_GIT_REVISION
  method_metadata_.git_revision = AEROFV_GIT_REVISION;
#endif
  method_metadata_.partitioner = "metis_kway";
  method_metadata_.halo_exchange = "neighbor_isend_irecv";
  method_metadata_.inviscid_flux = "rusanov_local_lax_friedrichs";
  method_metadata_.entropy_fix = std::nullopt;
  method_metadata_.viscous_flux =
      config_.physics.mode == PhysicsMode::laminar
          ? "corrected_central_primitive_gradient_cell_center_normal_difference"
          : "disabled";
  method_metadata_.time_integrator = config_.run_control.type == RunType::transient
                                         ? "bdf2_dual_time"
                                         : "implicit_local_pseudo_time";
  method_metadata_.implicit_solver =
      config_.run_control.type == RunType::steady
          ? "matrix_free_gmres_pseudo_transient_newton"
          : "matrix_free_gmres_bdf2_pseudo_transient_newton";
  method_metadata_.reconstruction =
      config_.run_control.type == RunType::steady
          ? "first_order_CFL_continuation_then_blended_weighted_least_squares_piecewise_linear"
          : "steady_base_continuation_then_full_weighted_least_squares_piecewise_linear";
  method_metadata_.limiter = "barth_jespersen_active";
  method_metadata_.positivity_preservation =
      "face_reconstruction_scaling_and_cell_local_fraction_to_boundary";
  method_metadata_.wall_boundary_output_semantics = "boundary_value";
  method_metadata_.true_bdf2_inner_loop =
      config_.run_control.type == RunType::transient;
}

BoundaryType FlowSolver::boundary_type(const Face &face) const {
  if (face.right_cell >= 0) {
    return BoundaryType::interior;
  }
  const auto found = config_.boundary_conditions.find(face.boundary_family);
  return found == config_.boundary_conditions.end() ? BoundaryType::unknown
                                                     : found->second;
}

int FlowSolver::other_cell(const Face &face, int local_cell) const {
  if (face.left_cell == local_cell) {
    return face.right_cell;
  }
  if (face.right_cell == local_cell) {
    return face.left_cell;
  }
  throw std::logic_error("cell-face incidence is inconsistent for local cell " +
                         std::to_string(local_cell) + " (face left=" +
                         std::to_string(face.left_cell) + ", right=" +
                         std::to_string(face.right_cell) + ")");
}

Vec2 FlowSolver::outward_normal(const Face &face, int local_cell) const {
  if (face.left_cell == local_cell) {
    return face.normal;
  }
  if (face.right_cell == local_cell) {
    return {-face.normal.x, -face.normal.y};
  }
  throw std::logic_error("cell-face incidence is inconsistent");
}

Primitive FlowSolver::virtual_neighbor(int local_cell, const Face &face) const {
  const int neighbor = other_cell(face, local_cell);
  if (neighbor >= 0) {
    return primitives_.at(static_cast<std::size_t>(neighbor));
  }
  return boundary_ghost_primitive(
      primitives_.at(static_cast<std::size_t>(local_cell)), freestream_,
      outward_normal(face, local_cell), boundary_type(face), config_.gas);
}

void FlowSolver::initialize_state(
    const std::optional<std::filesystem::path> &restart_file) {
  std::fill(states_.begin(), states_.end(), freestream_state_);
  initialized_from_restart_ = restart_file.has_value();
  if (restart_file) {
    load_restart(*restart_file);
  }
  exchange_conservative_halo(mesh_, states_, communicator_);
  refresh_primitives();
}

void FlowSolver::load_restart(const std::filesystem::path &path) {
  std::vector<Conservative> global_states;
  int read_ok = 1;
  std::string read_error;
  if (rank_ == 0) {
    try {
      std::ifstream input(path, std::ios::binary);
      if (!input) {
        throw std::runtime_error("could not open restart file: " + path.string());
      }
      constexpr std::array<char, 16> expected{{
          'A', 'E', 'R', 'O', 'F', 'V', '_', 'R',
          'E', 'S', 'T', 'A', 'R', 'T', '\0', '\0'}};
      std::array<char, 16> magic{};
      std::uint32_t version = 0;
      std::uint64_t count = 0;
      input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
      input.read(reinterpret_cast<char *>(&version), sizeof(version));
      input.read(reinterpret_cast<char *>(&count), sizeof(count));
      if (!input || magic != expected || version != 1U ||
          count != static_cast<std::uint64_t>(mesh_.global_cell_count)) {
        throw std::runtime_error("restart header/version/cell count is incompatible");
      }
      global_states.assign(static_cast<std::size_t>(count), zero_state());
      std::vector<bool> seen(static_cast<std::size_t>(count), false);
      for (std::uint64_t index = 0; index < count; ++index) {
        std::int64_t global_id = -1;
        Conservative state{};
        input.read(reinterpret_cast<char *>(&global_id), sizeof(global_id));
        input.read(reinterpret_cast<char *>(state.data()),
                   static_cast<std::streamsize>(sizeof(double) * state.size()));
        if (!input || global_id < 0 ||
            global_id >= static_cast<std::int64_t>(count) ||
            seen[static_cast<std::size_t>(global_id)] ||
            !physically_valid(state, config_.gas)) {
          throw std::runtime_error("restart contains invalid cell id/state data");
        }
        seen[static_cast<std::size_t>(global_id)] = true;
        global_states[static_cast<std::size_t>(global_id)] = state;
      }
      char trailing = 0;
      if (input.read(&trailing, 1)) {
        throw std::runtime_error("restart file has unexpected trailing data");
      }
    } catch (const std::exception &error) {
      read_ok = 0;
      read_error = error.what();
    }
  }
  mpi_check(MPI_Bcast(&read_ok, 1, MPI_INT, 0, communicator_),
            "MPI_Bcast(restart status)");
  std::uint64_t error_size = rank_ == 0 ? read_error.size() : 0U;
  mpi_check(MPI_Bcast(&error_size, 1, MPI_UINT64_T, 0, communicator_),
            "MPI_Bcast(restart error size)");
  if (rank_ != 0) {
    read_error.resize(static_cast<std::size_t>(error_size));
  }
  if (error_size > 0) {
    mpi_check(MPI_Bcast(read_error.data(), static_cast<int>(error_size), MPI_CHAR,
                        0, communicator_),
              "MPI_Bcast(restart error)");
  }
  if (read_ok == 0) {
    throw std::runtime_error(read_error);
  }

  const int local_count = mesh_.owned_cell_count;
  std::vector<int> counts(rank_ == 0 ? static_cast<std::size_t>(ranks_) : 0U);
  mpi_check(MPI_Gather(&local_count, 1, MPI_INT,
                       rank_ == 0 ? counts.data() : nullptr, 1, MPI_INT, 0,
                       communicator_),
            "MPI_Gather(restart owned counts)");
  std::vector<int> displacements;
  std::vector<std::int64_t> requested_ids;
  if (rank_ == 0) {
    displacements.resize(static_cast<std::size_t>(ranks_));
    int total = 0;
    for (int peer = 0; peer < ranks_; ++peer) {
      displacements[static_cast<std::size_t>(peer)] = total;
      total += counts[static_cast<std::size_t>(peer)];
    }
    if (total != mesh_.global_cell_count) {
      throw std::runtime_error("restart distribution owned counts do not cover mesh");
    }
    requested_ids.resize(static_cast<std::size_t>(total));
  }
  mpi_check(MPI_Gatherv(mesh_.global_cell_ids.data(), local_count, MPI_INT64_T,
                        rank_ == 0 ? requested_ids.data() : nullptr,
                        rank_ == 0 ? counts.data() : nullptr,
                        rank_ == 0 ? displacements.data() : nullptr, MPI_INT64_T,
                        0, communicator_),
            "MPI_Gatherv(restart global ids)");

  std::vector<double> send_values;
  std::vector<int> value_counts;
  std::vector<int> value_displacements;
  if (rank_ == 0) {
    send_values.resize(requested_ids.size() * 4U);
    for (std::size_t i = 0; i < requested_ids.size(); ++i) {
      const std::int64_t id = requested_ids[i];
      if (id < 0 || id >= static_cast<std::int64_t>(global_states.size())) {
        throw std::runtime_error("partition requests invalid restart global id");
      }
      std::copy(global_states[static_cast<std::size_t>(id)].begin(),
                global_states[static_cast<std::size_t>(id)].end(),
                send_values.begin() + static_cast<std::ptrdiff_t>(4U * i));
    }
    value_counts.resize(static_cast<std::size_t>(ranks_));
    value_displacements.resize(static_cast<std::size_t>(ranks_));
    for (int peer = 0; peer < ranks_; ++peer) {
      value_counts[static_cast<std::size_t>(peer)] =
          4 * counts[static_cast<std::size_t>(peer)];
      value_displacements[static_cast<std::size_t>(peer)] =
          4 * displacements[static_cast<std::size_t>(peer)];
    }
  }
  std::vector<double> local_values(static_cast<std::size_t>(4 * local_count));
  mpi_check(MPI_Scatterv(rank_ == 0 ? send_values.data() : nullptr,
                         rank_ == 0 ? value_counts.data() : nullptr,
                         rank_ == 0 ? value_displacements.data() : nullptr,
                         MPI_DOUBLE, local_values.data(), 4 * local_count,
                         MPI_DOUBLE, 0, communicator_),
            "MPI_Scatterv(restart states)");
  for (int cell = 0; cell < local_count; ++cell) {
    for (std::size_t component = 0; component < 4; ++component) {
      states_[static_cast<std::size_t>(cell)][component] =
          local_values[4U * static_cast<std::size_t>(cell) + component];
    }
  }
}

void FlowSolver::refresh_primitives() {
  for (std::size_t cell = 0; cell < states_.size(); ++cell) {
    if (!physically_valid(states_[cell], config_.gas)) {
      if (cell < static_cast<std::size_t>(mesh_.owned_cell_count)) {
        throw std::runtime_error("nonphysical conservative state in owned cell " +
                                 std::to_string(mesh_.global_cell_ids[cell]));
      }
      states_[cell] = sanitize_conservative(states_[cell], config_.gas);
    }
    primitives_[cell] = conservative_to_primitive(states_[cell], config_.gas);
  }
}

void FlowSolver::compute_gradients_and_limiters(bool update_limiters) {
  exchange_conservative_halo(mesh_, states_, communicator_);
  refresh_primitives();
  std::fill(gradients_.begin(), gradients_.end(), PrimitiveGradient{});

  for (int cell_id = 0; cell_id < mesh_.owned_cell_count; ++cell_id) {
    const Cell &cell = mesh_.cells[static_cast<std::size_t>(cell_id)];
    double a_xx = 0.0;
    double a_xy = 0.0;
    double a_yy = 0.0;
    std::array<Vec2, 4> right_hand{};
    for (int face_id : cell.faces) {
      const Face &face = mesh_.faces.at(static_cast<std::size_t>(face_id));
      const int neighbor = other_cell(face, cell_id);
      Vec2 neighbor_center{};
      Primitive neighbor_state{};
      if (neighbor >= 0) {
        neighbor_center = mesh_.cells.at(static_cast<std::size_t>(neighbor)).centroid;
        neighbor_state = primitives_.at(static_cast<std::size_t>(neighbor));
      } else {
        neighbor_center = 2.0 * face.center - cell.centroid;
        neighbor_state = virtual_neighbor(cell_id, face);
      }
      const Vec2 displacement = neighbor_center - cell.centroid;
      const double distance2 = std::max(dot(displacement, displacement), kTiny);
      const double weight = 1.0 / distance2;
      a_xx += weight * displacement.x * displacement.x;
      a_xy += weight * displacement.x * displacement.y;
      a_yy += weight * displacement.y * displacement.y;
      for (std::size_t component = 0; component < 4; ++component) {
        const double difference = primitive_component(neighbor_state, component) -
                                  primitive_component(primitives_[cell_id], component);
        right_hand[component].x += weight * displacement.x * difference;
        right_hand[component].y += weight * displacement.y * difference;
      }
    }
    const double trace = a_xx + a_yy;
    const double regularization = 1.0e-12 * std::max(trace, 1.0);
    a_xx += regularization;
    a_yy += regularization;
    const double determinant = a_xx * a_yy - a_xy * a_xy;
    if (determinant > 1.0e-24 * std::max(trace * trace, 1.0)) {
      for (std::size_t component = 0; component < 4; ++component) {
        gradients_[cell_id][component] =
            {(a_yy * right_hand[component].x - a_xy * right_hand[component].y) /
                 determinant,
             (-a_xy * right_hand[component].x + a_xx * right_hand[component].y) /
                 determinant};
      }
    }
  }
  exchange_primitive_gradient_halo(mesh_, gradients_, communicator_);

  if (!update_limiters) {
    // Freeze the piecewise Barth--Jespersen switches within each matrix-free
    // linear solve. Re-evaluating min/max branches for every finite-difference
    // perturbation makes Jv discontinuous and undermines Krylov convergence.
    return;
  }

  for (int cell_id = 0; cell_id < mesh_.owned_cell_count; ++cell_id) {
    const Cell &cell = mesh_.cells[static_cast<std::size_t>(cell_id)];
    Primitive minimum = primitives_[cell_id];
    Primitive maximum = primitives_[cell_id];
    std::vector<Vec2> offsets;
    offsets.reserve(cell.faces.size());
    for (int face_id : cell.faces) {
      const Face &face = mesh_.faces.at(static_cast<std::size_t>(face_id));
      update_extrema(minimum, maximum, virtual_neighbor(cell_id, face));
      offsets.push_back(face.center - cell.centroid);
    }
    limiters_[cell_id] = barth_jespersen_limiters(
        primitives_[cell_id], gradients_[cell_id], minimum, maximum, offsets);
  }
  exchange_limiter_halo(mesh_, limiters_, communicator_);
}

Primitive FlowSolver::face_reconstruction(int local_cell,
                                          const Face &face) const {
  if (!reconstruction_active_) {
    return primitives_.at(static_cast<std::size_t>(local_cell));
  }
  const Cell &cell = mesh_.cells.at(static_cast<std::size_t>(local_cell));
  PrimitiveLimiter blended_limiter =
      limiters_.at(static_cast<std::size_t>(local_cell));
  for (double &component : blended_limiter) {
    component *= reconstruction_blend_;
  }
  const Primitive candidate = reconstruct_primitive(
      primitives_.at(static_cast<std::size_t>(local_cell)),
      gradients_.at(static_cast<std::size_t>(local_cell)), face.center - cell.centroid,
      blended_limiter);
  return positivity_limited_reconstruction(
      primitives_.at(static_cast<std::size_t>(local_cell)), candidate, config_.gas);
}

PrimitiveGradient FlowSolver::viscous_face_gradient(const Face &face) const {
  if (face.left_cell < 0) {
    throw std::logic_error("viscous face has no left cell");
  }
  const int left_cell = face.left_cell;
  const Primitive &left_center =
      primitives_.at(static_cast<std::size_t>(left_cell));
  Primitive right_center{};
  Vec2 displacement{};
  const PrimitiveGradient &left_gradient =
      gradients_.at(static_cast<std::size_t>(left_cell));
  const PrimitiveGradient *right_gradient = &left_gradient;
  if (face.right_cell >= 0) {
    right_center = primitives_.at(static_cast<std::size_t>(face.right_cell));
    displacement = mesh_.cells.at(static_cast<std::size_t>(face.right_cell)).centroid -
                   mesh_.cells.at(static_cast<std::size_t>(left_cell)).centroid;
    right_gradient =
        &gradients_.at(static_cast<std::size_t>(face.right_cell));
  } else {
    // This ghost is constructed from the cell-centred state and represents the
    // mirrored endpoint used by the boundary finite difference.  The
    // reconstructed face state remains reserved for inviscid fluxes and the
    // constitutive face average below.
    right_center = virtual_neighbor(left_cell, face);
    displacement = 2.0 * (face.center -
                          mesh_.cells.at(static_cast<std::size_t>(left_cell)).centroid);
  }
  return corrected_primitive_face_gradient(
      left_gradient, *right_gradient, left_center, right_center, displacement,
      face.normal, kTiny);
}

FlowSolver::SpatialAssembly FlowSolver::assemble_spatial(std::int64_t step,
                                                         double physical_time,
                                                         bool freeze_limiters) {
  // Robust first-order continuation establishes the nonlinear basin, after
  // which all final production iterations use the required second-order
  // reconstruction. Viscous gradients remain active throughout.
  if (reconstruction_blend_override_ >= 0.0) {
    reconstruction_blend_ =
        std::clamp(reconstruction_blend_override_, 0.0, 1.0);
  } else if (config_.run_control.type == RunType::transient) {
    reconstruction_blend_ = 1.0;
  } else {
    constexpr double blend_steps = 500.0;
    reconstruction_blend_ = std::clamp(
        (static_cast<double>(step) -
         static_cast<double>(config_.run_control.pseudo_cfl_ramp_steps)) /
            blend_steps,
        0.0, 1.0);
  }
  reconstruction_active_ = reconstruction_blend_ > 0.0;
  compute_gradients_and_limiters(!freeze_limiters);
  SpatialAssembly assembly;
  assembly.residual.assign(mesh_.cells.size(), zero_state());
  assembly.spectral_sum.assign(mesh_.cells.size(), 0.0);
  assembly.face_coupling.assign(mesh_.faces.size(), 0.0);
  assembly.force.step = step;
  assembly.force.physical_time = physical_time;

  std::array<double, 5> local_force{}; // pressure x/y, viscous x/y, total moment
  for (std::size_t face_id = 0; face_id < mesh_.faces.size(); ++face_id) {
    const Face &face = mesh_.faces[face_id];
    if (face.left_cell < 0) {
      throw std::runtime_error("local face has no left cell");
    }
    const Primitive left = face_reconstruction(face.left_cell, face);
    Primitive right{};
    if (face.right_cell >= 0) {
      right = face_reconstruction(face.right_cell, face);
    } else {
      right = boundary_ghost_primitive(left, freestream_, face.normal,
                                       boundary_type(face), config_.gas);
    }
    const Conservative left_state = primitive_to_conservative(left, config_.gas);
    const Conservative right_state = primitive_to_conservative(right, config_.gas);
    Conservative inviscid{};
    const BoundaryType face_boundary = boundary_type(face);
    if (face.right_cell < 0 &&
        (face_boundary == BoundaryType::slip_wall ||
         face_boundary == BoundaryType::no_slip_adiabatic_wall)) {
      inviscid = impermeable_wall_flux(left, face.normal, config_.gas);
    } else {
      inviscid = rusanov_flux(left_state, right_state, face.normal, config_.gas);
      if (config_.run_control.type == RunType::transient) {
        const double scale =
            config_.run_control.rusanov_dissipation_scale.value_or(1.0);
        if (std::abs(scale - 1.0) > 1.0e-14) {
          const Conservative central =
              0.5 * (euler_normal_flux(left_state, left, face.normal) +
                     euler_normal_flux(right_state, right, face.normal));
          inviscid = central + scale * (inviscid - central);
        }
      }
    }

    Conservative viscous{};
    PrimitiveGradient face_gradient{};
    if (config_.physics.mode == PhysicsMode::laminar) {
      face_gradient = viscous_face_gradient(face);
      Primitive face_state{0.5 * (left.rho + right.rho),
                           0.5 * (left.u + right.u),
                           0.5 * (left.v + right.v),
                           0.5 * (left.p + right.p)};
      if (face.right_cell < 0 &&
          boundary_type(face) == BoundaryType::no_slip_adiabatic_wall) {
        face_state = boundary_surface_primitive(left, freestream_, face.normal,
                                                boundary_type(face), config_.gas);
      }
      viscous = viscous_normal_flux(face_state, face_gradient, face.normal,
                                    dynamic_viscosity_, config_.gas);
    }
    const Conservative flux = (inviscid - viscous) * face.length;
    if (mesh_.is_owned_cell(face.left_cell)) {
      assembly.residual[static_cast<std::size_t>(face.left_cell)] += flux;
    }
    if (face.right_cell >= 0 && mesh_.is_owned_cell(face.right_cell)) {
      assembly.residual[static_cast<std::size_t>(face.right_cell)] -= flux;
    }

    const double lambda_convective =
        face.length *
        std::max(std::abs(normal_velocity(left, face.normal)) +
                     sound_speed(left, config_.gas),
                 std::abs(normal_velocity(right, face.normal)) +
                     sound_speed(right, config_.gas));
    double lambda_viscous = 0.0;
    if (config_.physics.mode == PhysicsMode::laminar) {
      Vec2 center_delta{};
      if (face.right_cell >= 0) {
        center_delta = mesh_.cells[static_cast<std::size_t>(face.right_cell)].centroid -
                       mesh_.cells[static_cast<std::size_t>(face.left_cell)].centroid;
      } else {
        center_delta = 2.0 *
                       (face.center -
                        mesh_.cells[static_cast<std::size_t>(face.left_cell)].centroid);
      }
      lambda_viscous = viscous_face_spectral_radius(
          left, face.length, std::max(std::abs(dot(center_delta, face.normal)), kTiny),
          dynamic_viscosity_, config_.gas);
    }
    const double strength = lambda_convective + lambda_viscous;
    assembly.face_coupling[face_id] = 0.5 * strength;
    if (mesh_.is_owned_cell(face.left_cell)) {
      assembly.spectral_sum[static_cast<std::size_t>(face.left_cell)] += strength;
    }
    if (face.right_cell >= 0 && mesh_.is_owned_cell(face.right_cell)) {
      assembly.spectral_sum[static_cast<std::size_t>(face.right_cell)] += strength;
    }

    const BoundaryType type = face_boundary;
    if (face.right_cell < 0 &&
        (type == BoundaryType::slip_wall ||
         type == BoundaryType::no_slip_adiabatic_wall)) {
      const Primitive wall = boundary_surface_primitive(
          left, freestream_, face.normal, type, config_.gas);
      const Vec2 pressure_force =
          (wall.p - freestream_.p) * face.length * face.normal;
      Vec2 viscous_force{};
      if (config_.physics.mode == PhysicsMode::laminar) {
        // viscous_normal_flux is traction exerted on the fluid; the body sees
        // the equal and opposite traction.
        viscous_force = {-viscous[1] * face.length,
                         -viscous[2] * face.length};
      }
      local_force[0] += pressure_force.x;
      local_force[1] += pressure_force.y;
      local_force[2] += viscous_force.x;
      local_force[3] += viscous_force.y;
      const Vec2 radius = face.center - config_.reference.moment_center;
      const Vec2 total_force = pressure_force + viscous_force;
      local_force[4] += cross(radius, total_force);
    }
  }

  std::array<double, 5> global_force{};
  mpi_check(MPI_Allreduce(local_force.data(), global_force.data(),
                          static_cast<int>(local_force.size()), MPI_DOUBLE, MPI_SUM,
                          communicator_),
            "MPI_Allreduce(forces)");
  const double denominator = dynamic_pressure_ * config_.reference.area;
  const Vec2 pressure{global_force[0], global_force[1]};
  const Vec2 viscous{global_force[2], global_force[3]};
  assembly.force.pressure_drag = dot(pressure, drag_direction_) / denominator;
  assembly.force.viscous_drag = dot(viscous, drag_direction_) / denominator;
  assembly.force.pressure_lift = dot(pressure, lift_direction_) / denominator;
  assembly.force.viscous_lift = dot(viscous, lift_direction_) / denominator;
  assembly.force.cd = assembly.force.pressure_drag + assembly.force.viscous_drag;
  assembly.force.cl = assembly.force.pressure_lift + assembly.force.viscous_lift;
  assembly.force.cmz = global_force[4] /
                       (denominator * config_.reference.length);
  return assembly;
}

FlowSolver::ResidualNorm FlowSolver::residual_norm(
    const std::vector<Conservative> &residual) const {
  if (residual.size() < static_cast<std::size_t>(mesh_.owned_cell_count)) {
    throw std::invalid_argument("residual vector does not cover owned cells");
  }
  const std::array<double, 4> scale{{
      std::max(std::abs(freestream_state_[0]), kTiny),
      std::max(freestream_.rho * config_.freestream.velocity_magnitude, kTiny),
      std::max(freestream_.rho * config_.freestream.velocity_magnitude, kTiny),
      std::max(std::abs(freestream_state_[3]), kTiny)}};
  std::array<double, 4> local_squares{};
  double local_maximum = 0.0;
  for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    const double volume = mesh_.cells[static_cast<std::size_t>(cell)].area;
    for (std::size_t component = 0; component < 4; ++component) {
      const double value = residual[static_cast<std::size_t>(cell)][component] /
                           (volume * scale[component]);
      local_squares[component] += value * value;
      local_maximum = std::max(local_maximum, std::abs(value));
    }
  }
  std::array<double, 4> global_squares{};
  double global_maximum = 0.0;
  mpi_check(MPI_Allreduce(local_squares.data(), global_squares.data(), 4,
                          MPI_DOUBLE, MPI_SUM, communicator_),
            "MPI_Allreduce(residual L2)");
  mpi_check(MPI_Allreduce(&local_maximum, &global_maximum, 1, MPI_DOUBLE,
                          MPI_MAX, communicator_),
            "MPI_Allreduce(residual Linf)");
  ResidualNorm result;
  const double cells = static_cast<double>(mesh_.global_cell_count);
  double total = 0.0;
  for (std::size_t component = 0; component < 4; ++component) {
    result.component_l2[component] = std::sqrt(global_squares[component] / cells);
    total += global_squares[component];
  }
  result.total_l2 = std::sqrt(total / (4.0 * cells));
  result.linf = global_maximum;
  return result;
}

FlowSolver::LinearResult FlowSolver::solve_linearized(
    const std::vector<Conservative> &residual,
    const SpatialAssembly &spatial,
    const std::vector<double> &physical_diagonal, double cfl,
    int minimum_sweeps, int maximum_sweeps, double target_ratio) {
  if (physical_diagonal.size() != mesh_.cells.size() || minimum_sweeps < 1 ||
      maximum_sweeps < minimum_sweeps) {
    throw std::invalid_argument("invalid block-Jacobi solve controls");
  }
  LinearResult result;
  result.increment.assign(mesh_.cells.size(), zero_state());
  std::vector<Conservative> next = result.increment;

  double local_rhs_square = 0.0;
  for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    for (double value : residual[static_cast<std::size_t>(cell)]) {
      local_rhs_square += value * value;
    }
  }
  double global_rhs_square = 0.0;
  mpi_check(MPI_Allreduce(&local_rhs_square, &global_rhs_square, 1, MPI_DOUBLE,
                          MPI_SUM, communicator_),
            "MPI_Allreduce(linear rhs)");
  const double rhs_norm = std::sqrt(std::max(global_rhs_square, kTiny));

  for (int sweep = 1; sweep <= maximum_sweeps; ++sweep) {
    exchange_conservative_increment_halo(mesh_, result.increment, communicator_);
    for (int cell_id = 0; cell_id < mesh_.owned_cell_count; ++cell_id) {
      const Cell &cell = mesh_.cells[static_cast<std::size_t>(cell_id)];
      Conservative neighbor_sum{};
      for (int face_id : cell.faces) {
        const Face &face = mesh_.faces.at(static_cast<std::size_t>(face_id));
        const int neighbor = other_cell(face, cell_id);
        if (neighbor >= 0) {
          neighbor_sum += spatial.face_coupling[static_cast<std::size_t>(face_id)] *
                          result.increment[static_cast<std::size_t>(neighbor)];
        }
      }
      const double pseudo_diagonal =
          spatial.spectral_sum[static_cast<std::size_t>(cell_id)] /
          std::max(cfl, 1.0e-8);
      const double diagonal =
          pseudo_diagonal + physical_diagonal[static_cast<std::size_t>(cell_id)] +
          spatial.spectral_sum[static_cast<std::size_t>(cell_id)] + kTiny;
      const Conservative jacobi =
          (-1.0 * residual[static_cast<std::size_t>(cell_id)] + neighbor_sum) /
          diagonal;
      next[static_cast<std::size_t>(cell_id)] =
          0.85 * jacobi + 0.15 * result.increment[static_cast<std::size_t>(cell_id)];
    }
    for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
      result.increment[static_cast<std::size_t>(cell)] =
          next[static_cast<std::size_t>(cell)];
    }
    exchange_conservative_increment_halo(mesh_, result.increment, communicator_);

    double local_defect_square = 0.0;
    for (int cell_id = 0; cell_id < mesh_.owned_cell_count; ++cell_id) {
      const Cell &cell = mesh_.cells[static_cast<std::size_t>(cell_id)];
      Conservative neighbor_sum{};
      for (int face_id : cell.faces) {
        const Face &face = mesh_.faces.at(static_cast<std::size_t>(face_id));
        const int neighbor = other_cell(face, cell_id);
        if (neighbor >= 0) {
          neighbor_sum += spatial.face_coupling[static_cast<std::size_t>(face_id)] *
                          result.increment[static_cast<std::size_t>(neighbor)];
        }
      }
      const double diagonal =
          spatial.spectral_sum[static_cast<std::size_t>(cell_id)] /
              std::max(cfl, 1.0e-8) +
          physical_diagonal[static_cast<std::size_t>(cell_id)] +
          spatial.spectral_sum[static_cast<std::size_t>(cell_id)] + kTiny;
      const Conservative defect =
          -1.0 * residual[static_cast<std::size_t>(cell_id)] + neighbor_sum -
          diagonal * result.increment[static_cast<std::size_t>(cell_id)];
      for (double value : defect) {
        local_defect_square += value * value;
      }
    }
    double global_defect_square = 0.0;
    mpi_check(MPI_Allreduce(&local_defect_square, &global_defect_square, 1,
                            MPI_DOUBLE, MPI_SUM, communicator_),
              "MPI_Allreduce(linear defect)");
    result.sweeps = sweep;
    result.final_ratio = std::sqrt(global_defect_square) / rhs_norm;
    if (sweep >= minimum_sweeps && result.final_ratio <= target_ratio) {
      break;
    }
  }
  return result;
}

FlowSolver::LinearResult FlowSolver::solve_newton_krylov(
    const SpatialAssembly &spatial,
    const std::vector<Conservative> &residual,
    const std::vector<double> &physical_diagonal, double cfl,
    std::int64_t step, double physical_time, double pseudo_mass_scale,
    int minimum_iterations, int maximum_iterations, double target_ratio) {
  if (minimum_iterations < 1 || maximum_iterations < minimum_iterations) {
    throw std::invalid_argument("invalid GMRES iteration controls");
  }
  if (residual.size() != mesh_.cells.size() ||
      physical_diagonal.size() != mesh_.cells.size()) {
    throw std::invalid_argument("GMRES fields do not cover the local mesh");
  }
  if (!std::isfinite(pseudo_mass_scale) || pseudo_mass_scale < 0.0) {
    throw std::invalid_argument("invalid GMRES pseudo-mass scale");
  }
  const int iterations = maximum_iterations;
  using Field = std::vector<Conservative>;
  const auto make_field = [&]() {
    return Field(mesh_.cells.size(), zero_state());
  };
  const auto global_dot = [&](const Field &left, const Field &right) {
    double local = 0.0;
    for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
      for (std::size_t component = 0; component < 4; ++component) {
        local += left[static_cast<std::size_t>(cell)][component] *
                 right[static_cast<std::size_t>(cell)][component];
      }
    }
    double global = 0.0;
    mpi_check(MPI_Allreduce(&local, &global, 1, MPI_DOUBLE, MPI_SUM,
                            communicator_),
              "MPI_Allreduce(GMRES dot)");
    return global;
  };
  const auto axpy = [&](Field &destination, double coefficient,
                        const Field &source) {
    for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
      for (std::size_t component = 0; component < 4; ++component) {
        destination[static_cast<std::size_t>(cell)][component] +=
            coefficient * source[static_cast<std::size_t>(cell)][component];
      }
    }
  };
  const auto scale = [&](Field &field, double coefficient) {
    for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
      field[static_cast<std::size_t>(cell)] *= coefficient;
    }
  };

  std::vector<Conservative> base_owned(
      states_.begin(), states_.begin() + mesh_.owned_cell_count);
  const std::array<double, 4> variable_scale{{
      std::max(std::abs(freestream_state_[0]), 1.0e-12),
      std::max(freestream_.rho * config_.freestream.velocity_magnitude, 1.0e-12),
      std::max(freestream_.rho * config_.freestream.velocity_magnitude, 1.0e-12),
      std::max(std::abs(freestream_state_[3]), 1.0e-12)}};
  std::vector<double> preconditioner(mesh_.cells.size(), 1.0);
  std::vector<double> pseudo_mass(mesh_.cells.size(), 0.0);
  for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    pseudo_mass[static_cast<std::size_t>(cell)] =
        pseudo_mass_scale * spatial.spectral_sum[static_cast<std::size_t>(cell)] /
        std::max(cfl, 1.0e-8);
    preconditioner[static_cast<std::size_t>(cell)] =
        pseudo_mass[static_cast<std::size_t>(cell)] +
        physical_diagonal[static_cast<std::size_t>(cell)] +
        spatial.spectral_sum[static_cast<std::size_t>(cell)] + kTiny;
  }

  const auto apply_operator = [&](const Field &direction) {
    Field physical_direction = make_field();
    for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
      for (std::size_t component = 0; component < 4; ++component) {
        physical_direction[static_cast<std::size_t>(cell)][component] =
            variable_scale[component] *
            direction[static_cast<std::size_t>(cell)][component];
      }
    }
    const double direction_norm = std::sqrt(
        std::max(global_dot(physical_direction, physical_direction), kTiny));
    double local_state_square = 0.0;
    for (const Conservative &state : base_owned) {
      for (double value : state) {
        local_state_square += value * value;
      }
    }
    double global_state_square = 0.0;
    mpi_check(MPI_Allreduce(&local_state_square, &global_state_square, 1,
                            MPI_DOUBLE, MPI_SUM, communicator_),
              "MPI_Allreduce(GMRES state norm)");
    double epsilon = 1.0e-7 *
                     (1.0 + std::sqrt(global_state_square)) /
                     direction_norm;
    for (int attempt = 0; attempt < 12; ++attempt) {
      int local_valid = 1;
      for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
        const Conservative candidate =
            base_owned[static_cast<std::size_t>(cell)] +
            epsilon * physical_direction[static_cast<std::size_t>(cell)];
        if (!physically_valid(candidate, config_.gas)) {
          local_valid = 0;
          break;
        }
      }
      int globally_valid = 0;
      mpi_check(MPI_Allreduce(&local_valid, &globally_valid, 1, MPI_INT,
                              MPI_MIN, communicator_),
                "MPI_Allreduce(GMRES perturbation validity)");
      if (globally_valid != 0) {
        break;
      }
      epsilon *= 0.25;
    }
    for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
      states_[static_cast<std::size_t>(cell)] =
          base_owned[static_cast<std::size_t>(cell)] +
          epsilon * physical_direction[static_cast<std::size_t>(cell)];
    }
    SpatialAssembly perturbed =
        assemble_spatial(step, physical_time, true);
    std::copy(base_owned.begin(), base_owned.end(), states_.begin());
    Field result = make_field();
    for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
      for (std::size_t component = 0; component < 4; ++component) {
        result[static_cast<std::size_t>(cell)][component] =
            ((perturbed.residual[static_cast<std::size_t>(cell)][component] -
              spatial.residual[static_cast<std::size_t>(cell)][component]) /
                 epsilon +
             pseudo_mass[static_cast<std::size_t>(cell)] *
                 physical_direction[static_cast<std::size_t>(cell)][component] +
             physical_diagonal[static_cast<std::size_t>(cell)] *
                 physical_direction[static_cast<std::size_t>(cell)][component]) /
            (preconditioner[static_cast<std::size_t>(cell)] *
             variable_scale[component]);
      }
    }
    return result;
  };

  Field right_hand = make_field();
  for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    right_hand[static_cast<std::size_t>(cell)] =
        (-1.0 / preconditioner[static_cast<std::size_t>(cell)]) *
        residual[static_cast<std::size_t>(cell)];
    for (std::size_t component = 0; component < 4; ++component) {
      right_hand[static_cast<std::size_t>(cell)][component] /=
          variable_scale[component];
    }
  }
  const double beta = std::sqrt(std::max(global_dot(right_hand, right_hand), kTiny));
  std::vector<Field> basis;
  basis.reserve(static_cast<std::size_t>(iterations + 1));
  basis.push_back(right_hand);
  scale(basis[0], 1.0 / beta);
  std::vector<double> hessenberg(
      static_cast<std::size_t>((iterations + 1) * iterations), 0.0);
  const auto h = [&](int row, int column) -> double & {
    return hessenberg[static_cast<std::size_t>(
        row * iterations + column)];
  };
  std::vector<double> cosine(static_cast<std::size_t>(iterations), 0.0);
  std::vector<double> sine(static_cast<std::size_t>(iterations), 0.0);
  std::vector<double> transformed_rhs(static_cast<std::size_t>(iterations + 1),
                                      0.0);
  transformed_rhs[0] = beta;
  int used = 0;
  double relative_residual = 1.0;
  for (int column = 0; column < iterations; ++column) {
    Field work = apply_operator(basis[static_cast<std::size_t>(column)]);
    for (int row = 0; row <= column; ++row) {
      h(row, column) = global_dot(work, basis[static_cast<std::size_t>(row)]);
      axpy(work, -h(row, column), basis[static_cast<std::size_t>(row)]);
    }
    h(column + 1, column) =
        std::sqrt(std::max(global_dot(work, work), 0.0));
    if (h(column + 1, column) > kTiny) {
      scale(work, 1.0 / h(column + 1, column));
    }
    basis.push_back(std::move(work));
    for (int row = 0; row < column; ++row) {
      const double upper = cosine[static_cast<std::size_t>(row)] * h(row, column) +
                           sine[static_cast<std::size_t>(row)] * h(row + 1, column);
      h(row + 1, column) =
          -sine[static_cast<std::size_t>(row)] * h(row, column) +
          cosine[static_cast<std::size_t>(row)] * h(row + 1, column);
      h(row, column) = upper;
    }
    const double rotation = std::hypot(h(column, column),
                                       h(column + 1, column));
    cosine[static_cast<std::size_t>(column)] =
        rotation > kTiny ? h(column, column) / rotation : 1.0;
    sine[static_cast<std::size_t>(column)] =
        rotation > kTiny ? h(column + 1, column) / rotation : 0.0;
    h(column, column) = rotation;
    h(column + 1, column) = 0.0;
    transformed_rhs[static_cast<std::size_t>(column + 1)] =
        -sine[static_cast<std::size_t>(column)] *
        transformed_rhs[static_cast<std::size_t>(column)];
    transformed_rhs[static_cast<std::size_t>(column)] *=
        cosine[static_cast<std::size_t>(column)];
    used = column + 1;
    relative_residual =
        std::abs(transformed_rhs[static_cast<std::size_t>(column + 1)]) / beta;
    if (used >= minimum_iterations && relative_residual <= target_ratio) {
      break;
    }
  }
  std::vector<double> coefficients(static_cast<std::size_t>(used), 0.0);
  for (int row = used - 1; row >= 0; --row) {
    double value = transformed_rhs[static_cast<std::size_t>(row)];
    for (int column = row + 1; column < used; ++column) {
      value -= h(row, column) * coefficients[static_cast<std::size_t>(column)];
    }
    coefficients[static_cast<std::size_t>(row)] =
        value / (std::abs(h(row, row)) > kTiny ? h(row, row) : kTiny);
  }
  LinearResult result;
  result.increment = make_field();
  for (int column = 0; column < used; ++column) {
    axpy(result.increment, coefficients[static_cast<std::size_t>(column)],
         basis[static_cast<std::size_t>(column)]);
  }
  for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    for (std::size_t component = 0; component < 4; ++component) {
      result.increment[static_cast<std::size_t>(cell)][component] *=
          variable_scale[component];
    }
  }
  result.sweeps = used;
  result.final_ratio = relative_residual;
  std::copy(base_owned.begin(), base_owned.end(), states_.begin());
  return result;
}

void FlowSolver::apply_increment(const std::vector<Conservative> &increment,
                                 double relaxation,
                                 double maximum_relative_change) {
  if (increment.size() != states_.size() || !(relaxation > 0.0) ||
      relaxation > 1.0 || !(maximum_relative_change > 0.0) ||
      maximum_relative_change > 1.0) {
    throw std::invalid_argument("invalid conservative increment");
  }
  for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    const Conservative center = states_[static_cast<std::size_t>(cell)];
    const Primitive base = conservative_to_primitive(center, config_.gas);
    double theta = relaxation;
    Conservative accepted = center;
    for (int trial = 0; trial < 24; ++trial) {
      const Conservative candidate =
          center + theta * increment[static_cast<std::size_t>(cell)];
      if (physically_valid(candidate, config_.gas)) {
        const Primitive q = conservative_to_primitive(candidate, config_.gas);
        const double acoustic_scale =
            sound_speed(base, config_.gas) +
            std::sqrt(base.u * base.u + base.v * base.v);
        const bool bounded_change =
            std::abs(q.rho - base.rho) <= maximum_relative_change * base.rho &&
            std::abs(q.p - base.p) <= maximum_relative_change * base.p &&
            std::hypot(q.u - base.u, q.v - base.v) <=
                maximum_relative_change * std::max(acoustic_scale, 1.0e-8);
        if (bounded_change) {
          accepted = candidate;
          break;
        }
      }
      theta *= 0.5;
    }
    states_[static_cast<std::size_t>(cell)] = accepted;
  }
}

double FlowSolver::ramped_cfl(std::int64_t step) const {
  const double initial = config_.run_control.cfl_initial;
  const double maximum = config_.run_control.cfl_max;
  if (config_.run_control.pseudo_cfl_ramp_steps <= 0 || maximum == initial) {
    return maximum;
  }
  const double fraction = std::clamp(
      static_cast<double>(step - 1) /
          static_cast<double>(config_.run_control.pseudo_cfl_ramp_steps),
      0.0, 1.0);
  return initial * std::pow(maximum / initial, fraction);
}

std::vector<SurfaceRecord> FlowSolver::build_surface_rows() const {
  std::vector<SurfaceRecord> rows;
  for (const Face &face : mesh_.faces) {
    if (face.right_cell >= 0 || !mesh_.is_owned_cell(face.left_cell)) {
      continue;
    }
    const BoundaryType type = boundary_type(face);
    if (type != BoundaryType::slip_wall &&
        type != BoundaryType::no_slip_adiabatic_wall) {
      continue;
    }
    const Primitive interior = face_reconstruction(face.left_cell, face);
    const Primitive wall = boundary_surface_primitive(
        interior, freestream_, face.normal, type, config_.gas);
    Vec2 body_viscous_traction{};
    if (config_.physics.mode == PhysicsMode::laminar) {
      const PrimitiveGradient gradient =
          viscous_face_gradient(face);
      const Conservative flux = viscous_normal_flux(
          wall, gradient, face.normal, dynamic_viscosity_, config_.gas);
      body_viscous_traction = {-flux[1], -flux[2]};
    }
    const Vec2 tangent{-face.normal.y, face.normal.x};
    SurfaceRecord row;
    row.x = face.center.x;
    row.y = face.center.y;
    row.nx = face.normal.x;
    row.ny = face.normal.y;
    row.pressure = wall.p;
    row.cp = (wall.p - freestream_.p) / dynamic_pressure_;
    row.cf = dot(body_viscous_traction, tangent) / dynamic_pressure_;
    row.rho = wall.rho;
    row.u = wall.u;
    row.v = wall.v;
    row.mach = std::sqrt(wall.u * wall.u + wall.v * wall.v) /
               sound_speed(wall, config_.gas);
    row.tag = face.boundary_family;
    rows.push_back(std::move(row));
  }
  return rows;
}

RunSummary FlowSolver::run_steady(OutputWriter &output,
                                  const std::string &command,
                                  double start_seconds) {
  const std::int64_t maximum_steps = *config_.run_control.max_steps;
  const double target_orders = *config_.run_control.residual_reduction_target;
  double initial_residual = -1.0;
  double final_orders = 0.0;
  bool converged = false;
  bool plateau = false;
  std::int64_t final_step = 0;
  std::deque<double> drag_history;
  std::deque<double> lift_history;
  std::deque<double> residual_tail;
  int observed_min = std::numeric_limits<int>::max();
  int observed_max = 0;
  std::int64_t observed_sum = 0;
  std::int64_t observed_count = 0;
  std::int64_t inner_target_misses = 0;
  double last_linear_ratio = 1.0;
  const std::vector<double> no_physical_diagonal(mesh_.cells.size(), 0.0);
  // The scheduled CFL remains the case-control value. This recovery path is
  // deliberately dormant unless the fully second-order nonlinear solve has
  // demonstrably left its basin at that scheduled value.
  const double recovery_cfl_cap =
      bounded_steady_recovery_cfl_cap(config_.run_control.cfl_max);
  constexpr double recovery_relaxation_cap = 0.15;
  constexpr double recovery_residual_rebound_factor = 3.0;
  constexpr int recovery_failure_score_threshold = 4;
  constexpr std::size_t history_window = 200;
  const std::int64_t second_order_start =
      config_.run_control.pseudo_cfl_ramp_steps + 500;
  bool recovery_active = false;
  std::int64_t recovery_activation_step = 0;
  int full_order_failure_score = 0;
  double best_full_order_residual = std::numeric_limits<double>::infinity();

  output.log("Starting steady implicit local-pseudo-time solve with CFL-ramp "
             "first-order continuation followed by blended least-squares "
             "reconstruction and Barth-Jespersen limiting.");
  for (std::int64_t step = 1; step <= maximum_steps; ++step) {
    const double scheduled_cfl = ramped_cfl(step);
    // The effective value is what enters both the Newton operator and the
    // residual record. The configured schedule itself is not changed.
    const double cfl = recovery_active
                           ? std::min(scheduled_cfl, recovery_cfl_cap)
                           : scheduled_cfl;
    SpatialAssembly spatial = assemble_spatial(step, 0.0);
    const ResidualNorm norm_value = residual_norm(spatial.residual);
    if (initial_residual < 0.0) {
      initial_residual = std::max(norm_value.total_l2, kTiny);
    }
    final_orders = std::log10(initial_residual /
                              std::max(norm_value.total_l2, kTiny));
    const bool full_second_order =
        reconstruction_blend_ >= 1.0 && step >= second_order_start;
    bool residual_rebound = false;
    if (full_second_order) {
      if (std::isfinite(best_full_order_residual)) {
        residual_rebound =
            norm_value.total_l2 > recovery_residual_rebound_factor *
                                      best_full_order_residual;
      }
      best_full_order_residual =
          std::min(best_full_order_residual, norm_value.total_l2);
    }
    ResidualRecord residual_record;
    residual_record.step = step;
    residual_record.physical_time = 0.0;
    residual_record.inner_iter = 0;
    residual_record.cfl = cfl;
    residual_record.dt = 0.0;
    residual_record.rho = norm_value.component_l2[0];
    residual_record.rhou = norm_value.component_l2[1];
    residual_record.rhov = norm_value.component_l2[2];
    residual_record.rhoE = norm_value.component_l2[3];
    residual_record.residual_l2 = norm_value.total_l2;
    residual_record.residual_linf = norm_value.linf;
    if (step % config_.outputs.write_residuals_every == 0 || step == 1 ||
        step == maximum_steps) {
      output.append_residual(residual_record);
    }
    if (step % config_.outputs.write_forces_every == 0 || step == 1 ||
        step == maximum_steps) {
      output.append_force(spatial.force);
    }
    final_step = step;

    drag_history.push_back(spatial.force.cd);
    lift_history.push_back(spatial.force.cl);
    residual_tail.push_back(norm_value.total_l2);
    if (drag_history.size() > history_window) {
      drag_history.pop_front();
      lift_history.pop_front();
      residual_tail.pop_front();
    }
    const bool stable_forces = drag_history.size() == history_window &&
                               relative_range(drag_history) < 2.0e-4 &&
                               relative_range(lift_history) < 2.0e-4;
    if (reconstruction_blend_ >= 1.0 && step >= second_order_start + 200 &&
        final_orders >= target_orders && stable_forces) {
      converged = true;
      break;
    }
    if (step % 100 == 0 || step == 1) {
      std::ostringstream message;
      message << "steady step=" << step << " CFL=" << std::setprecision(6)
              << cfl << " residual=" << std::scientific
              << norm_value.total_l2 << " reduction=" << std::fixed
              << std::setprecision(3) << final_orders << " orders Cd="
              << spatial.force.cd << " Cl=" << spatial.force.cl;
      output.log(message.str());
      if (rank_ == 0) {
        std::cout << message.str() << '\n' << std::flush;
      }
    }
    if (step == maximum_steps) {
      // A terminal plateau is accepted only with substantial residual
      // reduction, a flat residual envelope, and stable forces.  It is stated
      // explicitly in run_status rather than mislabeled as target convergence.
      bool residual_flat = false;
      if (residual_tail.size() == history_window) {
        const auto [minimum, maximum] =
            std::minmax_element(residual_tail.begin(), residual_tail.end());
        residual_flat = *maximum / std::max(*minimum, kTiny) < 1.08;
      }
      plateau = final_orders >= std::max(2.0, target_orders - 0.5) &&
                stable_forces && residual_flat;
      break;
    }

    // Do not spend an ill-conditioned Krylov solve driving the temporary
    // first-order continuation state toward roundoff. Once it is already well
    // beyond the requested reduction, hold it while the prescribed CFL ramp
    // completes; nonlinear corrections resume as soon as second order blends
    // in on the next iteration.
    if (reconstruction_blend_ == 0.0 &&
        final_orders >= target_orders + 2.0) {
      continue;
    }

    const LinearResult linear = solve_newton_krylov(
        spatial, spatial.residual, no_physical_diagonal, cfl, step, 0.0, 1.0,
        config_.run_control.min_inner_iterations,
        config_.run_control.max_inner_iterations,
        config_.run_control.inner_residual_reduction_target);
    observed_min = std::min(observed_min, linear.sweeps);
    observed_max = std::max(observed_max, linear.sweeps);
    observed_sum += linear.sweeps;
    ++observed_count;
    last_linear_ratio = linear.final_ratio;
    if (linear.final_ratio >
        config_.run_control.inner_residual_reduction_target) {
      ++inner_target_misses;
    }
    const bool max_iteration_inner_failure =
        linear.sweeps >= config_.run_control.max_inner_iterations &&
        linear.final_ratio >
            config_.run_control.inner_residual_reduction_target;
    if (!recovery_active && full_second_order) {
      if (max_iteration_inner_failure) {
        full_order_failure_score = std::min(full_order_failure_score + 1, 32);
      } else {
        // A near-miss should not erase evidence of a sustained difficult
        // nonlinear region, but the score must decay instead of latching.
        full_order_failure_score = std::max(full_order_failure_score - 1, 0);
      }
      // Before the 200-sample force window exists, a large residual rebound
      // supplies the evidence. Once it exists, it must also reject the normal
      // stable-force completion condition before recovery can activate.
      const bool force_history_inconsistent =
          drag_history.size() < history_window || !stable_forces;
      if (max_iteration_inner_failure &&
          full_order_failure_score >= recovery_failure_score_threshold &&
          residual_rebound && force_history_inconsistent) {
        recovery_active = true;
        recovery_activation_step = step;
        std::ostringstream recovery_message;
        recovery_message
            << "Activating steady nonlinear recovery at step " << step
            << ": sustained max-iteration inner failures and a "
            << recovery_residual_rebound_factor
            << "x full-order residual rebound; future effective CFL is "
            << "capped at " << recovery_cfl_cap
            << " and nonlinear relaxation at " << recovery_relaxation_cap
            << ".";
        output.log(recovery_message.str());
        if (rank_ == 0) {
          std::cout << recovery_message.str() << '\n' << std::flush;
        }
      }
    }
    // The pseudo-transient Newton direction solves (J + M/dtau)dU = -R.
    // It is not, in general, a strict descent direction for ||R|| at every
    // finite pseudo time step.  Requiring monotone spatial-residual decrease
    // can therefore freeze an otherwise valid continuation.  Positivity and
    // large local changes are handled conservatively by apply_increment.
    const double nominal_nonlinear_relaxation =
        linear.final_ratio <=
                config_.run_control.inner_residual_reduction_target
            ? 0.6
            : 0.25;
    const double nonlinear_relaxation =
        recovery_active
            ? std::min(nominal_nonlinear_relaxation, recovery_relaxation_cap)
            : nominal_nonlinear_relaxation;
    apply_increment(linear.increment, nonlinear_relaxation);
    if (step % 100 == 0) {
      std::ostringstream linear_message;
      linear_message << "GMRES iterations=" << linear.sweeps
                     << " relative defect=" << std::scientific
                     << linear.final_ratio << " relaxation="
                     << nonlinear_relaxation;
      output.log(linear_message.str());
    }
  }

  if (!converged && !plateau) {
    throw std::runtime_error("steady solve did not reach its residual target or a "
                             "credible stable plateau by max_steps");
  }
  method_metadata_.observed_min_inner_iterations =
      observed_count > 0 ? observed_min : 0;
  method_metadata_.observed_max_inner_iterations = observed_max;
  method_metadata_.observed_mean_inner_iterations =
      observed_count > 0
          ? static_cast<double>(observed_sum) / static_cast<double>(observed_count)
          : 0.0;
  method_metadata_.typical_inner_iterations = static_cast<int>(
      std::lround(method_metadata_.observed_mean_inner_iterations));
  method_metadata_.inner_target_misses = static_cast<int>(std::min<std::int64_t>(
      inner_target_misses, std::numeric_limits<int>::max()));
  method_metadata_.inner_target_converged_fraction =
      observed_count > 0
          ? static_cast<double>(observed_count - inner_target_misses) /
                static_cast<double>(observed_count)
          : 1.0;
  method_metadata_.last_inner_residual_ratio = last_linear_ratio;
  method_metadata_.steady_recovery_activated = recovery_active;
  method_metadata_.steady_recovery_activation_step = recovery_activation_step;
  method_metadata_.steady_recovery_cfl_cap =
      recovery_active ? recovery_cfl_cap : 0.0;
  method_metadata_.steady_recovery_relaxation_cap =
      recovery_active ? recovery_relaxation_cap : 0.0;

  RunSummary summary;
  summary.command = command;
  summary.wall_time_seconds = wall_seconds() - start_seconds;
  summary.final_step = final_step;
  summary.final_physical_time = 0.0;
  summary.convergence_status = "converged";
  summary.residual_reduction_orders = final_orders;
  summary.notes = plateau
                      ? "Stable terminal residual/force plateau accepted after substantial reduction; see histories."
                      : "Requested residual reduction and terminal force stability achieved.";
  if (recovery_active) {
    summary.notes += " Full-order nonlinear recovery activated at step " +
                     std::to_string(recovery_activation_step) +
                     "; effective CFL was capped at " +
                     std::to_string(recovery_cfl_cap) +
                     " and relaxation at " +
                     std::to_string(recovery_relaxation_cap) + ".";
  }
  summary.completed = true;
  return summary;
}

void FlowSolver::warm_start_transient(OutputWriter &output) {
  constexpr std::int64_t first_order_steps = 1000;
  constexpr std::int64_t blend_steps = 500;
  constexpr std::int64_t maximum_steps = 3000;
  constexpr double warmup_cfl_maximum = 50.0;
  const std::vector<double> no_physical_diagonal(mesh_.cells.size(), 0.0);
  double initial_residual = -1.0;
  double final_reduction = 0.0;
  std::int64_t final_warmup_step = 0;

  output.log("Initializing the transient from a numerically converged steady "
             "base flow before applying the generic symmetry perturbation.");
  for (std::int64_t step = 1; step <= maximum_steps; ++step) {
    reconstruction_blend_override_ = std::clamp(
        (static_cast<double>(step) - static_cast<double>(first_order_steps)) /
            static_cast<double>(blend_steps),
        0.0, 1.0);
    const double cfl_fraction = std::clamp(
        static_cast<double>(step - 1) /
            static_cast<double>(first_order_steps),
        0.0, 1.0);
    const double cfl = std::pow(warmup_cfl_maximum, cfl_fraction);
    SpatialAssembly spatial = assemble_spatial(step, 0.0);
    const ResidualNorm norm_value = residual_norm(spatial.residual);
    if (initial_residual < 0.0) {
      initial_residual = std::max(norm_value.total_l2, kTiny);
    }
    final_reduction = std::log10(
        initial_residual / std::max(norm_value.total_l2, kTiny));
    final_warmup_step = step;
    if (step % 100 == 0 || step == 1) {
      std::ostringstream message;
      message << "transient base-flow step=" << step << " CFL="
              << std::setprecision(6) << cfl << " residual="
              << std::scientific << norm_value.total_l2 << " reduction="
              << std::fixed << std::setprecision(3) << final_reduction
              << " orders Cd=" << spatial.force.cd;
      output.log(message.str());
      if (rank_ == 0) {
        std::cout << message.str() << '\n' << std::flush;
      }
    }
    if (reconstruction_blend_override_ >= 1.0 &&
        step >= first_order_steps + blend_steps + 200 &&
        final_reduction >= 4.0) {
      break;
    }
    const LinearResult linear = solve_newton_krylov(
        spatial, spatial.residual, no_physical_diagonal, cfl, step, 0.0,
        1.0, 3, 30, 0.01);
    const double relaxation = linear.final_ratio <= 0.01 ? 0.6 : 0.25;
    apply_increment(linear.increment, relaxation);
  }
  reconstruction_blend_override_ = -1.0;
  if (final_reduction < 3.0) {
    throw std::runtime_error(
        "transient steady base-flow initialization failed to reduce the residual");
  }

  exchange_conservative_halo(mesh_, states_, communicator_);
  refresh_primitives();
  // A deterministic, geometry-agnostic transverse perturbation releases an
  // unstable symmetric base flow without encoding a target force/frequency.
  for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    const Vec2 center = mesh_.cells[static_cast<std::size_t>(cell)].centroid -
                        config_.reference.moment_center;
    Primitive state = primitives_[static_cast<std::size_t>(cell)];
    const double length = config_.reference.length;
    const double radius2 = dot(center, center) / (length * length);
    state.v += 1.0e-4 * config_.freestream.velocity_magnitude *
               std::sin(2.0 * kPi * center.x / length) *
               std::exp(-0.05 * radius2);
    states_[static_cast<std::size_t>(cell)] =
        primitive_to_conservative(state, config_.gas);
  }
  exchange_conservative_halo(mesh_, states_, communicator_);
  refresh_primitives();
  output.log("Transient base-flow initialization completed at pseudo step " +
             std::to_string(final_warmup_step) + " with " +
             std::to_string(final_reduction) + " residual orders.");
}

RunSummary FlowSolver::run_transient(OutputWriter &output,
                                     const std::string &command,
                                     double start_seconds) {
  const double physical_dt = *config_.run_control.time_step;
  const double final_time = *config_.run_control.final_time;
  const std::int64_t physical_steps =
      static_cast<std::int64_t>(std::llround(final_time / physical_dt));
  if (std::abs(static_cast<double>(physical_steps) * physical_dt - final_time) >
      1.0e-10 * std::max(1.0, final_time)) {
    throw std::invalid_argument("final_time is not an integer number of time steps");
  }
  if (!initialized_from_restart_) {
    warm_start_transient(output);
  }
  std::vector<Conservative> previous = states_;
  std::vector<Conservative> previous_previous = states_;
  std::vector<double> lift_history;
  std::vector<double> drag_history;
  lift_history.reserve(static_cast<std::size_t>(physical_steps));
  drag_history.reserve(static_cast<std::size_t>(physical_steps));
  int observed_min = std::numeric_limits<int>::max();
  int observed_max = 0;
  std::int64_t observed_sum = 0;
  int target_misses = 0;
  double last_ratio = 1.0;
  double next_field_time = config_.outputs.write_field_every_time.value_or(
      std::numeric_limits<double>::infinity());

  output.log("Starting true physical-time solve: backward Euler startup then "
             "frozen-history BDF2 with nonlinear/linear inner iterations.");
  for (std::int64_t step = 1; step <= physical_steps; ++step) {
    const double physical_time = static_cast<double>(step) * physical_dt;
    const bool bdf2 = step > 1;
    const double alpha = bdf2 ? 1.5 : 1.0;
    const double beta = bdf2 ? -2.0 : -1.0;
    const double gamma = bdf2 ? 0.5 : 0.0;
    const double cfl = ramped_cfl(step);
    std::vector<double> physical_diagonal(mesh_.cells.size(), 0.0);
    for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
      physical_diagonal[static_cast<std::size_t>(cell)] =
          alpha * mesh_.cells[static_cast<std::size_t>(cell)].area / physical_dt;
    }

    if (bdf2) {
      // A second-order extrapolated predictor is consistent with BDF2 and
      // starts the nonlinear solve close to U^{n+1}. The two history fields
      // themselves remain frozen until the physical step is accepted.
      for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
        const Conservative extrapolated =
            2.0 * previous[static_cast<std::size_t>(cell)] -
            previous_previous[static_cast<std::size_t>(cell)];
        states_[static_cast<std::size_t>(cell)] = positivity_limited_state(
            previous[static_cast<std::size_t>(cell)], extrapolated,
            config_.gas);
      }
    }

    double initial_inner_residual = -1.0;
    int used_inner = 0;
    bool target_met = false;
    SpatialAssembly accepted_spatial;
    ResidualNorm accepted_norm;
    for (int inner = 1; inner <= config_.run_control.max_inner_iterations;
         ++inner) {
      SpatialAssembly spatial = assemble_spatial(step, physical_time);
      std::vector<Conservative> total = spatial.residual;
      for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
        const double coefficient =
            mesh_.cells[static_cast<std::size_t>(cell)].area / physical_dt;
        total[static_cast<std::size_t>(cell)] +=
            coefficient *
            (alpha * states_[static_cast<std::size_t>(cell)] +
             beta * previous[static_cast<std::size_t>(cell)] +
             gamma * previous_previous[static_cast<std::size_t>(cell)]);
      }
      const ResidualNorm norm_value = residual_norm(total);
      if (initial_inner_residual < 0.0) {
        initial_inner_residual = std::max(norm_value.total_l2, kTiny);
      }
      const double ratio =
          norm_value.total_l2 / std::max(initial_inner_residual, kTiny);
      ResidualRecord record;
      record.step = step;
      record.physical_time = physical_time;
      record.inner_iter = inner;
      record.cfl = cfl;
      record.dt = physical_dt;
      record.rho = norm_value.component_l2[0];
      record.rhou = norm_value.component_l2[1];
      record.rhov = norm_value.component_l2[2];
      record.rhoE = norm_value.component_l2[3];
      record.residual_l2 = norm_value.total_l2;
      record.residual_linf = norm_value.linf;
      if (inner % config_.outputs.write_residuals_every == 0 || inner == 1) {
        output.append_residual(record);
      }
      used_inner = inner;
      last_ratio = ratio;
      accepted_spatial = std::move(spatial);
      accepted_norm = norm_value;
      if (inner >= config_.run_control.min_inner_iterations &&
          ratio <= config_.run_control.inner_residual_reduction_target) {
        target_met = true;
        break;
      }
      if (inner == config_.run_control.max_inner_iterations) {
        break;
      }
      const LinearResult linear = solve_newton_krylov(
          accepted_spatial, total, physical_diagonal, cfl, step,
          physical_time, inner <= 2 ? 1.0 : 0.0, 3, 5, 0.05);
      const double relaxation = linear.final_ratio <= 0.1 ? 1.0 : 0.5;
      apply_increment(linear.increment, relaxation, 0.5);
      if (step <= 3 && inner <= 5) {
        std::ostringstream diagnostic;
        diagnostic << "transient Newton step=" << step << " inner=" << inner
                   << " GMRES=" << linear.sweeps << " defect="
                   << std::scientific << linear.final_ratio
                   << " relaxation=" << relaxation;
        output.log(diagnostic.str());
      }
    }
    observed_min = std::min(observed_min, used_inner);
    observed_max = std::max(observed_max, used_inner);
    observed_sum += used_inner;
    if (!target_met) {
      ++target_misses;
    }
    if (step % config_.outputs.write_forces_every == 0 ||
        step == physical_steps) {
      output.append_force(accepted_spatial.force);
    }
    lift_history.push_back(accepted_spatial.force.cl);
    drag_history.push_back(accepted_spatial.force.cd);

    // The two previous states remained frozen for every inner iteration above.
    // Only an accepted physical step changes BDF2 history.
    previous_previous = previous;
    previous = states_;

    if (physical_time + 1.0e-12 >= next_field_time) {
      output.write_transient_field(states_, gradients_, step, physical_time);
      next_field_time += *config_.outputs.write_field_every_time;
    }
    if (step % 100 == 0 || step == 1) {
      std::ostringstream message;
      message << "physical step=" << step << '/' << physical_steps
              << " t=" << std::fixed << std::setprecision(3) << physical_time
              << " inner=" << used_inner << " ratio=" << std::scientific
              << last_ratio << " Cd=" << std::fixed << std::setprecision(6)
              << accepted_spatial.force.cd << " Cl=" << accepted_spatial.force.cl;
      output.log(message.str());
      if (rank_ == 0) {
        std::cout << message.str() << '\n' << std::flush;
      }
    }
    (void)accepted_norm;
  }

  const double converged_fraction =
      1.0 - static_cast<double>(target_misses) /
                static_cast<double>(physical_steps);
  const std::size_t tail_begin =
      static_cast<std::size_t>((3 * physical_steps) / 5);
  const auto statistics = [](const std::vector<double> &values,
                             std::size_t begin, std::size_t end) {
    const double count = static_cast<double>(end - begin);
    const double mean = std::accumulate(values.begin() + static_cast<std::ptrdiff_t>(begin),
                                        values.begin() + static_cast<std::ptrdiff_t>(end), 0.0) /
                        count;
    double square = 0.0;
    for (std::size_t i = begin; i < end; ++i) {
      square += (values[i] - mean) * (values[i] - mean);
    }
    return std::pair<double, double>{mean, std::sqrt(square / count)};
  };
  const auto [tail_drag_mean, tail_drag_std] =
      statistics(drag_history, tail_begin, drag_history.size());
  const auto [tail_lift_mean, tail_lift_std] =
      statistics(lift_history, tail_begin, lift_history.size());
  const std::size_t tail_middle = tail_begin +
                                  (lift_history.size() - tail_begin) / 2;
  const auto [drag_mean_a, drag_std_a] =
      statistics(drag_history, tail_begin, tail_middle);
  const auto [drag_mean_b, drag_std_b] =
      statistics(drag_history, tail_middle, drag_history.size());
  const auto [lift_mean_a, lift_std_a] =
      statistics(lift_history, tail_begin, tail_middle);
  const auto [lift_mean_b, lift_std_b] =
      statistics(lift_history, tail_middle, lift_history.size());
  (void)tail_drag_std;
  (void)tail_lift_mean;
  (void)drag_std_a;
  (void)drag_std_b;
  (void)lift_mean_a;
  (void)lift_mean_b;
  const auto tail_minmax = std::minmax_element(
      lift_history.begin() + static_cast<std::ptrdiff_t>(tail_begin),
      lift_history.end());
  const double lift_amplitude = 0.5 * (*tail_minmax.second - *tail_minmax.first);
  const bool statistically_settled =
      converged_fraction >= 0.95 && tail_drag_mean > 0.0 &&
      lift_amplitude > 1.0e-4 && tail_lift_std > 1.0e-4 &&
      std::abs(drag_mean_a - drag_mean_b) <
          std::max(0.03, 0.08 * std::abs(tail_drag_mean)) &&
      lift_std_b > 0.5 * lift_std_a && lift_std_b < 2.0 * lift_std_a;
  if (!statistically_settled) {
    throw std::runtime_error(
        "transient reached final_time but failed inner-convergence or "
        "statistical vortex-street settlement checks");
  }

  method_metadata_.observed_min_inner_iterations = observed_min;
  method_metadata_.observed_max_inner_iterations = observed_max;
  method_metadata_.observed_mean_inner_iterations =
      static_cast<double>(observed_sum) / static_cast<double>(physical_steps);
  method_metadata_.typical_inner_iterations = static_cast<int>(
      std::lround(method_metadata_.observed_mean_inner_iterations));
  method_metadata_.inner_target_misses = target_misses;
  method_metadata_.inner_target_converged_fraction = converged_fraction;
  method_metadata_.last_inner_residual_ratio = last_ratio;

  RunSummary summary;
  summary.command = command;
  summary.wall_time_seconds = wall_seconds() - start_seconds;
  summary.final_step = physical_steps;
  summary.final_physical_time = final_time;
  summary.convergence_status = "statistically_periodic";
  summary.residual_reduction_orders =
      -std::log10(std::max(last_ratio, 1.0e-300));
  std::ostringstream notes;
  notes << "Frozen-history BDF2 reached t=" << final_time
        << "; converged inner-step fraction=" << converged_fraction
        << ", post-transient mean Cd=" << tail_drag_mean
        << ", lift amplitude=" << lift_amplitude << '.';
  summary.notes = notes.str();
  summary.completed = true;
  return summary;
}

RunSummary FlowSolver::run(
    OutputWriter &output,
    const std::optional<std::filesystem::path> &restart_file,
    const std::string &command) {
  const double start = wall_seconds();
  initialize_state(restart_file);
  RunSummary summary = config_.run_control.type == RunType::steady
                           ? run_steady(output, command, start)
                           : run_transient(output, command, start);
  compute_gradients_and_limiters();
  surface_rows_ = build_surface_rows();
  output.update_method_metadata(method_metadata_);
  output.write_final(states_, gradients_, surface_rows_, summary);
  return summary;
}

} // namespace aerofv
