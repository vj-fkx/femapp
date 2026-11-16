// Measures the solution of a solid polygon section (NFR-008b-02). Not part
// of ctest: run it on a Release build.
//
// Usage: bench_section [vertices]
//   A wavy outline of `vertices` vertices (default 200) with a circular
//   hole, triangulated with the default size and solved (warping, shear
//   centre, Iw and shear functions).

#include <fem/core/section.hpp>

#include <Eigen/Core>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <numbers>
#include <span>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

int run(int argc, char** argv)
{
    const std::span<char* const> args(argv, static_cast<std::size_t>(argc));
    const int vertices = args.size() > 1 ? std::stoi(args[1]) : 200;
    if (vertices < 8) {
        std::fputs("usage: bench_section [vertices, at least 8]\n", stderr);
        return EXIT_FAILURE;
    }
    fem::core::SolidPolygon shape;
    shape.regions.emplace_back();
    auto& region = shape.regions.front();
    const int holeVertices = 48;
    region.holes.emplace_back();
    for (int i = 0; i < vertices; ++i) {
        const double angle = 2.0 * std::numbers::pi * i / vertices;
        const double radius = 0.2 * (1.0 + 0.1 * std::cos(7.0 * angle));
        region.outline.emplace_back(radius * std::cos(angle), 0.6 * radius * std::sin(angle));
    }
    for (int i = 0; i < holeVertices; ++i) {
        const double angle = -2.0 * std::numbers::pi * i / holeVertices;
        region.holes.front().emplace_back(0.05 * std::cos(angle) + 0.03, 0.04 * std::sin(angle));
    }
    const auto start = Clock::now();
    const auto solution = fem::core::solveSection({.name = "Bench", .shape = shape});
    const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
    if (!solution) {
        std::fprintf(stderr, "%s\n", solution.error().message.c_str());
        return EXIT_FAILURE;
    }
    const fem::core::SectionProperties& p = (*solution)->properties();
    std::printf("%d + %d vertices: solved in %.3f s (A %.6g, J %.6g, Iw %.6g)\n", vertices,
                holeVertices, seconds, p.area, p.torsionConstant, p.warpingConstant);
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
