#pragma once

#include <fem/core/element_type.hpp>
#include <fem/core/expected.hpp>
#include <fem/core/shape_functions.hpp>

#include <Eigen/Core>
#include <array>

namespace fem::core {

// Coordinates of the nodes of an element, in local order, in fixed storage
// (filled by the caller: no allocation, REQ-006-05).
struct ElementNodes
{
    ElementType type = ElementType::Line2;
    int count = 0;
    std::array<Eigen::Vector3d, kMaxNodes> x{};
};

// Geometry of an element at one reference point (REQ-006-03).
struct PointGeometry
{
    Eigen::Vector3d position = Eigen::Vector3d::Zero();
    // det J for solids; length or area element |J| for lines and surfaces,
    // also when they lie in space.
    double measure = 0.0;
    // N_i and the physical derivatives dN_i/dx (for lines and surfaces, the
    // gradient along them).
    ShapeValues shape;
};

// REQ-006-03, 04: fails for a solid whose det J is zero or negative, or a line
// or surface of zero measure. The message does not name the element; the
// caller adds its id.
[[nodiscard]] Expected<PointGeometry> evaluateGeometry(const ElementNodes& nodes,
                                                       const std::array<double, 3>& xi);

} // namespace fem::core
