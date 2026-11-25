// REQ-007-09, 10, 14: validation of the model before the analysis.

// GCC 13 at -O2 warns that the std::vector of the inactive NodeIds or
// ElementIds alternative "may be used uninitialized" when a temporary holding
// a GroupRef is moved into the model: a false positive of its variant
// analysis.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include "bar_meshes.hpp"

#include <fem/core/static_analysis.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

using fem::core::DofType;
using fem::core::ElementIds;
using fem::core::ElementKind;
using fem::core::GroupRef;
using fem::core::Model;
using fem::core::ModelIssue;
using fem::core::NodeIds;
using fem::core::test::TrussSpec;

namespace {

// A triangle of bars in the XY plane (nodes 1, 2, 3; bars 1, 2, 3) and a free
// node 4 used by no bar, with a group of points "base" on nodes 1 and 2.
Model triangleModel()
{
    TrussSpec spec;
    spec.nodes = {{0.0, 0.0, 0.0}, {4.0, 0.0, 0.0}, {2.0, 3.0, 0.0}, {9.0, 9.0, 0.0}};
    spec.bars = {{0, 1}, {1, 2}, {2, 0}};
    spec.lineGroups = {{"bars", {0, 1, 2}}};
    spec.pointGroups = {{"base", {0, 1}}};
    Model model;
    model.mesh = fem::core::test::trussMesh(spec);
    model.materials.push_back({.name = "Steel",
                               .youngsModulus = 200e9,
                               .poissonsRatio = 0.3,
                               .density = 7850.0,
                               .thermalExpansion = 1.2e-5,
                               .conductivity = {},
                               .specificHeat = {}});
    model.sections.push_back({.name = "Rod", .area = 1e-4});
    model.assignments.push_back({.elements = GroupRef{"bars"},
                                 .kind = ElementKind::Truss2D,
                                 .material = "Steel",
                                 .section = "Rod"});
    model.restraints.push_back({.nodes = GroupRef{"base"}, .dof = DofType::V, .value = 0.0});
    model.restraints.push_back({.nodes = NodeIds{{1}}, .dof = DofType::U, .value = 0.0});
    model.nodalForces.push_back({.nodes = NodeIds{{3}}, .force = Eigen::Vector3d(1.0, -2.0, 0.0)});
    return model;
}

std::vector<std::string> messages(const Model& model)
{
    std::vector<std::string> result;
    for (const ModelIssue& issue : fem::core::validate(model)) {
        result.push_back(issue.message);
    }
    return result;
}

bool has(const std::vector<std::string>& list, const std::string& message)
{
    return std::ranges::find(list, message) != list.end();
}

} // namespace

TEST_CASE("a complete model has no issues", "[core][validation]")
{
    Model model = triangleModel();
    CHECK(messages(model).empty());
    // Actions that need more properties, all given.
    model.gravity = Eigen::Vector3d(0.0, -9.81, 0.0);
    model.temperatureChanges.push_back({.elements = ElementIds{{1}}, .deltaT = 30.0});
    model.initialStrains.push_back({.elements = GroupRef{"bars"}, .strain = 1e-4});
    CHECK(messages(model).empty());
}

// REQ-007-14: each problem names the item.
TEST_CASE("missing pieces of the model", "[core][validation]")
{
    SECTION("no mesh")
    {
        const Model model;
        CHECK(messages(model) == std::vector<std::string>{"The model has no mesh."});
    }
    SECTION("no assignment")
    {
        Model model = triangleModel();
        model.assignments.clear();
        model.restraints.clear();
        model.nodalForces.clear();
        CHECK(messages(model) ==
              std::vector<std::string>{"No element has an assignment: the model is empty."});
    }
    SECTION("unassigned elements")
    {
        Model model = triangleModel();
        model.assignments.front().elements = ElementIds{{1, 2}};
        CHECK(messages(model) == std::vector<std::string>{"Elements without an assignment: 3."});
    }
    SECTION("two assignments")
    {
        Model model = triangleModel();
        model.assignments.push_back(model.assignments.front());
        model.assignments.back().elements = ElementIds{{2}};
        CHECK(has(messages(model), "The element 2 has two assignments (1 and 2)."));
    }
    SECTION("unknown material and section")
    {
        Model model = triangleModel();
        model.assignments.front().material = "Wood";
        model.assignments.front().section = "Tube";
        const auto list = messages(model);
        CHECK(has(list, "Assignment 1: material \"Wood\" does not exist."));
        CHECK(has(list, "Assignment 1: section \"Tube\" does not exist."));
    }
    SECTION("properties not given or not positive")
    {
        Model model = triangleModel();
        model.materials.front().youngsModulus.reset();
        model.sections.front().area = 0.0;
        model.materials.front().density = -1.0;
        model.materials.front().thermalExpansion.reset();
        model.gravity = Eigen::Vector3d(0.0, -9.81, 0.0);
        model.temperatureChanges.push_back({.elements = GroupRef{"bars"}, .deltaT = 10.0});
        const auto list = messages(model);
        CHECK(has(list, "Material \"Steel\": the Young's modulus must be given and positive."));
        CHECK(has(list, "Section \"Rod\": the area must be given and positive."));
        CHECK(has(list, "Material \"Steel\": the density must be given and positive for the "
                        "self-weight."));
        CHECK(has(list, "Material \"Steel\": the thermal expansion must be given for the "
                        "temperature change."));
        CHECK(list.size() == 4); // each once, though three bars use them
    }
    SECTION("targets that do not resolve")
    {
        Model model = triangleModel();
        model.assignments.front().elements = GroupRef{"beams"};
        model.restraints.front().nodes = NodeIds{{42}};
        model.nodalForces.front().nodes = GroupRef{"top"};
        model.temperatureChanges.push_back({.elements = ElementIds{{9}}, .deltaT = 1.0});
        model.initialStrains.push_back({.elements = GroupRef{"base"}, .strain = 1.0});
        const auto list = messages(model);
        CHECK(has(list, "Assignment 1: The mesh has no group \"beams\" of lines."));
        CHECK(has(list, "Restraint 1: The mesh has no node 42."));
        CHECK(has(list, "Nodal force 1: The mesh has no group \"top\"."));
        CHECK(has(list, "Temperature change 1: The mesh has no element 9."));
        CHECK(has(list, "Initial strain 1: The mesh has no group \"base\" of lines."));
    }
}

// REQ-007-09, 10: what a bar must be.
TEST_CASE("bars that cannot be trusses", "[core][validation]")
{
    SECTION("a 2D truss out of the XY plane")
    {
        TrussSpec spec;
        spec.nodes = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.5}};
        spec.bars = {{0, 1}};
        Model model = triangleModel();
        model.mesh = fem::core::test::trussMesh(spec);
        model.assignments.front().elements = ElementIds{{1}};
        model.restraints.clear();
        model.nodalForces.clear();
        CHECK(messages(model) ==
              std::vector<std::string>{"The element 1 is a 2D truss bar but does not lie in the "
                                       "XY plane."});
        model.assignments.front().kind = ElementKind::Truss3D;
        CHECK(messages(model).empty());
    }
    SECTION("an element that is not a 2-node line")
    {
        fem::core::MeshBuilder builder;
        for (int i = 0; i < 3; ++i) {
            REQUIRE(builder.addNode(i + 1, Eigen::Vector3d(i % 2, i == 2 ? 1.0 : 0.0, 0.0))
                        .has_value());
        }
        const std::vector<fem::core::ExternalId> triangle = {1, 2, 3};
        REQUIRE(builder.addElement(fem::core::ElementType::Tri3, 5, triangle).has_value());
        auto mesh = builder.build();
        REQUIRE(mesh.has_value());
        Model model = triangleModel();
        model.mesh = std::make_shared<const fem::core::Mesh>(std::move(*mesh));
        model.assignments.front().elements = ElementIds{{5}};
        model.restraints.clear();
        model.nodalForces.clear();
        CHECK(messages(model) ==
              std::vector<std::string>{"The element 5 is a Tri3, not a 2-node line, and cannot "
                                       "be a truss bar."});
    }
    SECTION("a bar of zero length")
    {
        TrussSpec spec;
        spec.nodes = {{1.0, 1.0, 0.0}, {1.0, 1.0, 0.0}};
        spec.bars = {{0, 1}};
        Model model = triangleModel();
        model.mesh = fem::core::test::trussMesh(spec);
        model.assignments.front().elements = ElementIds{{1}};
        model.restraints.clear();
        model.nodalForces.clear();
        CHECK(messages(model) == std::vector<std::string>{"The element 1 has zero length."});
    }
}

// REQ-007-14: supports and loads only where there are dofs.
TEST_CASE("supports and loads on nodes without dofs", "[core][validation]")
{
    Model model = triangleModel();
    model.restraints.push_back({.nodes = NodeIds{{4}}, .dof = DofType::U, .value = 0.0});
    model.restraints.push_back({.nodes = NodeIds{{3}}, .dof = DofType::W, .value = 0.0});
    model.restraints.push_back({.nodes = NodeIds{{3}}, .dof = DofType::RZ, .value = 0.0});
    model.nodalForces.push_back({.nodes = NodeIds{{4}}, .force = Eigen::Vector3d(1.0, 0.0, 0.0)});
    model.nodalForces.push_back({.nodes = NodeIds{{2}}, .force = Eigen::Vector3d(0.0, 0.0, 1.0)});
    const auto list = messages(model);
    CHECK(list == std::vector<std::string>{
                      "Restraint 3: the node 4 is not connected to any bar.",
                      "Restraint 4: the node 3 has no dof w (it only belongs to 2D trusses).",
                      "Restraint 5: a truss model only has the dofs u, v and w.",
                      "Nodal force 2: the node 4 is not connected to any bar.",
                      "Nodal force 3: the node 2 has no dof w (it only belongs to 2D trusses)."});
}

// Points define groups of nodes and need no assignment.
TEST_CASE("point elements need no assignment", "[core][validation]")
{
    const Model model = triangleModel();
    REQUIRE(model.mesh->elementCount() == 5); // 3 bars and 2 points
    CHECK(messages(model).empty());
}

// REQ-007-12: one bar along x pulled at its end.
TEST_CASE("analysis of a single bar", "[core][analysis]")
{
    TrussSpec spec;
    spec.nodes = {{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}};
    spec.bars = {{0, 1}};
    Model model = triangleModel();
    model.mesh = fem::core::test::trussMesh(spec);
    model.assignments.front().elements = ElementIds{{1}};
    model.restraints = {{.nodes = NodeIds{{1}}, .dof = DofType::U, .value = 0.0},
                        {.nodes = NodeIds{{1, 2}}, .dof = DofType::V, .value = 0.0}};
    model.nodalForces = {{.nodes = NodeIds{{2}}, .force = Eigen::Vector3d(1000.0, 0.0, 0.0)}};

    const auto result = fem::core::runLinearStatic(model);
    REQUIRE(result.has_value());
    // EA = 2e7, L = 2: u = F L / EA = 1e-4.
    CHECK(std::abs(result->displacements[1].x() - 1e-4) < 1e-16);
    CHECK(result->displacements[1].y() == 0.0);
    CHECK(std::abs(result->reactions[0].x() + 1000.0) < 1e-9);
    CHECK(std::abs(result->reactions[1].y()) < 1e-9);
    CHECK(result->reactions[1].x() == 0.0); // free dof
    CHECK(std::abs(result->axialForce[0] - 1000.0) < 1e-9);
    CHECK(std::abs(result->strain[0] - 5e-5) < 1e-18);
    CHECK(std::abs(result->stress[0] - 1e7) < 1e-6);
    CHECK(result->mesh == model.mesh);
}

// REQ-007-13: the model is only read.
TEST_CASE("the analysis leaves the model as it was", "[core][analysis]")
{
    Model model = triangleModel();
    model.gravity = Eigen::Vector3d(0.0, -9.81, 0.0);
    model.temperatureChanges.push_back({.elements = GroupRef{"bars"}, .deltaT = 15.0});
    const Model before = model;
    const auto useCount = model.mesh.use_count();

    const auto first = fem::core::runLinearStatic(model);
    REQUIRE(first.has_value());
    CHECK(model.mesh == before.mesh);
    CHECK(model.materials.size() == before.materials.size());
    CHECK(model.materials.front().youngsModulus == before.materials.front().youngsModulus);
    CHECK(model.sections.front().area == before.sections.front().area);
    CHECK(model.restraints.size() == before.restraints.size());
    CHECK(model.nodalForces.front().force == before.nodalForces.front().force);
    CHECK(model.gravity == before.gravity);
    CHECK(model.temperatureChanges.front().deltaT == before.temperatureChanges.front().deltaT);

    // The same model gives the same result, sequential or parallel.
    const auto second = fem::core::runLinearStatic(model, {.threads = 1, .control = {}});
    REQUIRE(second.has_value());
    for (std::size_t n = 0; n < first->displacements.size(); ++n) {
        CHECK((first->displacements[n] - second->displacements[n]).norm() < 1e-15);
    }
    CHECK(model.mesh.use_count() == useCount + 2); // held by the two results
}

// REQ-007-14: the analysis refuses an invalid model with every problem.
TEST_CASE("the analysis refuses an invalid model", "[core][analysis]")
{
    Model model = triangleModel();
    model.materials.front().youngsModulus = 0.0;
    model.restraints.push_back({.nodes = NodeIds{{4}}, .dof = DofType::U, .value = 0.0});
    const auto result = fem::core::runLinearStatic(model);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message ==
          "The model has 2 problems:\n"
          "- Material \"Steel\": the Young's modulus must be given and positive.\n"
          "- Restraint 3: the node 4 is not connected to any bar.");

    CHECK(fem::core::runLinearStatic(Model{}).error().message ==
          "The model has 1 problem:\n- The model has no mesh.");
}

// REQ-007-15: a node without stiffness in a direction, found on the diagonal.
TEST_CASE("a node without stiffness in a direction", "[core][analysis]")
{
    Model model = triangleModel();
    model.assignments.front().kind = ElementKind::Truss3D; // flat, in 3D
    model.restraints.push_back({.nodes = GroupRef{"base"}, .dof = DofType::W, .value = 0.0});
    const auto result = fem::core::runLinearStatic(model);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message ==
          "The structure is unstable: no bar gives stiffness to\n- the node 3 in w");
}

// REQ-007-15: a square without its diagonal folds.
TEST_CASE("a mechanism is reported with a node and a direction", "[core][analysis]")
{
    TrussSpec spec;
    spec.nodes = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {0.0, 1.0, 0.0}};
    spec.bars = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};
    Model model = triangleModel();
    model.mesh = fem::core::test::trussMesh(spec);
    model.assignments.front().elements = ElementIds{{1, 2, 3, 4}};
    model.restraints = {{.nodes = NodeIds{{1}}, .dof = DofType::U, .value = 0.0},
                        {.nodes = NodeIds{{1, 2}}, .dof = DofType::V, .value = 0.0}};
    model.nodalForces = {{.nodes = NodeIds{{3}}, .force = Eigen::Vector3d(1.0, 0.0, 0.0)}};
    const auto result = fem::core::runLinearStatic(model);
    REQUIRE_FALSE(result.has_value());
    const std::string& message = result.error().message;
    INFO(message);
    CHECK(message.starts_with("The structure is a mechanism: the node "));
    CHECK(message.ends_with(" can move without resistance (a support may be missing)."));

    // With the diagonal it stands.
    spec.bars.push_back({0, 2});
    model.mesh = fem::core::test::trussMesh(spec);
    model.assignments.front().elements = ElementIds{{1, 2, 3, 4, 5}};
    CHECK(fem::core::runLinearStatic(model).has_value());
}
