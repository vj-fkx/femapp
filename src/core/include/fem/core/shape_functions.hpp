#pragma once

#include <fem/core/element_type.hpp>

#include <array>

namespace fem::core {

// Largest number of nodes among the types with shape functions (Hex27).
inline constexpr int kMaxNodes = 27;

// Values and derivatives of the shape functions at one point. Fixed size: no
// allocation (REQ-006-05).
struct ShapeValues
{
    int count = 0;                                     // nodes of the type
    std::array<double, kMaxNodes> n{};                 // N_i
    std::array<std::array<double, 3>, kMaxNodes> dn{}; // dN_i/dxi_k, k < dimension
};

// REQ-006-01: Line2, Line3, Tri3, Tri6, Quad4, Quad8, Quad9, Tet4, Tet10,
// Hex8, Hex20, Hex27, Wedge6, Wedge15, Wedge18, Pyramid5.
[[nodiscard]] bool hasShapeFunctions(ElementType type) noexcept;

// Lagrange shape functions in Gmsh's local node order, at `xi` in Gmsh's
// reference element of the type (unused coordinates are ignored). The type
// must have shape functions. The first call for a type prepares its tables;
// later calls do not allocate.
void evaluateShape(ElementType type, const std::array<double, 3>& xi, ShapeValues& out);

} // namespace fem::core
