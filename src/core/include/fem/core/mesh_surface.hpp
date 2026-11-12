#pragma once

#include <fem/core/element_type.hpp>
#include <fem/core/mesh.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace fem::core {

// A face of a solid element, by local vertex index, ordered so that its
// normal (right-hand rule) points out of the element. Gmsh vertex numbering.
struct LocalFace
{
    std::array<std::uint8_t, 4> vertices{};
    std::uint8_t vertexCount = 0; // 3 or 4
};

// Faces of the solid families (tetrahedron, hexahedron, wedge, pyramid);
// empty for the others.
[[nodiscard]] std::span<const LocalFace> elementFaces(ElementFamily family) noexcept;

struct SurfaceFace
{
    ElementIndex element = 0;
    std::array<NodeIndex, 4> nodes{}; // vertices only; nodes[3] unused for triangles
    std::uint8_t nodeCount = 0;       // 3 or 4
};

struct SurfaceEdge
{
    NodeIndex a = 0;
    NodeIndex b = 0;
    ElementIndex element = 0; // an element that owns the edge
};

// What can be seen of a mesh, built from the vertices of its elements
// (higher-order nodes are ignored: straight edges, planar faces).
struct MeshSurface
{
    std::vector<SurfaceFace> faces2d; // faces of the 2D elements, as given
    std::vector<SurfaceFace> faces3d; // faces owned by exactly one 3D element, outward
    std::vector<SurfaceEdge> edges2d; // unique edges of faces2d
    std::vector<SurfaceEdge> edges3d; // unique edges of faces3d
};

// REQ-004-09, REQ-004-10.
[[nodiscard]] MeshSurface extractSurface(const Mesh& mesh);

} // namespace fem::core
