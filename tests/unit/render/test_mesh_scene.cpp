#include "mesh_fixtures.hpp"

#include <fem/render/mesh_scene.hpp>
#include <fem/render/selection.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstddef>
#include <set>

using Catch::Matchers::WithinAbs;
using fem::core::ElementType;
using fem::render::ColorMode;
using fem::render::LineSet;
using fem::render::MeshScene;
using fem::render::Rgba;

namespace {

std::shared_ptr<const fem::core::Mesh> mixedMesh()
{
    // A hexahedron, a quadrilateral and a triangle beside it, a line and a point.
    const std::vector<Eigen::Vector3d> nodes = {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1},
        {1, 1, 1}, {0, 1, 1}, {2, 0, 0}, {2, 1, 0}, {3, 0, 0}, {4, 0, 0},
    };
    return fem::render::test::buildMesh(nodes,
                                        {
                                            {ElementType::Hex8, {0, 1, 2, 3, 4, 5, 6, 7}},
                                            {ElementType::Quad4, {1, 8, 9, 2}},
                                            {ElementType::Tri3, {8, 10, 9}},
                                            {ElementType::Line2, {10, 11}},
                                            {ElementType::Point1, {11}},
                                        },
                                        {{"plates", {1, 2}}, {"plate", {1}}, {"bar", {3}}});
}

} // namespace

// REQ-004-09, REQ-004-16: draw streams and their ranges per dimension.
TEST_CASE("scene splits primitives by dimension", "[render][scene]")
{
    const auto scene = fem::render::buildMeshScene(mixedMesh());

    // 2D: quad (2 triangles) + triangle; 3D: 6 hexahedron faces (12 triangles).
    CHECK(scene->triangles(2).first == 0);
    CHECK(scene->triangles(2).count == 3);
    CHECK(scene->triangles(3).first == 3);
    CHECK(scene->triangles(3).count == 12);
    CHECK(scene->triangleVertices().size() == std::size_t{3} * 15);
    CHECK(scene->triangleElements().size() == 15);

    // 1D element, 2D edges (4 + 3 - 1 shared), 3D edges (12).
    CHECK(scene->lines(LineSet::Elements1d).count == 1);
    CHECK(scene->lines(LineSet::Edges2d).count == 6);
    CHECK(scene->lines(LineSet::Edges3d).count == 12);
    CHECK(scene->lines(LineSet::Edges3d).first == 7);
    CHECK(scene->lineVertices().size() == std::size_t{2} * 19);

    CHECK(scene->pointElements().size() == 1);
    CHECK(scene->surfaceNodes(0).size() == 1);
    CHECK(scene->surfaceNodes(1).size() == 2);
    CHECK(scene->surfaceNodes(2).size() == 5);
    CHECK(scene->surfaceNodes(3).size() == 8);
    CHECK(scene->allNodes(3).size() == 8);
    CHECK(scene->surfaceNodes(4).empty());
}

// REQ-004-18: positions are stored relative to the centre of the mesh.
TEST_CASE("scene positions are relative to the mesh centre", "[render][scene]")
{
    const Eigen::Vector3d shift(1.0e6, -2.0e6, 3.0e6);
    const auto scene = fem::render::buildMeshScene(fem::render::test::hexGrid(2, shift));
    CHECK((scene->origin() - (shift + Eigen::Vector3d(1, 1, 1))).norm() < 1e-9);
    for (const Eigen::Vector3f& p : scene->triangleVertices()) {
        CHECK(p.cwiseAbs().maxCoeff() <= 1.0F);
        // Unit coordinates survive exactly despite the large offset.
        CHECK(p.x() == std::round(p.x()));
    }
}

TEST_CASE("scene of an empty mesh", "[render][scene]")
{
    const auto scene = fem::render::buildMeshScene(fem::render::test::buildMesh({}, {}));
    CHECK(scene->triangleVertices().empty());
    CHECK(scene->lineVertices().empty());
    CHECK(scene->bounds().isEmpty());
    CHECK(scene->elementColors(ColorMode::ByGroup).empty());
}

// REQ-004-14, REQ-004-15
TEST_CASE("element colors by dimension and by group", "[render][scene][color]")
{
    const auto mesh = mixedMesh();
    const auto scene = fem::render::buildMeshScene(mesh);
    const auto element = [&](fem::core::ExternalId id) {
        return static_cast<std::size_t>(mesh->findElement(id).value_or(-1));
    };

    const std::vector<Rgba> byDimension = scene->elementColors(ColorMode::ByDimension);
    CHECK(byDimension[element(1)] == fem::render::dimensionColor(3));
    CHECK(byDimension[element(2)] == fem::render::dimensionColor(2));
    CHECK(byDimension[element(3)] == fem::render::dimensionColor(2));
    CHECK(byDimension[element(4)] == fem::render::dimensionColor(1));
    CHECK(byDimension[element(5)] == fem::render::dimensionColor(0));

    const std::vector<Rgba> byGroup = scene->elementColors(ColorMode::ByGroup);
    // "plates" is group 0 and contains elements 2 and 3; element 2 is also in
    // "plate" (group 1) but keeps the color of the first group.
    CHECK(byGroup[element(2)] == fem::render::groupColor(0));
    CHECK(byGroup[element(3)] == fem::render::groupColor(0));
    CHECK(byGroup[element(4)] == fem::render::groupColor(2));
    CHECK(byGroup[element(1)] == fem::render::kNoGroupColor);
    CHECK(byGroup[element(5)] == fem::render::kNoGroupColor);

    // The palette repeats after twelve groups and its colors are distinct.
    CHECK(fem::render::groupColor(12) == fem::render::groupColor(0));
    std::set<Rgba> palette;
    for (std::size_t g = 0; g < 12; ++g) {
        palette.insert(fem::render::groupColor(g));
    }
    CHECK(palette.size() == 12);
}

// REQ-004-24, REQ-004-25, REQ-004-28
TEST_CASE("selection adds, removes, clears and drops a dimension", "[render][selection]")
{
    const auto mesh = mixedMesh();
    fem::render::Selection selection;
    selection.reset(fem::render::SelectionMode::Elements, *mesh);
    CHECK(selection.count() == 0);

    const std::vector<int> all = {0, 1, 2, 3, 4};
    selection.add(all);
    selection.add(all); // twice: no change
    CHECK(selection.count() == 5);

    const std::vector<int> some = {1, 4, 99, -1};
    selection.remove(some);
    CHECK(selection.count() == 3);
    CHECK_FALSE(selection.contains(1));
    CHECK(selection.contains(0));

    // Internal order follows the element types: point, line, triangle,
    // quadrilateral, hexahedron. Left: point, triangle and quadrilateral.
    // Hiding 2D takes out the triangle and the quadrilateral.
    const int point = mesh->findElement(5).value_or(-1);
    const int triangle = mesh->findElement(3).value_or(-1);
    REQUIRE(selection.contains(triangle));
    selection.removeElementsOfDimension(*mesh, 2);
    CHECK_FALSE(selection.contains(triangle));
    CHECK(selection.contains(point));
    CHECK(selection.count() == 1);
    CHECK(selection.items() == std::vector<int>{point});

    selection.clear();
    CHECK(selection.count() == 0);

    selection.reset(fem::render::SelectionMode::Nodes, *mesh);
    const std::vector<int> nodes = {0, 11};
    selection.add(nodes);
    CHECK(selection.count() == 2);
    selection.removeElementsOfDimension(*mesh, 0); // no effect on nodes
    CHECK(selection.count() == 2);
}
