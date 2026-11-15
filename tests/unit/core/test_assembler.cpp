#include "poisson_element.hpp"
#include "unit_meshes.hpp"

#include <fem/core/assembler.hpp>
#include <fem/core/dof_map.hpp>
#include <fem/core/linear_solver.hpp>
#include <fem/core/linear_static.hpp>
#include <fem/core/mesh_builder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

using fem::core::DofMap;
using fem::core::DofType;
using fem::core::ElementIndex;
using fem::core::ElementSet;
using fem::core::ElementType;
using fem::core::NodalLoad;
using fem::core::test::PoissonElement;

namespace {

// Three nodes at x = 0, 1, 3 and two bars: element matrices [1 -1; -1 1] / L.
fem::core::Mesh bars()
{
    fem::core::MeshBuilder builder;
    REQUIRE(builder.addNode(1, Eigen::Vector3d(0, 0, 0)).has_value());
    REQUIRE(builder.addNode(2, Eigen::Vector3d(1, 0, 0)).has_value());
    REQUIRE(builder.addNode(3, Eigen::Vector3d(3, 0, 0)).has_value());
    REQUIRE(builder.addElement(ElementType::Line2, 1, std::vector<fem::core::ExternalId>{1, 2})
                .has_value());
    REQUIRE(builder.addElement(ElementType::Line2, 2, std::vector<fem::core::ExternalId>{2, 3})
                .has_value());
    auto built = builder.build();
    REQUIRE(built.has_value());
    return std::move(*built);
}

double source(const Eigen::Vector3d& x)
{
    return 1.0 + x.x() * x.y() - x.z();
}

} // namespace

// REQ-006-12, 13, 14, 10: the matrix of two elements, a settlement on the
// right side and a nodal load.
TEST_CASE("assembly of two bars by hand", "[core][assembly]")
{
    const fem::core::Mesh mesh = bars();
    const PoissonElement element;
    const std::vector<ElementIndex> all = {0, 1};
    const std::vector<ElementSet> sets = {{.elements = all, .formulation = &element}};
    DofMap dofs = DofMap::build(mesh, sets);
    REQUIRE(dofs.prescribe(0, DofType::T, 2.0).has_value());
    const std::vector<NodalLoad> loads = {{.node = 2, .type = DofType::T, .value = 3.0}};

    const auto system = fem::core::assemble(mesh, dofs, sets, loads);
    REQUIRE(system.has_value());
    const Eigen::MatrixXd k = Eigen::MatrixXd(system->matrix);
    REQUIRE(k.rows() == 2);
    CHECK(std::abs(k(0, 0) - 1.5) < 1e-14);
    CHECK(std::abs(k(1, 0) + 0.5) < 1e-14);
    CHECK(std::abs(k(1, 1) - 0.5) < 1e-14);
    CHECK(k(0, 1) == 0.0);                         // lower triangle only
    CHECK(std::abs(system->rhs[0] - 2.0) < 1e-14); // -K_10 * 2
    CHECK(std::abs(system->rhs[1] - 3.0) < 1e-14);

    // REQ-006-15, 17: u1 = 2 + 3 * 1, u2 = 5 + 3 * 2; reaction -3 at node 1.
    fem::core::SparseCholesky solver;
    const auto solution = fem::core::solveLinear(mesh, dofs, sets, loads, solver);
    REQUIRE(solution.has_value());
    CHECK(std::abs(solution->values[0] - 2.0) < 1e-12);
    CHECK(std::abs(solution->values[1] - 5.0) < 1e-12);
    CHECK(std::abs(solution->values[2] - 11.0) < 1e-12);
    CHECK(std::abs(solution->reactions[0] + 3.0) < 1e-12);
    CHECK(solution->reactions[1] == 0.0);
}

// REQ-006-13: the parallel assembly equals the sequential one.
TEST_CASE("parallel assembly equals the sequential one", "[core][assembly]")
{
    const auto unit = fem::core::test::unitMesh(ElementType::Tet4, 15, 0.03);
    REQUIRE(unit.mesh.elementCount() > 20000);
    const PoissonElement element(source);
    const std::vector<ElementSet> sets = {{.elements = unit.elements, .formulation = &element}};
    DofMap dofs = DofMap::build(unit.mesh, sets);
    for (const auto node : unit.boundary) {
        REQUIRE(dofs.prescribe(node, DofType::T, 0.5).has_value());
    }

    const auto sequential = fem::core::assemble(unit.mesh, dofs, sets, {}, 1);
    const auto parallel = fem::core::assemble(unit.mesh, dofs, sets, {}, 0);
    REQUIRE(sequential.has_value());
    REQUIRE(parallel.has_value());
    REQUIRE(sequential->matrix.nonZeros() == parallel->matrix.nonZeros());
    const double scale = sequential->matrix.coeffs().cwiseAbs().maxCoeff();
    CHECK((sequential->matrix.coeffs() - parallel->matrix.coeffs()).cwiseAbs().maxCoeff() <=
          1e-12 * scale);
    CHECK((sequential->rhs - parallel->rhs).cwiseAbs().maxCoeff() <=
          1e-12 * sequential->rhs.cwiseAbs().maxCoeff());
}

// REQ-006-04, REQ-006-14: an inverted element and a load on a missing dof.
TEST_CASE("assembly reports element and load errors", "[core][assembly]")
{
    fem::core::MeshBuilder builder;
    REQUIRE(builder.addNode(1, Eigen::Vector3d(0, 0, 0)).has_value());
    REQUIRE(builder.addNode(2, Eigen::Vector3d(1, 0, 0)).has_value());
    REQUIRE(builder.addNode(3, Eigen::Vector3d(0, 1, 0)).has_value());
    REQUIRE(builder.addNode(4, Eigen::Vector3d(0, 0, 1)).has_value());
    // Nodes 2 and 3 swapped: negative volume.
    REQUIRE(builder.addElement(ElementType::Tet4, 7, std::vector<fem::core::ExternalId>{1, 3, 2, 4})
                .has_value());
    const auto mesh = builder.build();
    REQUIRE(mesh.has_value());
    const PoissonElement element;
    const std::vector<ElementIndex> all = {0};
    const std::vector<ElementSet> sets = {{.elements = all, .formulation = &element}};
    const DofMap dofs = DofMap::build(*mesh, sets);
    const auto system = fem::core::assemble(*mesh, dofs, sets, {});
    REQUIRE_FALSE(system.has_value());
    CHECK(system.error().message.find("Element 7") != std::string::npos);

    const fem::core::Mesh twoBars = bars();
    const std::vector<ElementIndex> one = {0};
    const std::vector<ElementSet> first = {{.elements = one, .formulation = &element}};
    const DofMap partial = DofMap::build(twoBars, first);
    const std::vector<NodalLoad> loads = {{.node = 2, .type = DofType::T, .value = 1.0}};
    CHECK_FALSE(fem::core::assemble(twoBars, partial, first, loads).has_value());
}

// REQ-006-16: a mechanism (no support) is refused; a badly scaled but well
// posed system is accepted.
TEST_CASE("the direct solver detects singular systems", "[core][solver]")
{
    const fem::core::Mesh mesh = bars();
    const PoissonElement element;
    const std::vector<ElementIndex> all = {0, 1};
    const std::vector<ElementSet> sets = {{.elements = all, .formulation = &element}};
    const DofMap dofs = DofMap::build(mesh, sets);
    fem::core::SparseCholesky solver;
    const auto solution = fem::core::solveLinear(mesh, dofs, sets, {}, solver);
    REQUIRE_FALSE(solution.has_value());
    CHECK(solution.error().message.find("singular") != std::string::npos);

    fem::core::SparseMatrix scaled(3, 3);
    scaled.insert(0, 0) = 1e6;
    scaled.insert(1, 1) = 1.0;
    scaled.insert(2, 2) = 1e-6;
    scaled.insert(1, 0) = 10.0;
    scaled.makeCompressed();
    REQUIRE(solver.factorize(scaled).has_value());
    const auto x = solver.solve(Eigen::Vector3d(1e6 + 10.0, 11.0, 1e-6));
    REQUIRE(x.has_value());
    CHECK((*x - Eigen::Vector3d(1, 1, 1)).norm() < 1e-9);
}

// REQ-006-17: every dof prescribed: no system to solve, reactions anyway.
TEST_CASE("a problem without free dofs", "[core][solver]")
{
    const fem::core::Mesh mesh = bars();
    const PoissonElement element;
    const std::vector<ElementIndex> all = {0, 1};
    const std::vector<ElementSet> sets = {{.elements = all, .formulation = &element}};
    DofMap dofs = DofMap::build(mesh, sets);
    for (fem::core::NodeIndex n = 0; n < 3; ++n) {
        REQUIRE(dofs.prescribe(n, DofType::T, static_cast<double>(n)).has_value());
    }
    fem::core::SparseCholesky solver;
    const auto solution = fem::core::solveLinear(mesh, dofs, sets, {}, solver);
    REQUIRE(solution.has_value());
    CHECK(solution->values[2] == 2.0);
    // u = 0, 1, 2 at x = 0, 1, 3: fluxes 1 and 0.5; reactions -1, 0.5, 0.5.
    CHECK(std::abs(solution->reactions[0] + 1.0) < 1e-12);
    CHECK(std::abs(solution->reactions[1] - 0.5) < 1e-12);
    CHECK(std::abs(solution->reactions[2] - 0.5) < 1e-12);
}
