#include <fem/core/mesh.hpp>
#include <fem/core/mesh_builder.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <Eigen/Core>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using fem::core::ElementHandle;
using fem::core::ElementType;
using fem::core::ExternalId;
using fem::core::Mesh;
using fem::core::MeshBuilder;
using fem::core::NodeIndex;

namespace {

ElementHandle add(MeshBuilder& builder, ElementType type, ExternalId id,
                  std::vector<ExternalId> nodes)
{
    const auto handle = builder.addElement(type, id, nodes);
    REQUIRE(handle.has_value());
    return *handle;
}

void addNode(MeshBuilder& builder, ExternalId id, double x, double y, double z)
{
    REQUIRE(builder.addNode(id, Eigen::Vector3d(x, y, z)).has_value());
}

// Mixed mesh: a point, two lines, a triangle and a hexahedron, with
// non-contiguous ids added in any order.
struct MixedMesh
{
    Mesh mesh;
    ElementHandle point;
    ElementHandle line1;
    ElementHandle line2;
    ElementHandle triangle;
    ElementHandle hexahedron;
};

MixedMesh buildMixedMesh()
{
    MeshBuilder builder;
    // Unit cube, ids 101..108, plus two loose nodes.
    addNode(builder, 101, 0, 0, 0);
    addNode(builder, 102, 1, 0, 0);
    addNode(builder, 103, 1, 1, 0);
    addNode(builder, 104, 0, 1, 0);
    addNode(builder, 105, 0, 0, 1);
    addNode(builder, 106, 1, 0, 1);
    addNode(builder, 107, 1, 1, 1);
    addNode(builder, 108, 0, 1, 1);
    addNode(builder, 7, -2, 3, 0.5);
    addNode(builder, 9, 4, -1, 2);

    MixedMesh result;
    result.hexahedron =
        add(builder, ElementType::Hex8, 500, {101, 102, 103, 104, 105, 106, 107, 108});
    result.line1 = add(builder, ElementType::Line2, 20, {7, 101});
    result.triangle = add(builder, ElementType::Tri3, 300, {101, 102, 103});
    result.point = add(builder, ElementType::Point1, 1, {9});
    result.line2 = add(builder, ElementType::Line2, 21, {101, 105});

    REQUIRE(builder.addGroup("volume", 3, {result.hexahedron}).has_value());
    REQUIRE(builder.addGroup("edges", 1, {result.line1, result.line2}).has_value());
    REQUIRE(builder.addGroup("first edge", 1, {result.line1}).has_value());
    REQUIRE(builder.addGroup("edges", 2, {result.triangle}).has_value());

    auto mesh = builder.build();
    REQUIRE(mesh.has_value());
    result.mesh = std::move(*mesh);
    return result;
}

std::string errorOf(const fem::core::Expected<void>& result)
{
    REQUIRE_FALSE(result.has_value());
    return result.error().message;
}

template <typename T>
std::string errorOf(const fem::core::Expected<T>& result)
{
    REQUIRE_FALSE(result.has_value());
    return result.error().message;
}

} // namespace

// REQ-003-04, REQ-003-05, REQ-003-06
TEST_CASE("a mixed mesh answers queries on nodes and elements", "[core][mesh]")
{
    const MixedMesh mixed = buildMixedMesh();
    const Mesh& mesh = mixed.mesh;

    CHECK(mesh.nodeCount() == 10);
    CHECK(mesh.elementCount() == 5);
    CHECK(mesh.elementCount(ElementType::Line2) == 2);
    CHECK(mesh.elementCount(ElementType::Hex8) == 1);
    CHECK(mesh.elementCount(ElementType::Tet4) == 0);
    CHECK(mesh.dimension() == 3);

    // Node ids and indices.
    REQUIRE(mesh.findNode(107).has_value());
    const NodeIndex node107 = *mesh.findNode(107);
    CHECK(mesh.nodeId(node107) == 107);
    CHECK(mesh.nodeCoordinates(node107) == Eigen::Vector3d(1, 1, 1));
    CHECK(mesh.nodeCoordinates(*mesh.findNode(7)) == Eigen::Vector3d(-2, 3, 0.5));
    CHECK_FALSE(mesh.findNode(8).has_value());

    // Blocks follow the ElementType order: Point1, Line2, Tri3, Hex8.
    const auto blocks = mesh.blocks();
    REQUIRE(blocks.size() == 4);
    CHECK(blocks[0].type == ElementType::Point1);
    CHECK(blocks[1].type == ElementType::Line2);
    CHECK(blocks[2].type == ElementType::Tri3);
    CHECK(blocks[3].type == ElementType::Hex8);
    CHECK(blocks[1].firstElement == 1);
    CHECK(blocks[3].firstElement == 4);

    // Element ids, types and nodes.
    for (const ExternalId id : {1, 20, 21, 300, 500}) {
        const auto element = mesh.findElement(id);
        REQUIRE(element.has_value());
        CHECK(mesh.elementId(*element) == id);
    }
    CHECK_FALSE(mesh.findElement(2).has_value());

    const auto hex = *mesh.findElement(500);
    CHECK(mesh.elementType(hex) == ElementType::Hex8);
    const auto hexNodes = mesh.elementNodes(hex);
    REQUIRE(hexNodes.size() == 8);
    CHECK(mesh.nodeId(hexNodes[0]) == 101);
    CHECK(mesh.nodeId(hexNodes[6]) == 107);

    const auto line = *mesh.findElement(21);
    CHECK(mesh.elementType(line) == ElementType::Line2);
    REQUIRE(mesh.elementNodes(line).size() == 2);
    CHECK(mesh.nodeId(mesh.elementNodes(line)[1]) == 105);
}

// REQ-003-07
TEST_CASE("bounding box of a mesh", "[core][mesh]")
{
    const MixedMesh mixed = buildMixedMesh();
    const fem::core::BoundingBox box = mixed.mesh.boundingBox();
    CHECK_FALSE(box.isEmpty());
    CHECK(box.min == Eigen::Vector3d(-2, -1, 0));
    CHECK(box.max == Eigen::Vector3d(4, 3, 2));

    MeshBuilder empty;
    const auto emptyMesh = empty.build();
    REQUIRE(emptyMesh.has_value());
    CHECK(emptyMesh->boundingBox().isEmpty());
    CHECK(emptyMesh->nodeCount() == 0);
    CHECK(emptyMesh->elementCount() == 0);
    CHECK(emptyMesh->dimension() == 0);
}

// REQ-003-11, REQ-003-12, REQ-003-13
TEST_CASE("groups are found by name and give their nodes", "[core][mesh][group]")
{
    const MixedMesh mixed = buildMixedMesh();
    const Mesh& mesh = mixed.mesh;
    REQUIRE(mesh.groups().size() == 4);

    const fem::core::Group* volume = mesh.findGroup("volume", 3);
    REQUIRE(volume != nullptr);
    CHECK(volume->dimension == 3);
    REQUIRE(volume->elements.size() == 1);
    CHECK(mesh.elementId(volume->elements[0]) == 500);
    CHECK(mesh.groupNodes(*volume).size() == 8);

    // An element in two groups; the point and the triangle in no 1D group.
    const fem::core::Group* edges = mesh.findGroup("edges", 1);
    const fem::core::Group* firstEdge = mesh.findGroup("first edge", 1);
    REQUIRE(edges != nullptr);
    REQUIRE(firstEdge != nullptr);
    CHECK(edges->elements.size() == 2);
    CHECK(firstEdge->elements.size() == 1);
    CHECK(firstEdge->elements[0] == edges->elements[0]);

    // Nodes of the 1D group: 7, 101 and 105, sorted and unique.
    const std::vector<NodeIndex> nodes = mesh.groupNodes(*edges);
    REQUIRE(nodes.size() == 3);
    CHECK(std::ranges::is_sorted(nodes));

    // The same name in two dimensions.
    CHECK(mesh.findGroups("edges").size() == 2);
    CHECK(mesh.findGroup("edges", 2) != nullptr);
    CHECK(mesh.findGroup("edges", 3) == nullptr);
    CHECK(mesh.findGroups("missing").empty());
}

// REQ-003-09
TEST_CASE("invalid elements are refused with a message naming them", "[core][mesh][error]")
{
    MeshBuilder builder;
    for (ExternalId id = 1; id <= 4; ++id) {
        addNode(builder, id, static_cast<double>(id), 0, 0);
    }

    const std::vector<ExternalId> tooFew = {1, 2, 3};
    const std::string wrongCount = errorOf(builder.addElement(ElementType::Tet4, 12, tooFew));
    CHECK_THAT(wrongCount, ContainsSubstring("Element 12 (Tet4)") && ContainsSubstring("3 nodes") &&
                               ContainsSubstring("needs 4"));

    const std::vector<ExternalId> missing = {1, 2, 999};
    const std::string missingNode = errorOf(builder.addElement(ElementType::Tri3, 13, missing));
    CHECK_THAT(missingNode, ContainsSubstring("Element 13") && ContainsSubstring("node 999"));

    const std::vector<ExternalId> repeated = {1, 2, 2};
    const std::string repeatedNode = errorOf(builder.addElement(ElementType::Tri3, 14, repeated));
    CHECK_THAT(repeatedNode, ContainsSubstring("Element 14") && ContainsSubstring("node 2") &&
                                 ContainsSubstring("more than once"));

    // The refused elements left nothing behind.
    const std::vector<ExternalId> good = {1, 2, 3};
    REQUIRE(builder.addElement(ElementType::Tri3, 15, good).has_value());
    const auto mesh = builder.build();
    REQUIRE(mesh.has_value());
    CHECK(mesh->elementCount() == 1);
    CHECK(mesh->elementNodes(0).size() == 3);
}

// REQ-003-10
TEST_CASE("repeated node and element ids are refused", "[core][mesh][error]")
{
    SECTION("nodes")
    {
        MeshBuilder builder;
        addNode(builder, 5, 0, 0, 0);
        addNode(builder, 6, 1, 0, 0);
        addNode(builder, 5, 2, 0, 0);
        CHECK_THAT(errorOf(builder.finishNodes()), ContainsSubstring("Node id 5"));
    }
    SECTION("elements")
    {
        MeshBuilder builder;
        addNode(builder, 1, 0, 0, 0);
        addNode(builder, 2, 1, 0, 0);
        add(builder, ElementType::Line2, 8, {1, 2});
        add(builder, ElementType::Point1, 8, {1});
        CHECK_THAT(errorOf(builder.build()), ContainsSubstring("Element id 8"));
    }
    SECTION("nodes after elements")
    {
        MeshBuilder builder;
        addNode(builder, 1, 0, 0, 0);
        add(builder, ElementType::Point1, 1, {1});
        CHECK_THAT(errorOf(builder.addNode(2, Eigen::Vector3d::Zero())),
                   ContainsSubstring("Node 2"));
    }
}

// REQ-003-14
TEST_CASE("invalid groups are refused with a message naming them", "[core][mesh][error]")
{
    MeshBuilder builder;
    addNode(builder, 1, 0, 0, 0);
    addNode(builder, 2, 1, 0, 0);
    addNode(builder, 3, 0, 1, 0);
    const ElementHandle line = add(builder, ElementType::Line2, 40, {1, 2});
    const ElementHandle triangle = add(builder, ElementType::Tri3, 41, {1, 2, 3});

    REQUIRE(builder.addGroup("wall", 2, {triangle}).has_value());
    CHECK_THAT(errorOf(builder.addGroup("wall", 2, {triangle})),
               ContainsSubstring("2D group named 'wall'"));
    CHECK_THAT(errorOf(builder.addGroup("mixed", 2, {triangle, line})),
               ContainsSubstring("'mixed'") && ContainsSubstring("element 40") &&
                   ContainsSubstring("1D"));
    // Same name in another dimension is fine.
    CHECK(builder.addGroup("wall", 1, {line}).has_value());
}

// NFR-003-01: memory close to the raw data (coordinates in double,
// connectivity in 32-bit integers).
TEST_CASE("mesh memory stays within 1.5 times the raw data", "[core][mesh][memory]")
{
    constexpr int kCells = 32; // 32^3 cubes, 6 tetrahedra each: 196 608 Tet4
    constexpr int kSide = kCells + 1;
    MeshBuilder builder;
    builder.reserveNodes(static_cast<std::size_t>(kSide) * kSide * kSide);
    ExternalId nodeId = 1;
    for (int k = 0; k < kSide; ++k) {
        for (int j = 0; j < kSide; ++j) {
            for (int i = 0; i < kSide; ++i) {
                REQUIRE(builder.addNode(nodeId++, Eigen::Vector3d(i, j, k)).has_value());
            }
        }
    }
    const auto at = [](int i, int j, int k) -> ExternalId {
        return 1 + i + kSide * (j + kSide * k);
    };

    std::vector<ElementHandle> all;
    ExternalId elementId = 1;
    for (int k = 0; k < kCells; ++k) {
        for (int j = 0; j < kCells; ++j) {
            for (int i = 0; i < kCells; ++i) {
                const std::array<ExternalId, 8> c = {at(i, j, k),
                                                     at(i + 1, j, k),
                                                     at(i + 1, j + 1, k),
                                                     at(i, j + 1, k),
                                                     at(i, j, k + 1),
                                                     at(i + 1, j, k + 1),
                                                     at(i + 1, j + 1, k + 1),
                                                     at(i, j + 1, k + 1)};
                // Six tetrahedra around the diagonal 0-6.
                const std::array<std::array<int, 4>, 6> tets = {{{0, 1, 2, 6},
                                                                 {0, 2, 3, 6},
                                                                 {0, 3, 7, 6},
                                                                 {0, 7, 4, 6},
                                                                 {0, 4, 5, 6},
                                                                 {0, 5, 1, 6}}};
                for (const auto& t : tets) {
                    const std::array<ExternalId, 4> nodes = {
                        c.at(static_cast<std::size_t>(t[0])), c.at(static_cast<std::size_t>(t[1])),
                        c.at(static_cast<std::size_t>(t[2])), c.at(static_cast<std::size_t>(t[3]))};
                    const auto handle = builder.addElement(ElementType::Tet4, elementId++, nodes);
                    REQUIRE(handle.has_value());
                    all.push_back(*handle);
                }
            }
        }
    }
    REQUIRE(builder.addGroup("domain", 3, std::move(all)).has_value());
    const auto mesh = builder.build();
    REQUIRE(mesh.has_value());

    const auto elements = static_cast<double>(mesh->elementCount());
    const auto nodes = static_cast<double>(mesh->nodeCount());
    const double raw = elements * 4 * sizeof(std::int32_t) + nodes * 3 * sizeof(double);
    const double ratio = static_cast<double>(mesh->memoryUsage()) / raw;
    INFO("memory / raw data = " << ratio);
    CHECK(mesh->elementCount() == 6 * kCells * kCells * kCells);
    CHECK(ratio <= 1.5);
}
