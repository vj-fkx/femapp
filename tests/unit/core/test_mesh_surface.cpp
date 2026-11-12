#include <fem/core/element_type.hpp>
#include <fem/core/mesh_builder.hpp>
#include <fem/core/mesh_surface.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <Eigen/Geometry>

#include <array>
#include <cstddef>
#include <map>
#include <utility>
#include <vector>

using Catch::Matchers::WithinRel;
using fem::core::ElementFamily;
using fem::core::ElementType;
using fem::core::ExternalId;
using fem::core::Mesh;
using fem::core::MeshBuilder;
using fem::core::MeshSurface;
using fem::core::SurfaceFace;

namespace {

using Vec3 = Eigen::Vector3d;

Mesh build(const std::vector<Vec3>& nodes,
           const std::vector<std::pair<ElementType, std::vector<ExternalId>>>& elements)
{
    MeshBuilder builder;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        REQUIRE(builder.addNode(static_cast<ExternalId>(i), nodes[i]).has_value());
    }
    ExternalId id = 1;
    for (const auto& [type, ids] : elements) {
        REQUIRE(builder.addElement(type, id++, ids).has_value());
    }
    auto mesh = builder.build();
    REQUIRE(mesh.has_value());
    return std::move(*mesh);
}

// n x n x n unit hexahedra.
Mesh hexGrid(int n)
{
    const int side = n + 1;
    std::vector<Vec3> nodes;
    for (int k = 0; k < side; ++k) {
        for (int j = 0; j < side; ++j) {
            for (int i = 0; i < side; ++i) {
                nodes.emplace_back(i, j, k);
            }
        }
    }
    const auto at = [side](int i, int j, int k) -> ExternalId {
        return i + side * (j + side * k);
    };
    std::vector<std::pair<ElementType, std::vector<ExternalId>>> elements;
    for (int k = 0; k < n; ++k) {
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                elements.emplace_back(
                    ElementType::Hex8,
                    std::vector<ExternalId>{at(i, j, k), at(i + 1, j, k), at(i + 1, j + 1, k),
                                            at(i, j + 1, k), at(i, j, k + 1), at(i + 1, j, k + 1),
                                            at(i + 1, j + 1, k + 1), at(i, j + 1, k + 1)});
            }
        }
    }
    return build(nodes, elements);
}

Vec3 position(const Mesh& mesh, fem::core::NodeIndex node)
{
    return mesh.nodeCoordinates(node);
}

// Area vector of a face (triangles, or quadrilaterals split along 0-2).
Vec3 areaVector(const Mesh& mesh, const SurfaceFace& face)
{
    const Vec3 a = position(mesh, face.nodes[0]);
    const Vec3 b = position(mesh, face.nodes[1]);
    const Vec3 c = position(mesh, face.nodes[2]);
    Vec3 area = 0.5 * (b - a).cross(c - a);
    if (face.nodeCount == 4) {
        const Vec3 d = position(mesh, face.nodes[3]);
        area += 0.5 * (c - a).cross(d - a);
    }
    return area;
}

// Volume enclosed by outward faces, by the divergence theorem.
double enclosedVolume(const Mesh& mesh, const std::vector<SurfaceFace>& faces)
{
    double volume = 0.0;
    for (const SurfaceFace& face : faces) {
        const Vec3 a = position(mesh, face.nodes[0]);
        const auto triangle = [&](const Vec3& p, const Vec3& q, const Vec3& r) {
            volume += p.dot(q.cross(r)) / 6.0;
        };
        triangle(a, position(mesh, face.nodes[1]), position(mesh, face.nodes[2]));
        if (face.nodeCount == 4) {
            triangle(a, position(mesh, face.nodes[2]), position(mesh, face.nodes[3]));
        }
    }
    return volume;
}

} // namespace

// REQ-004-09: local faces point out of the reference elements.
TEST_CASE("element faces point outwards", "[core][surface]")
{
    for (const ElementType type :
         {ElementType::Tet4, ElementType::Hex8, ElementType::Wedge6, ElementType::Pyramid5}) {
        const auto& info = fem::core::elementTypeInfo(type);
        INFO(info.name);
        const auto reference = fem::core::referenceNodeCoordinates(type);
        std::vector<Vec3> vertices;
        Vec3 centroid = Vec3::Zero();
        for (int v = 0; v < info.vertexCount; ++v) {
            const auto& p = reference[static_cast<std::size_t>(v)];
            vertices.emplace_back(p[0], p[1], p[2]);
            centroid += vertices.back();
        }
        centroid /= info.vertexCount;

        const auto faces = fem::core::elementFaces(info.family);
        REQUIRE_FALSE(faces.empty());
        for (const auto& face : faces) {
            const Vec3& a = vertices.at(face.vertices[0]);
            const Vec3& b = vertices.at(face.vertices[1]);
            const Vec3& c = vertices.at(face.vertices[2]);
            Vec3 faceCentroid = Vec3::Zero();
            for (std::size_t v = 0; v < face.vertexCount; ++v) {
                faceCentroid += vertices.at(face.vertices.at(v));
            }
            faceCentroid /= face.vertexCount;
            CHECK((b - a).cross(c - a).dot(faceCentroid - centroid) > 0.0);
        }
    }
    CHECK(fem::core::elementFaces(ElementFamily::Triangle).empty());
}

// REQ-004-09, REQ-004-30
TEST_CASE("surface of a single hexahedron", "[core][surface]")
{
    const Mesh mesh = hexGrid(1);
    const MeshSurface surface = fem::core::extractSurface(mesh);
    CHECK(surface.faces2d.empty());
    CHECK(surface.faces3d.size() == 6);
    CHECK(surface.edges3d.size() == 12);
    CHECK_THAT(enclosedVolume(mesh, surface.faces3d), WithinRel(1.0, 1e-12));
}

// REQ-004-09, REQ-004-30: internal faces are dropped.
TEST_CASE("surface of a block of hexahedra has only outer faces", "[core][surface]")
{
    constexpr int kN = 4;
    const Mesh mesh = hexGrid(kN);
    const MeshSurface surface = fem::core::extractSurface(mesh);
    CHECK(surface.faces3d.size() == 6 * kN * kN);
    // Outer edges: 12 cube edges of kN segments plus the grid lines on each face.
    CHECK(surface.edges3d.size() == 12 * kN + 6 * 2 * (kN - 1) * kN);
    CHECK_THAT(enclosedVolume(mesh, surface.faces3d), WithinRel(kN * kN * kN, 1e-12));

    double area = 0.0;
    for (const SurfaceFace& face : surface.faces3d) {
        area += areaVector(mesh, face).norm();
    }
    CHECK_THAT(area, WithinRel(6.0 * kN * kN, 1e-12));
}

// REQ-004-09: a hexahedron, a pyramid on top of it and a tetrahedron against
// a side of the pyramid share faces that must disappear.
TEST_CASE("surface of mixed solids has no internal face", "[core][surface]")
{
    const std::vector<Vec3> nodes = {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1},
        {1, 0, 1}, {1, 1, 1}, {0, 1, 1}, {0.5, 0.5, 1.5}, {0.5, -0.5, 1.2},
    };
    const Mesh mesh = build(nodes, {
                                       {ElementType::Hex8, {0, 1, 2, 3, 4, 5, 6, 7}},
                                       {ElementType::Pyramid5, {4, 5, 6, 7, 8}},
                                       {ElementType::Tet4, {4, 5, 8, 9}},
                                   });
    const MeshSurface surface = fem::core::extractSurface(mesh);
    // Hexahedron 5 + pyramid 3 + tetrahedron 3.
    CHECK(surface.faces3d.size() == 11);

    // Closed and outward: hexahedron 1, pyramid 1/6, tetrahedron 0.35/6.
    CHECK_THAT(enclosedVolume(mesh, surface.faces3d), WithinRel(1.0 + 1.0 / 6.0 + 0.35 / 6.0, 1e-12));

    // Every edge of a closed surface is shared by exactly two faces.
    std::map<std::pair<int, int>, int> uses;
    for (const SurfaceFace& face : surface.faces3d) {
        for (std::size_t i = 0; i < face.nodeCount; ++i) {
            const int a = face.nodes.at(i);
            const int b = face.nodes.at((i + 1) % face.nodeCount);
            ++uses[{std::min(a, b), std::max(a, b)}];
        }
    }
    for (const auto& [edge, count] : uses) {
        CHECK(count == 2);
    }
    CHECK(uses.size() == surface.edges3d.size());
}

// REQ-004-09, REQ-004-10: 2D elements are kept as they are; 1D and 0D
// elements give no faces; higher-order elements use their vertices.
TEST_CASE("surface of 2D and higher-order elements", "[core][surface]")
{
    const std::vector<Vec3> nodes = {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {2, 0, 0}, {0.5, 0, 0}, {1.5, 0.5, 0}, {1, 0.5, 0},
    };
    const Mesh mesh = build(nodes, {
                                       {ElementType::Quad4, {0, 1, 2, 3}},
                                       {ElementType::Tri6, {1, 4, 2, 5, 6, 7}},
                                       {ElementType::Line2, {0, 3}},
                                       {ElementType::Point1, {4}},
                                   });
    const MeshSurface surface = fem::core::extractSurface(mesh);
    REQUIRE(surface.faces2d.size() == 2);
    CHECK(surface.faces3d.empty());
    CHECK(surface.edges3d.empty());
    // Quad 4 edges + triangle 3, sharing 1-2.
    CHECK(surface.edges2d.size() == 6);

    for (const SurfaceFace& face : surface.faces2d) {
        const auto type = mesh.elementType(face.element);
        CHECK(face.nodeCount == fem::core::elementTypeInfo(type).vertexCount);
    }
    // The Tri6 face uses its three vertices only.
    const auto tri = std::ranges::find_if(surface.faces2d, [&](const SurfaceFace& face) {
        return mesh.elementType(face.element) == ElementType::Tri6;
    });
    REQUIRE(tri != surface.faces2d.end());
    CHECK(mesh.nodeId(tri->nodes[0]) == 1);
    CHECK(mesh.nodeId(tri->nodes[1]) == 4);
    CHECK(mesh.nodeId(tri->nodes[2]) == 2);
}
