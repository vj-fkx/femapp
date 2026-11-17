// REQ-008c-02 to 04: the bars with the axial force of the reference state
// (design 008c §5.1 to 5.4).

#include <fem/core/beam_element.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

namespace {

bool near(double value, double reference, double tolerance)
{
    return std::abs(value - reference) <= tolerance * std::max(1.0, std::abs(reference));
}

} // namespace

// REQ-008c-02, 04: the profile of the axial force along the bar.
TEST_CASE("the axial force profile", "[core][beam][second-order]")
{
    using fem::core::AxialProfile;
    const double length = 4.0;
    SECTION("inactive and constant")
    {
        const AxialProfile none;
        CHECK_FALSE(none.active());
        CHECK(none.at(1.0, length) == 0.0);
        CHECK(none.largestCompression() == 0.0);
        const AxialProfile constant = AxialProfile::constant(-3e4);
        CHECK(constant.active());
        CHECK(constant.parts() == 1);
        CHECK(constant.at(2.7, length) == -3e4);
        CHECK(constant.largestCompression() == 3e4);
        CHECK(AxialProfile::constant(5.0).largestCompression() == 0.0);
        CHECK(constant.scaled(2.0).at(0.0, length) == -6e4);
    }
    SECTION("linear within each part, from its Gauss points")
    {
        // N(s) = -1000 + 300 s sampled at the Gauss points of 3 parts: exact.
        const auto n = [](double s) { return -1000.0 + 300.0 * s; };
        AxialProfile profile;
        profile.count = 6;
        for (int part = 0; part < 3; ++part) {
            const auto points = AxialProfile::gaussPoints(part, 3, length);
            CHECK(points[0] > part * length / 3);
            CHECK(points[1] < (part + 1) * length / 3);
            profile.values.at(static_cast<std::size_t>(2 * part)) = n(points[0]);
            profile.values.at(static_cast<std::size_t>(2 * part + 1)) = n(points[1]);
        }
        CHECK(profile.parts() == 3);
        for (const double s : {0.0, 0.4, 1.33, 2.0, 3.1, length}) {
            CHECK(near(profile.at(s, length), n(s), 1e-12));
        }
        // The largest compression is at the start, beyond the first point.
        CHECK(near(profile.largestCompression(), 1000.0, 1e-12));
        CHECK(near(profile.scaled(-1.0).largestCompression(), n(length), 1e-12));
    }
}
