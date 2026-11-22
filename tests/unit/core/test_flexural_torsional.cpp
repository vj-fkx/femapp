// REQ-008d-10, 11 (design 008d §5.5, ADR 0008): the flexural-torsional
// terms of the exact bar, from the second variation of a quadratic
// functional: the stiffness stays symmetric, the bending planes with N only
// are those of the 008c, and the bar cut in two is the whole bar.

#include "beam_models.hpp"
#include "detail/static_stages.hpp"

#include <fem/core/beam_element.hpp>
#include <fem/core/beam_loads.hpp>
#include <fem/core/beam_state.hpp>
#include <fem/core/linear_solver.hpp>
#include <fem/core/mesh_builder.hpp>
#include <fem/core/stability_analysis.hpp>
#include <fem/core/static_analysis.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <Eigen/Eigenvalues>
#include <Eigen/LU>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using fem::core::BeamData;
using fem::core::ReferencePoint;
using fem::core::ReferenceProfile;
using fem::core::SpanLoads;
using fem::core::StateBarMatrix;

namespace {

// A bar of an I-like section with its shear centre off the centroid, the
// Wagner properties of a monosymmetric section, and EIw when `warping`.
BeamData sample(bool warping)
{
    BeamData beam;
    beam.axial = 4.0e9;
    beam.torsional = 2.0e5;
    beam.bendingY = 3.0e6;
    beam.bendingZ = 9.0e6;
    beam.shearCentre = Eigen::Vector2d(0.03, -0.01);
    beam.polarRadius2 = 0.02;
    beam.monosymmetryY = 0.05;
    beam.monosymmetryZ = -0.12;
    if (warping) {
        beam.warpingStiffness = 8.0e4;
    }
    return beam;
}

std::shared_ptr<const ReferenceProfile>
profileOf(const ReferencePoint& start, const ReferencePoint& end, int parts, double length)
{
    ReferenceProfile profile;
    profile.count = 2 * parts;
    for (int part = 0; part < parts; ++part) {
        const auto points = ReferenceProfile::gaussPoints(part, parts, length);
        for (std::size_t k = 0; k < 2; ++k) {
            const double w = points.at(k) / length;
            profile.values.at(2 * static_cast<std::size_t>(part) +
                              k) = {.n = start.n + (end.n - start.n) * w,
                                    .vy = start.vy + (end.vy - start.vy) * w,
                                    .vz = start.vz + (end.vz - start.vz) * w,
                                    .t = start.t + (end.t - start.t) * w,
                                    .my = start.my + (end.my - start.my) * w,
                                    .mz = start.mz + (end.mz - start.mz) * w};
        }
    }
    return std::make_shared<const ReferenceProfile>(profile);
}

double asymmetry(const StateBarMatrix& k)
{
    return (k - k.transpose()).norm() / k.norm();
}

} // namespace

// T14: the stiffness of the bar with a reference state of N, shears, torque
// and moments, varying, with and without warping, is symmetric.
TEST_CASE("the flexural-torsional bar is symmetric", "[core][beam][flexural-torsional]")
{
    const double l = 5.0;
    const SpanLoads none;
    for (const bool warping : {false, true}) {
        INFO("warping " << warping);
        BeamData beam = sample(warping);
        // Mz from 2e5 to -1e5 (Vy = 6e4), My from -5e4 to 1e4, N compression.
        beam.reference =
            profileOf({.n = -3e5, .vy = 6e4, .vz = -1.2e4, .t = 2e3, .my = -5e4, .mz = 2e5},
                      {.n = -3e5, .vy = 6e4, .vz = -1.2e4, .t = 2e3, .my = 1e4, .mz = -1e5}, 2, l);
        const auto bar = fem::core::stateBarMatrices(beam, l, none, false);
        CHECK(asymmetry(bar.stiffness) <= 1e-12);
    }
}

// T14: with N only, the bending planes are those of the 008c (no shear
// centre, no Wagner coefficients there), and the uniform torsion stiffens
// (tension) or softens (compression) by Wagner's N (Iy + Iz) / A.
TEST_CASE("with N only, the flexural-torsional bar is the 008c bar and Wagner's torsion",
          "[core][beam][flexural-torsional]")
{
    const double l = 4.0;
    const SpanLoads none;
    BeamData beam = sample(false);
    beam.shearCentre.setZero();
    beam.monosymmetryY = beam.monosymmetryZ = 0.0;
    const double n = -2e5;
    beam.reference = std::make_shared<const ReferenceProfile>(ReferenceProfile::constant(n));
    const auto bar = fem::core::stateBarMatrices(beam, l, none, false);
    BeamData plain = beam;
    plain.polarRadius2 = 0.0; // the 008c bar: no Wagner term
    const auto reference = fem::core::stateBarMatrices(plain, l, none, false);
    // Dofs per end: u, v, w, rx, ry, rz.
    for (int i = 0; i < 12; ++i) {
        for (int j = 0; j < 12; ++j) {
            if (i % 6 == 3 || j % 6 == 3) {
                continue;
            }
            INFO(i << " " << j);
            CHECK(std::abs(bar.stiffness(i, j) - reference.stiffness(i, j)) <=
                  1e-12 * reference.stiffness.norm());
        }
    }
    // Uniform torsion: (GJ + N r0^2) / L, with r0^2 = (Iy + Iz) / A here.
    const double torsion = (beam.torsional + n * beam.polarRadius2) / l;
    CHECK(std::abs(bar.stiffness(3, 3) - torsion) <= 1e-12 * std::abs(torsion));
    CHECK(std::abs(bar.stiffness(3, 9) + torsion) <= 1e-12 * std::abs(torsion));
}

// T14: the bar cut at a third and condensed is the whole bar, with the
// varying reference state of both pieces taken from the whole one.
TEST_CASE("the flexural-torsional bar cut in two is the whole bar",
          "[core][beam][flexural-torsional]")
{
    const double l = 6.0;
    const double cut = 2.0;
    const SpanLoads none;
    for (const bool warping : {false, true}) {
        INFO("warping " << warping);
        const ReferencePoint start{
            .n = -1e5, .vy = 3e4, .vz = 0.0, .t = 0.0, .my = 0.0, .mz = 1.2e5};
        const ReferencePoint end{.n = -1e5, .vy = 3e4, .vz = 0.0, .t = 0.0, .my = 0.0, .mz = -6e4};
        const auto lerp = [&](double s) {
            const double w = s / l;
            return ReferencePoint{.n = start.n,
                                  .vy = start.vy,
                                  .vz = 0.0,
                                  .t = 0.0,
                                  .my = 0.0,
                                  .mz = start.mz + (end.mz - start.mz) * w};
        };
        BeamData whole = sample(warping);
        whole.reference = profileOf(start, end, 1, l);
        BeamData first = sample(warping);
        first.reference = profileOf(start, lerp(cut), 1, cut);
        BeamData second = sample(warping);
        second.reference = profileOf(lerp(cut), end, 1, l - cut);
        const auto k = fem::core::stateBarMatrices(whole, l, none, false).stiffness;
        const auto a = fem::core::stateBarMatrices(first, cut, none, false).stiffness;
        const auto b = fem::core::stateBarMatrices(second, l - cut, none, false).stiffness;
        const Eigen::Index d = k.rows() / 2;
        // Assemble the two on (start, middle, end) and condense the middle.
        Eigen::MatrixXd joined = Eigen::MatrixXd::Zero(3 * d, 3 * d);
        joined.topLeftCorner(2 * d, 2 * d) += a;
        joined.bottomRightCorner(2 * d, 2 * d) += b;
        Eigen::MatrixXd outer(2 * d, 2 * d);
        const Eigen::MatrixXd kmm = joined.block(d, d, d, d);
        Eigen::MatrixXd kom(2 * d, d);
        kom << joined.block(0, d, d, d), joined.block(2 * d, d, d, d);
        outer << joined.block(0, 0, d, d), joined.block(0, 2 * d, d, d),
            joined.block(2 * d, 0, d, d), joined.block(2 * d, 2 * d, d, d);
        const Eigen::MatrixXd condensed = outer - kom * kmm.lu().solve(kom.transpose());
        CHECK((condensed - k).norm() <= 1e-9 * k.norm());
    }
}

namespace {

using fem::core::test::kE;
using fem::core::test::kG;

// A section given by its properties (principal axes; z is the lateral
// direction, y the plane of the major bending).
struct Props
{
    double area = 6e-3;
    double iy = 4e-6; // lateral bending, in x-z
    double iz = 8e-5; // major bending, in x-y
    double j = 2e-7;
    double iw = 4e-8; // 0: no warping
    double ys = 0.0;  // the shear centre on y
    double betaZ = 0.0;
};

// One bar of length l along global X. Its local y is global Z and z is
// -global Y: the major moment Mz is about -Y, the lateral displacement along
// -Y. Fork supports: u, v, w and the twist held at both ends (u at node 1).
fem::core::Model forkBeam(const Props& p, double l)
{
    fem::core::test::TrussSpec spec;
    spec.nodes = {{0.0, 0.0, 0.0}, {l, 0.0, 0.0}};
    spec.bars = {{0, 1}};
    fem::core::Model model = fem::core::test::beamModel(spec, fem::core::ElementKind::Beam3D);
    fem::core::Section& s = model.sections.front();
    s.area = p.area;
    s.inertiaY = p.iy;
    s.inertiaZ = p.iz;
    s.torsionConstant = p.j;
    s.shearCentre = Eigen::Vector2d(p.ys, 0.0);
    s.monosymmetryZ = p.betaZ;
    s.shearAreaY.reset();
    s.shearAreaZ.reset();
    if (p.iw > 0.0) {
        s.warpingConstant = p.iw;
        model.assignments.front().warping = true;
    }
    using fem::core::DofType;
    fem::core::test::restrain(model, {1}, {DofType::U, DofType::V, DofType::W, DofType::RX});
    fem::core::test::restrain(model, {2}, {DofType::V, DofType::W, DofType::RX});
    return model;
}

// Equal and opposite moments m about the major axis at the ends (uniform
// Mz = m in the bar), and the first buckling factor.
double momentFactor(fem::core::Model model, double m)
{
    model.nodalForces.push_back(
        {.nodes = fem::core::NodeIds{{1}}, .moment = Eigen::Vector3d(0.0, -m, 0.0)});
    model.nodalForces.push_back(
        {.nodes = fem::core::NodeIds{{2}}, .moment = Eigen::Vector3d(0.0, m, 0.0)});
    const auto result = fem::core::runLinearBuckling(model);
    if (!result) {
        INFO(result.error().message);
        REQUIRE(result.has_value());
    }
    REQUIRE_FALSE(result->modes.empty());
    return result->modes.front().factor;
}

double relative(double value, double reference)
{
    return std::abs(value - reference) / std::abs(reference);
}

} // namespace

// T15 (AC-5): lateral-torsional buckling of a beam on fork supports under
// a uniform moment, one element: Mcr = (pi / L) sqrt(E Iy G J (1 + pi^2 E
// Iw / (G J L^2))), with warping and without it (Iw = 0); with the lateral
// bending and the warping held at both ends, the same with L / 2.
TEST_CASE("lateral-torsional buckling under a uniform moment",
          "[core][beam][flexural-torsional][buckling]")
{
    const double l = 6.0;
    const double m = 1.0e4;
    const auto mcr = [&](const Props& p, double length) {
        const double gj = kG * p.j;
        return std::numbers::pi / length *
               std::sqrt(kE * p.iy * gj *
                         (1.0 + std::numbers::pi * std::numbers::pi * kE * p.iw /
                                    (gj * length * length)));
    };
    SECTION("with warping")
    {
        const Props p;
        CHECK(relative(momentFactor(forkBeam(p, l), m) * m, mcr(p, l)) <= 1e-8);
    }
    SECTION("without warping (Iw = 0)")
    {
        Props p;
        p.iw = 0.0;
        CHECK(relative(momentFactor(forkBeam(p, l), m) * m, mcr(p, l)) <= 1e-8);
    }
    SECTION("lateral bending and warping held at both ends")
    {
        const Props p;
        fem::core::Model model = forkBeam(p, l);
        fem::core::test::restrain(model, {1, 2}, {fem::core::DofType::RZ});
        model.warpingEnds.push_back({.elements = fem::core::ElementIds{{1}},
                                     .condition = fem::core::WarpingCondition::Restrained});
        // Clamped against the lateral mode: k = 0.5 for both.
        CHECK(relative(momentFactor(model, m) * m, mcr(p, l / 2.0)) <= 1e-8);
    }
}

// T15 (AC-5): a monosymmetric I under a uniform moment: the two critical
// moments, for the moment and for its reverse, are the roots of
// M^2 - beta_z Py M - Py (G J + pi^2 E Iw / L^2) = 0 with Py = pi^2 E Iy / L^2
// (Trahair, 1993): their product and their difference.
TEST_CASE("lateral-torsional buckling of a monosymmetric I",
          "[core][beam][flexural-torsional][buckling]")
{
    const double l = 6.0;
    const double m = 1.0e4;
    Props p;
    p.ys = 0.05;
    p.betaZ = 0.18;
    const double py = std::numbers::pi * std::numbers::pi * kE * p.iy / (l * l);
    const double torsion = kG * p.j + std::numbers::pi * std::numbers::pi * kE * p.iw / (l * l);
    const double positive = momentFactor(forkBeam(p, l), m) * m;
    const double negative = momentFactor(forkBeam(p, l), -m) * m;
    CHECK(relative(positive * negative, py * torsion) <= 1e-8);
    CHECK(relative(std::abs(positive - negative), p.betaZ * py) <= 1e-8);
}

// T15 (AC-6): torsional buckling of a doubly symmetric column with a low
// torsional stiffness, fork supports, warping free at the ends:
// Pcr = (G J + pi^2 E Iw / L^2) / r0^2, below Euler's.
TEST_CASE("torsional buckling of a column", "[core][beam][flexural-torsional][buckling]")
{
    const double l = 6.0;
    const double force = 1.0e4;
    Props p;
    p.area = 4e-3;
    p.iy = p.iz = 4e-5;
    p.j = 1e-7;
    p.iw = 1e-9;
    fem::core::Model model = forkBeam(p, l);
    model.nodalForces.push_back(
        {.nodes = fem::core::NodeIds{{2}}, .force = Eigen::Vector3d(-force, 0.0, 0.0)});
    const auto result = fem::core::runLinearBuckling(model);
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->modes.empty());
    const double r02 = (p.iy + p.iz) / p.area;
    const double expected =
        (kG * p.j + std::numbers::pi * std::numbers::pi * kE * p.iw / (l * l)) / r02;
    REQUIRE(expected < std::numbers::pi * std::numbers::pi * kE * p.iy / (l * l));
    CHECK(relative(result->modes.front().factor * force, expected) <= 1e-8);
}

// T15 (AC-6): flexural-torsional buckling of a tee (its shear centre at ys
// on its axis of symmetry y): the lateral bending (in x-z, EIy) couples with
// the twist, (P_lat - P)(P_T - P) r0^2 = P^2 ys^2 (Timoshenko and Gere).
TEST_CASE("flexural-torsional buckling of a tee column",
          "[core][beam][flexural-torsional][buckling]")
{
    const double l = 6.0;
    const double force = 1.0e4;
    Props p;
    p.area = 3e-3;
    p.iy = 3e-6;
    p.iz = 6e-6;
    p.j = 1e-7;
    p.iw = 0.0;
    p.ys = 0.06;
    fem::core::Model model = forkBeam(p, l);
    model.nodalForces.push_back(
        {.nodes = fem::core::NodeIds{{2}}, .force = Eigen::Vector3d(-force, 0.0, 0.0)});
    const auto result = fem::core::runLinearBuckling(model);
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->modes.empty());
    const double r02 = (p.iy + p.iz) / p.area + p.ys * p.ys;
    const double lateral = std::numbers::pi * std::numbers::pi * kE * p.iy / (l * l);
    const double torsional = kG * p.j / r02;
    const double ratio = 1.0 - p.ys * p.ys / r02;
    const double sum = lateral + torsional;
    const double expected =
        (sum - std::sqrt(sum * sum - 4.0 * ratio * lateral * torsional)) / (2.0 * ratio);
    CHECK(relative(result->modes.front().factor * force, expected) <= 1e-8);
}

// T16: the bar with loads at a height (a distributed one and a concentrated
// one) and a reference state with their factor stays symmetric.
TEST_CASE("the bar with loads at a height is symmetric", "[core][beam][flexural-torsional]")
{
    const double l = 5.0;
    for (const bool warping : {false, true}) {
        INFO("warping " << warping);
        BeamData beam = sample(warping);
        ReferenceProfile profile =
            *profileOf({.n = -1e5, .vy = 2e4, .vz = 0.0, .t = 0.0, .my = 0.0, .mz = 5e4},
                       {.n = -1e5, .vy = 2e4, .vz = 0.0, .t = 0.0, .my = 0.0, .mz = -5e4}, 2, l);
        profile.loadFactor = 3.0;
        beam.reference = std::make_shared<const ReferenceProfile>(profile);
        SpanLoads loads;
        fem::core::SpanPiece piece;
        piece.from = 0.5;
        piece.to = 4.0;
        piece.start << 0.0, -2e3, 5e2, 0.0;
        piece.end << 0.0, -1e3, 0.0, 0.0;
        piece.point = Eigen::Vector2d(0.12, 0.02);
        loads.pieces = {piece};
        loads.points = {{.at = 2.5,
                         .force = Eigen::Vector3d(0.0, -4e3, 1e3),
                         .moment = Eigen::Vector3d::Zero(),
                         .point = Eigen::Vector2d(-0.1, 0.0)}};
        const auto bar = fem::core::stateBarMatrices(beam, l, loads, false);
        CHECK(asymmetry(bar.stiffness) <= 1e-12);
    }
}

namespace {

// REQ-008d-13 (AC-5): an independent reference, Rayleigh-Ritz on the
// functional of Trahair (1993) for a doubly symmetric beam on fork supports
// (sine series for the lateral displacement w and the twist phi):
//   1/2 int (E Iy w''^2 + G J phi'^2 + E Iw phi''^2) + lambda int Mz phi w''
//   - 1/2 lambda (q a int phi^2 + P a phi(L/2)^2),
// a the height of the load above the shear centre. Uniform load q, or a
// load P at the middle; lambda for q = 1 or P = 1.
// Rayleigh-Ritz on sine series (fork supports) for the moment `moment(x)`
// along the bar per unit factor; with a load at the height a (none for
// a = 0), distributed (unit per length) when `uniform`, else a unit load at
// the middle.
double ritz(const Props& p, double l, const std::function<double(double)>& moment, bool uniform,
            double a)
{
    constexpr int kTerms = 120;
    const int n = 2 * kTerms;
    Eigen::MatrixXd k = Eigen::MatrixXd::Zero(n, n);
    Eigen::MatrixXd g = Eigen::MatrixXd::Zero(n, n);
    const auto wave = [&](int i) { return (i + 1) * std::numbers::pi / l; };
    for (int i = 0; i < kTerms; ++i) {
        const double c = wave(i);
        k(i, i) = kE * p.iy * std::pow(c, 4) * l / 2.0;
        k(kTerms + i, kTerms + i) = (kG * p.j * c * c + kE * p.iw * std::pow(c, 4)) * l / 2.0;
    }
    // Gauss-Legendre on each half (a kink of M at the middle for P).
    constexpr std::array<double, 8> kX = {
        -0.9602898564975363, -0.7966664774136267, -0.5255324099163290, -0.1834346424956498,
        0.1834346424956498,  0.5255324099163290,  0.7966664774136267,  0.9602898564975363};
    constexpr std::array<double, 8> kW = {
        0.1012285362903763, 0.2223810344533745, 0.3137066458778873, 0.3626837833783620,
        0.3626837833783620, 0.3137066458778873, 0.2223810344533745, 0.1012285362903763};
    constexpr int kSlices = 32;
    for (int slice = 0; slice < kSlices; ++slice) {
        const double x0 = l * slice / kSlices;
        const double h = l / kSlices;
        for (std::size_t q = 0; q < kX.size(); ++q) {
            const double x = x0 + h * (kX.at(q) + 1.0) / 2.0;
            const double w = kW.at(q) * h / 2.0;
            const double m = moment(x);
            for (int i = 0; i < kTerms; ++i) {
                for (int j = 0; j < kTerms; ++j) {
                    // M phi_j w_i'' = -c_i^2 M sin_i sin_j.
                    const double coupling =
                        -w * m * wave(i) * wave(i) * std::sin(wave(i) * x) * std::sin(wave(j) * x);
                    g(i, kTerms + j) += coupling;
                    g(kTerms + j, i) += coupling;
                    if (uniform) {
                        g(kTerms + i, kTerms + j) -=
                            w * a * std::sin(wave(i) * x) * std::sin(wave(j) * x);
                    }
                }
            }
        }
    }
    if (!uniform) {
        for (int i = 0; i < kTerms; ++i) {
            for (int j = 0; j < kTerms; ++j) {
                g(kTerms + i, kTerms + j) -=
                    a * std::sin(wave(i) * l / 2) * std::sin(wave(j) * l / 2);
            }
        }
    }
    // (K + lambda G) v = 0: nu = 1 / lambda of (-G) v = nu K v.
    const Eigen::GeneralizedSelfAdjointEigenSolver<Eigen::MatrixXd> eigen(-g, k);
    return 1.0 / eigen.eigenvalues().maxCoeff();
}

double ritzFactor(const Props& p, double l, bool uniform, double a)
{
    const auto moment = [&](double x) {
        return uniform ? x * (l - x) / 2.0 : (x < l / 2 ? x / 2.0 : (l - x) / 2.0);
    };
    return ritz(p, l, moment, uniform, a);
}

// The first buckling factor of the fork beam under a uniform load q (local
// -y, global -Z) or a load P at the middle, at the height a above the
// centroid (the shear centre).
double heightFactor(const Props& p, double l, bool uniform, double a, double load)
{
    fem::core::Model model = forkBeam(p, l);
    if (uniform) {
        model.distributedLoads.push_back({.elements = fem::core::ElementIds{{1}},
                                          .startValue = Eigen::Vector3d(0.0, 0.0, -load),
                                          .endValue = Eigen::Vector3d(0.0, 0.0, -load),
                                          .point = Eigen::Vector2d(a, 0.0)});
    } else {
        model.concentratedLoads.push_back({.elements = fem::core::ElementIds{{1}},
                                           .force = Eigen::Vector3d(0.0, 0.0, -load),
                                           .point = Eigen::Vector2d(a, 0.0)});
    }
    const auto result = fem::core::runLinearBuckling(model);
    if (!result) {
        INFO(result.error().message);
        REQUIRE(result.has_value());
    }
    REQUIRE_FALSE(result->modes.empty());
    return result->modes.front().factor * load;
}

} // namespace

// T16 (AC-5): a beam on fork supports under a uniform load and under a
// load at the middle, applied on the top flange, at the shear centre and
// on the bottom flange: one element against Rayleigh-Ritz (1e-6), and the
// critical moment against the tables of C1 and C2 (NCCI SN003: 1.127 and
// 0.454 for the uniform load, 1.348 and 0.630 for the load at the middle,
// k = 1), within 1% at the shear centre and 5% at a height.
TEST_CASE("the height of the load in lateral-torsional buckling",
          "[core][beam][flexural-torsional][buckling]")
{
    const double l = 6.0;
    const Props p;
    const double py = std::numbers::pi * std::numbers::pi * kE * p.iy / (l * l);
    const auto table = [&](double c1, double c2, double zg) {
        return c1 * py *
               (std::sqrt(p.iw / p.iy +
                          l * l * kG * p.j / (std::numbers::pi * std::numbers::pi * kE * p.iy) +
                          std::pow(c2 * zg, 2)) -
                c2 * zg);
    };
    for (const bool uniform : {true, false}) {
        for (const double a : {0.15, 0.0, -0.15}) {
            INFO("uniform " << uniform << ", height " << a);
            const double load = uniform ? 1.0e3 : 5.0e3;
            const double engine = heightFactor(p, l, uniform, a, load);
            CHECK(relative(engine, ritzFactor(p, l, uniform, a)) <= 1e-6);
            const double mcr = uniform ? engine * l * l / 8.0 : engine * l / 4.0;
            const double reference = uniform ? table(1.127, 0.454, a) : table(1.348, 0.630, a);
            // The three-factor formula is approximate with the load at a
            // height (2.7 to 3.2% here for the load at the middle): 1% at the
            // shear centre, 5% at a height (maintainer's decision).
            CHECK(relative(mcr, reference) <= (a == 0.0 ? 1e-2 : 5e-2));
        }
    }
}

namespace {

// The fork beam of `bars` equal bars along x (the inner nodes free).
fem::core::Model forkChain(const Props& p, double l, int bars)
{
    fem::core::Model model = forkBeam(p, l);
    fem::core::test::TrussSpec spec;
    for (int i = 0; i <= bars; ++i) {
        spec.nodes.emplace_back(l * i / bars, 0.0, 0.0);
    }
    for (int i = 0; i < bars; ++i) {
        spec.bars.push_back({i, i + 1});
    }
    fem::core::Model chain = fem::core::test::beamModel(spec, fem::core::ElementKind::Beam3D);
    chain.sections = model.sections;
    chain.assignments.front().warping = model.assignments.front().warping;
    using fem::core::DofType;
    const auto last = static_cast<fem::core::ExternalId>(bars) + 1;
    fem::core::test::restrain(chain, {1}, {DofType::U, DofType::V, DofType::W, DofType::RX});
    fem::core::test::restrain(chain, {last}, {DofType::V, DofType::W, DofType::RX});
    return chain;
}

double firstFactor(const fem::core::Model& model)
{
    const auto result = fem::core::runLinearBuckling(model);
    if (!result) {
        INFO(result.error().message);
        REQUIRE(result.has_value());
    }
    REQUIRE_FALSE(result->modes.empty());
    return result->modes.front().factor;
}

} // namespace

// T17 (design 008d §5.6): the parts of a bar stay short enough not to
// buckle on their own, so the count of the bar is right whatever its
// subdivision: one bar or three give the same factor, for a uniform load
// on the top flange (lateral torsion with the load height) and for the
// torsional buckling of a column (a mode inside the bar).
TEST_CASE("the same factor with one bar or three", "[core][beam][flexural-torsional][buckling]")
{
    const double l = 6.0;
    SECTION("a uniform load on the top flange")
    {
        const Props p;
        const auto loaded = [&](int bars) {
            fem::core::Model model = forkChain(p, l, bars);
            fem::core::ElementIds all;
            for (int i = 1; i <= bars; ++i) {
                all.ids.push_back(i);
            }
            model.distributedLoads.push_back({.elements = all,
                                              .startValue = Eigen::Vector3d(0.0, 0.0, -1e3),
                                              .endValue = Eigen::Vector3d(0.0, 0.0, -1e3),
                                              .point = Eigen::Vector2d(0.15, 0.0)});
            return model;
        };
        CHECK(relative(firstFactor(loaded(3)), firstFactor(loaded(1))) <= 1e-8);
    }
    SECTION("the torsional buckling of a column")
    {
        Props p;
        p.area = 4e-3;
        p.iy = p.iz = 4e-5;
        p.j = 1e-7;
        p.iw = 1e-9;
        const auto pushed = [&](int bars) {
            fem::core::Model model = forkChain(p, l, bars);
            model.nodalForces.push_back(
                {.nodes = fem::core::NodeIds{{static_cast<fem::core::ExternalId>(bars) + 1}},
                 .force = Eigen::Vector3d(-1e4, 0.0, 0.0)});
            return model;
        };
        CHECK(relative(firstFactor(pushed(2)), firstFactor(pushed(1))) <= 1e-8);
    }
}

// T17: a bar beyond the reach of its parts is unstable (kUnstableBar) and
// its parts stay bounded: without warping, Wagner's term past G J; with
// warping, a moment so large that the parts would be too many.
TEST_CASE("a bar beyond the reach of its parts is unstable", "[core][beam][flexural-torsional]")
{
    const double l = 4.0;
    const SpanLoads none;
    SECTION("no warping: the torsion buckles along the whole bar")
    {
        BeamData beam = sample(false);
        beam.reference = std::make_shared<const ReferenceProfile>(
            ReferenceProfile::constant(-1.1 * beam.torsional / beam.polarRadius2));
        CHECK(fem::core::stateBarMatrices(beam, l, none, false).negativePivots >=
              fem::core::kUnstableBar);
    }
    SECTION("warping: a moment beyond the parts")
    {
        BeamData beam = sample(true);
        beam.reference = std::make_shared<const ReferenceProfile>(
            ReferenceProfile::constant(ReferencePoint{.mz = 1e15}));
        CHECK(fem::core::stateBarMatrices(beam, l, none, false).negativePivots >=
              fem::core::kUnstableBar);
    }
}

namespace {

// The cantilever of `bars` equal bars along x, clamped at x = 0.
fem::core::Model cantilever(const Props& p, double l, int bars)
{
    fem::core::Model model = forkChain(p, l, bars);
    model.restraints.clear();
    fem::core::test::fixAll(model, {1}, fem::core::ElementKind::Beam3D);
    return model;
}

// The shape of a mode along a bar, which the test requires.
const fem::core::BeamResult& beamOf(const fem::core::BucklingMode& mode, std::size_t element)
{
    const std::optional<fem::core::BeamResult>& beam = mode.beams.at(element);
    if (!beam) {
        throw std::logic_error("the mode has no shape along the bar");
    }
    return *beam;
}

// P_y = pi^2 E Iy / L^2 and the critical moment of the tables of C1 with
// the load at the shear centre (k = 1).
double tableMoment(const Props& p, double l, double c1)
{
    const double py = std::numbers::pi * std::numbers::pi * kE * p.iy / (l * l);
    return c1 * py *
           std::sqrt(p.iw / p.iy +
                     l * l * kG * p.j / (std::numbers::pi * std::numbers::pi * kE * p.iy));
}

} // namespace

// T19 (AC-5): a linear moment, M at one end only (psi = 0): one bar
// against the bar cut in four and against Rayleigh-Ritz (1e-6), and the
// critical moment against C1 = 1.879 (NCCI SN003, k = 1) within 5%: the
// table ignores kappa, and the exact C1 is 1.804 here (maintainer's
// decision, as for the loads at a height).
TEST_CASE("lateral-torsional buckling under a linear moment",
          "[core][beam][flexural-torsional][buckling]")
{
    const double l = 6.0;
    const double m = 1.0e4;
    const Props p;
    const auto loaded = [&](int bars) {
        fem::core::Model model = forkChain(p, l, bars);
        model.nodalForces.push_back(
            {.nodes = fem::core::NodeIds{{static_cast<fem::core::ExternalId>(bars) + 1}},
             .moment = Eigen::Vector3d(0.0, m, 0.0)});
        return model;
    };
    const double one = firstFactor(loaded(1));
    CHECK(relative(firstFactor(loaded(4)), one) <= 1e-6);
    CHECK(relative(one, ritz(p, l, [&](double x) { return m * x / l; }, true, 0.0)) <= 1e-6);
    CHECK(relative(one * m, tableMoment(p, l, 1.879)) <= 5e-2);
}

// T19 (AC-5): a load at the middle, at the shear centre, as a concentrated
// load on one bar and as a nodal force between two bars (1e-6).
TEST_CASE("a load at the middle: in the span or at a node",
          "[core][beam][flexural-torsional][buckling]")
{
    const double l = 6.0;
    const double load = 5.0e3;
    const Props p;
    fem::core::Model span = forkBeam(p, l);
    span.concentratedLoads.push_back(
        {.elements = fem::core::ElementIds{{1}}, .force = Eigen::Vector3d(0.0, 0.0, -load)});
    fem::core::Model node = forkChain(p, l, 2);
    node.nodalForces.push_back(
        {.nodes = fem::core::NodeIds{{2}}, .force = Eigen::Vector3d(0.0, 0.0, -load)});
    CHECK(relative(firstFactor(span), firstFactor(node)) <= 1e-6);
}

// T19 (AC-5): a cantilever with a load at its tip, at the shear centre,
// without warping: P = 4.012599 sqrt(E Iy G J) / L^2 (Timoshenko and Gere;
// the constant by shooting), and one bar against two (1e-6).
TEST_CASE("lateral-torsional buckling of a cantilever",
          "[core][beam][flexural-torsional][buckling]")
{
    const double l = 4.0;
    const double load = 1.0e3;
    Props p;
    p.iw = 0.0;
    const auto loaded = [&](int bars) {
        fem::core::Model model = cantilever(p, l, bars);
        model.nodalForces.push_back(
            {.nodes = fem::core::NodeIds{{static_cast<fem::core::ExternalId>(bars) + 1}},
             .force = Eigen::Vector3d(0.0, 0.0, -load)});
        return model;
    };
    const double one = firstFactor(loaded(1));
    CHECK(relative(firstFactor(loaded(2)), one) <= 1e-6);
    const double expected = 4.01259934 * std::sqrt(kE * p.iy * kG * p.j) / (l * l);
    CHECK(relative(one * load, expected) <= 1e-6);
}

// T19 (AC-6, REQ-008d-15): the modes, normalized by the largest
// translation or, without one, the largest rotation, with the warping per
// node and psi and B along the bars.
TEST_CASE("flexural-torsional buckling modes", "[core][beam][flexural-torsional][buckling]")
{
    const double l = 6.0;
    SECTION("lateral torsion under a uniform moment: v = A sin, phi = B sin")
    {
        // The nodes do not move: by the lateral slope at the ends, A pi / L
        // = 1; then B / A = Py / Mcr.
        const double m = 1.0e4;
        const Props p;
        fem::core::Model model = forkBeam(p, l);
        model.nodalForces.push_back(
            {.nodes = fem::core::NodeIds{{1}}, .moment = Eigen::Vector3d(0.0, -m, 0.0)});
        model.nodalForces.push_back(
            {.nodes = fem::core::NodeIds{{2}}, .moment = Eigen::Vector3d(0.0, m, 0.0)});
        const auto result = fem::core::runLinearBuckling(model);
        REQUIRE(result.has_value());
        REQUIRE_FALSE(result->modes.empty());
        const fem::core::BucklingMode& mode = result->modes.front();
        const double mcr = mode.factor * m;
        const double py = std::numbers::pi * std::numbers::pi * kE * p.iy / (l * l);
        const double ratio = py / mcr; // B / A
        CHECK(std::abs(std::abs(mode.rotations[0].z()) - 1.0) <= 1e-8);
        CHECK(std::abs(mode.rotations[0].y()) <= 1e-8);
        // The warping free at the ends is inside the bar (no nodal dof):
        // psi = phi' = B pi / L cos there.
        REQUIRE(mode.warping.size() == 2);
        CHECK(mode.warping[0] == 0.0);
        const fem::core::BeamResult& beam = beamOf(mode, 0);
        CHECK(relative(std::abs(beam.at(0.0).warping), ratio) <= 1e-6);
        CHECK(relative(std::abs(beam.at(l).warping), ratio) <= 1e-6);
        const fem::core::BeamPoint middle = beam.at(l / 2.0);
        // At the middle: phi = B, psi = 0, and the bimoment E Iw phi''.
        const double b = ratio * l / std::numbers::pi;
        CHECK(relative(std::abs(middle.rotation.x()), b) <= 1e-6);
        CHECK(std::abs(middle.warping) <= 1e-6 * ratio);
        const double bimoment = kE * p.iw * b * std::pow(std::numbers::pi / l, 2);
        CHECK(relative(std::abs(middle.forces.b), bimoment) <= 1e-6);
    }
    SECTION("a column free to twist at its top: by the twist")
    {
        // Two bars, the warping continuous at the middle node (a nodal
        // dof); the twist held at the base only, the warping free at the
        // ends: the column twists rigidly, phi = x / L, at P = G J / r0^2,
        // and psi = 1 / L.
        const double force = 1.0e4;
        Props p;
        p.area = 4e-3;
        p.iy = p.iz = 4e-5;
        p.j = 1e-7;
        p.iw = 1e-9;
        fem::core::Model model = forkChain(p, l, 2);
        model.restraints.clear();
        using fem::core::DofType;
        fem::core::test::restrain(model, {1}, {DofType::U, DofType::V, DofType::W, DofType::RX});
        fem::core::test::restrain(model, {3}, {DofType::V, DofType::W});
        model.nodalForces.push_back(
            {.nodes = fem::core::NodeIds{{3}}, .force = Eigen::Vector3d(-force, 0.0, 0.0)});
        const auto result = fem::core::runLinearBuckling(model);
        REQUIRE(result.has_value());
        REQUIRE_FALSE(result->modes.empty());
        const fem::core::BucklingMode& mode = result->modes.front();
        const double r02 = (p.iy + p.iz) / p.area;
        CHECK(relative(mode.factor * force, kG * p.j / r02) <= 1e-8);
        CHECK(std::abs(std::abs(mode.rotations[2].x()) - 1.0) <= 1e-8);
        CHECK(relative(std::abs(mode.rotations[1].x()), 0.5) <= 1e-6);
        REQUIRE(mode.warping.size() == 3);
        CHECK(relative(std::abs(mode.warping[1]), 1.0 / l) <= 1e-6);
        CHECK(relative(std::abs(beamOf(mode, 0).at(0.0).warping), 1.0 / l) <= 1e-6);
    }
}

// T19 (REQ-008d-16): tension and no bending moment: no buckling factor.
TEST_CASE("a beam in tension without moment has no buckling factor",
          "[core][beam][flexural-torsional][buckling]")
{
    fem::core::Model model = forkBeam(Props{}, 6.0);
    model.nodalForces.push_back(
        {.nodes = fem::core::NodeIds{{2}}, .force = Eigen::Vector3d(1.0e4, 0.0, 0.0)});
    const auto result = fem::core::runLinearBuckling(model);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("compressed, bent or twisted") != std::string::npos);
}

namespace {

// Argyris's right-angle frame (Argyris et al., 1979): A = (0, 0, 0),
// B = (L, 0, 0), C = (L, L, 0), L = 240 mm, a strip 30 x 0.6 mm (0.6 out
// of the plane), E = 71240 N/mm^2, nu = 0.31; the moment m about Z at A and
// -m at C: the support reactions vanish and both members bend under the
// same uniform in-plane moment. Supports: hinges about Z (Argyris), or forks
// (no out-of-plane displacement, no twist of the member). `parts` bars per
// member.
fem::core::Model argyrisFrame(double m, int parts, bool hinged)
{
    constexpr double kLength = 240.0;
    fem::core::test::TrussSpec spec;
    for (int i = 0; i <= parts; ++i) {
        spec.nodes.emplace_back(kLength * i / parts, 0.0, 0.0);
    }
    for (int i = 1; i <= parts; ++i) {
        spec.nodes.emplace_back(kLength, kLength * i / parts, 0.0);
    }
    for (int i = 0; i < 2 * parts; ++i) {
        spec.bars.push_back({i, i + 1});
    }
    fem::core::Model model = fem::core::test::beamModel(spec, fem::core::ElementKind::Beam3D);
    fem::core::Material& material = model.materials.front();
    material.youngsModulus = 71240.0;
    material.poissonsRatio = 0.31;
    fem::core::Section& s = model.sections.front();
    s.area = 30.0 * 0.6;
    s.inertiaY = 0.6 * std::pow(30.0, 3) / 12.0; // in the plane
    s.inertiaZ = 30.0 * std::pow(0.6, 3) / 12.0; // out of the plane (local y = Z)
    s.torsionConstant = 30.0 * std::pow(0.6, 3) / 3.0;
    s.shearAreaY.reset();
    s.shearAreaZ.reset();
    using fem::core::DofType;
    const auto c = static_cast<fem::core::ExternalId>(2 * parts) + 1;
    fem::core::test::restrain(model, {1}, {DofType::U, DofType::V, DofType::W, DofType::RX});
    fem::core::test::restrain(model, {c}, {DofType::U, DofType::W, DofType::RY});
    if (hinged) {
        fem::core::test::restrain(model, {1}, {DofType::RY});
        fem::core::test::restrain(model, {c}, {DofType::RX});
    }
    model.nodalForces.push_back(
        {.nodes = fem::core::NodeIds{{1}}, .moment = Eigen::Vector3d(0.0, 0.0, m)});
    model.nodalForces.push_back(
        {.nodes = fem::core::NodeIds{{c}}, .moment = Eigen::Vector3d(0.0, 0.0, -m)});
    return model;
}

// The same frame with the moment m about X at A and -m at C: a torque in AB
// that the joint turns into a bending moment of BC about X. A and C hold
// the translations (v free at C) and the rotations about Y and Z.
fem::core::Model torqueFrame(double m, int parts)
{
    fem::core::Model model = argyrisFrame(0.0, parts, false);
    model.restraints.clear();
    model.nodalForces.clear();
    using fem::core::DofType;
    const auto c = static_cast<fem::core::ExternalId>(2 * parts) + 1;
    fem::core::test::restrain(model, {1},
                              {DofType::U, DofType::V, DofType::W, DofType::RY, DofType::RZ});
    fem::core::test::restrain(model, {c}, {DofType::U, DofType::W, DofType::RY, DofType::RZ});
    model.nodalForces.push_back(
        {.nodes = fem::core::NodeIds{{1}}, .moment = Eigen::Vector3d(m, 0.0, 0.0)});
    model.nodalForces.push_back(
        {.nodes = fem::core::NodeIds{{c}}, .moment = Eigen::Vector3d(-m, 0.0, 0.0)});
    return model;
}

} // namespace

// T20 (ADR 0008): the lateral buckling of Argyris's right-angle frame, the
// test of the moments at a joint of bars at an angle. The reference is an
// independent derivation (the maintainer's choice, 2026-11-19): each member
// a linearized rod in global vectors, dr' = theta x t, theta' = C^-1 (dm -
// theta x M0), dm' = -t x dn, every vector continuous at the joint, the
// applied moments semitangential; the critical moment is the root of the
// determinant of the end conditions. On hinges it is the critical moment of
// each member alone, (pi / L) sqrt(E Iz G J) = 622.208 N mm, for m and -m:
// Argyris et al. (1979) give +-622 N mm (1%). On forks: +176.3477 and
// -445.8604 N mm. One bar or two per member.
TEST_CASE("the lateral buckling of Argyris's right-angle frame",
          "[core][beam][flexural-torsional][buckling]")
{
    const double m = 100.0;
    const double g = 71240.0 / (2.0 * 1.31);
    const double alone =
        std::numbers::pi / 240.0 *
        std::sqrt(71240.0 * 30.0 * std::pow(0.6, 3) / 12.0 * g * 30.0 * std::pow(0.6, 3) / 3.0);
    for (const int parts : {1, 2}) {
        INFO(parts << " bars per member");
        const double positive = firstFactor(argyrisFrame(m, parts, true)) * m;
        const double negative = firstFactor(argyrisFrame(-m, parts, true)) * m;
        CHECK(relative(positive, 622.0) <= 1e-2);
        CHECK(relative(positive, alone) <= 1e-6);
        CHECK(relative(negative, alone) <= 1e-6);
        CHECK(relative(firstFactor(argyrisFrame(m, parts, false)) * m, 176.34772588) <= 1e-6);
        CHECK(relative(firstFactor(argyrisFrame(-m, parts, false)) * m, 445.86036134) <= 1e-6);
    }
}

// The reference torque at a joint of bars at an angle (after T20, T21):
// the frame of Argyris with a torque in AB and a bending moment about X in
// BC, against the same linearized rod: +-27967.6735 N mm (1e-6), with one
// bar or two per member.
TEST_CASE("a torque turned into a bending moment at a joint",
          "[core][beam][flexural-torsional][buckling]")
{
    const double m = 1000.0;
    for (const int parts : {1, 2}) {
        INFO(parts << " bars per member");
        CHECK(relative(firstFactor(torqueFrame(m, parts)) * m, 27967.67354013) <= 1e-6);
        CHECK(relative(firstFactor(torqueFrame(-m, parts)) * m, 27967.67354013) <= 1e-6);
    }
}

// T21b (ADR 0008 addendum): Greenhill's shaft, clamped against bending at
// both ends, a torque at the end free to twist: T L / EI = 8.9868189568
// (2.8606 pi; the root of the same linearized rod), with EIy = EIz, one bar
// or three (1e-8).
TEST_CASE("Greenhill's shaft", "[core][beam][flexural-torsional][buckling]")
{
    const double l = 4.0;
    const double torque = 1.0e3;
    Props p;
    p.iy = p.iz = 5e-6;
    p.iw = 0.0;
    const auto shaft = [&](int bars) {
        fem::core::Model model = forkChain(p, l, bars);
        model.restraints.clear();
        using fem::core::DofType;
        const auto last = static_cast<fem::core::ExternalId>(bars) + 1;
        fem::core::test::fixAll(model, {1}, fem::core::ElementKind::Beam3D);
        fem::core::test::restrain(model, {last},
                                  {DofType::V, DofType::W, DofType::RY, DofType::RZ});
        model.nodalForces.push_back(
            {.nodes = fem::core::NodeIds{{last}}, .moment = Eigen::Vector3d(torque, 0.0, 0.0)});
        return model;
    };
    const double expected = 8.986818956806337 * kE * p.iy / l;
    CHECK(relative(firstFactor(shaft(1)) * torque, expected) <= 1e-8);
    CHECK(relative(firstFactor(shaft(3)) * torque, expected) <= 1e-8);
}

namespace {

// T23 (AC-8): a horizontally curved I-beam along a circle of angle beta in
// the plane XY (centre at the origin, crown on +Y), length L = 403.32 in,
// the section of Yoo (1982) as in Yang and Kuo (1987): E = 29000 ksi, G =
// 11200 ksi, A = 14.4 in^2, the out-of-plane bending I = 273 in^4 (local
// z), J = 1.414 in^4, Cw = 2070 in^6 when `warped`, and the in-plane
// bending I (local y = Z) `inPlane`. Fork supports: the end nodes held in
// every rotation, and the beam released there in both bending moments
// (local My and Mz), so that only the twist about the end tangent is held
// (stiff arms would spoil the scaling of the matrix, and the count with
// it); the translations held at A (the start), Z and Y at B (`pinned`: also
// X). n Line3 arcs (`curved`), or n straight chords with the warping
// continuous at the kinks.
struct Arch
{
    double beta = 0.0;
    int n = 2;
    bool curved = true;
    bool warped = true;
    double inPlane = 93.0;
    double area = 14.4;
    bool pinned = false;
    bool iso = false;   // Line3 isoparametric instead of arcs (T24)
    double shear = 0.0; // the shear areas (0: Euler-Bernoulli)
};

constexpr double kYooLength = 403.32;
constexpr double kYooE = 29000.0;
constexpr double kYooG = 11200.0;
constexpr double kYooOut = 273.0;
constexpr double kYooJ = 1.414; // NOLINT(modernize-use-std-numbers): J of the section, not sqrt 2
constexpr double kYooCw = 2070.0;

fem::core::Model yooArch(const Arch& arch)
{
    const int n = arch.n;
    const double r = kYooLength / arch.beta;
    const double start = std::numbers::pi / 2.0 + arch.beta / 2.0;
    const auto point = [&](double s) {
        const double a = start - arch.beta * s;
        return Eigen::Vector3d(r * std::cos(a), r * std::sin(a), 0.0);
    };
    fem::core::MeshBuilder builder;
    for (int k = 0; k <= n; ++k) {
        REQUIRE(builder.addNode(k + 1, point(static_cast<double>(k) / n)).has_value());
    }
    for (int k = 0; arch.curved && k < n; ++k) {
        REQUIRE(builder.addNode(1001 + k, point((k + 0.5) / n)).has_value());
    }
    fem::core::ElementIds bars;
    for (int k = 0; k < n; ++k) {
        const auto id = static_cast<fem::core::ExternalId>(k) + 1;
        const auto type =
            arch.curved ? fem::core::ElementType::Line3 : fem::core::ElementType::Line2;
        const std::vector<fem::core::ExternalId> line =
            arch.curved ? std::vector<fem::core::ExternalId>{id, id + 1, 1000 + id}
                        : std::vector<fem::core::ExternalId>{id, id + 1};
        REQUIRE(builder.addElement(type, id, line).has_value());
        bars.ids.push_back(id);
    }
    const auto last = static_cast<fem::core::ExternalId>(n) + 1;
    auto mesh = builder.build();
    REQUIRE(mesh.has_value());
    fem::core::Model model;
    model.mesh = std::make_shared<const fem::core::Mesh>(std::move(*mesh));
    model.materials.push_back({.name = "Steel",
                               .youngsModulus = kYooE,
                               .poissonsRatio = kYooE / (2.0 * kYooG) - 1.0,
                               .density = {},
                               .thermalExpansion = {},
                               .conductivity = {},
                               .specificHeat = {}});
    fem::core::Section yoo{.name = "Yoo", .area = arch.area};
    yoo.inertiaY = arch.inPlane;
    yoo.inertiaZ = kYooOut;
    yoo.torsionConstant = kYooJ;
    if (arch.shear > 0.0) {
        yoo.shearAreaY = arch.shear;
        yoo.shearAreaZ = arch.shear;
    }
    if (arch.warped) {
        yoo.warpingConstant = kYooCw;
    }
    model.sections.push_back(yoo);
    fem::core::Assignment beam{.elements = bars,
                               .kind = fem::core::ElementKind::Beam3D,
                               .material = "Steel",
                               .section = "Yoo",
                               .theory = arch.shear > 0.0 ? fem::core::BeamTheory::Timoshenko
                                                          : fem::core::BeamTheory::EulerBernoulli};
    beam.warping = arch.warped;
    if (arch.iso) {
        beam.curved = fem::core::CurvedFormulation::Isoparametric;
    }
    model.assignments.push_back(beam);
    // The forks: both bending moments released at the ends of the beam.
    for (const auto& [element, end] :
         {std::pair{fem::core::ExternalId{1}, fem::core::BarEnd::Start},
          std::pair{static_cast<fem::core::ExternalId>(n), fem::core::BarEnd::End}}) {
        fem::core::EndConnection fork{.elements = fem::core::ElementIds{{element}}, .end = end};
        fork.stiffness[4] = 0.0;
        fork.stiffness[5] = 0.0;
        model.endConnections.push_back(fork);
    }
    if ((!arch.curved || arch.iso) && arch.warped) {
        for (int k = 0; k + 1 < n; ++k) {
            const auto id = static_cast<fem::core::ExternalId>(k) + 1;
            model.warpingEnds.push_back({.elements = fem::core::ElementIds{{id}},
                                         .end = fem::core::BarEnd::End,
                                         .condition = fem::core::WarpingCondition::Continuous});
            model.warpingEnds.push_back({.elements = fem::core::ElementIds{{id + 1}},
                                         .end = fem::core::BarEnd::Start,
                                         .condition = fem::core::WarpingCondition::Continuous});
        }
    }
    using fem::core::DofType;
    fem::core::test::restrain(model, {1, last}, {DofType::RX, DofType::RY, DofType::RZ});
    fem::core::test::restrain(model, {1}, {DofType::U, DofType::V, DofType::W});
    fem::core::test::restrain(model, {last}, {DofType::V, DofType::W});
    if (arch.pinned) {
        fem::core::test::restrain(model, {last}, {DofType::U});
    }
    return model;
}

// The uniform moment m about Z (local y): m at the start of the beam, -m at
// its end, on the beam side of the forks.
fem::core::Model bentArch(const Arch& arch, double m)
{
    fem::core::Model model = yooArch(arch);
    model.concentratedLoads.push_back({.elements = fem::core::ElementIds{{1}},
                                       .axes = fem::core::Axes::Local,
                                       .moment = Eigen::Vector3d(0.0, m, 0.0),
                                       .at = {.value = 0.0, .fraction = true}});
    model.concentratedLoads.push_back(
        {.elements = fem::core::ElementIds{{static_cast<fem::core::ExternalId>(arch.n)}},
         .axes = fem::core::Axes::Local,
         .moment = Eigen::Vector3d(0.0, -m, 0.0),
         .at = {.value = 1.0, .fraction = true}});
    return model;
}

// A uniform compression N = q R: the radial load q (local z, inwards) on
// the arch pinned in its plane at both ends.
fem::core::Model pressedArch(Arch arch, double q)
{
    arch.pinned = true;
    fem::core::Model model = yooArch(arch);
    fem::core::ElementIds bars;
    for (int k = 0; k < arch.n; ++k) {
        bars.ids.push_back(static_cast<fem::core::ExternalId>(k) + 1);
    }
    model.distributedLoads.push_back({.elements = bars,
                                      .axes = fem::core::Axes::Local,
                                      .startValue = Eigen::Vector3d(0.0, 0.0, q),
                                      .endValue = Eigen::Vector3d(0.0, 0.0, q)});
    return model;
}

// The roots of a x^2 + b x + c, the smaller first.
std::array<double, 2> quadraticRoots(double a, double b, double c)
{
    const double d = std::sqrt(b * b - 4.0 * a * c);
    const double x1 = (-b - d) / (2.0 * a);
    const double x2 = (-b + d) / (2.0 * a);
    return {std::min(x1, x2), std::max(x1, x2)};
}

// The critical moments of the circular beam on forks under a uniform
// moment, from Yang and Kuo's Eqs. 32a-b with v = a1 sin(lambda x), theta =
// a2 sin(lambda x): with `published`, their Eqs. 34-35 (the Jacobian and
// the radial stresses through r, and their warping term); otherwise r = 0
// and Vlasov's warping E Cw (theta'' + v''/R)'', the theory of the exact
// arc. Cw = 0 without warping. The two roots, negative first.
std::array<double, 2> curvedMoments(double beta, double cw, bool published)
{
    const double rr = kYooLength / beta;
    const double l2 = std::pow(std::numbers::pi / kYooLength, 2);
    const double l4 = l2 * l2;
    const double ei = kYooE * kYooOut;
    const double gj = kYooG * kYooJ;
    const double ecw = kYooE * cw;
    if (published) {
        const double r2 = (93.0 + kYooOut) / 14.4;
        const double s = l2 - 1.0 / (rr * rr);
        const double c1 = -s * (1.0 - r2 / (rr * rr)) * l2;
        const double c2 = l2 / rr * s * (ei + gj - ecw * s + r2 * (ecw / (rr * rr) + ei) * s);
        const double c3 = l2 * s * s * (ei * (ecw * l2 + gj) + gj * ecw / (rr * rr));
        return quadraticRoots(c1, c2, c3);
    }
    // The 2 x 2 matrix of the equations, a0 + M a1; det = 0.
    const double off = ei * l2 / rr + gj * l2 / rr + ecw * l4 / rr;
    Eigen::Matrix2d a0;
    a0 << ei * l4 + gj * l2 / (rr * rr) + ecw * l4 / (rr * rr), off, off,
        ei / (rr * rr) + gj * l2 + ecw * l4;
    Eigen::Matrix2d a1;
    a1 << -l2 / rr, -l2, -l2, -1.0 / rr;
    const double b =
        a0(0, 0) * a1(1, 1) + a0(1, 1) * a1(0, 0) - a0(0, 1) * a1(1, 0) - a0(1, 0) * a1(0, 1);
    return quadraticRoots(a1.determinant(), b, a0.determinant());
}

// The critical uniform compression of the circular beam on forks, from the
// energy whose variation gives Yang and Kuo's Eqs. 32 with r = 0: EIz (v'' -
// theta / R)^2, (G J - N r0^2) (theta' + v'/R)^2, Vlasov's E Cw (theta'' +
// v''/R)^2 and -N v'^2, with sines; the least positive root. With r0 = Cw = 0
// it is the formula of Timoshenko and Gere, N = (EI / R^2) (pi^2 / beta^2 -
// 1)^2 / (pi^2 / beta^2 + EI / G J).
double curvedCompression(double beta, double cw, double r02)
{
    const double rr = kYooLength / beta;
    const double l2 = std::pow(std::numbers::pi / kYooLength, 2);
    const double ei = kYooE * kYooOut;
    // K(N) = K0 - N D, det K(N) = det K0 - N (K0_00 D_11 + K0_11 D_00 - 2 K0_01
    // D_01) + N^2 det D.
    const Eigen::Vector2d bend(l2, 1.0 / rr);
    const Eigen::Vector2d twist(1.0 / rr, 1.0);
    const Eigen::Matrix2d k0 = ei * bend * bend.transpose() +
                               (kYooG * kYooJ + kYooE * cw * l2) * l2 * twist * twist.transpose();
    Eigen::Matrix2d d = r02 * l2 * twist * twist.transpose();
    d(0, 0) += l2;
    const double b = -(k0(0, 0) * d(1, 1) + k0(1, 1) * d(0, 0) - 2.0 * k0(0, 1) * d(0, 1));
    const double a = d.determinant();
    if (a == 0.0) {
        return -k0.determinant() / b;
    }
    const auto roots = quadraticRoots(a, b, k0.determinant());
    return roots[0] > 0.0 ? roots[0] : roots[1];
}

} // namespace

// T23 (AC-8, REQ-008d-14): the out-of-plane buckling of a circular beam
// under a uniform moment (Yoo's beam, Yang and Kuo, 1987). The exact arc
// follows the curved theory of the engine (r = 0, Vlasov's warping): its
// closed form within 1e-8 with and without warping (the linearized rod of
// T20, along the circle, gives the same without warping); the published
// Eq. 34 within 1% (it adds the Jacobian and the radial stresses through r,
// and another warping term: 5e-4 to 8e-4 here); a chain of 32 straight bars
// with the warping continuous at the kinks within 1% (maintainer's choice,
// 2026-11-22: straight chords converge slowly to the curved beam); a large
// radius is the straight beam (1e-8, the mean of the two moments).
TEST_CASE("lateral buckling of a curved beam under a uniform moment",
          "[core][beam][flexural-torsional][buckling][arc]")
{
    const double m = 100.0;
    for (const double degrees : {30.0, 90.0}) {
        const double beta = degrees * std::numbers::pi / 180.0;
        INFO(degrees << " degrees");
        for (const bool warped : {true, false}) {
            INFO("warping " << warped);
            const auto exact = curvedMoments(beta, warped ? kYooCw : 0.0, false);
            const double positive = firstFactor(bentArch({.beta = beta, .warped = warped}, m)) * m;
            const double negative = firstFactor(bentArch({.beta = beta, .warped = warped}, -m)) * m;
            CHECK(relative(positive, exact[1]) <= 1e-8);
            CHECK(relative(negative, -exact[0]) <= 1e-8);
        }
        const auto published = curvedMoments(beta, kYooCw, true);
        const double positive = firstFactor(bentArch({.beta = beta}, m)) * m;
        CHECK(relative(positive, published[1]) <= 1e-2);
        const double chain = firstFactor(bentArch({.beta = beta, .n = 32, .curved = false}, m)) * m;
        CHECK(relative(chain, positive) <= 1e-2);
    }
    // Large radius: the straight beam, (pi / L) sqrt(E I (G J + pi^2 E Cw /
    // L^2)). The two critical moments part to first order in 1 / R, by about
    // (E I + G J) / 2 R (3e-6 here); their mean is the straight one.
    const double straight =
        std::numbers::pi / kYooLength *
        std::sqrt(kYooE * kYooOut *
                  (kYooG * kYooJ + std::pow(std::numbers::pi / kYooLength, 2) * kYooE * kYooCw));
    const double plus = firstFactor(bentArch({.beta = 1e-6}, m)) * m;
    const double minus = firstFactor(bentArch({.beta = 1e-6}, -m)) * m;
    CHECK(relative((plus + minus) / 2.0, straight) <= 1e-8);
}

// T23 (AC-8, REQ-008d-14): the out-of-plane buckling of a circular arch
// under a uniform compression (the radial load q, N = q R, pinned in its
// plane). The arch is made axially rigid (A = 1e8 in^2): with a real area
// its shortening between the pins bends it (with A = 14.4 in^2 the thrust
// is 28% below q R), and the compression is no longer uniform; r0^2 then
// vanishes, and Wagner's term stays with the straight bars. The in-plane
// bending is stiffened (I = 2000 in^4) so that the arch buckles out of its
// plane first. The exact arc against the closed form of the same theory
// (Vlasov's warping) within 1e-6 (the shortening left with A = 1e8 is
// about 6e-8), with and without warping; without warping that closed form
// is the formula of Timoshenko and Gere; chains of 32 and 64 straight bars,
// extrapolated to first order, within 1%.
TEST_CASE("out-of-plane buckling of a curved arch under a uniform compression",
          "[core][beam][flexural-torsional][buckling][arc]")
{
    const double q = 0.01;
    for (const double degrees : {30.0, 90.0}) {
        const double beta = degrees * std::numbers::pi / 180.0;
        const double radius = kYooLength / beta;
        INFO(degrees << " degrees");
        for (const bool warped : {true, false}) {
            INFO("warping " << warped);
            const Arch arch{.beta = beta, .warped = warped, .inPlane = 2000.0, .area = 1e8};
            const double engine = firstFactor(pressedArch(arch, q)) * q * radius;
            const double closed = curvedCompression(beta, warped ? kYooCw : 0.0, 0.0);
            INFO("engine " << engine << ", closed form " << closed);
            CHECK(relative(engine, closed) <= 1e-6);
            if (!warped) {
                const double ei = kYooE * kYooOut;
                const double k2 = std::pow(std::numbers::pi / beta, 2);
                const double timoshenko =
                    ei / (radius * radius) * std::pow(k2 - 1.0, 2) / (k2 + ei / (kYooG * kYooJ));
                CHECK(relative(closed, timoshenko) <= 1e-12);
                // The chain axially stiff enough (A = 1e5 in^2), not as stiff
                // as the arc: chords of A = 1e8 spoil the scaling of the
                // matrix. Under compression the chords converge to first order
                // (4.1% with 32, 2.1% with 64 at 90 degrees): extrapolated as
                // such, 2 c64 - c32.
                const auto chain = [&](int bars) {
                    Arch chord = arch;
                    chord.n = bars;
                    chord.curved = false;
                    chord.area = 1e5;
                    return firstFactor(pressedArch(chord, q)) * q * radius;
                };
                CHECK(relative(2.0 * chain(64) - chain(32), engine) <= 1e-2);
            }
        }
    }
}

namespace {

// The rate of convergence of three values at n, 2n and 4n elements to the
// exact one over the range: half the log2 of the ratio of the first and last
// errors. Feature 008d (T90): the ratio of the last two alone moved from 3.08
// on Linux to 2.90 on macOS (errors of 1e-7 at 64 elements, where the
// platforms part by a few 1e-9); over the range the rounding weighs half.
double rate(const std::array<double, 3>& values, double exact)
{
    return std::log2(std::abs(values[0] - exact) / std::abs(values[2] - exact)) / 2.0;
}

// A straight fork beam of n Line3 isoparametric elements along X (the
// supports and the section of forkBeam), the warping continuous between
// the elements, under a uniform load q (down) at the height a.
fem::core::Model isoForkBeam(const Props& p, double l, int n, double a, double q)
{
    fem::core::MeshBuilder builder;
    for (int k = 0; k <= n; ++k) {
        REQUIRE(builder.addNode(k + 1, Eigen::Vector3d(l * k / n, 0.0, 0.0)).has_value());
    }
    for (int k = 0; k < n; ++k) {
        REQUIRE(
            builder.addNode(1001 + k, Eigen::Vector3d(l * (k + 0.5) / n, 0.0, 0.0)).has_value());
    }
    fem::core::ElementIds bars;
    for (int k = 0; k < n; ++k) {
        const auto id = static_cast<fem::core::ExternalId>(k) + 1;
        REQUIRE(builder
                    .addElement(fem::core::ElementType::Line3, id,
                                std::vector<fem::core::ExternalId>{id, id + 1, 1000 + id})
                    .has_value());
        bars.ids.push_back(id);
    }
    auto mesh = builder.build();
    REQUIRE(mesh.has_value());
    fem::core::Model model = forkBeam(p, l);
    model.mesh = std::make_shared<const fem::core::Mesh>(std::move(*mesh));
    model.restraints.clear();
    model.assignments.front().elements = bars;
    model.assignments.front().curved = fem::core::CurvedFormulation::Isoparametric;
    for (int k = 0; k + 1 < n; ++k) {
        const auto id = static_cast<fem::core::ExternalId>(k) + 1;
        model.warpingEnds.push_back({.elements = fem::core::ElementIds{{id}},
                                     .end = fem::core::BarEnd::End,
                                     .condition = fem::core::WarpingCondition::Continuous});
        model.warpingEnds.push_back({.elements = fem::core::ElementIds{{id + 1}},
                                     .end = fem::core::BarEnd::Start,
                                     .condition = fem::core::WarpingCondition::Continuous});
    }
    using fem::core::DofType;
    const auto last = static_cast<fem::core::ExternalId>(n) + 1;
    fem::core::test::restrain(model, {1}, {DofType::U, DofType::V, DofType::W, DofType::RX});
    fem::core::test::restrain(model, {last}, {DofType::V, DofType::W, DofType::RX});
    model.distributedLoads.push_back({.elements = bars,
                                      .startValue = Eigen::Vector3d(0.0, 0.0, -q),
                                      .endValue = Eigen::Vector3d(0.0, 0.0, -q),
                                      .point = Eigen::Vector2d(a, 0.0)});
    return model;
}

} // namespace

// T24 (design 008d §5.3, AC-8): the full geometric stiffness of the
// isoparametric beam, the second variation of the rod about its reference
// state in global components. The cases of T23 converge to the exact arc
// (16, 32 and 64 elements) with a rate of at least 3 and within 1e-5
// (maintainer's decision, 2026-11-22: the small negative moments are still
// short of the asymptote there, rates of 3.2 to 3.8): Yoo's beam with
// warping under a uniform moment of either sign, and the arch under a
// uniform compression (its real area: the shortening bends the arc and the
// isoparametric beam alike). With shear areas (Timoshenko): the
// penalty of Euler-Bernoulli, G As = 1e6 EI / l^2, spoils the scaling of
// fine meshes (64 elements: 3e-5, growing). With N only it is the bar of
// the 008c: those suites hold.
TEST_CASE("the isoparametric beam buckles as the arc",
          "[core][beam][flexural-torsional][buckling][iso]")
{
    const double beta = 90.0 * std::numbers::pi / 180.0;
    const double m = 100.0;
    const auto check = [](const std::function<double(int, bool)>& factor) {
        const double exact = factor(2, false);
        std::array<double, 3> iso{};
        for (std::size_t k = 0; k < 3; ++k) {
            iso.at(k) = factor(16 << k, true);
        }
        CHECK(relative(iso[2], exact) <= 1e-5);
        CHECK(rate(iso, exact) >= 3.0);
    };
    for (const double sign : {1.0, -1.0}) {
        INFO("uniform moment, sign " << sign);
        check([&](int n, bool iso) {
            return firstFactor(
                bentArch({.beta = beta, .n = n, .iso = iso, .shear = 7.0}, sign * m));
        });
    }
    INFO("uniform compression");
    check([&](int n, bool iso) {
        return firstFactor(pressedArch(
            {.beta = beta, .n = n, .warped = false, .inPlane = 2000.0, .iso = iso, .shear = 7.0},
            0.01));
    });
}

// T24: a straight beam of isoparametric elements with what the curved cases
// leave out: a monosymmetric section (Wagner's beta and the eccentric shear
// centre), a moment and a shear that vary, and the load on the top flange.
// It converges to the exact bar with a rate of at least 3.5 (4, 8 and 16
// elements), within 1e-5.
TEST_CASE("the isoparametric beam with the load at a height",
          "[core][beam][flexural-torsional][buckling][iso]")
{
    const double l = 6.0;
    const double q = 1.0e3;
    Props p;
    p.ys = 0.05;
    p.betaZ = 0.18;
    const double exact = heightFactor(p, l, true, 0.15, q) / q;
    std::array<double, 3> iso{};
    for (std::size_t k = 0; k < 3; ++k) {
        iso.at(k) = firstFactor(isoForkBeam(p, l, 4 << k, 0.15, q));
    }
    CHECK(relative(iso[2], exact) <= 1e-5);
    CHECK(rate(iso, exact) >= 3.5);
}

// T24b (design 008d §5.8): the second order is a symmetric P-delta: the bar
// about its undeformed shape with the spatial forces of the displaced
// equilibrium, the exact straight bar with the P-delta of its own
// deflection. A cantilever without warping under a vertical tip load P (0.3
// and 0.6 of its lateral-buckling load) and a small lateral one H: one bar
// and four agree on d(tip y)/dH within 1e-6. An independent P-delta
// iteration of the linear rod (non-symmetric: the twist the torque of P-delta
// varies along the bar) gives 2.8579815272e-5 and 3.7464191957e-5, within
// 1e-3 (1.2e-4 and 6.9e-4: the non-symmetric part, which the symmetric
// stiffness of ADR 0008 leaves out). The exact nonlinear rod gives
// 2.8476e-5 and 3.6654e-5: the section rotation (Kirby and Nethercot) is
// the feature 008e.
TEST_CASE("the second order is P-delta", "[core][second-order][flexural-torsional]")
{
    Props p;
    p.iw = 0.0;
    const double l = 4.0;
    const double pcr = 4.0126 * std::sqrt(kE * p.iy * kG * p.j) / (l * l);
    const std::array<std::pair<double, double>, 2> cases = {std::pair{0.3, 2.8579815272e-5},
                                                            std::pair{0.6, 3.7464191957e-5}};
    for (const auto& [fraction, rod] : cases) {
        INFO("P / Pcr = " << fraction);
        const double load = fraction * pcr;
        const double h = 1e-4 * load;
        const auto response = [&](int bars) {
            const auto tipY = [&](double lateral) {
                fem::core::Model model = cantilever(p, l, bars);
                model.nodalForces.push_back(
                    {.nodes = fem::core::NodeIds{{static_cast<fem::core::ExternalId>(bars) + 1}},
                     .force = Eigen::Vector3d(0.0, lateral, -load)});
                auto result = fem::core::runSecondOrderStatic(
                    model, {.tolerance = 1e-12, .maxIterations = 200});
                if (!result) {
                    INFO(result.error().message);
                    REQUIRE(result.has_value());
                }
                return result->result.displacements.back().y();
            };
            return (tipY(h) - tipY(-h)) / (2.0 * h);
        };
        const double one = response(1);
        CHECK(relative(response(4), one) <= 1e-6);
        CHECK(relative(one, rod) <= 1e-3);
    }
}

// T24b: the second order of a curved beam with the flexural-torsional
// terms, both elements feeding back the spatial moments: Yoo's beam without
// warping (90 degrees, in-plane I = 2000 in^4, shear areas) under half its
// negative critical moment and a small torque at the crown. The exact arc
// does not depend on its elements (2 or 8, 1e-6); the isoparametric beam
// converges to it (8, 16, 32 elements: rate >= 3, 1e-4). Before T24b the
// two fed back moments in different conventions and parted by 4e-4.
TEST_CASE("the second order of a curved beam", "[core][second-order][flexural-torsional][iso]")
{
    const double beta = 90.0 * std::numbers::pi / 180.0;
    const double m = 0.5 * curvedMoments(beta, 0.0, false)[0];
    const auto twist = [&](int n, bool iso) {
        fem::core::Model model = bentArch(
            {.beta = beta, .n = n, .warped = false, .inPlane = 2000.0, .iso = iso, .shear = 7.0},
            m);
        model.nodalForces.push_back(
            {.nodes = fem::core::NodeIds{{static_cast<fem::core::ExternalId>(n / 2) + 1}},
             .moment = Eigen::Vector3d(0.01, 0.0, 0.0)});
        auto result = fem::core::runSecondOrderStatic(model);
        if (!result) {
            INFO(result.error().message);
            REQUIRE(result.has_value());
        }
        return result->result.rotations.at(static_cast<std::size_t>(n / 2)).x();
    };
    const double exact = twist(2, false);
    CHECK(relative(twist(8, false), exact) <= 1e-6);
    std::array<double, 3> iso{};
    for (std::size_t k = 0; k < 3; ++k) {
        iso.at(k) = twist(8 << k, true);
    }
    CHECK(relative(iso[2], exact) <= 1e-4);
    CHECK(rate(iso, exact) >= 3.0);
}
