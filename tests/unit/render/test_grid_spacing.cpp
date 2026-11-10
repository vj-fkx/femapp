#include <fem/render/grid_spacing.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using fem::render::GridSpacing;
using fem::render::gridSpacing;
using fem::render::kGridMinPixelsPerCell;

// REQ-002-21
TEST_CASE("grid spacing is a power of ten with readable cells", "[render][grid]")
{
    // Sweeps fourteen decades of zoom.
    for (int step = 0; step < 100; ++step) {
        const double unitsPerPixel = 1e-7 * std::pow(1.38, step);
        const GridSpacing spacing = gridSpacing(unitsPerPixel);

        const double exponent = std::log10(spacing.minor);
        CHECK_THAT(exponent, WithinAbs(std::round(exponent), 1e-9));
        CHECK_THAT(spacing.major, WithinRel(10.0 * spacing.minor, 1e-12));

        // Cells are at least the minimum size and less than ten times that.
        const double cellPixels = spacing.minor / unitsPerPixel;
        CHECK(cellPixels >= kGridMinPixelsPerCell * (1.0 - 1e-9));
        CHECK(cellPixels < 10.0 * kGridMinPixelsPerCell * (1.0 + 1e-9));

        CHECK(spacing.minorFade >= 0.0);
        CHECK(spacing.minorFade <= 1.0);
    }
}

// REQ-002-21
TEST_CASE("grid spacing never grows while zooming in", "[render][grid]")
{
    double previousMinor = gridSpacing(1e6).minor;
    for (int step = 0; step < 150; ++step) {
        const double unitsPerPixel = 1e6 / std::pow(1.2, step);
        const double minor = gridSpacing(unitsPerPixel).minor;
        CHECK(minor <= previousMinor);
        previousMinor = minor;
    }
}

// REQ-002-21: minor lines reach full opacity right before they become major.
TEST_CASE("grid levels hand over without a visible jump", "[render][grid]")
{
    // Cells are exactly at the minimum size when minor == unitsPerPixel * minimum.
    const double switchPoint = 1.0 / kGridMinPixelsPerCell;

    const GridSpacing zoomedOut = gridSpacing(switchPoint * 1.0001);
    const GridSpacing zoomedIn = gridSpacing(switchPoint * 0.9999);

    CHECK_THAT(zoomedOut.minor, WithinRel(10.0, 1e-9));
    CHECK(zoomedOut.minorFade > 0.99);

    CHECK_THAT(zoomedIn.minor, WithinRel(1.0, 1e-9));
    CHECK(zoomedIn.minorFade < 0.01);
    // The lines that were minor are now the major ones.
    CHECK_THAT(zoomedIn.major, WithinRel(zoomedOut.minor, 1e-9));
}

TEST_CASE("grid spacing tolerates invalid input", "[render][grid]")
{
    CHECK(gridSpacing(0.0).minor == 1.0);
    CHECK(gridSpacing(-1.0).minor == 1.0);
    CHECK(gridSpacing(std::nan("")).minor == 1.0);
}
