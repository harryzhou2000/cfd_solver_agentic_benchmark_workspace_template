#pragma once

#include <array>
#include <cstdint>

namespace cfd {

using Real = double;
using GlobalIndex = std::int64_t;
using LocalIndex = std::int32_t;

inline constexpr int kSpatialDimension = 2;
inline constexpr int kStateVariables = 4;
inline constexpr int kSupportedCaseSchemaVersion = 1;

using Vec2 = std::array<Real, kSpatialDimension>;
using State = std::array<Real, kStateVariables>;
using Grad2 = std::array<Vec2, kSpatialDimension>;

struct Primitive {
    Real rho{};
    Real u{};
    Real v{};
    Real p{};
    Real T{};
    Real a{};
    Real H{};
};

enum class BoundaryType { farfield, slip_wall, no_slip_adiabatic_wall };
enum class RunType { steady, transient };

}  // namespace cfd
