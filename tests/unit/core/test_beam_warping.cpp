// REQ-008d-01, 02, 04, 07, 08: the analysis of beams with warping (design
// 008d §5.1, 5.2), against the closed forms of mixed torsion: with
// k^2 = GJ / E Iw, GJ theta'' - E Iw theta'''' = -m.

#include "beam_models.hpp"

#include <fem/core/model.hpp>
#include <fem/core/static_analysis.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <cmath>
#include <cstddef>

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
