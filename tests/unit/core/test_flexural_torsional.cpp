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
#include <fem/core/stability_analysis.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <Eigen/Eigenvalues>
#include <Eigen/LU>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
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
