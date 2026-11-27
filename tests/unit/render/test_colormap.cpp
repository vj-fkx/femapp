// Feature 012 (design §5.5; REQ-012-11, 12): the colors of a result.

#include <fem/render/colormap.hpp>

#include <QImage>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <set>

using fem::render::Rgba;

namespace {

constexpr Rgba rgb(std::uint32_t r, std::uint32_t g, std::uint32_t b)
{
    return r | (g << 8U) | (b << 16U) | (0xFFU << 24U);
}

} // namespace

// REQ-012-12: blue at the minimum, red at the maximum, through cyan, green
// and yellow.
TEST_CASE("the color scale", "[render][colormap]")
{
    using fem::render::colormapColor;
    CHECK(colormapColor(0.0) == rgb(0, 0, 255));
    CHECK(colormapColor(0.25) == rgb(0, 255, 255));
    CHECK(colormapColor(0.5) == rgb(0, 255, 0));
    CHECK(colormapColor(0.75) == rgb(255, 255, 0));
    CHECK(colormapColor(1.0) == rgb(255, 0, 0));
    CHECK(colormapColor(0.125) == rgb(0, 128, 255));
    // Clamped outside [0, 1].
    CHECK(colormapColor(-3.0) == colormapColor(0.0));
    CHECK(colormapColor(7.0) == colormapColor(1.0));
}

// REQ-012-11, 12, AC-3: bands, limits and values outside the range.
TEST_CASE("the bands of a range", "[render][colormap]")
{
    using fem::render::bandOf;
    using fem::render::valueColor;
    SECTION("N bands give exactly N colors")
    {
        for (const int bands : {2, 5, 10, 11}) {
            CAPTURE(bands);
            std::set<Rgba> colors;
            for (int i = 0; i <= 1000; ++i) {
                colors.insert(valueColor(-2.0 + 6.0 * i / 1000.0, -2.0, 4.0, bands));
            }
            CHECK(colors.size() == static_cast<std::size_t>(bands));
        }
    }
    SECTION("a value on a limit goes to the band above, the maximum to the last")
    {
        CHECK(bandOf(0.0, 0.0, 10.0, 5) == 0);
        CHECK(bandOf(1.999, 0.0, 10.0, 5) == 0);
        CHECK(bandOf(2.0, 0.0, 10.0, 5) == 1);
        CHECK(bandOf(8.0, 0.0, 10.0, 5) == 4);
        CHECK(bandOf(10.0, 0.0, 10.0, 5) == 4);
        CHECK(valueColor(2.0, 0.0, 10.0, 5) == valueColor(3.0, 0.0, 10.0, 5));
        CHECK(valueColor(2.0, 0.0, 10.0, 5) != valueColor(1.9, 0.0, 10.0, 5));
    }
    SECTION("outside the range, the colors of the ends")
    {
        CHECK(valueColor(-50.0, 0.0, 10.0, 0) == fem::render::colormapColor(0.0));
        CHECK(valueColor(50.0, 0.0, 10.0, 0) == fem::render::colormapColor(1.0));
        CHECK(valueColor(-50.0, 0.0, 10.0, 5) == valueColor(0.0, 0.0, 10.0, 5));
        CHECK(valueColor(50.0, 0.0, 10.0, 5) == valueColor(10.0, 0.0, 10.0, 5));
        CHECK(bandOf(-50.0, 0.0, 10.0, 5) == 0);
        CHECK(bandOf(50.0, 0.0, 10.0, 5) == 4);
    }
    SECTION("the colors of the bands are those of their centres")
    {
        CHECK(valueColor(0.5, 0.0, 10.0, 5) == fem::render::colormapColor(0.1));
        CHECK(valueColor(9.0, 0.0, 10.0, 5) == fem::render::colormapColor(0.9));
    }
    SECTION("continuous, an empty range, a value that is not finite")
    {
        CHECK(valueColor(2.5, 0.0, 10.0, 0) == fem::render::colormapColor(0.25));
        CHECK(valueColor(3.0, 3.0, 3.0, 0) == fem::render::colormapColor(0.0));
        CHECK(bandOf(3.0, 3.0, 3.0, 5) == 0);
        CHECK(valueColor(std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0, 0) ==
              fem::render::noValueColor());
    }
}

// The texture of the shader follows the scale.
TEST_CASE("the color scale texture", "[render][colormap]")
{
    const QImage image = fem::render::colormapImage();
    REQUIRE(image.width() == 256);
    REQUIRE(image.height() == 1);
    for (const int i : {0, 64, 128, 200, 255}) {
        CAPTURE(i);
        const Rgba c = fem::render::colormapColor(i / 255.0);
        const QRgb pixel = image.pixel(i, 0);
        CHECK(qRed(pixel) == static_cast<int>(c & 0xFFU));
        CHECK(qGreen(pixel) == static_cast<int>((c >> 8U) & 0xFFU));
        CHECK(qBlue(pixel) == static_cast<int>((c >> 16U) & 0xFFU));
        CHECK(qAlpha(pixel) == 255);
    }
}
