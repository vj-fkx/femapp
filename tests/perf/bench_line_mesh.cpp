// Measures the line mesher (NFR-009-01). Not part of ctest: run it on a
// Release build.
//
// Usage: bench_line_mesh [bays] [storeys]
//   A space frame of bays x bays x storeys (default 26 x 26 x 24, about
//   100 000 pieces): each column one line over the whole height, the beams
//   one line per bay ending on the columns (T joints), and an X brace in
//   every bay of every frame line in both directions (crossings, and T
//   joints where the braces end on the columns). The bays are 6 wide and
//   the storeys 3.5 high.

#include "detail/segment_bvh.hpp"

#include <fem/geom/drawing.hpp>
#include <fem/mesher/line_mesher.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using fem::geom::Vec3;

double secondsSince(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

void building(fem::geom::Drawing& drawing, int bays, int storeys)
{
    const double width = 6.0;
    const double height = 3.5;
    fem::geom::DrawingEdit edit;
    const auto line = [&](const Vec3& a, const Vec3& b) {
        edit.added.push_back(fem::geom::Entity{
            .id = drawing.nextId(), .shape = fem::geom::LineEntity{.start = a, .end = b}});
    };
    const auto at = [&](int i, int j, int k) { return Vec3(width * i, width * j, height * k); };
    for (int i = 0; i <= bays; ++i) {
        for (int j = 0; j <= bays; ++j) {
            line(at(i, j, 0), at(i, j, storeys));
        }
    }
    for (int k = 1; k <= storeys; ++k) {
        for (int a = 0; a < bays; ++a) {
            for (int b = 0; b <= bays; ++b) {
                line(at(a, b, k), at(a + 1, b, k));
                line(at(b, a, k), at(b, a + 1, k));
            }
        }
    }
    for (int k = 0; k < storeys; ++k) {
        for (int a = 0; a < bays; ++a) {
            for (int b = 0; b <= bays; ++b) {
                line(at(a, b, k), at(a + 1, b, k + 1));
                line(at(a + 1, b, k), at(a, b, k + 1));
                line(at(b, a, k), at(b, a + 1, k + 1));
                line(at(b, a + 1, k), at(b, a, k + 1));
            }
        }
    }
    drawing.apply(edit);
}

int run(int argc, char** argv)
{
    const int bays = argc > 1 ? std::atoi(argv[1]) : 26;
    const int storeys = argc > 2 ? std::atoi(argv[2]) : 24;
    auto start = Clock::now();
    fem::geom::Drawing drawing;
    building(drawing, bays, storeys);
    std::printf("%zu pieces (%d x %d bays, %d storeys); drawn in %.3f s\n", drawing.size(), bays,
                bays, storeys, secondsSince(start));

    // The search alone (design §5.2): boxes, tree and pairs.
    start = Clock::now();
    std::vector<fem::mesher::detail::Box3> boxes;
    boxes.reserve(drawing.size());
    const double tolerance = drawing.tolerance();
    for (const fem::geom::Entity& entity : drawing.entities()) {
        for (const fem::geom::Segment& s : fem::geom::segmentsOf(entity)) {
            boxes.push_back(fem::mesher::detail::segmentBox(s.a, s.b, tolerance));
        }
    }
    const fem::mesher::detail::SegmentBvh bvh(boxes);
    const double built = secondsSince(start);
    start = Clock::now();
    const auto pairs = bvh.pairs();
    std::printf("search: tree %.3f s, %zu pairs %.3f s\n", built, pairs.size(),
                secondsSince(start));

    // NFR-009-01: the whole generation, best of three.
    double best = 1e300;
    fem::mesher::LineMeshReport report;
    for (int run = 0; run < 3; ++run) {
        start = Clock::now();
        auto result = fem::mesher::meshLines(drawing);
        const double seconds = secondsSince(start);
        if (!result) {
            std::fprintf(stderr, "meshing failed (kind %d)\n",
                         static_cast<int>(result.error().kind));
            return 1;
        }
        best = std::min(best, seconds);
        report = std::move(result->report);
    }
    std::printf("meshLines: %.3f s (best of 3); %d nodes, %d elements, %zu splits, %d merged\n",
                best, report.nodes, report.elements, report.splits.size(), report.mergedNodes);
    std::printf("target (NFR-009-01): <= 2 s: %s\n", best <= 2.0 ? "met" : "NOT met");
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
}
