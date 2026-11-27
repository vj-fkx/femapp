#pragma once

// Feature 012 (design §4; REQ-012-18): a click on the shape of a result as
// it is drawn: the bar and the point of it, or a node.

#include <fem/core/mesh.hpp>
#include <fem/core/result_sampling.hpp>
#include <fem/render/camera.hpp>
#include <fem/render/result_scene.hpp>

#include <optional>

namespace fem::render {

struct ResultPick
{
    core::ElementIndex element = -1; // the bar clicked, or that of the node
    core::Station station;           // the point of the bar (x along the segment)
    core::NodeIndex node = -1;       // the node, when one is within the tolerance
};

// The point of the shape drawn with `scale` (positions + scale x
// displacements) nearest to `pixel`, within `tolerancePx`: a node first,
// else the nearest segment, x interpolated along it; nothing otherwise.
[[nodiscard]] std::optional<ResultPick> pickResult(const ResultScene& scene, double scale,
                                                   const Camera& camera, const Vec2& pixel,
                                                   double tolerancePx = 6.0);

} // namespace fem::render
