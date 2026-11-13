#include <fem/geom/point_resolver.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace fem::geom {

namespace {

using Vec2 = Eigen::Vector2d;

constexpr double kTiny = 1e-12;

// Pixel of a world point, or std::nullopt behind a perspective camera.
std::optional<Vec2> project(const ScreenView& view, const Vec3& p)
{
    const Eigen::Vector4d clip = view.viewProjection * Eigen::Vector4d(p.x(), p.y(), p.z(), 1.0);
    if (!(clip.w() > kTiny)) {
        return std::nullopt;
    }
    const double x = clip.x() / clip.w();
    const double y = clip.y() / clip.w();
    return Vec2((x + 1.0) * 0.5 * view.width, (1.0 - y) * 0.5 * view.height);
}

double distanceToSegment(const Vec2& p, const Vec2& a, const Vec2& b)
{
    const Vec2 ab = b - a;
    const double length2 = ab.squaredNorm();
    const double t = length2 > 0.0 ? std::clamp((p - a).dot(ab) / length2, 0.0, 1.0) : 0.0;
    return (p - (a + t * ab)).norm();
}

// Ranking of candidates: the closest to the cursor; on a tie, endpoint, then
// intersection, then midpoint.
int rank(SnapMode mode)
{
    switch (mode) {
    case SnapMode::Endpoint:
        return 0;
    case SnapMode::Intersection:
        return 1;
    case SnapMode::Midpoint:
        return 2;
    }
    return 3;
}

struct Best
{
    double distancePx = std::numeric_limits<double>::infinity();
    std::optional<ResolvedPoint> point;

    void offer(const ScreenView& view, const Vec3& p, SnapMode mode, double aperturePx)
    {
        const auto pixel = project(view, p);
        if (!pixel) {
            return;
        }
        const double d = (*pixel - view.cursor).norm();
        if (d > aperturePx) {
            return;
        }
        constexpr double kTiePx = 1e-6;
        const bool better =
            !point || d < distancePx - kTiePx ||
            (d <= distancePx + kTiePx && point->snap && rank(mode) < rank(*point->snap));
        if (better) {
            distancePx = d;
            point = ResolvedPoint{.point = p, .snap = mode};
        }
    }
};

} // namespace

ClosestPoints closestPoints(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d)
{
    // Ericson, Real-Time Collision Detection, 5.1.9.
    const Vec3 d1 = b - a;
    const Vec3 d2 = d - c;
    const Vec3 r = a - c;
    const double aa = d1.squaredNorm();
    const double ee = d2.squaredNorm();
    const double f = d2.dot(r);
    double s = 0.0;
    double t = 0.0;
    if (aa <= kTiny && ee <= kTiny) {
        s = 0.0;
        t = 0.0;
    } else if (aa <= kTiny) {
        t = std::clamp(f / ee, 0.0, 1.0);
    } else {
        const double cc = d1.dot(r);
        if (ee <= kTiny) {
            s = std::clamp(-cc / aa, 0.0, 1.0);
        } else {
            const double bb = d1.dot(d2);
            const double denominator = aa * ee - bb * bb;
            s = denominator > kTiny ? std::clamp((bb * f - cc * ee) / denominator, 0.0, 1.0) : 0.0;
            t = (bb * s + f) / ee;
            if (t < 0.0) {
                t = 0.0;
                s = std::clamp(-cc / aa, 0.0, 1.0);
            } else if (t > 1.0) {
                t = 1.0;
                s = std::clamp((bb - cc) / aa, 0.0, 1.0);
            }
        }
    }
    const Vec3 p = a + s * d1;
    const Vec3 q = c + t * d2;
    return {.distance = (p - q).norm(), .onFirst = p, .onSecond = q};
}

void PointResolver::refresh(const Drawing& drawing) const
{
    if (m_drawing == &drawing && m_revision == drawing.revision()) {
        return;
    }
    m_drawing = &drawing;
    m_revision = drawing.revision();
    m_points.clear();
    m_segments.clear();
    for (const Entity& entity : drawing.entities()) {
        const auto base = static_cast<std::uint32_t>(m_points.size());
        const auto vertices = verticesOf(entity);
        m_points.insert(m_points.end(), vertices.begin(), vertices.end());
        // Same pieces, in the same order, as segmentsOf.
        const auto pieces = static_cast<std::uint32_t>(segmentsOf(entity).size());
        const auto count = static_cast<std::uint32_t>(vertices.size());
        for (std::uint32_t k = 0; k < pieces; ++k) {
            m_segments.push_back({base + k, base + (k + 1) % count});
        }
    }
    m_tolerance = drawing.tolerance();
}

std::optional<ResolvedPoint> PointResolver::resolve(const Drawing& drawing, const ScreenView& view,
                                                    const DraftingSettings& settings,
                                                    const std::optional<Vec3>& last) const
{
    // 1. Object snap (REQ-005-19).
    if (settings.objectSnap) {
        refresh(drawing);
        const double aperture = settings.aperturePx;

        // Every vertex projected once.
        m_screen.resize(m_points.size());
        m_visible.resize(m_points.size());
        for (std::size_t k = 0; k < m_points.size(); ++k) {
            const auto pixel = project(view, m_points[k]);
            m_visible[k] = pixel ? 1 : 0;
            m_screen[k] = pixel.value_or(Vec2::Zero());
        }

        Best best;
        if (settings.isOn(SnapMode::Endpoint)) {
            for (std::size_t k = 0; k < m_points.size(); ++k) {
                if (m_visible[k] != 0 && (m_screen[k] - view.cursor).norm() <= aperture) {
                    best.offer(view, m_points[k], SnapMode::Endpoint, aperture);
                }
            }
        }

        // Only the pieces passing under the cursor can have their midpoint or
        // an intersection there.
        std::vector<const std::array<std::uint32_t, 2>*> near;
        for (const auto& segment : m_segments) {
            const auto a = segment[0];
            const auto b = segment[1];
            if (m_visible[a] != 0 && m_visible[b] != 0 &&
                distanceToSegment(view.cursor, m_screen[a], m_screen[b]) <= aperture) {
                near.push_back(&segment);
            }
        }
        if (settings.isOn(SnapMode::Midpoint)) {
            for (const auto* segment : near) {
                best.offer(view, 0.5 * (m_points[(*segment)[0]] + m_points[(*segment)[1]]),
                           SnapMode::Midpoint, aperture);
            }
        }
        if (settings.isOn(SnapMode::Intersection)) {
            for (std::size_t i = 0; i < near.size(); ++i) {
                for (std::size_t j = i + 1; j < near.size(); ++j) {
                    const auto& s = *near[i];
                    const auto& t = *near[j];
                    const ClosestPoints closest = closestPoints(m_points[s[0]], m_points[s[1]],
                                                                m_points[t[0]], m_points[t[1]]);
                    if (closest.distance <= m_tolerance) {
                        best.offer(view, 0.5 * (closest.onFirst + closest.onSecond),
                                   SnapMode::Intersection, aperture);
                    }
                }
            }
        }
        if (best.point) {
            return best.point;
        }
    }

    // 2. The plane z = 0, or the height of the last point with ortho on.
    const double z = settings.ortho && last ? last->z() : 0.0;
    if (std::abs(view.rayDirection.z()) < kTiny) {
        return std::nullopt;
    }
    const double t = (z - view.rayOrigin.z()) / view.rayDirection.z();
    if (!view.orthographic && !(t > 0.0)) {
        return std::nullopt;
    }
    Vec3 p = view.rayOrigin + t * view.rayDirection;
    p.z() = z;

    // 3. Grid (REQ-005-22).
    if (settings.gridSnap && settings.gridStep > 0.0) {
        p.x() = std::round(p.x() / settings.gridStep) * settings.gridStep;
        p.y() = std::round(p.y() / settings.gridStep) * settings.gridStep;
    }

    // 4. Ortho (REQ-005-23): along X or Y from the last point.
    if (settings.ortho && last) {
        const Vec3 delta = p - *last;
        p = std::abs(delta.x()) >= std::abs(delta.y()) ? Vec3(*last + Vec3(delta.x(), 0.0, 0.0))
                                                       : Vec3(*last + Vec3(0.0, delta.y(), 0.0));
    }
    return ResolvedPoint{.point = p, .snap = std::nullopt};
}

} // namespace fem::geom
