// Measures the object snap search with a large drawing (NFR-005-01). Not part
// of ctest: run it on a Release build.
//
// Usage: bench_drafting [segments]   (default 100000)

#include <fem/geom/drawing.hpp>
#include <fem/geom/point_resolver.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <optional>
#include <random>
#include <span>
#include <string>

namespace {

using Clock = std::chrono::steady_clock;
using fem::geom::Vec3;

constexpr double kSize = 1000.0; // drawing extent, world units
constexpr double kWidth = 1920.0;
constexpr double kHeight = 1080.0;
constexpr int kMoves = 200;

fem::geom::ScreenView topView(double x, double y)
{
    // Orthographic view from above framing the whole drawing.
    const double halfHeight = 0.55 * kSize;
    const double halfWidth = halfHeight * kWidth / kHeight;
    const double cx = 0.5 * kSize;
    const double cy = 0.5 * kSize;
    fem::geom::ScreenView view;
    view.viewProjection << 1.0 / halfWidth, 0, 0, -cx / halfWidth, //
        0, 1.0 / halfHeight, 0, -cy / halfHeight,                  //
        0, 0, -1e-4, 0,                                            //
        0, 0, 0, 1;
    view.width = kWidth;
    view.height = kHeight;
    view.orthographic = true;
    view.rayOrigin = Vec3(x, y, 100.0);
    view.rayDirection = -Vec3::UnitZ();
    view.cursor = {((x - cx) / halfWidth + 1.0) * 0.5 * kWidth,
                   (1.0 - (y - cy) / halfHeight) * 0.5 * kHeight};
    return view;
}

int run(int argc, char** argv)
{
    const std::span<char* const> args(argv, static_cast<std::size_t>(argc));
    const int segments = args.size() > 1 ? std::stoi(args[1]) : 100000;

    // Random short lines over the square: many crossings, as in a dense frame.
    std::mt19937 random(42);
    std::uniform_real_distribution<double> position(0.0, kSize);
    std::uniform_real_distribution<double> offset(-5.0, 5.0);
    fem::geom::Drawing drawing;
    fem::geom::DrawingEdit edit;
    edit.added.reserve(static_cast<std::size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        const Vec3 a(position(random), position(random), 0.0);
        const Vec3 b = a + Vec3(offset(random), offset(random), 0.0);
        edit.added.push_back(
            {.id = drawing.nextId(), .shape = fem::geom::LineEntity{.start = a, .end = b}});
    }
    drawing.apply(edit);

    const fem::geom::PointResolver resolver;
    const fem::geom::DraftingSettings settings;

    auto start = Clock::now();
    (void)resolver.resolve(drawing, topView(500.0, 500.0), settings, std::nullopt);
    const double first = std::chrono::duration<double>(Clock::now() - start).count();

    int snapped = 0;
    start = Clock::now();
    for (int i = 0; i < kMoves; ++i) {
        const double t = static_cast<double>(i) / kMoves;
        const auto point = resolver.resolve(drawing, topView(200.0 + 600.0 * t, 300.0 + 400.0 * t),
                                            settings, std::nullopt);
        snapped += point && point->snap ? 1 : 0;
    }
    const double each = std::chrono::duration<double>(Clock::now() - start).count() / kMoves;

    std::printf("%d segments\n  first resolve (builds the cache) %.2f ms\n"
                "  resolve per mouse move %.2f ms (%d of %d moves snapped)\n",
                segments, first * 1e3, each * 1e3, snapped, kMoves);
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
