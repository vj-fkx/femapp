// REQ-008c-09 to 12: the linear buckling analysis (design 008c §5.6, ADR
// 0007), exact in the bars: one element per member.

#include "beam_models.hpp"

#include <fem/core/stability_analysis.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <string>

using fem::core::DofType;
using fem::core::ElementKind;
using fem::core::test::kE;
using fem::core::test::kG;
using fem::core::test::kIy;
using fem::core::test::kIz;
using fem::core::test::kShearArea;
using fem::core::test::near;
using Vec = Eigen::Vector3d;

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kL = 4.0;
constexpr double kP = 1e3; // the load case: kP downwards at the top

fem::core::BucklingResult buckle(const fem::core::Model& model, int count = 1)
{
    auto result = fem::core::runLinearBuckling(model, {.count = count});
    if (!result) {
        INFO(result.error().message);
        REQUIRE(result.has_value());
    }
    return std::move(*result);
}

// A column of n elements from (0, 0) up to (0, kL) loaded by kP down at the
// top (node n + 1); no supports yet.
fem::core::Model column(int n, ElementKind kind = ElementKind::Beam2D,
                        fem::core::BeamTheory theory = fem::core::BeamTheory::EulerBernoulli)
{
    fem::core::test::TrussSpec spec;
    for (int i = 0; i <= n; ++i) {
        spec.nodes.emplace_back(0.0, kL * i / n, 0.0);
    }
    for (int i = 0; i < n; ++i) {
        spec.bars.push_back({i, i + 1});
    }
    fem::core::Model model = fem::core::test::beamModel(spec, kind, theory);
    model.nodalForces.push_back(
        {.nodes = fem::core::NodeIds{{static_cast<fem::core::ExternalId>(n) + 1}},
         .force = Vec(0, -kP, 0)});
    return model;
}

// The roots of tan u = u (fixed-pinned): the first.
constexpr double kFixedPinned = 4.493409457909064;

constexpr double kEi = kE * kIz;

} // namespace

// REQ-008c-09, 10, AC-4: Euler's four cases with one element, in the plane.
TEST_CASE("Euler's columns with one element", "[core][buckling]")
{
    const double euler = kPi * kPi * kEi / (kL * kL);
    SECTION("pinned at both ends")
    {
        fem::core::Model model = column(1);
        fem::core::test::restrain(model, {1}, {DofType::U, DofType::V});
        fem::core::test::restrain(model, {2}, {DofType::U});
        CHECK(near(buckle(model).modes.at(0).factor * kP, euler, 1e-8));
    }
    SECTION("clamped and free")
    {
        fem::core::Model model = column(1);
        fem::core::test::fixAll(model, {1}, ElementKind::Beam2D);
        CHECK(near(buckle(model).modes.at(0).factor * kP, euler / 4.0, 1e-8));
    }
    SECTION("clamped and pinned")
    {
        fem::core::Model model = column(1);
        fem::core::test::fixAll(model, {1}, ElementKind::Beam2D);
        fem::core::test::restrain(model, {2}, {DofType::U});
        CHECK(near(buckle(model).modes.at(0).factor * kP,
                   kFixedPinned * kFixedPinned * kEi / (kL * kL), 1e-8));
    }
    SECTION("clamped at both ends (the top slides along the axis)")
    {
        fem::core::Model model = column(1);
        fem::core::test::fixAll(model, {1}, ElementKind::Beam2D);
        fem::core::test::restrain(model, {2}, {DofType::U, DofType::RZ});
        CHECK(near(buckle(model).modes.at(0).factor * kP, 4.0 * euler, 1e-8));
    }
    SECTION("the first three factors of the pinned column, in order")
    {
        fem::core::Model model = column(1);
        fem::core::test::restrain(model, {1}, {DofType::U, DofType::V});
        fem::core::test::restrain(model, {2}, {DofType::U});
        const auto result = buckle(model, 3);
        REQUIRE(result.modes.size() == 3);
        for (int m = 1; m <= 3; ++m) {
            CHECK(near(result.modes.at(static_cast<std::size_t>(m - 1)).factor * kP, m * m * euler,
                       1e-8));
        }
    }
}

// REQ-008c-10, AC-4: Engesser, in space, with a spring, with a release and
// on an elastic foundation.
TEST_CASE("columns with shear, springs, releases and foundations", "[core][buckling]")
{
    const double euler = kPi * kPi * kEi / (kL * kL);
    SECTION("Engesser: Pe / (1 + Pe / G As)")
    {
        fem::core::Model model = column(1, ElementKind::Beam2D, fem::core::BeamTheory::Timoshenko);
        fem::core::test::restrain(model, {1}, {DofType::U, DofType::V});
        fem::core::test::restrain(model, {2}, {DofType::U});
        const double ga = kG * kShearArea;
        CHECK(near(buckle(model).modes.at(0).factor * kP, euler / (1.0 + euler / ga), 1e-8));
    }
    SECTION("in space: the weak axis, then the repeated next factor")
    {
        fem::core::Model model = column(1, ElementKind::Beam3D);
        fem::core::test::restrain(model, {1}, {DofType::U, DofType::V, DofType::W, DofType::RY});
        fem::core::test::restrain(model, {2}, {DofType::U, DofType::W, DofType::RY});
        const auto result = buckle(model, 3);
        REQUIRE(result.modes.size() == 3);
        const double weak = kPi * kPi * kE * kIy / (kL * kL);
        CHECK(near(result.modes.at(0).factor * kP, weak, 1e-8));
        // 4 pi^2 E Iy = pi^2 E Iz here: the second factor is double.
        CHECK(near(result.modes.at(1).factor * kP, 4.0 * weak, 1e-8));
        CHECK(near(result.modes.at(2).factor * kP, 4.0 * weak, 1e-8));
    }
    SECTION("a rotational spring at the clamped base: u tan u = k l / EI")
    {
        const double k = 2.0 * kEi / kL;
        fem::core::Model model = column(1);
        fem::core::test::fixAll(model, {1}, ElementKind::Beam2D);
        fem::core::EndConnection spring{.elements = fem::core::ElementIds{{1}},
                                        .end = fem::core::BarEnd::Start};
        spring.stiffness[5] = k;
        model.endConnections.push_back(spring);
        double low = 0.1;
        double high = kPi / 2 - 1e-12;
        for (int i = 0; i < 200; ++i) {
            const double u = (low + high) / 2;
            (u * std::tan(u) < k * kL / kEi ? low : high) = u;
        }
        CHECK(near(buckle(model).modes.at(0).factor * kP, low * low * kEi / (kL * kL), 1e-8));
    }
    SECTION("a release at the top of a clamped column: clamped and pinned")
    {
        fem::core::Model model = column(1);
        fem::core::test::fixAll(model, {1}, ElementKind::Beam2D);
        fem::core::test::restrain(model, {2}, {DofType::U, DofType::RZ});
        fem::core::EndConnection hinge{.elements = fem::core::ElementIds{{1}},
                                       .end = fem::core::BarEnd::End};
        hinge.stiffness[5] = 0.0;
        model.endConnections.push_back(hinge);
        CHECK(near(buckle(model).modes.at(0).factor * kP,
                   kFixedPinned * kFixedPinned * kEi / (kL * kL), 1e-8));
    }
    SECTION("on an elastic foundation (Hetenyi): two half waves")
    {
        const double k = 8.0 * kPi * kPi * kPi * kPi * kEi / (kL * kL * kL * kL) / 1.5;
        fem::core::Model model = column(1);
        fem::core::test::restrain(model, {1}, {DofType::U, DofType::V});
        fem::core::test::restrain(model, {2}, {DofType::U});
        model.foundations.push_back(
            {.elements = fem::core::ElementIds{{1}}, .translational = Vec(0, k, k)});
        double critical = 1e300;
        int waves = 0;
        for (int m = 1; m <= 6; ++m) {
            const double value = kEi * std::pow(m * kPi / kL, 2) + k * std::pow(kL / (m * kPi), 2);
            if (value < critical) {
                critical = value;
                waves = m;
            }
        }
        REQUIRE(waves == 2);
        CHECK(near(buckle(model).modes.at(0).factor * kP, critical, 1e-8));
    }
}

// REQ-008c-11, AC-4: the modes, normalized, and their shape along the bars.
TEST_CASE("buckling modes", "[core][buckling]")
{
    SECTION("pinned column: a sine inside the bar, by the end rotations")
    {
        fem::core::Model model = column(1);
        fem::core::test::restrain(model, {1}, {DofType::U, DofType::V});
        fem::core::test::restrain(model, {2}, {DofType::U});
        const auto mode = buckle(model).modes.at(0);
        // The nodes do not move sideways: normalized by the rotation.
        CHECK(near(std::abs(mode.rotations[0].z()), 1.0, 1e-8));
        CHECK(near(mode.rotations[1].z(), -mode.rotations[0].z(), 1e-8));
        REQUIRE(mode.beams.at(0).has_value());
        const fem::core::BeamResult& beam = *mode.beams.at(0);
        const double amplitude = kL / kPi; // a slope of 1 at the ends
        CHECK(near(std::abs(beam.at(kL / 2).displacement.y()), amplitude, 1e-8));
        CHECK(
            near(std::abs(beam.at(kL / 4).displacement.y()), amplitude * std::sin(kPi / 4), 1e-8));
    }
    SECTION("cantilever: the top moves 1, the shape 1 - cos(pi x / 2 l)")
    {
        fem::core::Model model = column(2);
        fem::core::test::fixAll(model, {1}, ElementKind::Beam2D);
        const auto mode = buckle(model).modes.at(0);
        CHECK(near(std::abs(mode.displacements[2].x()), 1.0, 1e-8));
        const double top = mode.displacements[2].x();
        CHECK(near(mode.displacements[1].x(), top * (1.0 - std::cos(kPi / 4)), 1e-8));
    }
}
