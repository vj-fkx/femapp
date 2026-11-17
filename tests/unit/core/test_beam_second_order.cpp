// REQ-008c-02 to 04: the bars with the axial force of the reference state
// (design 008c §5.1 to 5.4).

#include <fem/core/beam_element.hpp>
#include <fem/core/beam_state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Cholesky>
#include <Eigen/LU>
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

namespace {

// A straight bar with E I = 2e6 about both axes (3D: EIy = 1.5e6), E A,
// G J and, for Timoshenko, G As = 4e7.
fem::core::BeamData bar(double n, bool timoshenko = false)
{
    fem::core::BeamData beam;
    beam.axial = 4e9;
    beam.torsional = 1.2e6;
    beam.bendingY = 1.5e6;
    beam.bendingZ = 2e6;
    if (timoshenko) {
        beam.shearY = beam.shearZ = 4e7;
    }
    beam.axialForce = fem::core::AxialProfile::constant(n);
    return beam;
}

// The stability functions of a bar with u = k L (Livesley; Timoshenko and
// Gere §1.11): the rotational stiffness s EI / L at an end with the other
// clamped, and the carry-over c.
struct Stability
{
    double s;
    double c;
};
Stability stability(double u, bool compression)
{
    if (compression) {
        return {u * (std::sin(u) - u * std::cos(u)) / (2.0 - 2.0 * std::cos(u) - u * std::sin(u)),
                (u - std::sin(u)) / (std::sin(u) - u * std::cos(u))};
    }
    return {u * (u * std::cosh(u) - std::sinh(u)) / (2.0 - 2.0 * std::cosh(u) + u * std::sinh(u)),
            (std::sinh(u) - u) / (u * std::cosh(u) - std::sinh(u))};
}

// The tip deflection of a cantilever of length l under a lateral force h
// and an axial force n (Engesser with shear stiffness ga; ga = 0: Euler-
// Bernoulli): compression tan, tension tanh (design 008c §5.1).
double cantileverTip(double l, double ei, double ga, double n, double h)
{
    const double a = ga > 0.0 ? 1.0 + n / ga : 1.0;
    const double k = std::sqrt(std::abs(n) / (ei * a));
    if (n < 0.0) {
        return h / -n * (std::tan(k * l) / (k * a) - l);
    }
    return h / n * (l - std::tanh(k * l) / (k * a));
}

using fem::core::StateBarMatrices;

} // namespace

// REQ-008c-02: the exact bar with a constant axial force against the closed
// forms, in compression and tension, in the plane and in space.
TEST_CASE("the exact bar with an axial force", "[core][beam][second-order]")
{
    const double l = 3.0;
    const fem::core::SpanLoads none;
    SECTION("no axial force is the bar of the 008b")
    {
        fem::core::BeamData first = bar(0.0);
        first.axialForce = {};
        const StateBarMatrices linear = fem::core::stateBarMatrices(first, l, none, false);
        const StateBarMatrices zero = fem::core::stateBarMatrices(bar(0.0), l, none, false);
        CHECK((zero.stiffness - linear.stiffness).norm() <= 1e-12 * linear.stiffness.norm());
        CHECK(zero.negativePivots == 0);
    }
    SECTION("stability functions s and c, in the plane")
    {
        const double ei = 2e6;
        for (const bool compression : {true, false}) {
            for (const double u : {0.3, 1.5, 3.0, 5.0}) {
                const double n = (compression ? -1.0 : 1.0) * u * u * ei / (l * l);
                const StateBarMatrices m = fem::core::stateBarMatrices(bar(n), l, none, true);
                const Stability f = stability(u, compression);
                INFO("u = " << u << (compression ? " compression" : " tension"));
                CHECK(near(m.stiffness(2, 2), f.s * ei / l, 1e-10));
                CHECK(near(m.stiffness(2, 5), f.s * f.c * ei / l, 1e-10));
                CHECK(near(m.stiffness(5, 5), f.s * ei / l, 1e-10));
            }
        }
    }
    SECTION("stability functions in space, about both axes")
    {
        const double u = 2.0;
        const double n = -u * u * 2e6 / (l * l); // u = 2 about z
        const StateBarMatrices m = fem::core::stateBarMatrices(bar(n), l, none, false);
        const Stability z = stability(u, true);
        CHECK(near(m.stiffness(5, 5), z.s * 2e6 / l, 1e-10));
        CHECK(near(m.stiffness(5, 11), z.s * z.c * 2e6 / l, 1e-10));
        const double uy = std::sqrt(-n / 1.5e6) * l;
        const Stability y = stability(uy, true);
        CHECK(near(m.stiffness(4, 4), y.s * 1.5e6 / l, 1e-10));
        CHECK(near(m.stiffness(4, 10), y.s * y.c * 1.5e6 / l, 1e-10));
    }
    SECTION("a cantilever with a lateral force: Euler-Bernoulli and Engesser")
    {
        const double h = 1e3;
        for (const bool timoshenko : {false, true}) {
            for (const double n : {-8e5, -2e5, 3e5, 2e6}) {
                const StateBarMatrices m =
                    fem::core::stateBarMatrices(bar(n, timoshenko), l, none, true);
                const Eigen::Matrix3d k22 = m.stiffness.bottomRightCorner(3, 3);
                const Eigen::Vector3d d = k22.inverse() * Eigen::Vector3d(0.0, h, 0.0);
                INFO("N = " << n << (timoshenko ? " Engesser" : " Euler-Bernoulli"));
                CHECK(near(d(1), cantileverTip(l, 2e6, timoshenko ? 4e7 : 0.0, n, h), 1e-10));
            }
        }
    }
    SECTION("a bar cut in two and condensed is the whole bar")
    {
        for (const double n : {-6e5, 9e5}) {
            const StateBarMatrices whole =
                fem::core::stateBarMatrices(bar(n, true), l, none, false);
            const StateBarMatrices half =
                fem::core::stateBarMatrices(bar(n, true), l / 2, none, false);
            // Condense the middle node of two halves.
            Eigen::MatrixXd k = Eigen::MatrixXd::Zero(18, 18);
            k.topLeftCorner(12, 12) += half.stiffness;
            k.bottomRightCorner(12, 12) += half.stiffness;
            const Eigen::MatrixXd kmm = k.block(6, 6, 6, 6);
            Eigen::MatrixXd kem(12, 6);
            kem << k.block(0, 6, 6, 6), k.block(12, 6, 6, 6);
            Eigen::MatrixXd kee(12, 12);
            kee << k.block(0, 0, 6, 6), k.block(0, 12, 6, 6), k.block(12, 0, 6, 6),
                k.block(12, 12, 6, 6);
            const Eigen::MatrixXd condensed = kee - kem * kmm.ldlt().solve(kem.transpose());
            CHECK((condensed - Eigen::MatrixXd(whole.stiffness)).norm() <=
                  1e-10 * whole.stiffness.norm());
        }
    }
    SECTION("a compression near G As is beyond any part: unstable")
    {
        const StateBarMatrices m =
            fem::core::stateBarMatrices(bar(-0.9 * 4e7, true), l, none, false);
        CHECK(m.negativePivots >= fem::core::kUnstableBar);
    }
    SECTION("the fixed-end moment of a uniform load")
    {
        const double q = 2e3;
        for (const double n : {-9e5, 1.2e6}) {
            fem::core::SpanLoads loads;
            loads.pieces.push_back({.from = 0.0,
                                    .to = l,
                                    .start = Eigen::Vector4d(0.0, -q, 0.0, 0.0),
                                    .end = Eigen::Vector4d(0.0, -q, 0.0, 0.0)});
            const StateBarMatrices m = fem::core::stateBarMatrices(bar(n), l, loads, true);
            const double v = std::sqrt(std::abs(n) / 2e6) * l / 2;
            const double psi = n < 0.0 ? 3.0 * (std::tan(v) - v) / (v * v * std::tan(v))
                                       : 3.0 * (v - std::tanh(v)) / (v * v * std::tanh(v));
            CHECK(near(std::abs(m.loads(2)), q * l * l / 12.0 * psi, 1e-10));
            CHECK(near(std::abs(m.loads(1)), q * l / 2, 1e-10));
        }
    }
}
