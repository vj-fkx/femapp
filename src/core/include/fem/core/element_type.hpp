#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace fem::core {

// Every Gmsh element type with a fixed number of nodes (REQ-003-01). The list
// is generated from Gmsh by tools/generate_element_types.py. Names are the
// family plus the number of nodes, with an "I" suffix for Gmsh's incomplete
// variants that share a node count with a complete one (e.g. Tri15, Tri15I).
enum class ElementType : std::uint8_t {
#define FEM_ELEMENT_TYPE(name, family, dimension, order, nodeCount, vertexCount, gmshType) name,
#include <fem/core/element_types.inc>
#undef FEM_ELEMENT_TYPE
};

namespace detail {
// One entry per type, only to count them.
inline constexpr std::array kElementTypeList = {
#define FEM_ELEMENT_TYPE(name, family, dimension, order, nodeCount, vertexCount, gmshType) 0,
#include <fem/core/element_types.inc>
#undef FEM_ELEMENT_TYPE
};
} // namespace detail

inline constexpr int kElementTypeCount = static_cast<int>(detail::kElementTypeList.size());

enum class ElementFamily : std::uint8_t {
    Point,
    Line,
    Triangle,
    Quadrilateral,
    Tetrahedron,
    Hexahedron,
    Wedge,
    Pyramid,
};

struct ElementTypeInfo
{
    std::string_view name; // "Tet10"
    ElementFamily family;
    int dimension; // 0..3
    int order;     // 1..10; 0 for the point
    int nodeCount;
    int vertexCount; // the vertices are always the first nodes
    int gmshType;    // Gmsh element type number, e.g. 11 for Tet10
};

[[nodiscard]] const ElementTypeInfo& elementTypeInfo(ElementType type) noexcept;

// All types, in enum order.
[[nodiscard]] std::span<const ElementType> allElementTypes() noexcept;

// Coordinates (u, v, w) of the nodes in Gmsh's reference element, in local
// node order. They document the node ordering (REQ-003-03), which is Gmsh's.
[[nodiscard]] std::span<const std::array<double, 3>>
referenceNodeCoordinates(ElementType type) noexcept;

} // namespace fem::core
