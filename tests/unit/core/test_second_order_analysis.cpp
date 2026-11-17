// REQ-008c-01, 03, 05 to 08: the second-order static analysis of models of
// beams and trusses (design 008c §5.5).

#include "beam_models.hpp"

#include <fem/core/mesh_builder.hpp>
#include <fem/core/stability_analysis.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using fem::core::ElementKind;
using fem::core::test::kE;
using fem::core::test::kG;
using fem::core::test::kIz;
using fem::core::test::kShearArea;
using fem::core::test::near;
using Vec = Eigen::Vector3d;

namespace {

fem::core::SecondOrderResult second(const fem::core::Model& model,
                                    const fem::core::SecondOrderOptions& options = {})
{
    auto result = fem::core::runSecondOrderStatic(model, options);
    if (!result) {
        INFO(result.error().message);
        REQUIRE(result.has_value());
    }
    return std::move(*result);
}

// The tip deflection of a cantilever under a lateral force h and an axial
// force n (Engesser with ga; ga = 0: Euler-Bernoulli), as in
// test_beam_second_order.cpp.
double cantileverTip(double l, double ei, double ga, double n, double h)
{
    const double a = ga > 0.0 ? 1.0 + n / ga : 1.0;
    const double k = std::sqrt(std::abs(n) / (ei * a));
    if (n < 0.0) {
        return h / -n * (std::tan(k * l) / (k * a) - l);
    }
    return h / n * (l - std::tanh(k * l) / (k * a));
}

// A 2D column of n elements from (0, 0) up to (0, l), clamped at the base,
// with a vertical load p (down) and a lateral force h at the top.
fem::core::Model column(double l, int n, double p, double h,
                        fem::core::BeamTheory theory = fem::core::BeamTheory::EulerBernoulli)
{
    fem::core::test::TrussSpec spec;
    for (int i = 0; i <= n; ++i) {
        spec.nodes.emplace_back(0.0, l * i / n, 0.0);
    }
    for (int i = 0; i < n; ++i) {
        spec.bars.push_back({i, i + 1});
    }
    fem::core::Model model = fem::core::test::beamModel(spec, ElementKind::Beam2D, theory);
    fem::core::test::fixAll(model, {1}, ElementKind::Beam2D);
    model.nodalForces.push_back(
        {.nodes = fem::core::NodeIds{{static_cast<fem::core::ExternalId>(n + 1)}},
         .force = Vec(h, -p, 0.0)});
    return model;
}

constexpr double kEi = kE * kIz; // in-plane bending of the 2D beams

} // namespace

// REQ-008c-01, 02, 05, 06, AC-1, AC-2: a cantilever column, exact with one
// element, in both theories; the moment along it includes P delta.
TEST_CASE("second order of a cantilever column", "[core][second-order]")
{
    const double l = 4.0;
    const double critical = std::numbers::pi * std::numbers::pi * kEi / (4.0 * l * l);
    const double h = 1e4;
    SECTION("Euler-Bernoulli and Engesser, one element")
    {
        for (const auto theory :
             {fem::core::BeamTheory::EulerBernoulli, fem::core::BeamTheory::Timoshenko}) {
            const double p = 0.5 * critical;
            const auto r = second(column(l, 1, p, h, theory));
            const double ga = theory == fem::core::BeamTheory::Timoshenko ? kG * kShearArea : 0.0;
            const double tip = cantileverTip(l, kEi, ga, -p, h);
            CHECK(near(r.result.displacements[1].x(), tip, 1e-8));
            // The moment at the root: h l + p delta, from the beam result.
            const auto& beam = fem::core::test::beamOf(r.result, 0);
            CHECK(near(std::abs(beam.at(0.0).forces.mz), h * l + p * tip, 1e-8));
            CHECK(r.iterations >= 2);
            CHECK(r.change <= 1e-8);
        }
    }
    SECTION("the amplification is about 1 / (1 - P / Pcr)")
    {
        const double p = 0.5 * critical;
        const auto linear = fem::core::test::solve(column(l, 1, p, h));
        const auto r = second(column(l, 1, p, h));
        const double amplification = r.result.displacements[1].x() / linear.displacements[1].x();
        CHECK(std::abs(amplification - 1.0 / (1.0 - 0.5)) <= 0.05 * 2.0);
    }
    SECTION("near the critical load it still converges: P / Pcr = 0.95")
    {
        const double p = 0.95 * critical;
        const auto r = second(column(l, 1, p, h));
        CHECK(r.iterations < 50);
        CHECK(near(r.result.displacements[1].x(), cantileverTip(l, kEi, 0.0, -p, h), 1e-6));
    }
    SECTION("one element or four: the same column")
    {
        const double p = 0.6 * critical;
        const auto one = second(column(l, 1, p, h));
        const auto four = second(column(l, 4, p, h));
        CHECK(near(four.result.displacements[4].x(), one.result.displacements[1].x(), 1e-8));
    }
}

// REQ-008c-01, 03, AC-2: two columns tied at the top by a truss link share
// the lateral force; a portal frame is the same with one or four elements
// per member.
TEST_CASE("second order of frames", "[core][second-order]")
{
    const double l = 4.0;
    const double critical = std::numbers::pi * std::numbers::pi * kEi / (4.0 * l * l);
    SECTION("two columns and a link")
    {
        fem::core::test::TrussSpec spec;
        spec.nodes = {Vec(0, 0, 0), Vec(0, l, 0), Vec(6, 0, 0), Vec(6, l, 0)};
        spec.bars = {{0, 1}, {2, 3}, {1, 3}};
        fem::core::Model model = fem::core::test::beamModel(spec, ElementKind::Beam2D);
        // The link is a truss bar.
        model.assignments.front().elements = fem::core::ElementIds{{1, 2}};
        model.assignments.push_back({.elements = fem::core::ElementIds{{3}},
                                     .kind = ElementKind::Truss2D,
                                     .material = "Steel",
                                     .section = "S"});
        fem::core::test::fixAll(model, {1, 3}, ElementKind::Beam2D);
        const double p = 0.4 * critical;
        const double h = 2e4;
        model.nodalForces.push_back({.nodes = fem::core::NodeIds{{2}}, .force = Vec(h, -p, 0)});
        model.nodalForces.push_back({.nodes = fem::core::NodeIds{{4}}, .force = Vec(0, -p, 0)});
        const auto r = second(model);
        // Each column a spring k_c = 1 / tip(1); the link k_l = EA / 6 between
        // them: k_c d1 + k_l (d1 - d2) = h, k_c d2 + k_l (d2 - d1) = 0.
        const double kc = 1.0 / cantileverTip(l, kEi, 0.0, -p, 1.0);
        const double kl = kE * fem::core::test::kArea / 6.0;
        const double d1 = h * (kc + kl) / (kc * (kc + 2.0 * kl));
        CHECK(near(r.result.displacements[1].x(), d1, 1e-8));
        CHECK(near(r.result.displacements[3].x(), d1 * kl / (kc + kl), 1e-8));
    }
    SECTION("a portal frame: one element per member or four")
    {
        const auto portal = [&](int n) {
            fem::core::test::TrussSpec spec;
            const auto add = [&](const Vec& a, const Vec& b) {
                const int first = static_cast<int>(spec.nodes.size());
                for (int i = 0; i <= n; ++i) {
                    spec.nodes.emplace_back(a + (b - a) * i / n);
                }
                for (int i = 0; i < n; ++i) {
                    spec.bars.push_back({first + i, first + i + 1});
                }
                return std::pair{first, first + n};
            };
            const auto left = add(Vec(0, 0, 0), Vec(0, l, 0));
            const auto right = add(Vec(6, 0, 0), Vec(6, l, 0));
            const auto beam = add(Vec(0, l, 0), Vec(6, l, 0));
            // Join the ends of the beam to the tops of the columns.
            for (auto& bar : spec.bars) {
                for (int& node : bar) {
                    if (node == beam.first) {
                        node = left.second;
                    } else if (node == beam.second) {
                        node = right.second;
                    }
                }
            }
            fem::core::Model model = fem::core::test::beamModel(spec, ElementKind::Beam2D);
            const auto id = [](int node) { return static_cast<fem::core::ExternalId>(node) + 1; };
            fem::core::test::fixAll(model, {id(left.first), id(right.first)}, ElementKind::Beam2D);
            model.nodalForces.push_back({.nodes = fem::core::NodeIds{{id(left.second)}},
                                         .force = Vec(3e4, -0.5 * critical, 0)});
            model.nodalForces.push_back({.nodes = fem::core::NodeIds{{id(right.second)}},
                                         .force = Vec(0, -0.5 * critical, 0)});
            const auto r = second(model);
            return r.result.displacements[static_cast<std::size_t>(left.second)].x();
        };
        CHECK(near(portal(4), portal(1), 1e-8));
    }
}

// REQ-008c-03, AC-3: trusses: a taut string stiffens against a lateral
// load; a compressed mast leans on its spring with P-Delta.
TEST_CASE("second order of trusses", "[core][second-order][truss]")
{
    const double ea = kE * fem::core::test::kArea;
    SECTION("a taut string held by a soft spring")
    {
        // Two bars along x from fixed ends, cooled: tension t; a vertical
        // bar below the middle node as the spring.
        const double l = 3.0;
        fem::core::test::TrussSpec spec;
        spec.nodes = {Vec(0, 0, 0), Vec(l, 0, 0), Vec(2 * l, 0, 0), Vec(l, -30.0, 0)};
        spec.bars = {{0, 1}, {1, 2}, {1, 3}};
        fem::core::Model model = fem::core::test::beamModel(spec, ElementKind::Truss2D);
        fem::core::test::restrain(model, {1, 3, 4}, {fem::core::DofType::U, fem::core::DofType::V});
        const double cooling = -50.0;
        model.temperatureChanges.push_back(
            {.elements = fem::core::ElementIds{{1, 2}}, .deltaT = cooling});
        const double f = 1e3;
        model.nodalForces.push_back({.nodes = fem::core::NodeIds{{2}}, .force = Vec(0, f, 0)});
        const auto r = second(model);
        const double tension =
            -ea * fem::core::test::steel().thermalExpansion.value_or(0.0) * cooling;
        const double spring = ea / 30.0;
        CHECK(near(r.result.axialForce[0], tension, 1e-8));
        CHECK(near(r.result.displacements[1].y(), f / (spring + 2.0 * tension / l), 1e-8));
    }
    SECTION("a compressed mast leaning on a spring")
    {
        const double l = 3.0;
        const double ls = 30.0;
        fem::core::test::TrussSpec spec;
        spec.nodes = {Vec(0, 0, 0), Vec(0, l, 0), Vec(-ls, l, 0)};
        spec.bars = {{0, 1}, {1, 2}};
        fem::core::Model model = fem::core::test::beamModel(spec, ElementKind::Truss2D);
        fem::core::test::restrain(model, {1, 3}, {fem::core::DofType::U, fem::core::DofType::V});
        const double spring = ea / ls;
        const double p = 0.5 * spring * l; // half of the critical k l
        const double h = 1e3;
        model.nodalForces.push_back({.nodes = fem::core::NodeIds{{2}}, .force = Vec(h, -p, 0)});
        const auto r = second(model);
        // The spring's own second order (its end sinks by p l / EA) is of
        // order 1e-7 here.
        CHECK(near(r.result.displacements[1].x(), h / (spring - p / l), 1e-6));
    }
}

// REQ-008c-08, AC-7: what the second order refuses.
TEST_CASE("second-order failures", "[core][second-order]")
{
    const double l = 4.0;
    const double critical = std::numbers::pi * std::numbers::pi * kEi / (4.0 * l * l);
    SECTION("above the critical load")
    {
        const auto r = fem::core::runSecondOrderStatic(column(l, 1, 1.05 * critical, 1e4));
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().message.find("unstable under these loads") != std::string::npos);
        CHECK(r.error().message.find("the node 2") != std::string::npos);
    }
    SECTION("too few iterations")
    {
        const auto r = fem::core::runSecondOrderStatic(column(l, 1, 0.5 * critical, 1e4),
                                                       {.maxIterations = 1});
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().message.find("did not converge in 1 iterations") != std::string::npos);
    }
    SECTION("a column released at both ends, beyond its own critical load")
    {
        fem::core::Model model = column(l, 1, 0.0, 0.0);
        model.restraints.clear();
        fem::core::test::restrain(
            model, {1}, {fem::core::DofType::U, fem::core::DofType::V, fem::core::DofType::RZ});
        fem::core::test::restrain(model, {2}, {fem::core::DofType::U, fem::core::DofType::RZ});
        for (const auto end : {fem::core::BarEnd::Start, fem::core::BarEnd::End}) {
            fem::core::EndConnection hinge{.elements = fem::core::ElementIds{{1}}, .end = end};
            hinge.stiffness[5] = 0.0;
            model.endConnections.push_back(hinge);
        }
        const double euler = std::numbers::pi * std::numbers::pi * kEi / (l * l);
        model.nodalForces.front().force = Vec(0, -1.05 * euler, 0);
        const auto r = fem::core::runSecondOrderStatic(model);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().message.find("inside the element 1") != std::string::npos);
        // Below its critical load it is fine.
        model.nodalForces.front().force = Vec(0, -0.9 * euler, 0);
        CHECK(fem::core::runSecondOrderStatic(model).has_value());
    }
}

namespace {

// A two-hinged half circle of radius r from (-r, 0) over the top to (r, 0):
// n Line3 elements (ends 1 .. n + 1, middles from 1001) with `curved`, or,
// without it, n straight Line2 bars on the chords. Timoshenko, the generic
// section; the crown is the node n / 2 + 1.
fem::core::Model arch(double r, int n, std::optional<fem::core::CurvedFormulation> curved)
{
    const double pi = std::numbers::pi;
    const auto point = [r](double a) { return Vec(r * std::cos(a), r * std::sin(a), 0.0); };
    fem::core::MeshBuilder builder;
    for (int k = 0; k <= n; ++k) {
        REQUIRE(builder.addNode(k + 1, point(pi - pi * k / n)).has_value());
    }
    if (curved) {
        for (int k = 0; k < n; ++k) {
            REQUIRE(builder.addNode(1001 + k, point(pi - pi * (k + 0.5) / n)).has_value());
        }
    }
    fem::core::ElementIds all;
    for (int k = 0; k < n; ++k) {
        const auto id = static_cast<fem::core::ExternalId>(k) + 1;
        if (curved) {
            const std::vector<fem::core::ExternalId> line = {id, id + 1, 1001 + id - 1};
            REQUIRE(builder.addElement(fem::core::ElementType::Line3, id, line).has_value());
        } else {
            const std::vector<fem::core::ExternalId> line = {id, id + 1};
            REQUIRE(builder.addElement(fem::core::ElementType::Line2, id, line).has_value());
        }
        all.ids.push_back(id);
    }
    auto mesh = builder.build();
    REQUIRE(mesh.has_value());
    fem::core::Model model;
    model.mesh = std::make_shared<const fem::core::Mesh>(std::move(*mesh));
    model.materials.push_back(fem::core::test::steel());
    model.sections.push_back(fem::core::test::genericSection());
    fem::core::Assignment assignment{.elements = all,
                                     .kind = ElementKind::Beam2D,
                                     .material = "Steel",
                                     .section = "S",
                                     .theory = fem::core::BeamTheory::Timoshenko};
    if (curved) {
        assignment.curved = *curved;
    }
    model.assignments.push_back(assignment);
    fem::core::test::restrain(model, {1, static_cast<fem::core::ExternalId>(n) + 1},
                              {fem::core::DofType::U, fem::core::DofType::V});
    return model;
}

// The vertical deflection of the crown in second order, with a load p
// there or a load q per length of arc, both downwards.
double crown(fem::core::Model model, int n, double p, double q)
{
    const auto top = static_cast<fem::core::ExternalId>(n / 2) + 1;
    if (p != 0.0) {
        model.nodalForces.push_back({.nodes = fem::core::NodeIds{{top}}, .force = Vec(0, -p, 0)});
    }
    if (q != 0.0) {
        model.distributedLoads.push_back({.elements = model.assignments.front().elements,
                                          .axes = fem::core::Axes::Global,
                                          .startValue = Vec(0, -q, 0),
                                          .endValue = Vec(0, -q, 0)});
    }
    return second(model).result.displacements[static_cast<std::size_t>(top) - 1].y();
}

} // namespace

// REQ-008c-04, AC-6: a two-hinged half circle arch in second order: the arc,
// the isoparametric beam and a fine chain of straight bars agree.
TEST_CASE("second order of a curved arch", "[core][second-order][arc]")
{
    using fem::core::CurvedFormulation;
    const double r = 5.0;
    for (const auto& [p, q] : {std::pair{1.5e6, 0.0}, std::pair{0.0, 3e5}}) {
        const double arc = crown(arch(r, 2, CurvedFormulation::Arc), 2, p, q);
        const double arc4 = crown(arch(r, 4, CurvedFormulation::Arc), 4, p, q);
        const double coarse = crown(arch(r, 64, std::nullopt), 64, p, q);
        const double fine = crown(arch(r, 128, std::nullopt), 128, p, q);
        const double chain = (4.0 * fine - coarse) / 3.0;
        std::vector<double> iso;
        for (const int n : {8, 16, 32}) {
            iso.push_back(crown(arch(r, n, CurvedFormulation::Isoparametric), n, p, q));
        }
        const double linear = [&] {
            fem::core::Model model = arch(r, 2, CurvedFormulation::Arc);
            model.nodalForces.push_back({.nodes = fem::core::NodeIds{{2}}, .force = Vec(0, -p, 0)});
            if (q != 0.0) {
                model.distributedLoads.push_back({.elements = model.assignments.front().elements,
                                                  .axes = fem::core::Axes::Global,
                                                  .startValue = Vec(0, -q, 0),
                                                  .endValue = Vec(0, -q, 0)});
            }
            return fem::core::test::solve(model).displacements[1].y();
        }();
        INFO("p " << p << " q " << q << ": linear " << linear << ", arc " << arc << ", arc4 "
                  << arc4 << ", chain " << chain << " (" << coarse << ", " << fine << "), iso "
                  << iso[0] << " " << iso[1] << " " << iso[2]);
        CHECK(std::abs(arc / linear - 1.0) > 1e-3); // the second order matters
        CHECK(near(arc4, arc, 1e-6));
        CHECK(near(chain, arc, 1e-6));
        // The isoparametric beam converges with h^4 (design 008b §5.5).
        for (std::size_t i = 0; i + 1 < iso.size(); ++i) {
            CHECK(std::log2(std::abs(iso[i] - arc) / std::abs(iso[i + 1] - arc)) >= 3.5);
        }
        CHECK(near(iso[2], arc, 2e-5));
    }
}
