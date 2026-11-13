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
    m_endpoints.clear();
    m_segments.clear();
    for (const Entity& entity : drawing.entities()) {
        const auto vertices = verticesOf(entity);
        m_endpoints.insert(m_endpoints.end(), vertices.begin(), vertices.end());
        const auto segments = segmentsOf(entity);
        m_segments.insert(m_segments.end(), segments.begin(), segments.end());
    }
}

std::optional<ResolvedPoint> PointResolver::resolve(const Drawing& drawing, const ScreenView& view,
                                                    const DraftingSettings& settings,
                                                    const std::optional<Vec3>& last) const
{
    // 1. Object snap (REQ-005-19).
    if (settings.objectSnap) {
        refresh(drawing);
        Best best;
        const double aperture = settings.aperturePx;
        if (settings.isOn(SnapMode::Endpoint)) {
            for (const Vec3& p : m_endpoints) {
                best.offer(view, p, SnapMode::Endpoint, aperture);
            }
        }
        if (settings.isOn(SnapMode::Midpoint)) {
            for (const Segment& s : m_segments) {
                best.offer(view, 0.5 * (s.a + s.b), SnapMode::Midpoint, aperture);
            }
        }
        if (settings.isOn(SnapMode::Intersection)) {
            // Only the segments passing under the cursor can cross there.
            std::vector<const Segment*> near;
            for (const Segment& s : m_segments) {
                const auto a = project(view, s.a);
                const auto b = project(view, s.b);
                if (a && b && distanceToSegment(view.cursor, *a, *b) <= aperture) {
                    near.push_back(&s);
                }
            }
            const double tolerance = drawing.tolerance();
            for (std::size_t i = 0; i < near.size(); ++i) {
                for (std::size_t j = i + 1; j < near.size(); ++j) {
                    const ClosestPoints closest =
                        closestPoints(near[i]->a, near[i]->b, near[j]->a, near[j]->b);
                    if (closest.distance <= tolerance) {
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
