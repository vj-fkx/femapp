// REQ-008d-01, 02, 04, 07, 08: the analysis of beams with warping (design
// 008d §5.1, 5.2), against the closed forms of mixed torsion: with
// k^2 = GJ / E Iw, GJ theta'' - E Iw theta'''' = -m.

#include "beam_models.hpp"

#include <fem/core/beam_result.hpp>
#include <fem/core/model.hpp>
#include <fem/core/section.hpp>
#include <fem/core/static_analysis.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <cmath>
#include <cstddef>
#include <numbers>

using fem::core::BarEnd;
using fem::core::DofType;
using fem::core::ElementIds;
using fem::core::ElementKind;
using fem::core::ExternalId;
using fem::core::Model;
using fem::core::NodeIds;
using fem::core::WarpingCondition;
using fem::core::test::kE;
using fem::core::test::kG;
using fem::core::test::kJ;
using fem::core::test::near;

namespace {

constexpr double kGJ = kG * kJ;

// n collinear bars along x from 0 to `length`, with warping and the
// generic section given Iw.
Model warpingBeam(int n, double length, double iw)
{
    fem::core::test::TrussSpec spec;
    for (int i = 0; i <= n; ++i) {
        spec.nodes.emplace_back(length * i / n, 0.0, 0.0);
    }
    for (int i = 0; i < n; ++i) {
        spec.bars.push_back({i, i + 1});
    }
    Model model = fem::core::test::beamModel(spec, ElementKind::Beam3D);
    model.sections.front().warpingConstant = iw;
    model.assignments.front().warping = true;
    return model;
}

// The warping condition of the first end of bar 1 and the last end of bar n.
void outerEnds(Model& model, int n, WarpingCondition condition)
{
    model.warpingEnds.push_back(
        {.elements = ElementIds{{1}}, .end = BarEnd::Start, .condition = condition});
    model.warpingEnds.push_back({.elements = ElementIds{{static_cast<ExternalId>(n)}},
                                 .end = BarEnd::End,
                                 .condition = condition});
}

// Simply supported for bending and held against twist at both ends (fork
// supports).
void forkSupports(Model& model, int n)
{
    const auto last = static_cast<ExternalId>(n) + 1;
    fem::core::test::restrain(model, {1}, {DofType::U, DofType::V, DofType::W, DofType::RX});
    fem::core::test::restrain(model, {last}, {DofType::V, DofType::W, DofType::RX});
}

double kOf(double iw)
{
    return std::sqrt(kGJ / (kE * iw));
}

} // namespace

// AC-1: a cantilever with warping restrained at the root and a torque T at
// the tip: theta(x) = T / (GJ k) (k x - tanh kL + sinh k(L - x) / cosh kL),
// B(0) = -T tanh(kL) / k, all the root torque carried by warping.
TEST_CASE("a cantilever with warping", "[core][beam][warping]")
{
    const double length = 4.0;
    const double iw = 2.0e-7;
    const double k = kOf(iw);
    const double torque = 1.0e4;
    const auto theta = [&](double x) {
        return torque / (kGJ * k) *
               (k * x - std::tanh(k * length) +
                std::sinh(k * (length - x)) / std::cosh(k * length));
    };
    const auto rate = [&](double x) {
        return torque / kGJ * (1.0 - std::cosh(k * (length - x)) / std::cosh(k * length));
    };
    for (const int n : {1, 4}) {
        INFO("bars " << n);
        Model model = warpingBeam(n, length, iw);
        fem::core::test::fixAll(model, {1}, ElementKind::Beam3D);
        model.warpingEnds.push_back({.elements = ElementIds{{1}},
                                     .end = BarEnd::Start,
                                     .condition = WarpingCondition::Restrained});
        model.nodalForces.push_back({.nodes = NodeIds{{static_cast<ExternalId>(n) + 1}},
                                     .moment = Eigen::Vector3d(torque, 0.0, 0.0)});
        const fem::core::StaticResult result = fem::core::test::solve(model);
        const auto tip = static_cast<std::size_t>(n);
        CHECK(near(result.rotations[tip].x(), theta(length), 1e-8));
        if (n == 4) {
            // The inner nodes share the warping of the collinear bars.
            CHECK(near(result.warping[2], rate(length / 2), 1e-8));
        }
        const fem::core::BeamResult& root = fem::core::test::beamOf(result, 0);
        CHECK(near(root.start().b, -torque * std::tanh(k * length) / k, 1e-8));
        CHECK(near(root.start().tw, torque, 1e-8));
        CHECK(std::abs(root.start().tsv) <= 1e-8 * torque);
        const double x = 0.3 * root.length();
        const fem::core::BeamPoint p = root.at(x);
        CHECK(near(p.rotation.x(), theta(x), 1e-8));
        CHECK(near(p.warping, rate(x), 1e-8));
        CHECK(near(p.forces.t, torque, 1e-8));
        CHECK(near(p.forces.tsv + p.forces.tw, p.forces.t, 1e-12, torque));
    }
}

// AC-1: fork supports, a uniform torque m. Free warping at the supports:
// theta(L/2) = m / (GJ k^2) (k^2 L^2 / 8 + 1 / cosh(kL/2) - 1). Restrained:
// theta(L/2) = m L^2 / (8 GJ) + C (1 - cosh(kL/2)), C = m L / (2 GJ k sinh(kL/2)).
TEST_CASE("a beam on fork supports under a uniform torque", "[core][beam][warping]")
{
    const double length = 6.0;
    const double iw = 5.0e-7;
    const double k = kOf(iw);
    const double m = 2.0e3;
    const double h = k * length / 2.0;
    const double freeMiddle = m / (kGJ * k * k) * (h * h / 2.0 + 1.0 / std::cosh(h) - 1.0);
    const double c = m * length / (2.0 * kGJ * k * std::sinh(h));
    const double restrainedMiddle = m * length * length / (8.0 * kGJ) + c * (1.0 - std::cosh(h));
    for (const bool restrained : {false, true}) {
        for (const int n : {2, 4}) {
            INFO("restrained " << restrained << ", bars " << n);
            Model model = warpingBeam(n, length, iw);
            forkSupports(model, n);
            if (restrained) {
                outerEnds(model, n, WarpingCondition::Restrained);
            }
            ElementIds all;
            for (int i = 1; i <= n; ++i) {
                all.ids.push_back(i);
            }
            model.distributedLoads.push_back({.elements = all,
                                              .axes = fem::core::Axes::Local,
                                              .startTorque = m,
                                              .endTorque = m});
            const fem::core::StaticResult result = fem::core::test::solve(model);
            const auto middle = static_cast<std::size_t>(n / 2);
            const double expected = restrained ? restrainedMiddle : freeMiddle;
            CHECK(near(result.rotations[middle].x(), expected, 1e-8));
            // By symmetry, no warping at the middle.
            CHECK(std::abs(result.warping[middle]) <= 1e-8 * expected / length);
        }
    }
}

// AC-1: fork supports, free warping, a torque T at the middle (a span load
// of one bar and a nodal torque of two): theta(L/2) = T / (2 GJ) (L/2 -
// tanh(kL/2) / k).
TEST_CASE("a beam on fork supports under a torque at the middle", "[core][beam][warping]")
{
    const double length = 5.0;
    const double iw = 3.0e-7;
    const double k = kOf(iw);
    const double torque = 4.0e3;
    const double expected = torque / (2.0 * kGJ) * (length / 2.0 - std::tanh(k * length / 2.0) / k);
    SECTION("a concentrated torque on one bar")
    {
        Model model = warpingBeam(1, length, iw);
        forkSupports(model, 1);
        model.concentratedLoads.push_back({.elements = ElementIds{{1}},
                                           .axes = fem::core::Axes::Local,
                                           .moment = Eigen::Vector3d(torque, 0.0, 0.0)});
        const fem::core::StaticResult result = fem::core::test::solve(model);
        const fem::core::BeamResult& bar = fem::core::test::beamOf(result, 0);
        CHECK(near(bar.at(length / 2.0).rotation.x(), expected, 1e-8));
    }
    SECTION("a nodal torque between two bars")
    {
        Model model = warpingBeam(2, length, iw);
        forkSupports(model, 2);
        model.nodalForces.push_back(
            {.nodes = NodeIds{{2}}, .moment = Eigen::Vector3d(torque, 0.0, 0.0)});
        const fem::core::StaticResult result = fem::core::test::solve(model);
        CHECK(near(result.rotations[1].x(), expected, 1e-8));
    }
}

// REQ-008d-02: the limits of mixed torsion, on the cantilever. With GJ -> 0
// (kL = 1e-5), pure warping torsion: theta(L) = T L^3 / (3 E Iw). With
// E Iw -> 0 (kL = 200), uniform torsion: theta(L) -> T L / GJ, within the
// boundary layer 1 / kL.
TEST_CASE("the limits of mixed torsion", "[core][beam][warping]")
{
    const double length = 3.0;
    const double torque = 5.0e2;
    const auto tipTwist = [&](double iw) {
        Model model = warpingBeam(1, length, iw);
        fem::core::test::fixAll(model, {1}, ElementKind::Beam3D);
        model.warpingEnds.push_back({.elements = ElementIds{{1}},
                                     .end = BarEnd::Start,
                                     .condition = WarpingCondition::Restrained});
        model.nodalForces.push_back(
            {.nodes = NodeIds{{2}}, .moment = Eigen::Vector3d(torque, 0.0, 0.0)});
        return fem::core::test::solve(model).rotations[1].x();
    };
    SECTION("GJ -> 0: pure warping torsion")
    {
        const double kl = 1e-5;
        const double iw = kGJ * length * length / (kE * kl * kl);
        const double expected = torque * std::pow(length, 3) / (3.0 * kE * iw);
        CHECK(near(tipTwist(iw), expected, 1e-8));
    }
    SECTION("E Iw -> 0: uniform torsion")
    {
        const double kl = 200.0;
        const double iw = kGJ * length * length / (kE * kl * kl);
        const double uniform = torque * length / kGJ;
        const double twist = tipTwist(iw);
        // The mixed closed form, and within 1 / kL of uniform torsion.
        CHECK(near(twist, uniform * (1.0 - std::tanh(kl) / kl), 1e-8));
        CHECK(near(twist, uniform, 1.0 / kl));
    }
}

// REQ-008d-04, 07: a bimoment Bw at the free tip of a cantilever whose root
// warping is held by a support: no torque, psi = C sinh kx with
// E Iw C k cosh kL = Bw; theta(L) = Bw / GJ (1 - 1 / cosh kL); the reaction
// bimoment is -Bw / cosh kL.
TEST_CASE("a nodal bimoment and its reaction", "[core][beam][warping]")
{
    const double length = 2.0;
    const double iw = 4.0e-7;
    const double k = kOf(iw);
    const double bimoment = 3.0e2;
    Model model = warpingBeam(1, length, iw);
    fem::core::test::fixAll(model, {1}, ElementKind::Beam3D);
    outerEnds(model, 1, WarpingCondition::Continuous);
    fem::core::test::restrain(model, {1}, {DofType::Warp});
    model.nodalForces.push_back({.nodes = NodeIds{{2}}, .bimoment = bimoment});
    const fem::core::StaticResult result = fem::core::test::solve(model);
    const double c = bimoment / (kE * iw * k * std::cosh(k * length));
    CHECK(
        near(result.rotations[1].x(), bimoment / kGJ * (1.0 - 1.0 / std::cosh(k * length)), 1e-8));
    CHECK(near(result.warping[1], c * std::sinh(k * length), 1e-8));
    CHECK(near(result.reactionBimoments[0], -bimoment / std::cosh(k * length), 1e-8));
    const fem::core::BeamResult& bar = fem::core::test::beamOf(result, 0);
    CHECK(near(bar.end().b, -bimoment, 1e-8));
    CHECK(std::abs(bar.start().t) <= 1e-8 * bimoment / length);
}

// REQ-008d-03 (AC-2): a bimoment spring kw at the root of a cantilever
// with a torque T at its free tip. With psi = T / GJ + A cosh kx + C sinh kx,
// B(L) = 0 and E Iw psi'(0) = kw psi(0): A = -(kw T / GJ) / (kw + E Iw k
// tanh kL) and theta(L) = T L / GJ + A tanh(kL) / k. kw -> 0 is the free
// root, kw -> infinity the restrained one.
TEST_CASE("a warping spring at the root of a cantilever", "[core][beam][warping]")
{
    const double length = 3.0;
    const double iw = 2.5e-7;
    const double k = kOf(iw);
    const double torque = 8.0e2;
    const double scale = kE * iw * k;
    const auto expected = [&](double kw) {
        const double a = -(kw * torque / kGJ) / (kw + scale * std::tanh(k * length));
        return torque * length / kGJ + a * std::tanh(k * length) / k;
    };
    const auto tipTwist = [&](WarpingCondition condition, double kw) {
        Model model = warpingBeam(1, length, iw);
        fem::core::test::fixAll(model, {1}, ElementKind::Beam3D);
        model.warpingEnds.push_back({.elements = ElementIds{{1}},
                                     .end = BarEnd::Start,
                                     .condition = condition,
                                     .stiffness = kw});
        model.nodalForces.push_back(
            {.nodes = NodeIds{{2}}, .moment = Eigen::Vector3d(torque, 0.0, 0.0)});
        return fem::core::test::solve(model).rotations[1].x();
    };
    const double free = tipTwist(WarpingCondition::Free, 0.0);
    const double restrained = tipTwist(WarpingCondition::Restrained, 0.0);
    CHECK(near(free, torque * length / kGJ, 1e-8));
    CHECK(near(tipTwist(WarpingCondition::Elastic, 0.0), free, 1e-12));
    CHECK(near(tipTwist(WarpingCondition::Elastic, 1e12 * scale), restrained, 1e-8));
    for (const double kw : {0.1 * scale, scale, 10.0 * scale}) {
        INFO("kw / (E Iw k) " << kw / scale);
        CHECK(near(tipTwist(WarpingCondition::Elastic, kw), expected(kw), 1e-8));
    }
}

// REQ-008d-03 (AC-2): two equal spans on fork supports, warping continuous
// over the middle support, a uniform torque: by symmetry, no warping over
// the middle, so each span is one bar with its warping restrained there.
TEST_CASE("two spans with continuous warping are one bar each", "[core][beam][warping]")
{
    const double span = 4.0;
    const double iw = 4.0e-7;
    const double m = 1.5e3;
    Model twoSpans = warpingBeam(2, 2.0 * span, iw);
    fem::core::test::restrain(twoSpans, {1}, {DofType::U});
    fem::core::test::restrain(twoSpans, {1, 2, 3}, {DofType::V, DofType::W, DofType::RX});
    twoSpans.distributedLoads.push_back({.elements = ElementIds{{1, 2}},
                                         .axes = fem::core::Axes::Local,
                                         .startTorque = m,
                                         .endTorque = m});
    Model oneSpan = warpingBeam(1, span, iw);
    forkSupports(oneSpan, 1);
    oneSpan.warpingEnds.push_back({.elements = ElementIds{{1}},
                                   .end = BarEnd::End,
                                   .condition = WarpingCondition::Restrained});
    oneSpan.distributedLoads.push_back({.elements = ElementIds{{1}},
                                        .axes = fem::core::Axes::Local,
                                        .startTorque = m,
                                        .endTorque = m});
    const fem::core::StaticResult two = fem::core::test::solve(twoSpans);
    const fem::core::StaticResult one = fem::core::test::solve(oneSpan);
    CHECK(std::abs(two.warping[1]) <=
          1e-10 * std::abs(fem::core::test::beamOf(two, 0).at(0.0).warping));
    for (const double x : {0.25 * span, 0.5 * span, 0.8 * span}) {
        const fem::core::BeamPoint a = fem::core::test::beamOf(two, 0).at(x);
        const fem::core::BeamPoint b = fem::core::test::beamOf(one, 0).at(x);
        CHECK(near(a.rotation.x(), b.rotation.x(), 1e-8));
        CHECK(near(a.forces.b, b.forces.b, 1e-8, std::abs(b.forces.b) + 1.0));
    }
}

// REQ-008d-03 (AC-2): at a node of a T, the two collinear bars share the
// warping and the third one is free; the default gives the same as the
// conditions given explicitly.
TEST_CASE("the warping at the node of a T", "[core][beam][warping]")
{
    fem::core::test::TrussSpec spec;
    spec.nodes = {{0.0, 0.0, 0.0}, {3.0, 0.0, 0.0}, {6.0, 0.0, 0.0}, {3.0, 2.0, 0.0}};
    spec.bars = {{0, 1}, {1, 2}, {1, 3}};
    Model model = fem::core::test::beamModel(spec, ElementKind::Beam3D);
    model.sections.front().warpingConstant = 3.0e-7;
    model.assignments.front().warping = true;
    fem::core::test::fixAll(model, {1, 3}, ElementKind::Beam3D);
    model.warpingEnds.push_back({.elements = ElementIds{{1}},
                                 .end = BarEnd::Start,
                                 .condition = WarpingCondition::Restrained});
    model.nodalForces.push_back(
        {.nodes = NodeIds{{2}}, .moment = Eigen::Vector3d(2.0e3, 5.0e2, 0.0)});
    model.nodalForces.push_back(
        {.nodes = NodeIds{{4}}, .force = Eigen::Vector3d(0.0, 0.0, -3.0e3)});
    Model explicitEnds = model;
    explicitEnds.warpingEnds.push_back({.elements = ElementIds{{1}},
                                        .end = BarEnd::End,
                                        .condition = WarpingCondition::Continuous});
    explicitEnds.warpingEnds.push_back({.elements = ElementIds{{2}},
                                        .end = BarEnd::Start,
                                        .condition = WarpingCondition::Continuous});
    explicitEnds.warpingEnds.push_back(
        {.elements = ElementIds{{3}}, .end = BarEnd::Start, .condition = WarpingCondition::Free});
    const fem::core::StaticResult byDefault = fem::core::test::solve(model);
    const fem::core::StaticResult bySaying = fem::core::test::solve(explicitEnds);
    for (std::size_t node = 0; node < 4; ++node) {
        CHECK((byDefault.rotations[node] - bySaying.rotations[node]).norm() <=
              1e-12 * byDefault.rotations[1].norm());
    }
    // The collinear bars share the warping of the node; the third is free.
    const double shared = byDefault.warping[1];
    CHECK(std::abs(shared) > 0.0);
    CHECK(near(fem::core::test::beamOf(byDefault, 0).at(3.0).warping, shared, 1e-8));
    CHECK(near(fem::core::test::beamOf(byDefault, 1).at(0.0).warping, shared, 1e-8));
    CHECK(std::abs(fem::core::test::beamOf(byDefault, 2).start().b) <=
          1e-10 * std::abs(fem::core::test::beamOf(byDefault, 0).start().b));
}

// REQ-008d-03, 05 (AC-2): an end plate at the tip of a cantilever whose
// root warping is restrained, a torque T at the tip: psi(0) = psi(L) = 0,
// theta(L) = T / GJ (L - 2 tanh(kL/2) / k). A torsion spring kt at the
// root adds T / kt; a rigid offset along the axis shortens the flexible
// length. The warping conditions hold at the ends of the flexible length.
TEST_CASE("end plates, torsion springs and offsets with warping", "[core][beam][warping]")
{
    const double length = 3.5;
    const double iw = 2.0e-7;
    const double k = kOf(iw);
    const double torque = 6.0e2;
    const auto plates = [&](double l) {
        return torque / kGJ * (l - 2.0 * std::tanh(k * l / 2.0) / k);
    };
    const auto cantilever = [&] {
        Model model = warpingBeam(1, length, iw);
        fem::core::test::fixAll(model, {1}, ElementKind::Beam3D);
        outerEnds(model, 1, WarpingCondition::Restrained);
        model.nodalForces.push_back(
            {.nodes = NodeIds{{2}}, .moment = Eigen::Vector3d(torque, 0.0, 0.0)});
        return model;
    };
    SECTION("end plates at both ends")
    {
        CHECK(near(fem::core::test::solve(cantilever()).rotations[1].x(), plates(length), 1e-8));
    }
    SECTION("a torsion spring at the root")
    {
        const double kt = 5.0 * kGJ / length;
        Model model = cantilever();
        fem::core::EndConnection connection{.elements = ElementIds{{1}}, .end = BarEnd::Start};
        connection.stiffness[3] = kt;
        model.endConnections.push_back(connection);
        CHECK(near(fem::core::test::solve(model).rotations[1].x(), plates(length) + torque / kt,
                   1e-8));
    }
    SECTION("a rigid offset along the axis at the root")
    {
        const double offset = 0.5;
        Model model = cantilever();
        model.rigidOffsets.push_back({.elements = ElementIds{{1}},
                                      .start = Eigen::Vector3d(offset, 0.0, 0.0),
                                      .end = Eigen::Vector3d::Zero()});
        CHECK(near(fem::core::test::solve(model).rotations[1].x(), plates(length - offset), 1e-8));
    }
}

// REQ-008d-05 (AC-2): a torsional elastic foundation kt on a beam on fork
// supports with free warping, a uniform torque m: theta(x) = sum over odd n
// of 4 m / (n pi) sin(n pi x / L) / (E Iw a^4 + GJ a^2 + kt), a = n pi / L.
TEST_CASE("warping on a torsional foundation", "[core][beam][warping]")
{
    const double length = 6.0;
    const double iw = 6.0e-7;
    const double m = 1.0e3;
    const double kt = 2.0e4;
    double expected = 0.0;
    for (int n = 1; n < 4000; n += 2) {
        const double a = n * std::numbers::pi / length;
        expected += 4.0 * m / (n * std::numbers::pi) * std::sin(a * length / 2.0) /
                    (kE * iw * std::pow(a, 4) + kGJ * a * a + kt);
    }
    for (const int bars : {1, 2}) {
        INFO("bars " << bars);
        Model model = warpingBeam(bars, length, iw);
        forkSupports(model, bars);
        ElementIds all;
        for (int i = 1; i <= bars; ++i) {
            all.ids.push_back(i);
        }
        model.foundations.push_back({.elements = all, .torsional = kt});
        model.distributedLoads.push_back(
            {.elements = all, .axes = fem::core::Axes::Local, .startTorque = m, .endTorque = m});
        const fem::core::StaticResult result = fem::core::test::solve(model);
        const double middle = bars == 1
                                  ? fem::core::test::beamOf(result, 0).at(length / 2.0).rotation.x()
                                  : result.rotations[1].x();
        CHECK(near(middle, expected, 1e-8));
    }
}

// REQ-008d-09 (AC-4): the stresses at the root of an I cantilever with its
// warping restrained there and a torque T at the tip: B(0) = -T tanh(kL) / k
// gives sigma_w = B h0 bf / (4 Iw) at the flange tips, and all the torque is
// warping torque, tau_w = 1.5 T / (h0 bf tf) at the flange centres.
TEST_CASE("the warping stresses at the root of an I cantilever", "[core][beam][warping]")
{
    const double h = 0.3;
    const double bf = 0.15;
    const double tf = 0.012;
    const double tw = 0.008;
    const double h0 = h - tf;
    const double length = 2.0;
    const double torque = 1.0e3;
    Model model = warpingBeam(1, length, 1.0);
    model.sections.front() = {.name = "S",
                              .shape = fem::core::ISection{.h = h, .bf = bf, .tf = tf, .tw = tw}};
    fem::core::test::fixAll(model, {1}, ElementKind::Beam3D);
    model.warpingEnds.push_back({.elements = ElementIds{{1}},
                                 .end = BarEnd::Start,
                                 .condition = WarpingCondition::Restrained});
    model.nodalForces.push_back(
        {.nodes = NodeIds{{2}}, .moment = Eigen::Vector3d(torque, 0.0, 0.0)});
    const auto properties = fem::core::sectionProperties(model.sections.front());
    REQUIRE(properties.has_value());
    const double iw = properties->warpingConstant;
    const double k = std::sqrt(kG * properties->torsionConstant / (kE * iw));
    const fem::core::StaticResult result = fem::core::test::solve(model);
    const fem::core::BeamForces root = fem::core::test::beamOf(result, 0).start();
    const double b0 = -torque * std::tanh(k * length) / k;
    CHECK(near(root.b, b0, 1e-8));
    for (const auto& s : fem::core::sectionStresses(model.sections.front(), root)) {
        INFO(s.name);
        if (s.name.starts_with("corner")) {
            CHECK(near(std::abs(s.warpingNormal), std::abs(b0) * h0 * bf / (4 * iw), 1e-8));
        }
        if (s.name == "top flange centre") {
            CHECK(near(s.warpingShear, 1.5 * torque / (h0 * bf * tf), 1e-8));
        }
    }
}
