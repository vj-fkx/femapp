#include "pick_util.hpp"

#include <fem/render/drawing_picker.hpp>

#include <limits>

namespace fem::render {

namespace {

using detail::Box2;
using detail::Projector;
using detail::ScreenPoint;

// Positions are projected relative to the centre of the drawing, so that
// large coordinates keep their precision (REQ-005-33).
Eigen::Vector3d originOf(const geom::Drawing& drawing)
{
    const auto box = drawing.bounds();
    return box ? Eigen::Vector3d(0.5 * (box->min + box->max)) : Eigen::Vector3d::Zero();
}

} // namespace

std::optional<geom::EntityId> DrawingPicker::pick(const geom::Drawing& drawing,
                                                  const Camera& camera, const Vec2& pixel,
                                                  double tolerancePx) const
{
    const Eigen::Vector3d origin = originOf(drawing);
    const Projector projector(camera, origin);
    std::optional<geom::EntityId> best;
    double bestDepth = std::numeric_limits<double>::infinity();
    double bestDistance = std::numeric_limits<double>::infinity();
    const auto offer = [&](geom::EntityId id, double distance, double depth) {
        if (distance > tolerancePx) {
            return;
        }
        // Closest to the camera; on equal depth, closest to the cursor.
        constexpr double kSameDepth = 1e-9;
        if (depth < bestDepth - kSameDepth ||
            (depth <= bestDepth + kSameDepth && distance < bestDistance)) {
            best = id;
            bestDepth = depth;
            bestDistance = distance;
        }
    };

    for (const geom::Entity& entity : drawing.entities()) {
        const auto segments = geom::segmentsOf(entity);
        if (segments.empty()) {
            for (const auto& p : geom::verticesOf(entity)) {
                const ScreenPoint s = projector.project(Eigen::Vector3d(p - origin));
                if (s.inFront) {
                    offer(entity.id, (s.pixel - pixel).norm(), s.depth);
                }
            }
            continue;
        }
        for (const geom::Segment& segment : segments) {
            const ScreenPoint a = projector.project(Eigen::Vector3d(segment.a - origin));
            const ScreenPoint b = projector.project(Eigen::Vector3d(segment.b - origin));
            if (!a.inFront || !b.inFront) {
                continue;
            }
            const auto [distance, u] = detail::distanceToSegment(pixel, a.pixel, b.pixel);
            offer(entity.id, distance, a.depth + u * (b.depth - a.depth));
        }
    }
    return best;
}

std::vector<geom::EntityId> DrawingPicker::pickRect(const geom::Drawing& drawing,
                                                    const Camera& camera,
                                                    const PickRect& rect) const
{
    const Eigen::Vector3d origin = originOf(drawing);
    const Projector projector(camera, origin);
    const Box2 box{.min = rect.from.cwiseMin(rect.to), .max = rect.from.cwiseMax(rect.to)};
    const bool crossing = rect.isCrossing();

    std::vector<geom::EntityId> result;
    for (const geom::Entity& entity : drawing.entities()) {
        bool allInside = true;
        bool anyInside = false;
        bool allInFront = true;
        for (const auto& p : geom::verticesOf(entity)) {
            const ScreenPoint s = projector.project(Eigen::Vector3d(p - origin));
            const bool inside = s.inFront && box.contains(s.pixel);
            allInside = allInside && inside;
            anyInside = anyInside || inside;
            allInFront = allInFront && s.inFront;
        }
        bool selected = allInside;
        if (!selected && crossing && allInFront) {
            selected = anyInside;
            for (const geom::Segment& segment : geom::segmentsOf(entity)) {
                if (selected) {
                    break;
                }
                const ScreenPoint a = projector.project(Eigen::Vector3d(segment.a - origin));
                const ScreenPoint b = projector.project(Eigen::Vector3d(segment.b - origin));
                selected = detail::segmentTouchesBox(a.pixel, b.pixel, box);
            }
        }
        if (selected) {
            result.push_back(entity.id);
        }
    }
    return result;
}

} // namespace fem::render
