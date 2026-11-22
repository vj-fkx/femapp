#include "detail/segment_bvh.hpp"

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>

#include <algorithm>
#include <cstddef>

namespace fem::mesher::detail {

namespace {

// Boxes per leaf.
constexpr std::int32_t kLeafSize = 4;

geom::Vec3 centre(const Box3& box)
{
    return 0.5 * (box.min + box.max);
}

} // namespace

Box3 segmentBox(const geom::Vec3& a, const geom::Vec3& b, double margin)
{
    const geom::Vec3 grow = geom::Vec3::Constant(margin);
    return {.min = a.cwiseMin(b) - grow, .max = a.cwiseMax(b) + grow};
}

SegmentBvh::SegmentBvh(std::span<const Box3> boxes)
    : m_boxes(boxes.begin(), boxes.end())
{
    const auto count = static_cast<std::int32_t>(m_boxes.size());
    m_order.resize(m_boxes.size());
    for (std::int32_t i = 0; i < count; ++i) {
        m_order[static_cast<std::size_t>(i)] = i;
    }
    if (count > 0) {
        m_nodes.reserve(2 * m_boxes.size() / kLeafSize + 2);
        build(0, count);
    }
}

std::int32_t SegmentBvh::build(std::int32_t first, std::int32_t count)
{
    const auto begin = m_order.begin() + first;
    const auto end = begin + count;
    Box3 box = m_boxes[static_cast<std::size_t>(*begin)];
    Box3 centres{.min = centre(box), .max = centre(box)};
    for (auto it = begin; it != end; ++it) {
        const Box3& b = m_boxes[static_cast<std::size_t>(*it)];
        box.min = box.min.cwiseMin(b.min);
        box.max = box.max.cwiseMax(b.max);
        centres.min = centres.min.cwiseMin(centre(b));
        centres.max = centres.max.cwiseMax(centre(b));
    }
    const auto index = static_cast<std::int32_t>(m_nodes.size());
    m_nodes.push_back({.box = box, .first = first, .count = count, .right = -1});
    if (count <= kLeafSize) {
        return index;
    }
    // Split at the median of the centres along their largest extent; ties
    // by index, so that the tree does not depend on the sort.
    Eigen::Index axis = 0;
    (centres.max - centres.min).maxCoeff(&axis);
    const std::int32_t half = count / 2;
    std::nth_element(begin, begin + half, end, [&](std::int32_t a, std::int32_t b) {
        const double ca = centre(m_boxes[static_cast<std::size_t>(a)])(axis);
        const double cb = centre(m_boxes[static_cast<std::size_t>(b)])(axis);
        return ca < cb || (ca == cb && a < b);
    });
    const std::int32_t left = build(first, half);
    const std::int32_t right = build(first + half, count - half);
    Node& node = m_nodes[static_cast<std::size_t>(index)];
    node.first = left;
    node.count = 0;
    node.right = right;
    return index;
}

void SegmentBvh::query(const Box3& box, std::vector<std::int32_t>& out) const
{
    out.clear();
    if (m_nodes.empty()) {
        return;
    }
    std::vector<std::int32_t> stack{0};
    while (!stack.empty()) {
        const Node& node = m_nodes[static_cast<std::size_t>(stack.back())];
        stack.pop_back();
        if (!node.box.overlaps(box)) {
            continue;
        }
        if (node.count > 0) {
            for (std::int32_t k = node.first; k < node.first + node.count; ++k) {
                const std::int32_t i = m_order[static_cast<std::size_t>(k)];
                if (m_boxes[static_cast<std::size_t>(i)].overlaps(box)) {
                    out.push_back(i);
                }
            }
        } else {
            stack.push_back(node.right);
            stack.push_back(node.first);
        }
    }
    std::ranges::sort(out);
}

std::vector<std::pair<std::int32_t, std::int32_t>> SegmentBvh::pairs(int threads) const
{
    // REQ-009-08: each box keeps its own list, joined in order afterwards,
    // so that the result does not depend on the threads.
    std::vector<std::vector<std::int32_t>> partners(m_boxes.size());
    tbb::task_arena arena(threads > 0 ? threads : tbb::task_arena::automatic);
    arena.execute([&] {
        tbb::parallel_for(tbb::blocked_range<std::size_t>(0, m_boxes.size()),
                          [&](const tbb::blocked_range<std::size_t>& range) {
                              std::vector<std::int32_t> found;
                              for (std::size_t i = range.begin(); i != range.end(); ++i) {
                                  query(m_boxes[i], found);
                                  const auto self = static_cast<std::int32_t>(i);
                                  auto& mine = partners[i];
                                  for (const std::int32_t j : found) {
                                      if (j > self) {
                                          mine.push_back(j);
                                      }
                                  }
                              }
                          });
    });
    std::vector<std::pair<std::int32_t, std::int32_t>> out;
    for (std::size_t i = 0; i < partners.size(); ++i) {
        for (const std::int32_t j : partners[i]) {
            out.emplace_back(static_cast<std::int32_t>(i), j);
        }
    }
    return out;
}

} // namespace fem::mesher::detail
