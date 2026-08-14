#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <mpi.h>

namespace cfd {

// Conservative state: [rho, rhou, rhov, rhoE]
// Primitive state:    [rho, u, v, p, T]
constexpr int NEQ = 4;
constexpr int NPRIM = 5;

using Real = double;
using ConsState = std::array<Real, NEQ>;
using PrimState = std::array<Real, NPRIM>;

// Boundary condition types
enum class BCType { None, Farfield, SlipWall, NoSlipAdiabaticWall };

inline const char* bcTypeName(BCType t) {
    switch (t) {
        case BCType::Farfield: return "farfield";
        case BCType::SlipWall: return "slip_wall";
        case BCType::NoSlipAdiabaticWall: return "no_slip_adiabatic_wall";
        default: return "none";
    }
}

// Element type
enum class ElemType { Tri3, Quad4, Bar2 };

inline int elemNumNodes(ElemType t) {
    switch (t) {
        case ElemType::Tri3: return 3;
        case ElemType::Quad4: return 4;
        case ElemType::Bar2: return 2;
    }
    return 0;
}

// Simple timer
inline double wallTime() {
    return MPI_Wtime();
}

// Clamp
inline Real clamp(Real x, Real lo, Real hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

} // namespace cfd
