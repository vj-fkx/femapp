#include "detail/segment_bvh.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

// Feature 009, T04 (design §5.2, NFR-009-01, REQ-009-08): the pairs of the
// hierarchy are those of an independent sweep along x, for any number of
// threads, with pieces of mixed sizes.

namespace {

using fem::mesher::detail::Box3;
using fem::mesher::detail::segmentBox;
using fem::mesher::detail::SegmentBvh;
using Vec3 = fem::geom::Vec3;
using Pair = std::pair<std::int32_t, std::int32_t>;

// Pieces in a cube of side 100: most short (0.1 to 2), some long (up to
// 60), a few tiny (1e-6), from a fixed seed.
std::vector<Box3> randomBoxes(int count, unsigned seed)
{
    std::mt19937 random(seed);
    std::uniform_real_distribution<double> place(0.0, 100.0);
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    std::uniform_real_distribution<double> kind(0.0, 1.0);
    std::vector<Box3> boxes;
    boxes.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const Vec3 a(place(random), place(random), place(random));
        Vec3 d(unit(random), unit(random), unit(random));
        d.normalize();
        const double k = kind(random);
        const double length =
            k < 0.05 ? 1e-6 : (k < 0.15 ? 60.0 * kind(random) : 0.1 + 2.0 * kind(random));
        boxes.push_back(segmentBox(a, a + length * d, 1e-7));
    }
    return boxes;
}

// The reference: sorted by min.x, each box against the ones that start
// before it ends along x.
std::vector<Pair> sweepPairs(const std::vector<Box3>& boxes)
{
    std::vector<std::int32_t> order(boxes.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = static_cast<std::int32_t>(i);
    }
    std::ranges::sort(order, [&](std::int32_t a, std::int32_t b) {
        return boxes[static_cast<std::size_t>(a)].min.x() <
               boxes[static_cast<std::size_t>(b)].min.x();
    });
    std::vector<Pair> pairs;
    for (std::size_t p = 0; p < order.size(); ++p) {
        const Box3& first = boxes[static_cast<std::size_t>(order[p])];
        for (std::size_t q = p + 1; q < order.size(); ++q) {
            const Box3& second = boxes[static_cast<std::size_t>(order[q])];
            if (second.min.x() > first.max.x()) {
                break;
            }
            if (first.overlaps(second)) {
                pairs.emplace_back(std::min(order[p], order[q]), std::max(order[p], order[q]));
            }
        }
    }
    std::ranges::sort(pairs);
    return pairs;
}

} // namespace

TEST_CASE("the box of a segment", "[mesher][bvh]")
{
    const Box3 box = segmentBox(Vec3(1, 5, -2), Vec3(3, 4, -2), 0.5);
    CHECK(box.min == Vec3(0.5, 3.5, -2.5));
    CHECK(box.max == Vec3(3.5, 5.5, -1.5));
    // Closed boxes: touching faces overlap.
    const Box3 next = segmentBox(Vec3(4, 5, -2), Vec3(5, 5, -2), 0.5);
    CHECK(box.overlaps(next));
    CHECK_FALSE(box.overlaps(segmentBox(Vec3(4.1, 5, -2), Vec3(5, 5, -2), 0.5)));
}

TEST_CASE("the pairs of the hierarchy are all the overlapping pairs", "[mesher][bvh]")
{
    for (const int count : {0, 1, 10, 1000, 20000}) {
        const std::vector<Box3> boxes = randomBoxes(count, 2026U + static_cast<unsigned>(count));
        const std::vector<Pair> expected = sweepPairs(boxes);
        const SegmentBvh bvh(boxes);
        CHECK(bvh.size() == boxes.size());
        for (const int threads : {1, 2, 0}) {
            INFO(count << " boxes, " << threads << " threads");
            CHECK(bvh.pairs(threads) == expected);
        }
    }
}

TEST_CASE("a query returns the overlapping boxes in order", "[mesher][bvh]")
{
    const std::vector<Box3> boxes = randomBoxes(3000, 7U);
    const SegmentBvh bvh(boxes);
    std::mt19937 random(11U);
    std::uniform_real_distribution<double> place(0.0, 100.0);
    std::vector<std::int32_t> found;
    for (int q = 0; q < 200; ++q) {
        const Vec3 p(place(random), place(random), place(random));
        const Box3 box = segmentBox(p, p, q % 2 == 0 ? 1e-7 : 3.0);
        bvh.query(box, found);
        std::vector<std::int32_t> expected;
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            if (boxes[i].overlaps(box)) {
                expected.push_back(static_cast<std::int32_t>(i));
            }
        }
        CHECK(found == expected);
    }
}
