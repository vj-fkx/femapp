#include "section_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <type_traits>

namespace fem::app {

namespace {

using V = Eigen::Vector2d;

// The rectangle y0 <= y <= y1, z0 <= z <= z1 (the rect() of core).
Ring rect(double y0, double y1, double z0, double z1, bool hole = false)
{
    return {.points = {V(y0, z0), V(y1, z0), V(y1, z1), V(y0, z1)}, .hole = hole};
}

Ring circle(double d, bool hole = false)
{
    constexpr int kSides = 64;
    Ring ring{.points = {}, .hole = hole};
    ring.points.reserve(kSides);
    for (int k = 0; k < kSides; ++k) {
        const double a = 2.0 * std::numbers::pi * k / kSides;
        ring.points.emplace_back(d / 2 * std::cos(a), d / 2 * std::sin(a));
    }
    return ring;
}

double signedArea(const std::vector<V>& points)
{
    double area = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const V& a = points[i];
        const V& b = points[(i + 1) % points.size()];
        area += a.x() * b.y() - b.x() * a.y();
    }
    return area / 2.0;
}

} // namespace

std::vector<Ring> outlineOf(const core::SectionShape& shape)
{
    return std::visit(
        [](const auto& s) -> std::vector<Ring> {
            using S = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<S, core::Rectangle>) {
                return {rect(-s.h / 2, s.h / 2, -s.b / 2, s.b / 2)};
            } else if constexpr (std::is_same_v<S, core::Circle>) {
                return {circle(s.d)};
            } else if constexpr (std::is_same_v<S, core::CircularTube>) {
                return {circle(s.d), circle(s.d - 2.0 * s.t, true)};
            } else if constexpr (std::is_same_v<S, core::RectangularTube>) {
                const double hb = s.b / 2;
                const double hh = s.h / 2;
                return {rect(-hh, hh, -hb, hb),
                        rect(-hh + s.t, hh - s.t, -hb + s.t, hb - s.t, true)};
            } else if constexpr (std::is_same_v<S, core::ISection>) {
                const double hh = s.h / 2;
                const double hb = s.bf / 2;
                return {rect(hh - s.tf, hh, -hb, hb), rect(-hh, -hh + s.tf, -hb, hb),
                        rect(-hh + s.tf, hh - s.tf, -s.tw / 2, s.tw / 2)};
            } else if constexpr (std::is_same_v<S, core::Angle>) {
                return {rect(0.0, s.h, 0.0, s.t), rect(0.0, s.t, s.t, s.b)};
            } else if constexpr (std::is_same_v<S, core::Channel>) {
                const double hh = s.h / 2;
                return {rect(-hh, hh, 0.0, s.tw), rect(hh - s.tf, hh, s.tw, s.bf),
                        rect(-hh, -hh + s.tf, s.tw, s.bf)};
            } else if constexpr (std::is_same_v<S, core::Tee>) {
                return {rect(s.h - s.tf, s.h, -s.bf / 2, s.bf / 2),
                        rect(0.0, s.h - s.tf, -s.tw / 2, s.tw / 2)};
            } else if constexpr (std::is_same_v<S, core::ThinWalled>) {
                // Each wall as a band of its thickness about its mid-line.
                std::vector<Ring> rings;
                for (const core::ThinWalled::Wall& wall : s.walls) {
                    if (wall.from < 0 || wall.to < 0 ||
                        static_cast<std::size_t>(std::max(wall.from, wall.to)) >= s.points.size()) {
                        continue;
                    }
                    const V a = s.points[static_cast<std::size_t>(wall.from)];
                    const V b = s.points[static_cast<std::size_t>(wall.to)];
                    const V along = b - a;
                    if (along.norm() <= 0.0) {
                        continue;
                    }
                    const V across = V(-along.y(), along.x()).normalized() * (wall.t / 2);
                    rings.push_back({.points = {a - across, b - across, b + across, a + across},
                                     .hole = false});
                }
                return rings;
            } else {
                std::vector<Ring> rings;
                for (const core::SolidPolygon::Region& region : s.regions) {
                    rings.push_back({.points = region.outline, .hole = false});
                    for (const auto& hole : region.holes) {
                        rings.push_back({.points = hole, .hole = true});
                    }
                }
                return rings;
            }
        },
        shape);
}

Eigen::Vector2d toDrawing(const core::SectionProperties& properties,
                          const Eigen::Vector2d& principal)
{
    // The inverse of core's toPrincipal: a rotation by the principal angle.
    const double c = std::cos(properties.principalAngle);
    const double s = std::sin(properties.principalAngle);
    return properties.centroid +
           V(c * principal.x() - s * principal.y(), s * principal.x() + c * principal.y());
}

std::variant<core::SolidPolygon, std::string>
solidFromPolylines(const geom::Drawing& drawing, std::span<const geom::EntityId> entities)
{
    if (entities.empty()) {
        return std::string("Select the closed polylines of the section first.");
    }
    std::vector<std::vector<V>> rings;
    for (const geom::EntityId id : entities) {
        const geom::Entity* entity = drawing.find(id);
        const auto* polyline =
            entity != nullptr ? std::get_if<geom::PolylineEntity>(&entity->shape) : nullptr;
        if (polyline == nullptr || !polyline->closed || polyline->vertices.size() < 3) {
            return "Entity " + std::to_string(static_cast<std::uint64_t>(id)) +
                   " is not a closed polyline of three or more vertices.";
        }
        std::vector<V> ring;
        ring.reserve(polyline->vertices.size());
        for (const geom::Vec3& p : polyline->vertices) {
            ring.emplace_back(p.x(), p.y());
        }
        rings.push_back(std::move(ring));
    }
    // The largest is the outline; the others are its holes.
    const auto outline = std::ranges::max_element(
        rings, {}, [](const std::vector<V>& r) { return std::abs(signedArea(r)); });
    core::SolidPolygon solid;
    core::SolidPolygon::Region region;
    region.outline = *outline;
    for (auto it = rings.begin(); it != rings.end(); ++it) {
        if (it != outline) {
            region.holes.push_back(*it);
        }
    }
    solid.regions.push_back(std::move(region));
    return solid;
}

} // namespace fem::app
