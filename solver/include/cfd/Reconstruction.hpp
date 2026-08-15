#pragma once

#include "cfd/CaseConfig.hpp"
#include "cfd/DistributedMesh.hpp"
#include "cfd/HaloExchange.hpp"
#include "cfd/Physics.hpp"

#include <array>
#include <vector>

namespace cfd {

struct ReconstructionData {
  std::array<Vec2, 4> gradient{};  // rho, u, v, p
  std::array<double, 4> limiter{{1.0, 1.0, 1.0, 1.0}};
};

struct FaceStates {
  Primitive left;
  Primitive right;
};

class Reconstructor {
 public:
  Reconstructor(const LocalMesh& mesh, const CaseConfig& config, MPI_Comm communicator);

  void compute(const std::vector<Conserved>& state, std::vector<ReconstructionData>& data,
               double reconstruction_factor = 1.0) const;
  FaceStates face_states(const LocalFace& face, const std::vector<Conserved>& state,
                         const std::vector<ReconstructionData>& data) const;
  Primitive boundary_value(const LocalFace& face, const Primitive& interior) const;

 private:
  Primitive boundary_ghost(const LocalFace& face, const Primitive& interior) const;
  Primitive characteristic_farfield(const LocalFace& face, const Primitive& interior) const;
  Primitive freestream() const;

  const LocalMesh& mesh_;
  const CaseConfig& config_;
  HaloExchange halo_;
};

}  // namespace cfd
