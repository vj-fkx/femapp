// REQ-008-01, 04, 06, 10, 13, 14, 17, 24, 25: validation of beam models.

// GCC 13 at -O2 warns that the std::vector of the inactive NodeIds or
// ElementIds alternative "may be used uninitialized" when a temporary holding
// a GroupRef is moved into the model: a false positive of its variant
// analysis.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include "beam_models.hpp"

#include <fem/core/mesh_builder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using fem::core::BeamTheory;
using fem::core::DofType;
using fem::core::ElementIds;
using fem::core::ElementKind;
using fem::core::Model;
using fem::core::NodeIds;
using fem::core::test::TrussSpec;

namespace {

std::vector<std::string> messages(const Model& model)
{
    std::vector<std::string> list;
    for (const auto& issue : fem::core::validate(model)) {
        list.push_back(issue.message);
    }
    return list;
}

bool has(const std::vector<std::string>& list, const std::string& message)
{
    const bool found = std::find(list.begin(), list.end(), message) != list.end();
    if (!found) {
        for (const auto& m : list) {
            UNSCOPED_INFO(m);
        }
    }
    return found;
}

// Two beams in a row along x: nodes 1, 2, 3; node 1 fixed.
Model rowModel(ElementKind kind, BeamTheory theory = BeamTheory::EulerBernoulli)
{
    TrussSpec spec;
    spec.nodes = {{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {4.0, 0.0, 0.0}};
    spec.bars = {{0, 1}, {1, 2}};
    Model model = fem::core::test::beamModel(spec, kind, theory);
    fem::core::test::fixAll(model, {1}, kind);
    return model;
}

} // namespace

TEST_CASE("a valid beam model has no problem", "[core][validation][beam]")
{
    CHECK(messages(rowModel(ElementKind::Beam2D)).empty());
    CHECK(messages(rowModel(ElementKind::Beam3D, BeamTheory::Timoshenko)).empty());
}

// REQ-008-01, 04, 24: material and section.
TEST_CASE("materials and sections of beams", "[core][validation][beam]")
{
    SECTION("Poisson's ratio for 3D and Timoshenko beams")
    {
        Model model = rowModel(ElementKind::Beam3D);
        model.materials.front().poissonsRatio.reset();
        CHECK(has(messages(model), "Material \"Steel\": the Poisson's ratio must be given and "
                                   "between -1 and 0.5 for beams."));
        model.materials.front().poissonsRatio = 0.5;
        CHECK(messages(model).size() == 1);
        // A 2D Euler-Bernoulli beam needs no shear modulus.
        Model plane = rowModel(ElementKind::Beam2D);
        plane.materials.front().poissonsRatio.reset();
        CHECK(messages(plane).empty());
    }
    SECTION("shape and properties together, and impossible shapes")
    {
        Model model = rowModel(ElementKind::Beam2D);
        model.sections.front().shape = fem::core::Rectangle{.b = 0.1, .h = 0.2};
        CHECK(messages(model) == std::vector<std::string>{"Section \"S\": give either a shape or "
                                                          "its properties, not both."});
        model.sections.front() = {.name = "S", .shape = fem::core::Rectangle{.b = -0.1, .h = 0.2}};
        CHECK(messages(model) ==
              std::vector<std::string>{"Section \"S\": the dimension b must be positive."});
    }
    SECTION("generic properties a beam needs")
    {
        Model model = rowModel(ElementKind::Beam3D, BeamTheory::Timoshenko);
        auto& section = model.sections.front();
        section.inertiaZ.reset();
        section.inertiaY = 0.0;
        section.torsionConstant.reset();
        section.shearAreaZ.reset();
        const auto list = messages(model);
        CHECK(has(list, "Section \"S\": the inertia Iz must be given and positive for beams."));
        CHECK(has(list, "Section \"S\": the inertia Iy must be given and positive for 3D beams."));
        CHECK(has(list, "Section \"S\": the torsion constant J must be given and positive for "
                        "3D beams."));
        CHECK(has(list, "Section \"S\": the shear areas Asy and Asz must be given and positive "
                        "for Timoshenko beams."));
        Model product = rowModel(ElementKind::Beam3D);
        product.sections.front().productOfInertia = 1e-4;
        CHECK(messages(product) ==
              std::vector<std::string>{"Section \"S\": Iy Iz - Iyz^2 must be positive."});
    }
    SECTION("a 2D beam needs a symmetric section")
    {
        Model model = rowModel(ElementKind::Beam2D);
        model.sections.front() = {.name = "S",
                                  .shape = fem::core::Angle{.b = 0.1, .h = 0.15, .t = 0.01}};
        CHECK(messages(model) == std::vector<std::string>{"Section \"S\": a 2D beam needs a "
                                                          "section symmetric about its local y "
                                                          "axis."});
        // A tee is symmetric about y; a channel has its shear centre off y.
        model.sections.front().shape = fem::core::Tee{.h = 0.2, .bf = 0.1, .tf = 0.01, .tw = 0.01};
        CHECK(messages(model).empty());
        model.sections.front().shape =
            fem::core::Channel{.h = 0.2, .bf = 0.1, .tf = 0.01, .tw = 0.01};
        CHECK(messages(model).size() == 1);
    }
}

// REQ-008-06, 10, 14: what a beam element must be.
TEST_CASE("elements that cannot be beams", "[core][validation][beam]")
{
    SECTION("2D beam out of the plane, also by an offset")
    {
        TrussSpec spec;
        spec.nodes = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.3}};
        spec.bars = {{0, 1}};
        Model model = fem::core::test::beamModel(spec, ElementKind::Beam2D);
        CHECK(messages(model) == std::vector<std::string>{"The element 1 is a 2D beam but does "
                                                          "not lie in the XY plane."});
        Model offset = rowModel(ElementKind::Beam2D);
        offset.rigidOffsets.push_back(
            {.elements = ElementIds{{2}}, .start = Eigen::Vector3d(0.0, 0.0, 0.1)});
        CHECK(messages(offset) == std::vector<std::string>{"The element 2 is a 2D beam but does "
                                                           "not lie in the XY plane."});
    }
    SECTION("an element that is not a 2-node line")
    {
        fem::core::MeshBuilder builder;
        for (int i = 0; i < 3; ++i) {
            REQUIRE(builder.addNode(i + 1, Eigen::Vector3d(i, 0.5 * i, 0.0)).has_value());
        }
        const std::vector<fem::core::ExternalId> line = {1, 3, 2};
        REQUIRE(builder.addElement(fem::core::ElementType::Line3, 4, line).has_value());
        auto mesh = builder.build();
        REQUIRE(mesh.has_value());
        Model model = rowModel(ElementKind::Beam3D);
        model.mesh = std::make_shared<const fem::core::Mesh>(std::move(*mesh));
        model.assignments.front().elements = ElementIds{{4}};
        model.restraints.clear();
        CHECK(messages(model) == std::vector<std::string>{"The element 4 is a Line3, not a "
                                                          "2-node line, and cannot be a beam."});
    }
    SECTION("a reference parallel to the bar")
    {
        Model model = rowModel(ElementKind::Beam3D);
        model.assignments.front().reference = Eigen::Vector3d(-2.0, 0.0, 0.0);
        const auto list = messages(model);
        CHECK(list.size() == 2);
        CHECK(has(list, "The element 1: the reference vector is parallel to the bar."));
    }
    SECTION("offsets that leave no flexible length")
    {
        Model model = rowModel(ElementKind::Beam3D);
        model.rigidOffsets.push_back({.elements = ElementIds{{1}},
                                      .start = Eigen::Vector3d(1.0, 0.0, 0.0),
                                      .end = Eigen::Vector3d(-1.0, 0.0, 0.0)});
        CHECK(messages(model) == std::vector<std::string>{"The element 1: the rigid offsets "
                                                          "leave no flexible length."});
    }
}

// REQ-008-11 to 15, 24: connections, offsets and foundations.
TEST_CASE("connections, offsets and foundations", "[core][validation][beam]")
{
    SECTION("unstable releases")
    {
        Model model = rowModel(ElementKind::Beam3D);
        fem::core::EndConnection free{.elements = ElementIds{{2}}, .end = fem::core::BarEnd::Both};
        free.stiffness[3] = 0.0; // torsion at both ends
        model.endConnections.push_back(free);
        CHECK(messages(model) ==
              std::vector<std::string>{"The element 2 is unstable on its own: its end releases "
                                       "let it move without strain."});
        // On a torsional foundation it holds.
        model.foundations.push_back({.elements = ElementIds{{2}}, .torsional = 1e3});
        CHECK(messages(model).empty());
    }
    SECTION("two items of a kind on one element")
    {
        Model model = rowModel(ElementKind::Beam2D);
        fem::core::EndConnection hinge{.elements = ElementIds{{1}}, .end = fem::core::BarEnd::End};
        hinge.stiffness[5] = 0.0;
        model.endConnections.push_back(hinge);
        model.endConnections.push_back(hinge);
        model.rigidOffsets.push_back({.elements = ElementIds{{2}}});
        model.rigidOffsets.push_back({.elements = ElementIds{{2}}});
        model.foundations.push_back({.elements = ElementIds{{1, 2}}});
        model.foundations.push_back({.elements = ElementIds{{1}}});
        const auto list = messages(model);
        CHECK(has(list, "The element 1 has two end connections at its end (1 and 2)."));
        CHECK(has(list, "The element 2 has two rigid offsets (1 and 2)."));
        CHECK(has(list, "The element 1 has two elastic foundations (1 and 2)."));
    }
    SECTION("negative stiffness")
    {
        Model model = rowModel(ElementKind::Beam2D);
        fem::core::EndConnection spring{.elements = ElementIds{{1}}};
        spring.stiffness[5] = -1.0;
        model.endConnections.push_back(spring);
        model.foundations.push_back(
            {.elements = ElementIds{{1}}, .translational = Eigen::Vector3d(0.0, -5.0, 0.0)});
        const auto list = messages(model);
        CHECK(has(list, "End connection 1: the stiffness must not be negative."));
        CHECK(has(list, "Elastic foundation 1: the stiffness must not be negative."));
    }
}

// REQ-008-16, 17, 24: beam-only items, positions and gradients.
TEST_CASE("loads of the span", "[core][validation][beam]")
{
    SECTION("beam items on a truss bar")
    {
        Model model = rowModel(ElementKind::Beam2D);
        model.sections.front().inertiaZ = 1e-5;
        model.assignments.front().elements = ElementIds{{1}};
        model.assignments.push_back({.elements = ElementIds{{2}},
                                     .kind = ElementKind::Truss2D,
                                     .material = "Steel",
                                     .section = "S"});
        model.endConnections.push_back({.elements = ElementIds{{2}}});
        model.rigidOffsets.push_back({.elements = ElementIds{{2}}});
        model.foundations.push_back({.elements = ElementIds{{2}}});
        model.distributedLoads.push_back({.elements = ElementIds{{1, 2}}});
        model.concentratedLoads.push_back({.elements = ElementIds{{2}}});
        model.temperatureGradients.push_back({.elements = ElementIds{{2}}});
        const auto list = messages(model);
        CHECK(has(list, "End connection 1: the element 2 is not a beam."));
        CHECK(has(list, "Rigid offset 1: the element 2 is not a beam."));
        CHECK(has(list, "Elastic foundation 1: the element 2 is not a beam."));
        CHECK(has(list, "Distributed load 1: the element 2 is not a beam."));
        CHECK(has(list, "Concentrated load 1: the element 2 is not a beam."));
        CHECK(has(list, "Temperature gradient 1: the element 2 is not a beam."));
        CHECK(list.size() == 6);
    }
    SECTION("positions on the flexible length")
    {
        Model model = rowModel(ElementKind::Beam3D);
        model.distributedLoads.push_back({.elements = ElementIds{{1}},
                                          .from = {.value = 0.8, .fraction = true},
                                          .to = {.value = 0.5, .fraction = false}});
        model.distributedLoads.push_back(
            {.elements = ElementIds{{2}}, .to = {.value = 2.5, .fraction = false}});
        model.concentratedLoads.push_back(
            {.elements = ElementIds{{1}}, .at = {.value = 1.2, .fraction = true}});
        // Within the flexible length left by an offset: 2 - 0.5.
        model.rigidOffsets.push_back(
            {.elements = ElementIds{{2}}, .start = Eigen::Vector3d(0.5, 0.0, 0.0)});
        model.concentratedLoads.push_back(
            {.elements = ElementIds{{2}}, .at = {.value = 1.5, .fraction = false}});
        CHECK(messages(model) ==
              std::vector<std::string>{
                  "Distributed load 1: on the element 1, the start is after the end.",
                  "Distributed load 2: on the element 2, the load goes beyond the flexible "
                  "length.",
                  "Concentrated load 1: on the element 1, the position is beyond the flexible "
                  "length."});
    }
    SECTION("a gradient needs the thermal expansion")
    {
        Model model = rowModel(ElementKind::Beam2D);
        model.materials.front().thermalExpansion.reset();
        model.temperatureGradients.push_back({.elements = ElementIds{{1, 2}}, .alongY = 10.0});
        CHECK(messages(model) ==
              std::vector<std::string>{"Material \"Steel\": the thermal expansion must be given "
                                       "for the temperature gradient."});
    }
}

// REQ-008-16, 24: moments and rotations only where there are rotations.
TEST_CASE("moments and rotations on nodes without them", "[core][validation][beam]")
{
    TrussSpec spec;
    spec.nodes = {{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {2.0, 1.0, 0.0}};
    spec.bars = {{0, 1}, {1, 2}};
    Model model = fem::core::test::beamModel(spec, ElementKind::Beam2D);
    model.assignments.front().elements = ElementIds{{1}};
    model.sections.push_back({.name = "Rod", .area = 1e-4});
    model.assignments.push_back({.elements = ElementIds{{2}},
                                 .kind = ElementKind::Truss2D,
                                 .material = "Steel",
                                 .section = "Rod"});
    fem::core::test::fixAll(model, {1}, ElementKind::Beam2D);
    fem::core::test::restrain(model, {3}, {DofType::U, DofType::V});
    model.restraints.push_back({.nodes = NodeIds{{3}}, .dof = DofType::RZ, .value = 0.0});
    model.restraints.push_back({.nodes = NodeIds{{2}}, .dof = DofType::RX, .value = 0.0});
    model.restraints.push_back({.nodes = NodeIds{{2}}, .dof = DofType::T, .value = 0.0});
    model.nodalForces.push_back({.nodes = NodeIds{{3}}, .moment = Eigen::Vector3d(0, 0, 1)});
    model.nodalForces.push_back({.nodes = NodeIds{{2}}, .moment = Eigen::Vector3d(0, 1, 0)});
    CHECK(messages(model) ==
          std::vector<std::string>{
              "Restraint 6: the node 3 has no dof rz (it belongs to no beam).",
              "Restraint 7: the node 2 has no dof rx (it only belongs to 2D bars).",
              "Restraint 8: a structural model has no dof T.",
              "Nodal force 1: the node 3 has no dof rz (it belongs to no beam).",
              "Nodal force 2: the node 2 has no dof ry (it only belongs to 2D bars)."});
}

// REQ-008-24: several problems at once.
TEST_CASE("several beam problems are listed together", "[core][validation][beam]")
{
    Model model = rowModel(ElementKind::Beam3D, BeamTheory::Timoshenko);
    model.materials.front().poissonsRatio.reset();
    model.sections.front().shearAreaY.reset();
    model.concentratedLoads.push_back({.elements = ElementIds{{9}}});
    const auto result = fem::core::runLinearStatic(model);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message ==
          "The model has 3 problems:\n"
          "- Material \"Steel\": the Poisson's ratio must be given and between -1 and 0.5 for "
          "beams.\n"
          "- Section \"S\": the shear areas Asy and Asz must be given and positive for Timoshenko "
          "beams.\n"
          "- Concentrated load 1: The mesh has no element 9.");
}

// REQ-008-25: a rotation that no beam holds.
TEST_CASE("a mechanism of rotation names the node", "[core][analysis][beam]")
{
    Model model = rowModel(ElementKind::Beam2D);
    fem::core::test::restrain(model, {3}, {DofType::V});
    // Both beams release the moment at the middle node.
    fem::core::EndConnection hinge{.elements = ElementIds{{1}}, .end = fem::core::BarEnd::End};
    hinge.stiffness[5] = 0.0;
    model.endConnections.push_back(hinge);
    hinge.elements = ElementIds{{2}};
    hinge.end = fem::core::BarEnd::Start;
    model.endConnections.push_back(hinge);
    const auto result = fem::core::runLinearStatic(model);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message ==
          "The structure is unstable: no bar gives stiffness to\n- the node 2 in rz\n"
          "(A rotation without stiffness: restrain it, or keep one beam at the node connected.)");
    // Restrained, it is a Gerber beam that stands.
    model.restraints.push_back({.nodes = NodeIds{{2}}, .dof = DofType::RZ, .value = 0.0});
    CHECK(fem::core::runLinearStatic(model).has_value());
}
