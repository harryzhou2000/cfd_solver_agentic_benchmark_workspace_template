#pragma once

#include <string>
#include <vector>

#include "gas.hpp"
#include "types.hpp"

namespace cfd {

// Boundary condition types supported by the solver (parsed from case-file
// boundary_conditions strings, e.g. "farfield", "slip_wall",
// "no_slip_adiabatic_wall").
enum class BCType {
    FarField,
    SlipWall,
    NoSlipAdiabaticWall,
    Unknown
};

// Maps a case-file BC type string to its enum value; returns BCType::Unknown
// for unrecognized strings.
BCType bc_type_from_string(const std::string& s);

// Backward-compatible alias used by the ghost-cell BC class.
using BoundaryType = BCType;

// Result of applying a boundary condition to one boundary face.
struct BCFlux {
    Vector4 U_ext;  // ghost/exterior state (used by the residual assembly)
    Vector4 flux;   // optional precomputed boundary flux (unused in Phase 3a:
                    // the assembly recomputes it from U_int and U_ext with the
                    // same numerical fluxes as interior faces)
};

// Applies a boundary condition to a boundary face with outward-pointing
// normal (nx, ny) (magnitude = face area, normalized internally).
//
// FarField (characteristic-free simple treatment):
//   inflow  (v_n < 0): U_ext = freestream state
//   outflow (v_n >= 0): U_ext = U_int (extrapolation)
// SlipWall (inviscid wall): velocity mirrored to zero normal component,
//   density and pressure extrapolated from the interior.
// NoSlipAdiabaticWall: wall velocity zero, temperature extrapolated
//   (adiabatic), density from p_wall = p_int and T_wall = T_int.
// Unknown: conservative fallback -- extrapolate the interior state.
BCFlux apply_boundary_condition(const Vector4& U_int, double nx, double ny,
                                BCType bc_type,
                                const FreestreamParams& freestream,
                                const GasParams& gas, double viscosity = 0.0);

// Ghost-cell BC object (Phase 1 API kept for compatibility; apply() now
// delegates to apply_boundary_condition).
class BoundaryCondition {
public:
    BoundaryCondition(BCType type, const FreestreamParams& freestream,
                      const GasParams& gas)
        : type_(type), freestream_(freestream), gas_(gas) {}

    // Fills the ghost cell state given the interior state and face normal.
    Vector4 apply(const Vector4& interior, const Vector2& face_normal,
                  bool face_on_boundary) const;

    BCType type() const { return type_; }

private:
    BCType type_;
    FreestreamParams freestream_;
    GasParams gas_;
};

// Creates the BC list for all boundary faces of the mesh.
std::vector<BoundaryCondition> create_boundary_conditions(
    const FreestreamParams& freestream, const GasParams& gas);

}  // namespace cfd
