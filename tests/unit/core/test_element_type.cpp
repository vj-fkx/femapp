#include <fem/core/element_type.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <span>

using fem::core::allElementTypes;
using fem::core::ElementFamily;
using fem::core::ElementType;
using fem::core::elementTypeInfo;
using fem::core::ElementTypeInfo;
using fem::core::referenceNodeCoordinates;

// REQ-003-01
TEST_CASE("all 90 Gmsh element types are available", "[core][element]")
{
    CHECK(fem::core::kElementTypeCount == 90);
    REQUIRE(allElementTypes().size() == 90);

    std::map<ElementFamily, int> perFamily;
    std::set<int> gmshTypes;
    std::set<std::string_view> names;
    for (const ElementType type : allElementTypes()) {
        const ElementTypeInfo& info = elementTypeInfo(type);
        ++perFamily[info.family];
        gmshTypes.insert(info.gmshType);
        names.insert(info.name);
    }

    // Design section 5.1.
    CHECK(perFamily[ElementFamily::Point] == 1);
    CHECK(perFamily[ElementFamily::Line] == 10);
    CHECK(perFamily[ElementFamily::Triangle] == 18);
    CHECK(perFamily[ElementFamily::Quadrilateral] == 19);
    CHECK(perFamily[ElementFamily::Tetrahedron] == 18);
    CHECK(perFamily[ElementFamily::Hexahedron] == 11);
    CHECK(perFamily[ElementFamily::Wedge] == 3);
    CHECK(perFamily[ElementFamily::Pyramid] == 10);

    CHECK(gmshTypes.size() == 90);
    CHECK(names.size() == 90);
}

// REQ-003-02
TEST_CASE("element type properties match the Gmsh definitions", "[core][element]")
{
    struct Expected
    {
        ElementType type;
        std::string_view name;
        ElementFamily family;
        int dimension;
        int order;
        int nodeCount;
        int vertexCount;
        int gmshType;
    };
    const std::array<Expected, 12> cases = {{
        {.type = ElementType::Point1,
         .name = "Point1",
         .family = ElementFamily::Point,
         .dimension = 0,
         .order = 0,
         .nodeCount = 1,
         .vertexCount = 1,
         .gmshType = 15},
        {.type = ElementType::Line2,
         .name = "Line2",
         .family = ElementFamily::Line,
         .dimension = 1,
         .order = 1,
         .nodeCount = 2,
         .vertexCount = 2,
         .gmshType = 1},
        {.type = ElementType::Line3,
         .name = "Line3",
         .family = ElementFamily::Line,
         .dimension = 1,
         .order = 2,
         .nodeCount = 3,
         .vertexCount = 2,
         .gmshType = 8},
        {.type = ElementType::Tri6,
         .name = "Tri6",
         .family = ElementFamily::Triangle,
         .dimension = 2,
         .order = 2,
         .nodeCount = 6,
         .vertexCount = 3,
         .gmshType = 9},
        {.type = ElementType::Quad8,
         .name = "Quad8",
         .family = ElementFamily::Quadrilateral,
         .dimension = 2,
         .order = 2,
         .nodeCount = 8,
         .vertexCount = 4,
         .gmshType = 16},
        {.type = ElementType::Quad9,
         .name = "Quad9",
         .family = ElementFamily::Quadrilateral,
         .dimension = 2,
         .order = 2,
         .nodeCount = 9,
         .vertexCount = 4,
         .gmshType = 10},
        {.type = ElementType::Tet10,
         .name = "Tet10",
         .family = ElementFamily::Tetrahedron,
         .dimension = 3,
         .order = 2,
         .nodeCount = 10,
         .vertexCount = 4,
         .gmshType = 11},
        {.type = ElementType::Hex20,
         .name = "Hex20",
         .family = ElementFamily::Hexahedron,
         .dimension = 3,
         .order = 2,
         .nodeCount = 20,
         .vertexCount = 8,
         .gmshType = 17},
        {.type = ElementType::Hex1000,
         .name = "Hex1000",
         .family = ElementFamily::Hexahedron,
         .dimension = 3,
         .order = 9,
         .nodeCount = 1000,
         .vertexCount = 8,
         .gmshType = 98},
        {.type = ElementType::Wedge15,
         .name = "Wedge15",
         .family = ElementFamily::Wedge,
         .dimension = 3,
         .order = 2,
         .nodeCount = 15,
         .vertexCount = 6,
         .gmshType = 18},
        {.type = ElementType::Pyramid5,
         .name = "Pyramid5",
         .family = ElementFamily::Pyramid,
         .dimension = 3,
         .order = 1,
         .nodeCount = 5,
         .vertexCount = 5,
         .gmshType = 7},
        {.type = ElementType::Tri15I,
         .name = "Tri15I",
         .family = ElementFamily::Triangle,
         .dimension = 2,
         .order = 5,
         .nodeCount = 15,
         .vertexCount = 3,
         .gmshType = 24},
    }};

    for (const Expected& expected : cases) {
        const ElementTypeInfo& info = elementTypeInfo(expected.type);
        INFO(expected.name);
        CHECK(info.name == expected.name);
        CHECK(info.family == expected.family);
        CHECK(info.dimension == expected.dimension);
        CHECK(info.order == expected.order);
        CHECK(info.nodeCount == expected.nodeCount);
        CHECK(info.vertexCount == expected.vertexCount);
        CHECK(info.gmshType == expected.gmshType);
    }
}

// REQ-003-02, REQ-003-03
TEST_CASE("reference coordinates cover every node, vertices first", "[core][element]")
{
    // The vertices of every type of a family are the same: the vertices are
    // the first nodes and only the higher-order nodes differ.
    std::map<ElementFamily, std::span<const std::array<double, 3>>> firstOfFamily;

    for (const ElementType type : allElementTypes()) {
        const ElementTypeInfo& info = elementTypeInfo(type);
        const auto reference = referenceNodeCoordinates(type);
        INFO(info.name);

        REQUIRE(reference.size() == static_cast<std::size_t>(info.nodeCount));
        CHECK(info.vertexCount <= info.nodeCount);
        CHECK(info.dimension == (info.family == ElementFamily::Point  ? 0
                                 : info.family == ElementFamily::Line ? 1
                                 : info.family == ElementFamily::Triangle ||
                                         info.family == ElementFamily::Quadrilateral
                                     ? 2
                                     : 3));

        const auto [it, inserted] = firstOfFamily.emplace(info.family, reference);
        const auto& vertices = it->second;
        for (int v = 0; v < info.vertexCount; ++v) {
            const auto index = static_cast<std::size_t>(v);
            CHECK(reference[index] == vertices[index]);
        }

        // Coordinates beyond the element's dimension are zero.
        for (const auto& point : reference) {
            for (int axis = info.dimension; axis < 3; ++axis) {
                CHECK(point[static_cast<std::size_t>(axis)] == 0.0);
            }
        }

        // No two nodes share a position.
        const std::set<std::array<double, 3>> distinct(reference.begin(), reference.end());
        CHECK(distinct.size() == reference.size());
    }
}

// REQ-003-03: the enum order groups the families and sorts by order.
TEST_CASE("element types are listed by family and order", "[core][element]")
{
    const auto types = allElementTypes();
    CHECK(std::ranges::is_sorted(types, [](ElementType a, ElementType b) {
        const ElementTypeInfo& x = elementTypeInfo(a);
        const ElementTypeInfo& y = elementTypeInfo(b);
        if (x.family != y.family) {
            return x.family < y.family;
        }
        return x.order < y.order;
    }));
    for (std::size_t i = 0; i < types.size(); ++i) {
        CHECK(static_cast<std::size_t>(types[i]) == i);
    }
}
