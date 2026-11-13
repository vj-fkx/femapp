#pragma once

#include <fem/geom/drawing.hpp>
#include <fem/render/camera.hpp>
#include <fem/render/mesh_picker.hpp>

#include <optional>
#include <vector>

namespace fem::render {

// REQ-005-25: click, window and crossing selection of drawing entities, on
// the CPU like the mesh picker (ADR 0003).
class DrawingPicker
{
public:
    // The entity under `pixel` closest to the camera: a line or polyline
    // piece, or a point, within `tolerancePx`.
    [[nodiscard]] std::optional<geom::EntityId> pick(const geom::Drawing& drawing,
                                                     const Camera& camera, const Vec2& pixel,
                                                     double tolerancePx) const;

    // Window (all vertices inside) or crossing (a vertex inside or a piece
    // crossing the rectangle), hidden or not. In drawing order.
    [[nodiscard]] std::vector<geom::EntityId>
    pickRect(const geom::Drawing& drawing, const Camera& camera, const PickRect& rect) const;
};

} // namespace fem::render
