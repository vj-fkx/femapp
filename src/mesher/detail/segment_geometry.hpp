#pragma once

// Feature 009 (design §5.3): how two straight pieces of a drawing touch,
// within the tolerance of the drawing. Every computation is relative to the
// first point of the first piece, so that the precision does not depend on
// the distance to the origin (NFR-009-02).

#include <fem/geom/drawing.hpp>

#include <cstdint>
#include <optional>

namespace fem::mesher::detail {

using geom::Vec3;

// The closest points of the segments a1-b1 and a2-b2: p1 = a1 + s (b1 - a1)
// and p2 = a2 + t (b2 - a2), with s and t in [0, 1] (Ericson, Real-Time
// Collision Detection, §5.1.9).
struct ClosestPoints
{
    double s = 0.0;
    double t = 0.0;
    Vec3 first = Vec3::Zero();
    Vec3 second = Vec3::Zero();
    double distance = 0.0;
};

[[nodiscard]] ClosestPoints closestPoints(const Vec3& a1, const Vec3& b1, const Vec3& a2,
                                          const Vec3& b2);

// The parameter s (p ~ a + s (b - a)) of a point that lies on the interior
// of the segment a-b within `tolerance`: farther than `tolerance` from both
// ends along it. Empty otherwise.
[[nodiscard]] std::optional<double> interiorParameter(const Vec3& p, const Vec3& a, const Vec3& b,
                                                      double tolerance);

enum class ContactKind : std::uint8_t {
    None,     // farther apart than the tolerance
    Ends,     // only end to end: the merge of the nodes joins them
    TJoint,   // an end of one on the interior of the other
    Crossing, // interior to interior
    Overlap   // collinear, sharing a stretch longer than the tolerance
};

// REQ-009-03, 04: the contact of the pieces a1-b1 (first) and a2-b2
// (second), both longer than `tolerance`.
struct Contact
{
    ContactKind kind = ContactKind::None;
    // Where the pieces are split, as parameters along each; a T joint
    // splits one of them, a crossing both.
    std::optional<double> splitFirst;
    std::optional<double> splitSecond;
    // The node: for a T joint, the end that lands on the other piece; for a
    // crossing, the middle of the two closest points.
    Vec3 position = Vec3::Zero();
    // Overlap: the shared stretch, on the first piece.
    Vec3 from = Vec3::Zero();
    Vec3 to = Vec3::Zero();
};

[[nodiscard]] Contact classify(const Vec3& a1, const Vec3& b1, const Vec3& a2, const Vec3& b2,
                               double tolerance);

} // namespace fem::mesher::detail
