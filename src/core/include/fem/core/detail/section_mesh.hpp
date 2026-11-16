#pragma once

// REQ-008b-02, 04: the triangulation of a solid polygon section (design 008b
// §5.1.3, ADR 0005). Internal to core: not a general mesh generator.

#include <fem/core/expected.hpp>
#include <fem/core/section.hpp>

#include <Eigen/Core>
#include <array>
#include <string_view>
#include <vector>

namespace fem::core::detail {

// Six-node triangles: corners 0, 1, 2 counterclockwise in the drawing (y, z),
// then the mid-sides of the edges 0-1, 1-2 and 2-0.
struct SectionMesh
{
    std::vector<Eigen::Vector2d> nodes;
    std::vector<std::array<int, 6>> triangles;
};

// The default largest edge: sqrt(A / 1500), about 3000 triangles.
[[nodiscard]] double defaultMeshSize(double area);

// Checks the polygons (REQ-008b-04), then triangulates them: ear clipping
// with the holes bridged to the outline, Lawson flips to the constrained
// Delaunay triangulation, Rivara's longest-edge bisection down to `size`
// (0: the default), and mid-side nodes. Fails naming the section, the region
// and the polygon.
[[nodiscard]] Expected<SectionMesh> meshSection(const SolidPolygon& shape,
                                                std::string_view section);

// The exact area of the polygons (outlines minus holes), for valid shapes.
[[nodiscard]] double polygonArea(const SolidPolygon& shape);

} // namespace fem::core::detail
