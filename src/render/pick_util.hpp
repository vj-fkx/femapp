#pragma once

// Screen-space helpers shared by the mesh and drawing pickers (design 005
// §5.6). Private to the render module.

#include <fem/render/camera.hpp>

#include <Eigen/Core>
#include <algorithm>
#include <array>
#include <utility>

namespace fem::render::detail {

struct ScreenPoint
{
    Vec2 pixel = Vec2::Zero();
    double depth = 0.0; // distance in front of the camera along its viewing direction
    bool inFront = false;
};

inline Mat4 translationTo(const Eigen::Vector3d& origin)
{
    Mat4 translation = Mat4::Identity();
    translation.block<3, 1>(0, 3) = origin;
    return translation;
}

// Projects scene-relative positions to pixels for one camera.
class Projector
{
public:
    Projector(const Camera& camera, const Eigen::Vector3d& origin)
        : m_view(camera.viewMatrix() * translationTo(origin))
        , m_clip(camera.projectionMatrix() * m_view)
        , m_width(camera.viewportWidth())
        , m_height(camera.viewportHeight())
    {}

    [[nodiscard]] ScreenPoint project(const Eigen::Vector3f& relative) const
    {
        return project(Eigen::Vector3d(relative.cast<double>()));
    }

    [[nodiscard]] ScreenPoint project(const Eigen::Vector3d& relative) const
    {
        const Eigen::Vector4d p(relative.x(), relative.y(), relative.z(), 1.0);
        const Eigen::Vector4d clip = m_clip * p;
        ScreenPoint result;
        result.depth = -(m_view.row(2).dot(p));
        result.inFront = clip.w() > 0.0;
        if (result.inFront) {
            const double x = clip.x() / clip.w();
            const double y = clip.y() / clip.w();
            result.pixel = {(x + 1.0) * 0.5 * m_width, (1.0 - y) * 0.5 * m_height};
        }
        return result;
    }

private:
    Mat4 m_view;
    Mat4 m_clip;
    double m_width;
    double m_height;
};

// Distance from p to the segment ab, and the parameter of the closest point.
inline std::pair<double, double> distanceToSegment(const Vec2& p, const Vec2& a, const Vec2& b)
{
    const Vec2 ab = b - a;
    const double length2 = ab.squaredNorm();
    const double u = length2 > 0.0 ? std::clamp((p - a).dot(ab) / length2, 0.0, 1.0) : 0.0;
    return {(p - (a + u * ab)).norm(), u};
}

struct Box2
{
    Vec2 min;
    Vec2 max;

    [[nodiscard]] bool contains(const Vec2& p) const noexcept
    {
        return p.x() >= min.x() && p.x() <= max.x() && p.y() >= min.y() && p.y() <= max.y();
    }
};

// Liang–Barsky: does the segment ab touch the box?
inline bool segmentTouchesBox(const Vec2& a, const Vec2& b, const Box2& box)
{
    double t0 = 0.0;
    double t1 = 1.0;
    const Vec2 d = b - a;
    const std::array<std::pair<double, double>, 4> planes = {{
        {-d.x(), a.x() - box.min.x()},
        {d.x(), box.max.x() - a.x()},
        {-d.y(), a.y() - box.min.y()},
        {d.y(), box.max.y() - a.y()},
    }};
    for (const auto& [p, q] : planes) {
        if (p == 0.0) {
            if (q < 0.0) {
                return false;
            }
            continue;
        }
        const double r = q / p;
        if (p < 0.0) {
            t0 = std::max(t0, r);
        } else {
            t1 = std::min(t1, r);
        }
        if (t0 > t1) {
            return false;
        }
    }
    return true;
}

inline bool pointInTriangle(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c)
{
    const auto cross = [](const Vec2& u, const Vec2& v) { return u.x() * v.y() - u.y() * v.x(); };
    const double d1 = cross(b - a, p - a);
    const double d2 = cross(c - b, p - b);
    const double d3 = cross(a - c, p - c);
    const bool negative = d1 < 0.0 || d2 < 0.0 || d3 < 0.0;
    const bool positive = d1 > 0.0 || d2 > 0.0 || d3 > 0.0;
    return !(negative && positive);
}

} // namespace fem::render::detail
