#pragma once

#include "cfd/CaseConfig.hpp"
#include "cfd/DistributedMesh.hpp"
#include "cfd/Physics.hpp"
#include "cfd/Reconstruction.hpp"

#include <array>
#include <vector>

namespace cfd {

struct ForceComponents {
  double pressure_drag = 0.0;
  double viscous_drag = 0.0;
  double pressure_lift = 0.0;
  double viscous_lift = 0.0;
  double moment_z = 0.0;
};

struct SpatialEvaluation {
  std::vector<Conserved> residual;  // volume-integrated residual on owned cells
  std::vector<double> spectral_radius;
  ForceComponents forces;
};

/// Integrates one-sided tangential wall shear from the adjacent cell velocity.
/// Normal velocity/stress is excluded so this matches the surface Cf definition.
Vec2 tangential_wall_shear_force(Vec2 velocity, Vec2 normal, double wall_distance,
                                 double viscosity, double area);

class SpatialOperator {
 public:
  SpatialOperator(const LocalMesh& mesh, const CaseConfig& config, MPI_Comm communicator);

  SpatialEvaluation evaluate(const std::vector<Conserved>& state,
                             std::vector<ReconstructionData>& reconstruction,
                             double reconstruction_factor = 1.0,
                             bool refresh_reconstruction = true) const;

 private:
  PrimitiveGradients face_gradients(const LocalFace& face, const FaceStates& face_state,
                                    const std::vector<Conserved>& state,
                                    const std::vector<ReconstructionData>& data) const;
  bool is_wall(const LocalFace& face) const;

  const LocalMesh& mesh_;
  const CaseConfig& config_;
  GasProperties gas_;
  double viscosity_ = 0.0;
  double rusanov_scale_ = 1.0;
  Reconstructor reconstructor_;
};

}  // namespace cfd
