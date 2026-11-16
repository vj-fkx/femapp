// REQ-007-01 to 07: materials, sections and the Model; resolution of the
// element and node targets on the mesh.

#include "truss_meshes.hpp"

#include <fem/core/model.hpp>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using fem::core::ElementIndex;
using fem::core::ExternalId;
using fem::core::GroupRef;
using fem::core::Model;
using fem::core::NodeIndex;
using fem::core::test::kPointIdBase;
using fem::core::test::TrussSpec;

namespace {

// Three bars in a row along x, with groups of lines and of points.
TrussSpec rowSpec()
{
    TrussSpec spec;
    for (int i = 0; i < 4; ++i) {
        spec.nodes.emplace_back(i, 0.0, 0.0);
    }
    spec.bars = {{0, 1}, {1, 2}, {2, 3}};
    spec.lineGroups = {{"left", {0, 1}}, {"ends", {2}}};
    spec.pointGroups = {{"ends", {0, 3}}, {"middle", {1}}};
    return spec;
}

} // namespace

// REQ-007-03, 04, 05: targets by group and by external ids.
TEST_CASE("element targets resolve to internal indices", "[core][model]")
{
    const auto mesh = fem::core::test::trussMesh(rowSpec());

    const auto left = fem::core::resolve(*mesh, fem::core::ElementTarget{GroupRef{"left"}});
    REQUIRE(left.has_value());
    CHECK(left.value_or(std::vector<ElementIndex>{}).size() == 2);

    const auto listed =
        fem::core::resolve(*mesh, fem::core::ElementTarget{fem::core::ElementIds{{3, 1, 3}}});
    REQUIRE(listed.has_value());
    const auto indices = listed.value_or(std::vector<ElementIndex>{});
    REQUIRE(indices.size() == 2); // sorted and unique
    CHECK(mesh->elementId(indices[0]) == 1);
    CHECK(mesh->elementId(indices[1]) == 3);

    // Only groups of lines are element targets.
    const auto middle = fem::core::resolve(*mesh, fem::core::ElementTarget{GroupRef{"middle"}});
    REQUIRE_FALSE(middle.has_value());
    CHECK(middle.error().message == "The mesh has no group \"middle\" of lines.");

    const auto unknown =
        fem::core::resolve(*mesh, fem::core::ElementTarget{fem::core::ElementIds{{7}}});
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().message == "The mesh has no element 7.");
}

TEST_CASE("node targets resolve to internal indices", "[core][model]")
{
    const auto mesh = fem::core::test::trussMesh(rowSpec());
    const auto ids = [&](const std::vector<NodeIndex>& nodes) {
        std::vector<ExternalId> result;
        result.reserve(nodes.size());
        for (const NodeIndex node : nodes) {
            result.push_back(mesh->nodeId(node));
        }
        return result;
    };

    // "ends" names a group of points and a group of lines: their nodes join.
    const auto ends = fem::core::resolve(*mesh, fem::core::NodeTarget{GroupRef{"ends"}});
    REQUIRE(ends.has_value());
    CHECK(ids(ends.value_or(std::vector<NodeIndex>{})) == std::vector<ExternalId>{1, 3, 4});

    const auto middle = fem::core::resolve(*mesh, fem::core::NodeTarget{GroupRef{"middle"}});
    REQUIRE(middle.has_value());
    CHECK(ids(middle.value_or(std::vector<NodeIndex>{})) == std::vector<ExternalId>{2});

    const auto listed =
        fem::core::resolve(*mesh, fem::core::NodeTarget{fem::core::NodeIds{{4, 2, 2}}});
    REQUIRE(listed.has_value());
    CHECK(ids(listed.value_or(std::vector<NodeIndex>{})) == std::vector<ExternalId>{2, 4});

    const auto missing = fem::core::resolve(*mesh, fem::core::NodeTarget{GroupRef{"top"}});
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().message == "The mesh has no group \"top\".");

    const auto unknown =
        fem::core::resolve(*mesh, fem::core::NodeTarget{fem::core::NodeIds{{kPointIdBase}}});
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().message == "The mesh has no node 100000.");
}

// REQ-007-01 to 03, 06, 07: editing and copying the model.
TEST_CASE("the model is edited and copied as a value", "[core][model]")
{
    Model model;
    model.mesh = fem::core::test::trussMesh(rowSpec());
    model.materials.push_back({.name = "Steel",
                               .youngsModulus = 210e9,
                               .poissonsRatio = 0.3,
                               .density = 7850.0,
                               .thermalExpansion = 1.2e-5,
                               .conductivity = {},
                               .specificHeat = {}});
    model.sections.push_back({.name = "Rod", .area = 1e-4});
    model.assignments.push_back({.elements = GroupRef{"left"},
                                 .kind = fem::core::ElementKind::Truss2D,
                                 .material = "Steel",
                                 .section = "Rod"});
    model.restraints.push_back(
        {.nodes = fem::core::NodeIds{{1}}, .dof = fem::core::DofType::U, .value = 0.0});
    model.nodalForces.push_back(
        {.nodes = GroupRef{"middle"}, .force = Eigen::Vector3d(0.0, -1.0, 0.0)});
    model.gravity = Eigen::Vector3d(0.0, -9.81, 0.0);
    model.temperatureChanges.push_back({.elements = GroupRef{"left"}, .deltaT = 20.0});
    model.initialStrains.push_back({.elements = fem::core::ElementIds{{3}}, .strain = 1e-4});

    REQUIRE(model.findMaterial("Steel") != nullptr);
    CHECK(model.findMaterial("Steel")->youngsModulus == 210e9);
    CHECK_FALSE(model.findMaterial("Steel")->conductivity.has_value());
    CHECK(model.findMaterial("Wood") == nullptr);
    REQUIRE(model.findSection("Rod") != nullptr);
    CHECK(model.findSection("Rod")->area == 1e-4);

    // A copy shares the mesh and is independent otherwise.
    Model copy = model;
    CHECK(copy.mesh == model.mesh);
    copy.materials.front().youngsModulus = 70e9;
    copy.sections.clear();
    copy.assignments.front().section = "Bar";
    CHECK(model.findMaterial("Steel")->youngsModulus == 210e9);
    CHECK(model.sections.size() == 1);
    CHECK(model.assignments.front().section == "Rod");
    CHECK(copy.findSection("Rod") == nullptr);

    // Removal leaves dangling names, caught by validation, not here.
    model.materials.erase(model.materials.begin());
    CHECK(model.findMaterial("Steel") == nullptr);
    CHECK(model.assignments.front().material == "Steel");
}
