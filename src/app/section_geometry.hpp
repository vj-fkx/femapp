#pragma once

// Feature 010 (REQ-010-05, 06): the geometry of a section for drawing it,
// and a solid polygon made from closed polylines of the drawing. No widgets,
// so that it is tested on its own.

#include <fem/core/section.hpp>
#include <fem/geom/drawing.hpp>
#include <fem/setup/units.hpp>

#include <Eigen/Core>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace fem::app {

// A closed ring of the outline in the drawing of the section (y, z): the
// material, or a hole in it.
struct Ring
{
    std::vector<Eigen::Vector2d> points;
    bool hole = false;
};

// The outline of a shape as the core builds it (design 008 §5.1): the
// rectangles of its parts, circles as 64-sided polygons, the walls of a
// thin-walled section with their thickness, the regions of a polygon.
[[nodiscard]] std::vector<Ring> outlineOf(const core::SectionShape& shape);

// A point in the principal axes from the centroid (as the core gives the
// shear centre and the stress points) back in the drawing (y, z).
[[nodiscard]] Eigen::Vector2d toDrawing(const core::SectionProperties& properties,
                                        const Eigen::Vector2d& principal);

// REQ-010-05: a solid polygon from closed polylines of the drawing, its
// (x, y) taken as the (y, z) of the section: the one of largest area is the
// outline, the others its holes. A text naming the problem when the
// entities are not closed polylines of at least three vertices, or none
// was given.
[[nodiscard]] std::variant<core::SolidPolygon, std::string>
solidFromPolylines(const geom::Drawing& drawing, std::span<const geom::EntityId> entities);

// REQ-010-05: a new section named `name`, of the shape at `shape` in
// core::SectionShape, or generic for -1, with dimensions of a common
// profile (a 0.3 m by 0.5 m rectangle, an IPE 300...) in `units`, ready to
// be edited in the panel.
[[nodiscard]] core::Section newSection(const std::string& name, int shape, setup::UnitSystem units);

} // namespace fem::app
