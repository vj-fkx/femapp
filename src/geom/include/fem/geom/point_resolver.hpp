#pragma once

#include <fem/geom/drawing.hpp>

#include <Eigen/Core>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace fem::geom {

// REQ-005-19
enum class SnapMode : std::uint8_t { Endpoint, Midpoint, Intersection };

// REQ-005-19, 21, 22, 23
struct DraftingSettings
{
    bool objectSnap = true;                         // F3
    std::array<bool, 3> modes = {true, true, true}; // per SnapMode
    bool gridSnap = false;                          // F9
    double gridStep = 1.0;
    bool ortho = false; // F8
    double aperturePx = 10.0;

    [[nodiscard]] bool isOn(SnapMode mode) const noexcept
    {
        return objectSnap && modes[static_cast<std::size_t>(mode)];
    }
};

// What the resolver needs from the screen, without depending on render.
struct ScreenView
{
    Eigen::Matrix4d viewProjection = Eigen::Matrix4d::Identity(); // world -> clip, y up
    double width = 1.0;                                           // pixels
    double height = 1.0;
    bool orthographic = false;     // the plane may then lie behind the eye
    Vec3 rayOrigin = Vec3::Zero(); // the ray under the cursor
    Vec3 rayDirection = -Vec3::UnitZ();
    Eigen::Vector2d cursor = Eigen::Vector2d::Zero(); // pixels, y down
};

struct ResolvedPoint
{
    Vec3 point = Vec3::Zero();
    std::optional<SnapMode> snap; // set when an object snap was taken
};

// REQ-005-14, 19, 22, 23: the point a click would give.
class PointResolver
{
public:
    // Object snap first; else the cursor on the plane z = 0 (z of `last` with
    // ortho on), rounded to the grid, constrained by ortho. std::nullopt when
    // there is neither a snap nor a plane point.
    [[nodiscard]] std::optional<ResolvedPoint> resolve(const Drawing& drawing,
                                                       const ScreenView& view,
                                                       const DraftingSettings& settings,
                                                       const std::optional<Vec3>& last) const;

private:
    void refresh(const Drawing& drawing) const;

    // Cached per drawing revision.
    mutable const Drawing* m_drawing = nullptr;
    mutable std::uint64_t m_revision = 0;
    mutable std::vector<Vec3> m_endpoints;
    mutable std::vector<Segment> m_segments;
};

// Closest points of segments ab and cd; returns the distance between them and
// the two points (parameters clamped to the segments).
struct ClosestPoints
{
    double distance = 0.0;
    Vec3 onFirst = Vec3::Zero();
    Vec3 onSecond = Vec3::Zero();
};
[[nodiscard]] ClosestPoints closestPoints(const Vec3& a, const Vec3& b, const Vec3& c,
                                          const Vec3& d);

} // namespace fem::geom
