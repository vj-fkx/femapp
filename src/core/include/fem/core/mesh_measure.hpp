#pragma once

#include <fem/core/mesh.hpp>

namespace fem::core {

// Length, area or volume of an element, from its vertices only: straight
// edges and planar faces, also for higher-order elements (REQ-003-08).
//
// - Points measure 1.
// - Lengths are positive.
// - Areas are signed by the Z component of the vector area when the mesh lies
//   in the XY plane, and positive otherwise (surfaces in 3D have no defined
//   orientation).
// - Volumes are signed: an inverted element has a negative volume.
[[nodiscard]] double elementMeasure(const Mesh& mesh, ElementIndex element);

} // namespace fem::core
