// REQ-008c-02 to 04: the bars with the axial force of the reference state
// (design 008c §5.1 to 5.4).

#include <fem/core/beam_element.hpp>
#include <fem/core/beam_state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Cholesky>
#include <Eigen/Eigenvalues>
#include <Eigen/LU>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>

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
            profile.values.at(2 * static_cast<std::size_t>(part)) = n(points[0]);
            profile.values.at(2 * static_cast<std::size_t>(part) + 1) = n(points[1]);
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
        return {.s = u * (std::sin(u) - u * std::cos(u)) /
                     (2.0 - 2.0 * std::cos(u) - u * std::sin(u)),
                .c = (u - std::sin(u)) / (std::sin(u) - u * std::cos(u))};
    }
    return {.s = u * (u * std::cosh(u) - std::sinh(u)) /
                 (2.0 - 2.0 * std::cosh(u) + u * std::sinh(u)),
            .c = (std::sinh(u) - u) / (u * std::cosh(u) - std::sinh(u))};
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

namespace {

// A simply supported bar in the plane (u1, v1, v2, u2 held; both rotations
// free), solved with the exact stiffness; the local dofs of its ends.
fem::core::StateBarVector simplySupported(const fem::core::BeamData& beam, double l,
                                          const fem::core::SpanLoads& loads)
{
    const StateBarMatrices m = fem::core::stateBarMatrices(beam, l, loads, true);
    Eigen::Matrix2d k;
    k << m.stiffness(2, 2), m.stiffness(2, 5), m.stiffness(5, 2), m.stiffness(5, 5);
    const Eigen::Vector2d rotations = k.inverse() * Eigen::Vector2d(m.loads(2), m.loads(5));
    fem::core::StateBarVector ends = fem::core::StateBarVector::Zero(6);
    ends(2) = rotations(0);
    ends(5) = rotations(1);
    return ends;
}

} // namespace

// REQ-008c-02, 06, AC-1: the beam-column of Timoshenko and Gere along the
// bar, and the second order on an elastic foundation.
TEST_CASE("beam-columns along the bar", "[core][beam][second-order]")
{
    const double l = 4.0;
    const double ei = 2e6;
    SECTION("simply supported under a uniform load: compression and tension")
    {
        const double q = 3e3;
        fem::core::SpanLoads loads;
        loads.pieces.push_back({.from = 0.0,
                                .to = l,
                                .start = Eigen::Vector4d(0.0, -q, 0.0, 0.0),
                                .end = Eigen::Vector4d(0.0, -q, 0.0, 0.0)});
        for (const double n : {-5e5, -1.1e6, 8e5}) {
            const fem::core::BeamData beam = bar(n);
            const auto ends = simplySupported(beam, l, loads);
            const auto middle = fem::core::stateAt(beam, l, loads, true, ends, l / 2);
            const double u = std::sqrt(std::abs(n) / ei) * l / 2;
            const double u2 = u * u;
            const double eta = n < 0.0 ? 12.0 * (2.0 / std::cos(u) - 2.0 - u2) / (5.0 * u2 * u2)
                                       : 12.0 * (2.0 / std::cosh(u) - 2.0 + u2) / (5.0 * u2 * u2);
            const double moment = n < 0.0 ? 2.0 * (1.0 / std::cos(u) - 1.0) / u2
                                          : 2.0 * (1.0 - 1.0 / std::cosh(u)) / u2;
            INFO("N = " << n);
            CHECK(near(std::abs(middle.displacement.y()),
                       5.0 * q * l * l * l * l / (384.0 * ei) * eta, 1e-10));
            CHECK(near(std::abs(middle.moment.z()), q * l * l / 8.0 * moment, 1e-10));
            // No moment at the pinned ends; the axial force carried along.
            CHECK(std::abs(fem::core::stateAt(beam, l, loads, true, ends, 0.0).moment.z()) <=
                  1e-8 * q * l * l);
        }
    }
    SECTION("simply supported under a load at the middle")
    {
        const double p = 4e4;
        fem::core::SpanLoads loads;
        loads.points.push_back({.at = l / 2, .force = Eigen::Vector3d(0.0, -p, 0.0)});
        for (const double n : {-7e5, 6e5}) {
            const fem::core::BeamData beam = bar(n);
            const auto ends = simplySupported(beam, l, loads);
            const auto middle = fem::core::stateAt(beam, l, loads, true, ends, l / 2);
            const double u = std::sqrt(std::abs(n) / ei) * l / 2;
            const double delta = n < 0.0 ? 3.0 * (std::tan(u) - u) / (u * u * u)
                                         : 3.0 * (u - std::tanh(u)) / (u * u * u);
            const double moment = n < 0.0 ? std::tan(u) / u : std::tanh(u) / u;
            CHECK(near(std::abs(middle.displacement.y()), p * l * l * l / (48.0 * ei) * delta,
                       1e-10));
            CHECK(near(std::abs(middle.moment.z()), p * l / 4.0 * moment, 1e-10));
        }
    }
    SECTION("the cantilever: the moment at the root is H L + P delta")
    {
        const double h = 1e3;
        const double n = -2e5; // below pi^2 EI / 4 l^2
        const StateBarMatrices m = fem::core::stateBarMatrices(bar(n), l, {}, true);
        const Eigen::Matrix3d k22 = m.stiffness.bottomRightCorner(3, 3);
        const Eigen::Vector3d tip = k22.inverse() * Eigen::Vector3d(0.0, h, 0.0);
        fem::core::StateBarVector ends = fem::core::StateBarVector::Zero(6);
        ends.tail(3) = tip;
        const auto root = fem::core::stateAt(bar(n), l, {}, true, ends, 0.0);
        CHECK(near(std::abs(root.moment.z()), h * l - n * tip(1), 1e-10));
        CHECK(near(tip(1), cantileverTip(l, ei, 0.0, n, h), 1e-10));
    }
    SECTION("on an elastic foundation: the critical load of Hetenyi")
    {
        // Pinned ends: N_cr = min over m of EI (m pi / l)^2 + k (l / m pi)^2.
        const double k = 8e6;
        const double pi = std::numbers::pi;
        double critical = 1e300;
        int waves = 0;
        for (int m = 1; m <= 6; ++m) {
            const double value = ei * std::pow(m * pi / l, 2) + k * std::pow(l / (m * pi), 2);
            if (value < critical) {
                critical = value;
                waves = m;
            }
        }
        REQUIRE(waves > 1); // the foundation shortens the waves
        // The pinned bar is stable just below and unstable just above: the
        // determinant of the rotational stiffness plus the internal count.
        const auto unstable = [&](double n) {
            fem::core::BeamData beam = bar(n);
            beam.foundation = Eigen::Vector4d(0.0, k, k, 0.0);
            const StateBarMatrices m = fem::core::stateBarMatrices(beam, l, {}, true);
            Eigen::Matrix2d kr;
            kr << m.stiffness(2, 2), m.stiffness(2, 5), m.stiffness(5, 2), m.stiffness(5, 5);
            const Eigen::SelfAdjointEigenSolver<Eigen::Matrix2d> eigen(kr);
            return m.negativePivots + (eigen.eigenvalues().array() < 0.0).count();
        };
        CHECK(unstable(-critical * (1.0 - 1e-6)) == 0);
        CHECK(unstable(-critical * (1.0 + 1e-6)) == 1);
    }
}

namespace {

// A profile of `parts` parts sampled from n(s) at the Gauss points.
template <typename F>
fem::core::AxialProfile sampled(const F& n, int parts, double length)
{
    fem::core::AxialProfile profile;
    profile.count = 2 * parts;
    for (int part = 0; part < parts; ++part) {
        const auto points = fem::core::AxialProfile::gaussPoints(part, parts, length);
        profile.values.at(2 * static_cast<std::size_t>(part)) = n(points[0]);
        profile.values.at(2 * static_cast<std::size_t>(part) + 1) = n(points[1]);
    }
    return profile;
}

// Greenhill's column (a cantilever under its own weight q per length):
// beta = q L^3 / EI makes theta = sum c_k x^(3k), from the free end, vanish
// at the clamped one: f(beta) = sum (-beta)^k / prod 3j (3j - 1) = 0.
double greenhill()
{
    const auto f = [](double beta) {
        double term = 1.0;
        double sum = 1.0;
        for (int k = 1; k < 60; ++k) {
            term *= -beta / (3.0 * k * (3.0 * k - 1.0));
            sum += term;
        }
        return sum;
    };
    double low = 6.0;
    double high = 9.0;
    for (int i = 0; i < 200; ++i) {
        const double middle = (low + high) / 2.0;
        (f(low) > 0.0) == (f(middle) > 0.0) ? low = middle : high = middle;
    }
    return (low + high) / 2.0;
}

} // namespace

// REQ-008c-02, NFR-008c-01: an axial force that varies along the bar, by
// the fourth-order Magnus integrator (design 008c §5.2).
TEST_CASE("an axial force that varies along the bar", "[core][beam][second-order]")
{
    const double l = 5.0;
    const fem::core::SpanLoads none;
    SECTION("equal values are the constant profile")
    {
        fem::core::BeamData varying = bar(0.0, true);
        varying.axialForce = sampled([](double) { return -4e5; }, 3, l);
        const StateBarMatrices a = fem::core::stateBarMatrices(varying, l, none, false);
        const StateBarMatrices b = fem::core::stateBarMatrices(bar(-4e5, true), l, none, false);
        CHECK((a.stiffness - b.stiffness).norm() <= 1e-10 * b.stiffness.norm());
    }
    SECTION("a linear force in one part or in three is the same bar")
    {
        const auto n = [l](double s) { return -6e5 + 2e5 * s / l; };
        fem::core::BeamData one = bar(0.0);
        one.axialForce = sampled(n, 1, l);
        fem::core::BeamData three = bar(0.0);
        three.axialForce = sampled(n, 3, l);
        const StateBarMatrices a = fem::core::stateBarMatrices(one, l, none, true);
        const StateBarMatrices b = fem::core::stateBarMatrices(three, l, none, true);
        CHECK((a.stiffness - b.stiffness).norm() <= 1e-8 * b.stiffness.norm());
    }
    SECTION("Greenhill's column: buckling under its own weight")
    {
        const double ei = 2e6;
        const double reference = greenhill();
        CHECK(std::abs(reference - 7.8373474) < 1e-6);
        // Clamped at s = 0, free at s = l: N(s) = -q (l - s).
        const auto unstable = [&](double beta) {
            const double q = beta * ei / (l * l * l);
            fem::core::BeamData beam = bar(0.0);
            beam.axialForce = sampled([&](double s) { return -q * (l - s); }, 1, l);
            const StateBarMatrices m = fem::core::stateBarMatrices(beam, l, none, true);
            const Eigen::Matrix3d k22 = m.stiffness.bottomRightCorner(3, 3);
            const Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> eigen(k22);
            return m.negativePivots + (eigen.eigenvalues().array() < 0.0).count() > 0;
        };
        double low = 5.0;
        double high = 10.0;
        REQUIRE_FALSE(unstable(low));
        REQUIRE(unstable(high));
        for (int i = 0; i < 60; ++i) {
            const double middle = (low + high) / 2.0;
            unstable(middle) ? high = middle : low = middle;
        }
        INFO("Greenhill " << reference << ", engine " << low);
        CHECK(std::abs(low - reference) <= 1e-8 * reference);
    }
}

// REQ-008c-08, 10: the junctions of the parts count the buckling loads of
// the bar with both ends clamped below N (design 008c §5.4).
TEST_CASE("the negative pivots of the parts", "[core][beam][second-order]")
{
    const double l = 6.0;
    const double ei = 2e6;
    // Clamped-clamped: 4 pi^2, 80.763 (tan(u/2) = u/2), 16 pi^2 times EI/l^2.
    const double pi = std::numbers::pi;
    const double first = 4.0 * pi * pi;
    const double second = 80.763;
    const double third = 16.0 * pi * pi;
    const auto count = [&](double factor, bool planar) {
        return fem::core::stateBarMatrices(bar(-factor * ei / (l * l)), l, {}, planar)
            .negativePivots;
    };
    SECTION("in the plane")
    {
        CHECK(count(0.5 * first, true) == 0);
        CHECK(count(0.99 * first, true) == 0);
        CHECK(count(1.01 * first, true) == 1);
        CHECK(count(0.99 * second, true) == 1);
        CHECK(count(1.01 * second, true) == 2);
        CHECK(count(1.01 * third, true) == 3);
        CHECK(fem::core::stateBarMatrices(bar(5e6), l, {}, true).negativePivots == 0);
    }
    SECTION("in space: both planes (EIy = 1.5e6 buckles first)")
    {
        // About y the factors are times 1.5e6 / 2e6.
        CHECK(count(0.99 * 0.75 * first, false) == 0);
        CHECK(count(1.01 * 0.75 * first, false) == 1);
        CHECK(count(1.01 * first, false) == 2);
    }
}

namespace {

// The 2D exact element of one bar from (0, 0, 0) to (x2, 0, 0); its count of
// negative pivots in `count`.
Eigen::MatrixXd planarElement(const fem::core::BeamData& beam, double x2, int& count)
{
    const std::array<fem::core::BeamData, 1> beams = {beam};
    const std::array<fem::core::SpanLoads, 1> loads{};
    std::array<int, 1> counts = {-1};
    const fem::core::ExactBeamElement element(fem::core::ElementKind::Beam2D, beams, loads, counts);
    fem::core::ElementNodes nodes;
    nodes.type = fem::core::ElementType::Line2;
    nodes.count = 2;
    nodes.x.at(0) = Eigen::Vector3d::Zero();
    nodes.x.at(1) = Eigen::Vector3d(x2, 0.0, 0.0);
    Eigen::MatrixXd k = Eigen::MatrixXd::Zero(6, 6);
    Eigen::VectorXd f = Eigen::VectorXd::Zero(6);
    REQUIRE(element.compute(nodes, 0, k, f).has_value());
    count = counts[0];
    return k;
}

} // namespace

// REQ-008c-02, 08, 10: the exact element with an axial force: the rigid
// offset in second order and the count of its connections.
TEST_CASE("the exact element with an axial force", "[core][beam][second-order]")
{
    const double l = 4.0;
    const double ei = 2e6;
    SECTION("a cantilever with a rigid offset at its tip: P-Delta of the arm")
    {
        const double p = 2e5;
        const double a = 0.5;
        const double h = 1e3;
        fem::core::BeamData beam = bar(-p);
        beam.offsets.at(1) = Eigen::Vector3d(-a, 0.0, 0.0); // node at l + a
        int count = 0;
        const Eigen::MatrixXd k = planarElement(beam, l + a, count);
        CHECK(count == 0);
        const Eigen::Matrix3d k22 = k.bottomRightCorner(3, 3);
        const Eigen::Vector3d node = k22.inverse() * Eigen::Vector3d(0.0, h, 0.0);
        // The flexible cantilever under the shear h and the moment
        // m = h a + p a theta at its tip, the arm turning with it.
        const double kk = std::sqrt(p / ei);
        const double u = kk * l;
        const double c1 = (1.0 - std::cos(u)) / (p * std::cos(u));
        const double c2 = std::tan(u) / (kk * ei);
        const double d1 = (std::tan(u) - u) / (p * kk);
        const double theta = h * (c1 + a * c2) / (1.0 - p * a * c2);
        const double m = h * a + p * a * theta;
        const double tip = h * d1 + m * c1;
        CHECK(near(node(1), tip + a * theta, 1e-10));
        CHECK(near(node(2), theta, 1e-10));
    }
    SECTION("released ends count the buckling of the pinned bar")
    {
        const double euler = std::numbers::pi * std::numbers::pi * ei / (l * l);
        for (const auto& [factor, expected] : {std::pair{0.99, 0}, std::pair{1.01, 1}}) {
            fem::core::BeamData beam = bar(-factor * euler);
            beam.springs.at(0).at(5) = 0.0;
            beam.springs.at(1).at(5) = 0.0;
            int count = -1;
            (void)planarElement(beam, l, count);
            CHECK(count == expected);
        }
    }
}
