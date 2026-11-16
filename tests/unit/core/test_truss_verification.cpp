// Verification of the truss analysis (constitution §5.1; REQ-007-06, 08, 11,
// 12; NFR-007-01): patch tests, analytical solutions in 2D and 3D, imposed
// strains and self-weight, and convergence of a hanging bar.

#include "bar_meshes.hpp"
#include "unit_meshes.hpp"

#include <fem/core/static_analysis.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <Eigen/Core>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <numbers>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

using fem::core::DofType;
using fem::core::ElementIds;
using fem::core::ElementKind;
using fem::core::ExternalId;
using fem::core::Model;
using fem::core::NodeIds;
using fem::core::StaticResult;
using fem::core::test::TrussSpec;

namespace {

constexpr double kYoung = 210e3; // N/mm2
constexpr double kArea = 150.0;  // mm2
constexpr double kEA = kYoung * kArea;
constexpr double kTolerance = 1e-10;

// All bars of `spec` with one material and section.
Model barsModel(const TrussSpec& spec, ElementKind kind)
{
    Model model;
    model.mesh = fem::core::test::trussMesh(spec);
    model.materials.push_back({.name = "Steel",
                               .youngsModulus = kYoung,
                               .poissonsRatio = 0.3,
                               .density = 7.85e-9,
                               .thermalExpansion = 1.2e-5,
                               .conductivity = {},
                               .specificHeat = {}});
    model.sections.push_back({.name = "Bar", .area = kArea});
    ElementIds bars;
    for (std::size_t j = 0; j < spec.bars.size(); ++j) {
        bars.ids.push_back(static_cast<ExternalId>(j) + 1);
    }
    model.assignments.push_back(
        {.elements = bars, .kind = kind, .material = "Steel", .section = "Bar"});
    return model;
}

// Prescribes `dofs` of the nodes (external ids) to `value`.
void fix(Model& model, const std::vector<ExternalId>& nodes, std::initializer_list<DofType> dofs,
         double value = 0.0)
{
    for (const DofType dof : dofs) {
        model.restraints.push_back({.nodes = NodeIds{nodes}, .dof = dof, .value = value});
    }
}

void load(Model& model, ExternalId node, const Eigen::Vector3d& force)
{
    model.nodalForces.push_back({.nodes = NodeIds{{node}}, .force = force});
}

StaticResult solve(const Model& model)
{
    auto result = fem::core::runLinearStatic(model);
    INFO((result ? std::string() : result.error().message));
    REQUIRE(result.has_value());
    return std::move(*result);
}

// |value - expected| within kTolerance of `scale` (default: |expected|).
bool near(double value, double expected, double scale = 0.0)
{
    const double reference = std::max(std::abs(expected), scale);
    return std::abs(value - expected) <= kTolerance * reference;
}

// The structure in 2D, or the same in 3D with w held at every node.
Model planarModel(const TrussSpec& spec, ElementKind kind)
{
    Model model = barsModel(spec, kind);
    if (kind == ElementKind::Truss3D) {
        std::vector<ExternalId> all;
        all.reserve(spec.nodes.size());
        for (std::size_t i = 0; i < spec.nodes.size(); ++i) {
            all.push_back(static_cast<ExternalId>(i) + 1);
        }
        fix(model, all, {DofType::W});
    }
    return model;
}

// The edges of the triangles (Tri3) or Kuhn tetrahedra (Tet4) of an n^d grid
// of the unit square or cube, interior nodes moved by up to `jitter` cells.
struct EdgeTruss
{
    TrussSpec spec;
    std::vector<ExternalId> boundary;
    std::vector<ExternalId> interior;
};

EdgeTruss edgeTruss(fem::core::ElementType linear, int n, double jitter)
{
    const int dims = linear == fem::core::ElementType::Tri3 ? 2 : 3;
    EdgeTruss truss;
    std::map<std::array<long long, 3>, int> index;
    std::set<std::pair<int, int>> edges;
    for (const auto& cell : fem::core::test::linearCells(linear, n)) {
        std::vector<int> ids;
        for (const Eigen::Vector3d& x : cell) {
            const std::array<long long, 3> key = {std::llround(x.x() * n), std::llround(x.y() * n),
                                                  std::llround(x.z() * n)};
            const auto [it, inserted] = index.try_emplace(key, static_cast<int>(index.size()));
            if (inserted) {
                truss.spec.nodes.push_back(x);
            }
            ids.push_back(it->second);
        }
        for (std::size_t i = 0; i < ids.size(); ++i) {
            for (std::size_t j = i + 1; j < ids.size(); ++j) {
                edges.insert(std::minmax(ids[i], ids[j]));
            }
        }
    }
    for (const auto& [a, b] : edges) {
        truss.spec.bars.push_back({a, b});
    }
    std::mt19937 random(7);
    std::uniform_real_distribution<double> shift(-jitter / n, jitter / n);
    for (std::size_t i = 0; i < truss.spec.nodes.size(); ++i) {
        Eigen::Vector3d& x = truss.spec.nodes[i];
        bool onBoundary = false;
        for (int k = 0; k < dims; ++k) {
            onBoundary = onBoundary || std::abs(x(k)) < 1e-12 || std::abs(x(k) - 1.0) < 1e-12;
        }
        (onBoundary ? truss.boundary : truss.interior).push_back(static_cast<ExternalId>(i) + 1);
        if (!onBoundary) {
            for (int k = 0; k < dims; ++k) {
                x(k) += shift(random);
            }
        }
    }
    return truss;
}

// Prescribes u = field(x) at the boundary nodes.
template <typename Field>
void prescribeField(Model& model, const EdgeTruss& truss, int dims, const Field& field)
{
    constexpr std::array<DofType, 3> kDofs = {DofType::U, DofType::V, DofType::W};
    for (const ExternalId id : truss.boundary) {
        const Eigen::Vector3d u = field(truss.spec.nodes[static_cast<std::size_t>(id - 1)]);
        for (int k = 0; k < dims; ++k) {
            fix(model, {id}, {kDofs.at(static_cast<std::size_t>(k))}, u(k));
        }
    }
}

} // namespace

// Patch test, rigid part: any small rigid motion of the boundary of a
// distorted truss moves it rigidly, without forces.
TEST_CASE("truss patch test: rigid motion", "[core][truss][verification]")
{
    const bool spatial = GENERATE(false, true);
    const auto linear = spatial ? fem::core::ElementType::Tet4 : fem::core::ElementType::Tri3;
    const int dims = spatial ? 3 : 2;
    const EdgeTruss truss = edgeTruss(linear, 3, 0.2);
    Model model = barsModel(truss.spec, spatial ? ElementKind::Truss3D : ElementKind::Truss2D);
    const Eigen::Vector3d t(2e-3, -1e-3, spatial ? 3e-3 : 0.0);
    const Eigen::Vector3d omega =
        spatial ? Eigen::Vector3d(1e-4, -2e-4, 3e-4) : Eigen::Vector3d(0.0, 0.0, 2e-4);
    const auto rigid = [&](const Eigen::Vector3d& x) -> Eigen::Vector3d {
        return t + omega.cross(x);
    };
    prescribeField(model, truss, dims, rigid);
    const StaticResult result = solve(model);

    for (const ExternalId id : truss.interior) {
        const auto node = static_cast<std::size_t>(id - 1);
        CHECK((result.displacements[node] - rigid(truss.spec.nodes[node])).norm() <=
              kTolerance * t.norm());
    }
    for (std::size_t e = 0; e < truss.spec.bars.size(); ++e) {
        CHECK(std::abs(result.axialForce[e]) <= kTolerance * kEA * omega.norm());
    }
}

// Patch test, uniform strain: on a regular grid every interior node has its
// bars in opposite pairs, so u = G x is the exact solution.
TEST_CASE("truss patch test: uniform strain", "[core][truss][verification]")
{
    const bool spatial = GENERATE(false, true);
    const auto linear = spatial ? fem::core::ElementType::Tet4 : fem::core::ElementType::Tri3;
    const int dims = spatial ? 3 : 2;
    const EdgeTruss truss = edgeTruss(linear, spatial ? 3 : 4, 0.0);
    Model model = barsModel(truss.spec, spatial ? ElementKind::Truss3D : ElementKind::Truss2D);
    Eigen::Matrix3d g;
    g << 1e-3, 2e-4, -1e-4, -3e-4, 5e-4, 2e-4, 1e-4, 4e-4, -6e-4;
    if (!spatial) {
        g.row(2).setZero();
        g.col(2).setZero();
    }
    const auto field = [&](const Eigen::Vector3d& x) -> Eigen::Vector3d { return g * x; };
    prescribeField(model, truss, dims, field);
    const StaticResult result = solve(model);

    REQUIRE_FALSE(truss.interior.empty());
    for (const ExternalId id : truss.interior) {
        const auto node = static_cast<std::size_t>(id - 1);
        CHECK((result.displacements[node] - field(truss.spec.nodes[node])).norm() <=
              kTolerance * g.norm());
    }
    for (std::size_t e = 0; e < truss.spec.bars.size(); ++e) {
        const auto& bar = truss.spec.bars[e];
        const Eigen::Vector3d d = truss.spec.nodes[static_cast<std::size_t>(bar[1])] -
                                  truss.spec.nodes[static_cast<std::size_t>(bar[0])];
        const Eigen::Vector3d unit = d.normalized();
        CHECK(near(result.axialForce[e], kEA * unit.dot(g * unit), kEA * g.norm()));
    }
}

// Analytical: one bar under an axial force, along y in 2D and along z in 3D.
TEST_CASE("truss analytical: axial bar", "[core][truss][verification]")
{
    const bool spatial = GENERATE(false, true);
    const double length = 1500.0;
    const double force = 12e3;
    TrussSpec spec;
    spec.nodes = {{0.0, 0.0, 0.0},
                  spatial ? Eigen::Vector3d(0.0, 0.0, length) : Eigen::Vector3d(0.0, length, 0.0)};
    spec.bars = {{0, 1}};
    Model model = barsModel(spec, spatial ? ElementKind::Truss3D : ElementKind::Truss2D);
    fix(model, {1}, {DofType::U, DofType::V});
    fix(model, {2}, {DofType::U});
    if (spatial) {
        fix(model, {1}, {DofType::W});
        fix(model, {2}, {DofType::V});
        load(model, 2, Eigen::Vector3d(0.0, 0.0, force));
    } else {
        load(model, 2, Eigen::Vector3d(0.0, force, 0.0));
    }
    const StaticResult result = solve(model);
    const int axis = spatial ? 2 : 1;
    CHECK(near(result.displacements[1](axis), force * length / kEA));
    CHECK(near(result.reactions[0](axis), -force));
    CHECK(near(result.axialForce[0], force));
    CHECK(near(result.stress[0], force / kArea));
    CHECK(near(result.strain[0], force / kEA));
}

// Analytical: two bars hanging from supports at (+-b, h), load P down at the
// apex (0, 0). N = P / (2 cos t), deflection P L / (2 EA cos^2 t).
TEST_CASE("truss analytical: symmetric V", "[core][truss][verification]")
{
    const ElementKind kind = GENERATE(ElementKind::Truss2D, ElementKind::Truss3D);
    const double b = 800.0;
    const double h = 600.0;
    const double p = 5e3;
    TrussSpec spec;
    spec.nodes = {{-b, h, 0.0}, {b, h, 0.0}, {0.0, 0.0, 0.0}};
    spec.bars = {{0, 2}, {1, 2}};
    Model model = planarModel(spec, kind);
    fix(model, {1, 2}, {DofType::U, DofType::V});
    load(model, 3, Eigen::Vector3d(0.0, -p, 0.0));
    const StaticResult result = solve(model);

    const double length = std::hypot(b, h);
    const double cosine = h / length;
    CHECK(near(result.axialForce[0], p / (2.0 * cosine)));
    CHECK(near(result.axialForce[1], p / (2.0 * cosine)));
    CHECK(near(result.displacements[2].y(), -p * length / (2.0 * kEA * cosine * cosine)));
    CHECK(near(result.displacements[2].x(), 0.0, std::abs(result.displacements[2].y())));
    CHECK(near(result.reactions[0].y() + result.reactions[1].y(), p));
}

// Analytical: an isostatic plane truss, solved by the method of joints.
// Bottom chord 1-2-3 (panels L), top node 4 at (L, h); P down at node 2.
TEST_CASE("truss analytical: isostatic plane truss", "[core][truss][verification]")
{
    const ElementKind kind = GENERATE(ElementKind::Truss2D, ElementKind::Truss3D);
    const double l = 3000.0;
    const double h = 2000.0;
    const double p = 40e3;
    TrussSpec spec;
    spec.nodes = {{0.0, 0.0, 0.0}, {l, 0.0, 0.0}, {2.0 * l, 0.0, 0.0}, {l, h, 0.0}};
    spec.bars = {{0, 1}, {1, 2}, {0, 3}, {3, 2}, {1, 3}};
    Model model = planarModel(spec, kind);
    fix(model, {1}, {DofType::U, DofType::V});
    fix(model, {3}, {DofType::V});
    load(model, 2, Eigen::Vector3d(0.0, -p, 0.0));
    const StaticResult result = solve(model);

    const double diagonal = std::hypot(l, h);
    const double chord = p * l / (2.0 * h);        // tension
    const double rafter = -p / 2.0 * diagonal / h; // compression
    CHECK(near(result.axialForce[0], chord));
    CHECK(near(result.axialForce[1], chord));
    CHECK(near(result.axialForce[2], rafter));
    CHECK(near(result.axialForce[3], rafter));
    CHECK(near(result.axialForce[4], p)); // the hanger
    CHECK(near(result.reactions[0].y(), p / 2.0));
    CHECK(near(result.reactions[2].y(), p / 2.0));
    CHECK(near(result.reactions[0].x(), 0.0, p));
}

// Analytical: a tripod of legs from radius r on the ground to the apex at
// height h, load P down. N = -P L / (3 h), deflection P L^3 / (3 EA h^2).
TEST_CASE("truss analytical: tripod", "[core][truss][verification]")
{
    const double r = 1000.0;
    const double h = 2500.0;
    const double p = 30e3;
    TrussSpec spec;
    for (int k = 0; k < 3; ++k) {
        const double angle = 2.0 * std::numbers::pi * k / 3.0 + 0.3;
        spec.nodes.emplace_back(r * std::cos(angle), r * std::sin(angle), 0.0);
    }
    spec.nodes.emplace_back(0.0, 0.0, h);
    spec.bars = {{0, 3}, {1, 3}, {2, 3}};
    Model model = barsModel(spec, ElementKind::Truss3D);
    fix(model, {1, 2, 3}, {DofType::U, DofType::V, DofType::W});
    load(model, 4, Eigen::Vector3d(0.0, 0.0, -p));
    const StaticResult result = solve(model);

    const double length = std::hypot(r, h);
    for (std::size_t e = 0; e < 3; ++e) {
        CHECK(near(result.axialForce[e], -p * length / (3.0 * h)));
        CHECK(near(result.reactions[e].z(), p / 3.0));
    }
    const double deflection = p * length * length * length / (3.0 * kEA * h * h);
    CHECK(near(result.displacements[3].z(), -deflection));
    CHECK(result.displacements[3].head<2>().norm() <= kTolerance * deflection);

    // Settling one support moves the isostatic tripod without forces.
    Model settled = barsModel(spec, ElementKind::Truss3D);
    fix(settled, {1, 2, 3}, {DofType::U, DofType::V});
    fix(settled, {1, 2}, {DofType::W});
    fix(settled, {3}, {DofType::W}, -5.0);
    const StaticResult moved = solve(settled);
    for (std::size_t e = 0; e < 3; ++e) {
        CHECK(std::abs(moved.axialForce[e]) <= kTolerance * kEA);
    }
    CHECK(moved.displacements[3].norm() > 0.1);
}

// Analytical: three bars from supports at (-b, h), (0, h), (b, h) to the node
// at the origin (hyperstatic), with a load and with a settlement of the
// middle support.
TEST_CASE("truss analytical: three concurrent bars", "[core][truss][verification]")
{
    const ElementKind kind = GENERATE(ElementKind::Truss2D, ElementKind::Truss3D);
    const double b = 1200.0;
    const double h = 1600.0;
    TrussSpec spec;
    spec.nodes = {{-b, h, 0.0}, {0.0, h, 0.0}, {b, h, 0.0}, {0.0, 0.0, 0.0}};
    spec.bars = {{0, 3}, {1, 3}, {2, 3}};
    const double length = std::hypot(b, h);
    const double cosine = h / length;
    const double stiffness = kEA / h + 2.0 * kEA * cosine * cosine / length;

    SECTION("a load")
    {
        const double p = 25e3;
        Model model = planarModel(spec, kind);
        fix(model, {1, 2, 3}, {DofType::U, DofType::V});
        load(model, 4, Eigen::Vector3d(0.0, -p, 0.0));
        const StaticResult result = solve(model);
        const double delta = p / stiffness;
        CHECK(near(result.displacements[3].y(), -delta));
        CHECK(near(result.axialForce[1], kEA * delta / h));
        CHECK(near(result.axialForce[0], kEA * delta * cosine / length));
        CHECK(near(result.axialForce[2], kEA * delta * cosine / length));
    }
    SECTION("a settlement")
    {
        const double s = 2.0;
        Model model = planarModel(spec, kind);
        fix(model, {1, 2, 3}, {DofType::U});
        fix(model, {1, 3}, {DofType::V});
        fix(model, {2}, {DofType::V}, -s);
        const StaticResult result = solve(model);
        const double delta = (kEA * s / h) / stiffness;
        CHECK(near(result.displacements[3].y(), -delta));
        CHECK(near(result.axialForce[1], kEA * (delta - s) / h));
        CHECK(near(result.axialForce[0], kEA * delta * cosine / length));
        // The middle bar is shortened and pushes its support up.
        CHECK(near(result.reactions[1].y(), kEA * (delta - s) / h));
    }
}

// REQ-007-11: temperature change and initial strain on two bars in a row
// (nodes 1-2-3 along x), held at both ends or free at one.
TEST_CASE("truss imposed strains", "[core][truss][verification]")
{
    const ElementKind kind = GENERATE(ElementKind::Truss2D, ElementKind::Truss3D);
    const double length = 1000.0; // each bar
    const double deltaT = 40.0;
    const double alpha = 1.2e-5;
    TrussSpec spec;
    spec.nodes = {{0.0, 0.0, 0.0}, {length, 0.0, 0.0}, {2.0 * length, 0.0, 0.0}};
    spec.bars = {{0, 1}, {1, 2}};
    spec.lineGroups = {{"all", {0, 1}}};

    SECTION("held at both ends")
    {
        Model model = planarModel(spec, kind);
        fix(model, {1, 2, 3}, {DofType::V});
        fix(model, {1, 3}, {DofType::U});
        model.temperatureChanges.push_back(
            {.elements = fem::core::GroupRef{"all"}, .deltaT = deltaT});
        StaticResult result = solve(model);
        for (std::size_t e = 0; e < 2; ++e) {
            CHECK(near(result.axialForce[e], -kEA * alpha * deltaT));
            CHECK(near(result.strain[e], -alpha * deltaT));
        }
        CHECK(near(result.displacements[1].x(), 0.0, alpha * deltaT * length));
        CHECK(near(result.reactions[0].x(), kEA * alpha * deltaT));
        CHECK(near(result.reactions[2].x(), -kEA * alpha * deltaT));

        // An initial strain acts the same way, and the two add up.
        model.temperatureChanges.clear();
        model.initialStrains.push_back({.elements = ElementIds{{1, 2}}, .strain = 3e-4});
        result = solve(model);
        CHECK(near(result.axialForce[0], -kEA * 3e-4));
        // Bar 2 also heated: in series, N = -EA (eps1 + eps2) / 2 in both and
        // the middle node moves by (eps1 - eps2) L / 2.
        model.temperatureChanges.push_back({.elements = ElementIds{{2}}, .deltaT = deltaT});
        result = solve(model);
        for (std::size_t e = 0; e < 2; ++e) {
            CHECK(near(result.axialForce[e], -kEA * (3e-4 + alpha * deltaT / 2.0)));
        }
        CHECK(near(result.displacements[1].x(), -alpha * deltaT * length / 2.0));
    }
    SECTION("free at one end")
    {
        Model model = planarModel(spec, kind);
        fix(model, {1, 2, 3}, {DofType::V});
        fix(model, {1}, {DofType::U});
        model.temperatureChanges.push_back(
            {.elements = fem::core::GroupRef{"all"}, .deltaT = deltaT});
        const StaticResult result = solve(model);
        CHECK(near(result.displacements[2].x(), 2.0 * alpha * deltaT * length));
        CHECK(near(result.displacements[1].x(), alpha * deltaT * length));
        for (std::size_t e = 0; e < 2; ++e) {
            CHECK(std::abs(result.axialForce[e]) <= kTolerance * kEA * alpha * deltaT);
        }
    }
}

namespace {

// A bar of length `length` hanging from node 1 at the top, in n elements
// down the y axis, under its own weight (g = 9810 mm/s2 downwards).
struct Hanging
{
    StaticResult result;
    std::vector<double> depth; // per node, below the support
};

constexpr double kGravity = 9810.0;

Hanging hangingBar(int n, double length, ElementKind kind)
{
    TrussSpec spec;
    std::vector<ExternalId> all;
    Hanging hanging;
    for (int i = 0; i <= n; ++i) {
        const double s = length * i / n;
        spec.nodes.emplace_back(0.0, -s, 0.0);
        hanging.depth.push_back(s);
        all.push_back(i + 1);
    }
    for (int i = 0; i < n; ++i) {
        spec.bars.push_back({i, i + 1});
    }
    Model model = planarModel(spec, kind);
    fix(model, all, {DofType::U}); // a chain has no lateral stiffness
    fix(model, {1}, {DofType::V});
    model.gravity = Eigen::Vector3d(0.0, -kGravity, 0.0);
    hanging.result = solve(model);
    return hanging;
}

} // namespace

// REQ-007-06, 11: self-weight. Reaction rho g A L; u(s) = rho g (L s - s^2/2) / E.
TEST_CASE("truss hanging under its own weight", "[core][truss][verification]")
{
    const ElementKind kind = GENERATE(ElementKind::Truss2D, ElementKind::Truss3D);
    const double length = 3000.0;
    const double rhoG = 7.85e-9 * kGravity;
    const Hanging hanging = hangingBar(5, length, kind);
    CHECK(near(hanging.result.reactions[0].y(), rhoG * kArea * length));
    for (std::size_t i = 0; i < hanging.depth.size(); ++i) {
        const double s = hanging.depth[i];
        CHECK(near(hanging.result.displacements[i].y(), -rhoG * (length * s - s * s / 2.0) / kYoung,
                   rhoG * length * length / kYoung));
    }
}

// REQ-007-12: nodal values stay exact and the L2 error of N falls as h.
TEST_CASE("truss convergence of a hanging bar", "[core][truss][verification]")
{
    const double length = 3000.0;
    const double rhoG = 7.85e-9 * kGravity;
    const auto exactForce = [&](double s) { return rhoG * kArea * (length - s); };
    // Gauss with 3 points: exact for the quadratic error inside an element.
    const std::array<double, 3> xi = {-std::sqrt(0.6), 0.0, std::sqrt(0.6)};
    const std::array<double, 3> weight = {5.0 / 9.0, 8.0 / 9.0, 5.0 / 9.0};

    std::vector<double> errors;
    for (const int n : {4, 8, 16}) {
        const Hanging hanging = hangingBar(n, length, ElementKind::Truss2D);
        double sum = 0.0;
        for (int e = 0; e < n; ++e) {
            const double s0 = hanging.depth[static_cast<std::size_t>(e)];
            const double s1 = hanging.depth[static_cast<std::size_t>(e) + 1];
            const double force = hanging.result.axialForce[static_cast<std::size_t>(e)];
            for (std::size_t q = 0; q < 3; ++q) {
                const double s = 0.5 * (s0 + s1) + 0.5 * (s1 - s0) * xi.at(q);
                const double difference = force - exactForce(s);
                sum += weight.at(q) * 0.5 * (s1 - s0) * difference * difference;
            }
            // Nodally exact, for every n.
            const double s = s1;
            CHECK(near(hanging.result.displacements[static_cast<std::size_t>(e) + 1].y(),
                       -rhoG * (length * s - s * s / 2.0) / kYoung,
                       rhoG * length * length / kYoung));
        }
        errors.push_back(std::sqrt(sum));
    }
    const double rate = std::log2(errors[1] / errors[2]);
    INFO("errors " << errors[0] << ", " << errors[1] << ", " << errors[2] << "; rate " << rate);
    CHECK(std::abs(rate - 1.0) < 0.1);
    CHECK(errors[2] < errors[1]);
}
