#include "detail/segment_geometry.hpp"

#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>

namespace fem::mesher::detail {

namespace {

// sin^2 of the angle under which two directions count as parallel for the
// closest points (Ericson's denominator test, made relative).
constexpr double kParallelSine2 = 1e-24;

// The closest points of the segments 0-d1 and r2-(r2 + d2), the first
// point of the first segment moved to the origin.
ClosestPoints closestRelative(const Vec3& d1, const Vec3& r2, const Vec3& d2)
{
    const Vec3 r = -r2; // first start minus second start
    const double a = d1.squaredNorm();
    const double e = d2.squaredNorm();
    const double f = d2.dot(r);
    double s = 0.0;
    double t = 0.0;
    if (a > 0.0 && e > 0.0) {
        const double b = d1.dot(d2);
        const double c = d1.dot(r);
        const double denom = a * e - b * b;
        if (denom > kParallelSine2 * a * e) {
            s = std::clamp((b * f - c * e) / denom, 0.0, 1.0);
        }
        t = (b * s + f) / e;
        if (t < 0.0) {
            t = 0.0;
            s = std::clamp(-c / a, 0.0, 1.0);
        } else if (t > 1.0) {
            t = 1.0;
            s = std::clamp((b - c) / a, 0.0, 1.0);
        }
    } else if (a > 0.0) {
        s = std::clamp(-d1.dot(r) / a, 0.0, 1.0);
    } else if (e > 0.0) {
        t = std::clamp(f / e, 0.0, 1.0);
    }
    ClosestPoints out;
    out.s = s;
    out.t = t;
    out.first = s * d1;
    out.second = r2 + t * d2;
    out.distance = (out.first - out.second).norm();
    return out;
}

// Interior of a piece of length `length` at the parameter s: farther than
// `tolerance` from both ends.
bool interior(double s, double length, double tolerance)
{
    return s * length > tolerance && (1.0 - s) * length > tolerance;
}

} // namespace

ClosestPoints closestPoints(const Vec3& a1, const Vec3& b1, const Vec3& a2, const Vec3& b2)
{
    ClosestPoints out = closestRelative(b1 - a1, a2 - a1, b2 - a2);
    out.first += a1;
    out.second += a1;
    return out;
}

std::optional<double> interiorParameter(const Vec3& p, const Vec3& a, const Vec3& b,
                                        double tolerance)
{
    const Vec3 d = b - a;
    const double length2 = d.squaredNorm();
    if (length2 <= 0.0) {
        return std::nullopt;
    }
    const Vec3 r = p - a;
    const double s = r.dot(d) / length2;
    if (!interior(s, std::sqrt(length2), tolerance) || (r - s * d).norm() > tolerance) {
        return std::nullopt;
    }
    return s;
}

Contact classify(const Vec3& a1, const Vec3& b1, const Vec3& a2, const Vec3& b2, double tolerance)
{
    // Relative to a1 (NFR-009-02).
    const Vec3 d1 = b1 - a1;
    const Vec3 r2 = a2 - a1;
    const Vec3 s2 = b2 - a1;
    const Vec3 d2 = b2 - a2;
    const double l1 = d1.norm();
    const double l2 = d2.norm();
    Contact out;

    // Parallel within the tolerance: the shorter piece turns away from the
    // other by less than the tolerance along its length.
    if (d1.cross(d2).norm() <= tolerance * std::max(l1, l2)) {
        const Vec3 u = d1 / l1;
        const double ta = r2.dot(u);
        const double tb = s2.dot(u);
        const double offA = (r2 - ta * u).norm();
        const double offB = (s2 - tb * u).norm();
        if (offA <= tolerance && offB <= tolerance) {
            // Collinear: a shared stretch is an overlap (REQ-009-04).
            const double lo = std::max(0.0, std::min(ta, tb));
            const double hi = std::min(l1, std::max(ta, tb));
            if (hi - lo > tolerance) {
                out.kind = ContactKind::Overlap;
                out.from = a1 + lo * u;
                out.to = a1 + hi * u;
                return out;
            }
            const bool touch = (r2).norm() <= tolerance || (s2).norm() <= tolerance ||
                               (r2 - d1).norm() <= tolerance || (s2 - d1).norm() <= tolerance;
            out.kind = touch ? ContactKind::Ends : ContactKind::None;
            return out;
        }
    }

    const ClosestPoints near = closestRelative(d1, r2, d2);
    if (near.distance > tolerance) {
        return out;
    }
    const bool inFirst = interior(near.s, l1, tolerance);
    const bool inSecond = interior(near.t, l2, tolerance);
    if (inFirst && inSecond) {
        out.kind = ContactKind::Crossing;
        out.splitFirst = near.s;
        out.splitSecond = near.t;
        out.position = a1 + 0.5 * (near.first + near.second);
    } else if (inFirst) {
        // An end of the second on the first: the node is that end, and the
        // first is split where it projects.
        const Vec3& end = near.t < 0.5 ? r2 : s2;
        out.kind = ContactKind::TJoint;
        out.splitFirst = std::clamp(end.dot(d1) / (l1 * l1), 0.0, 1.0);
        out.position = a1 + end;
    } else if (inSecond) {
        const Vec3 end = near.s < 0.5 ? Vec3::Zero() : d1;
        out.kind = ContactKind::TJoint;
        out.splitSecond = std::clamp((end - r2).dot(d2) / (l2 * l2), 0.0, 1.0);
        out.position = a1 + end;
    } else {
        out.kind = ContactKind::Ends;
    }
    return out;
}

} // namespace fem::mesher::detail
