#pragma once

#include "physics.hpp"

#include <array>
#include <vector>

namespace aerofv {

// Linear primitive reconstruction: q(x_f) = q_c + phi grad(q_c).(x_f-x_c).
Primitive reconstruct_primitive(const Primitive &center,
                                const PrimitiveGradient &gradient,
                                const Vec2 &face_offset,
                                const std::array<double, 4> &limiter =
                                    {1.0, 1.0, 1.0, 1.0});

// Construct a non-orthogonal corrected primitive gradient for a viscous face.
// The two primitive arguments must be values at the endpoints of
// center_displacement (normally the two cell centres, or a cell centre and its
// mirrored boundary ghost).  In particular, reconstructed values evaluated at
// the face must not be supplied here: they are co-located in smooth flow and
// their difference would spuriously set the normal derivative to zero.
// The tangential part is the average of the supplied least-squares gradients;
// the normal part is corrected to reproduce the endpoint difference.
PrimitiveGradient corrected_primitive_face_gradient(
    const PrimitiveGradient &left_gradient,
    const PrimitiveGradient &right_gradient, const Primitive &left_center,
    const Primitive &right_center, const Vec2 &center_displacement,
    const Vec2 &unit_normal, double epsilon = 1.0e-14);

// Scalar Barth-Jespersen coefficient.  neighbor_min/max are extrema over the
// cell and its stencil; face_offsets are all reconstruction evaluation points
// (normally the incident face centroids).
double barth_jespersen_limiter(double center_value, const Vec2 &gradient,
                               double neighbor_min, double neighbor_max,
                               const std::vector<Vec2> &face_offsets,
                               double epsilon = 1.0e-14);
std::array<double, 4> barth_jespersen_limiters(
    const Primitive &center, const PrimitiveGradient &gradient,
    const Primitive &neighbor_min, const Primitive &neighbor_max,
    const std::vector<Vec2> &face_offsets, double epsilon = 1.0e-14);

// Return theta in [0, 1] so center + theta(candidate-center) is admissible.
// The pressure condition is checked on the actual conservative state, not by
// separately clipping conserved components.
double positivity_scale(const Conservative &center,
                        const Conservative &candidate, const GasModel &gas,
                        unsigned bisection_iterations = 48);
Conservative positivity_limited_state(const Conservative &center,
                                      const Conservative &candidate,
                                      const GasModel &gas,
                                      unsigned bisection_iterations = 48);
Primitive positivity_limited_reconstruction(
    const Primitive &center, const Primitive &candidate, const GasModel &gas,
    unsigned bisection_iterations = 48);

} // namespace aerofv
