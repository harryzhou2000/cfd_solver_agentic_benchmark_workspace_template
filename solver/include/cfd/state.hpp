#pragma once

#include <array>

#include "cfd/case_config.hpp"
#include "cfd/common.hpp"

namespace cfd {

using Conserved = std::array<Real, 4>;

struct Primitive {
  Real rho{1.0};
  Real u{0.0};
  Real v{0.0};
  Real p{1.0};
};

struct Thermo {
  Real a{1.0};
  Real E{1.0};
  Real T{1.0};
  Real mach{0.0};
};

Primitive freestream_primitive(const CaseConfig& cfg);
Conserved primitive_to_conserved(const Primitive& q, const GasModel& gas);
Primitive conserved_to_primitive(const Conserved& u, const GasModel& gas);
Thermo thermo_from_primitive(const Primitive& q, const GasModel& gas);
Real viscosity(const CaseConfig& cfg);
Real dynamic_pressure(const CaseConfig& cfg);
Conserved operator+(const Conserved& a, const Conserved& b);
Conserved operator-(const Conserved& a, const Conserved& b);
Conserved operator*(Real s, const Conserved& a);
Conserved& operator+=(Conserved& a, const Conserved& b);
Conserved& operator-=(Conserved& a, const Conserved& b);

}  // namespace cfd
