// Verification of the kernel with the test-only Poisson element (REQ-006-12
// to 17, constitution §5.1): patch test and convergence against an exact
// solution.

#include "poisson_element.hpp"
#include "unit_meshes.hpp"

#include <fem/core/dof_map.hpp>
#include <fem/core/isoparametric.hpp>
#include <fem/core/linear_solver.hpp>
#include <fem/core/linear_static.hpp>
#include <fem/core/quadrature.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <string>
#include <vector>

using fem::core::DofMap;
using fem::core::DofType;
using fem::core::ElementSet;
using fem::core::ElementType;
using fem::core::test::PoissonElement;
using fem::core::test::UnitMesh;

namespace {

constexpr double kPi = std::numbers::pi;

// Linear field: reproduced exactly by every element (patch test).
double linearField(const Eigen::Vector3d& x)
{
    return 1.0 + 2.0 * x.x() - x.y() + 3.0 * x.z();
}

int gDimension = 1;

// u = prod sin(pi x_k) over the dimensions, and its source d pi^2 u.
double exactField(const Eigen::Vector3d& x)
{
    double u = 1.0;
    for (int k = 0; k < gDimension; ++k) {
        u *= std::sin(kPi * x(k));
    }
    return u;
}

double exactSource(const Eigen::Vector3d& x)
{
    return gDimension * kPi * kPi * exactField(x);
}

struct Solved
{
    DofMap dofs;
    fem::core::LinearSolution solution;
};

// Solves -lap u = s on `unit` with u = `boundary` on the boundary nodes.
Solved solve(const UnitMesh& unit, const PoissonElement& element,
             double (*boundary)(const Eigen::Vector3d&))
{
    const std::vector<ElementSet> sets = {{.elements = unit.elements, .formulation = &element}};
    DofMap dofs = DofMap::build(unit.mesh, sets);
    for (const auto node : unit.boundary) {
        const double value = boundary(unit.mesh.nodeCoordinates(node));
        REQUIRE(dofs.prescribe(node, DofType::T, value).has_value());
    }
    fem::core::SparseCholesky solver;
    auto solution = fem::core::solveLinear(unit.mesh, dofs, sets, {}, solver);
    REQUIRE(solution.has_value());
    return {.dofs = std::move(dofs), .solution = std::move(*solution)};
}

// L2 norm of u_h - u over the mesh.
double l2Error(const UnitMesh& unit, const Solved& solved, double (*exact)(const Eigen::Vector3d&))
{
    const auto& mesh = unit.mesh;
    double sum = 0.0;
    fem::core::ElementNodes nodes;
    for (const auto element : unit.elements) {
        nodes.type = mesh.elementType(element);
        const auto local = mesh.elementNodes(element);
        nodes.count = static_cast<int>(local.size());
        for (std::size_t i = 0; i < local.size(); ++i) {
            nodes.x.at(i) = mesh.nodeCoordinates(local[i]);
        }
        // Degree 2p + 3: enough for the error of a smooth field.
        const auto& info = fem::core::elementTypeInfo(nodes.type);
        const int degree =
            std::min(2 * info.order + 3, fem::core::maxQuadratureDegree(info.family));
        for (const auto& point : fem::core::quadrature(info.family, degree)) {
            const auto g = fem::core::evaluateGeometry(nodes, point.xi);
            REQUIRE(g.has_value());
            double uh = 0.0;
            for (std::size_t i = 0; i < local.size(); ++i) {
                const auto dof = solved.dofs.find(local[i], DofType::T).value_or(0);
                uh += g->shape.n.at(i) * solved.solution.values[dof];
            }
            const double e = uh - exact(g->position);
            sum += point.weight * g->measure * e * e;
        }
    }
    return std::sqrt(sum);
}

} // namespace

// REQ-006-01 to 04, 12 to 17: every element reproduces a linear field on a
// distorted mesh, and the reactions balance.
TEST_CASE("Poisson patch test", "[core][poisson]")
{
    const ElementType type =
        GENERATE(ElementType::Line2, ElementType::Line3, ElementType::Tri3, ElementType::Tri6,
                 ElementType::Quad4, ElementType::Quad8, ElementType::Quad9, ElementType::Tet4,
                 ElementType::Tet10, ElementType::Hex8, ElementType::Hex20, ElementType::Hex27,
                 ElementType::Wedge6, ElementType::Wedge15, ElementType::Wedge18);
    const auto& info = fem::core::elementTypeInfo(type);
    INFO("type " << std::string(info.name));
    const UnitMesh unit = fem::core::test::unitMesh(type, info.dimension == 3 ? 3 : 4, 0.04);
    const PoissonElement element;
    const Solved solved = solve(unit, element, linearField);

    double total = 0.0;
    for (fem::core::DofIndex d = 0; d < solved.dofs.size(); ++d) {
        const auto x = unit.mesh.nodeCoordinates(solved.dofs.nodeOf(d));
        CHECK(std::abs(solved.solution.values[d] - linearField(x)) < 1e-10);
        total += solved.solution.reactions[d];
    }
    // No source: the boundary fluxes add up to zero.
    CHECK(std::abs(total) < 1e-10);
}

// REQ-006-17: with a unit source the reactions balance it.
TEST_CASE("reactions balance the source", "[core][poisson]")
{
    const ElementType type = GENERATE(ElementType::Tri6, ElementType::Hex8, ElementType::Tet10);
    const auto& info = fem::core::elementTypeInfo(type);
    const UnitMesh unit = fem::core::test::unitMesh(type, 3, 0.04);
    const PoissonElement element([](const Eigen::Vector3d&) { return 1.0; });
    const Solved solved = solve(unit, element, [](const Eigen::Vector3d&) { return 0.0; });
    double total = 0.0;
    for (fem::core::DofIndex d = 0; d < solved.dofs.size(); ++d) {
        total += solved.solution.reactions[d];
    }
    // The volume of the unit domain is 1 (the distortion keeps it).
    INFO("type " << std::string(info.name));
    CHECK(std::abs(total + 1.0) < 1e-10);
}

// REQ-006-01, 03, 06, 13, 15, 17: the L2 error falls as h^(p+1).
TEST_CASE("Poisson convergence rates", "[core][poisson]")
{
    struct Case
    {
        ElementType type;
        int coarse;
        double rate;
    };
    const Case c = GENERATE(Case{.type = ElementType::Line2, .coarse = 8, .rate = 2.0},
                            Case{.type = ElementType::Line3, .coarse = 4, .rate = 3.0},
                            Case{.type = ElementType::Tri3, .coarse = 8, .rate = 2.0},
                            Case{.type = ElementType::Tri6, .coarse = 4, .rate = 3.0},
                            Case{.type = ElementType::Quad4, .coarse = 8, .rate = 2.0},
                            Case{.type = ElementType::Quad8, .coarse = 4, .rate = 3.0},
                            Case{.type = ElementType::Quad9, .coarse = 4, .rate = 3.0},
                            Case{.type = ElementType::Tet4, .coarse = 3, .rate = 2.0},
                            Case{.type = ElementType::Tet10, .coarse = 2, .rate = 3.0},
                            Case{.type = ElementType::Hex8, .coarse = 3, .rate = 2.0},
                            Case{.type = ElementType::Hex20, .coarse = 2, .rate = 3.0},
                            Case{.type = ElementType::Hex27, .coarse = 2, .rate = 3.0},
                            Case{.type = ElementType::Wedge6, .coarse = 3, .rate = 2.0},
                            Case{.type = ElementType::Wedge15, .coarse = 2, .rate = 3.0},
                            Case{.type = ElementType::Wedge18, .coarse = 2, .rate = 3.0});
    const auto& info = fem::core::elementTypeInfo(c.type);
    INFO("type " << std::string(info.name));
    gDimension = info.dimension;
    const PoissonElement element(exactSource);

    std::vector<double> errors;
    for (int level = 0; level < 3; ++level) {
        const UnitMesh unit = fem::core::test::unitMesh(c.type, c.coarse << level);
        const Solved solved = solve(unit, element, exactField);
        errors.push_back(l2Error(unit, solved, exactField));
    }
    const double rate = std::log2(errors[1] / errors[2]);
    INFO("errors " << errors[0] << ", " << errors[1] << ", " << errors[2] << "; rate " << rate);
    CHECK(std::abs(rate - c.rate) < 0.2);
    CHECK(errors[2] < errors[1]);
}
