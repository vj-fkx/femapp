#pragma once

#include <fem/core/element_type.hpp>

#include <array>
#include <span>

namespace fem::core {

struct QuadraturePoint
{
    std::array<double, 3> xi{}; // in Gmsh's reference element of the family
    double weight = 0.0;        // the weights add up to the reference measure
};

// REQ-006-06, 07: a rule exact for polynomials up to `degree` at least
// (0 <= degree <= maxQuadratureDegree(family)). The points live as long as
// the program, so the span can be kept. Point: one point of weight 1.
[[nodiscard]] std::span<const QuadraturePoint> quadrature(ElementFamily family, int degree);

// 9 for lines, quadrilaterals and hexahedra; 5 for the others.
[[nodiscard]] int maxQuadratureDegree(ElementFamily family) noexcept;

} // namespace fem::core
