#pragma once

// Feature 009 (design §5.2, NFR-009-01): a static bounding volume hierarchy
// of axis-aligned boxes, to find the pieces of a drawing that may touch
// without testing every pair.

#include <fem/geom/drawing.hpp>

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace fem::mesher::detail {

struct Box3
{
    geom::Vec3 min = geom::Vec3::Zero();
    geom::Vec3 max = geom::Vec3::Zero();

    // Closed boxes: touching faces overlap.
    [[nodiscard]] bool overlaps(const Box3& other) const
    {
        return (min.array() <= other.max.array()).all() && (other.min.array() <= max.array()).all();
    }
};

// The box of the segment a-b grown by `margin` on every side.
[[nodiscard]] Box3 segmentBox(const geom::Vec3& a, const geom::Vec3& b, double margin);

class SegmentBvh
{
public:
    // Built once from the boxes, which it copies; indices refer to them.
    explicit SegmentBvh(std::span<const Box3> boxes);

    [[nodiscard]] std::size_t size() const noexcept { return m_boxes.size(); }

    // The indices of the boxes that overlap `box`, in increasing order,
    // appended to `out` after clearing it.
    void query(const Box3& box, std::vector<std::int32_t>& out) const;

    // Every pair (i, j), i < j, of overlapping boxes, sorted; the same for
    // any number of threads (0: all cores).
    [[nodiscard]] std::vector<std::pair<std::int32_t, std::int32_t>> pairs(int threads = 0) const;

private:
    struct Node
    {
        Box3 box;
        std::int32_t first = 0; // leaf: the first index in m_order; inner: the left child
        std::int32_t count = 0; // leaf: the number of boxes; inner: 0
        std::int32_t right = -1;
    };

    std::int32_t build(std::int32_t first, std::int32_t count);

    std::vector<Box3> m_boxes;
    std::vector<std::int32_t> m_order;
    std::vector<Node> m_nodes;
};

} // namespace fem::mesher::detail
