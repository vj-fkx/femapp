#include <fem/core/mesh_surface.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>

namespace fem::core {

namespace {

constexpr NodeIndex kNoNode = std::numeric_limits<NodeIndex>::max();

// Design section 5.1: outward faces, Gmsh vertex numbering.
constexpr std::array<LocalFace, 4> kTetFaces = {{
    {.vertices = {0, 2, 1, 0}, .vertexCount = 3},
    {.vertices = {0, 1, 3, 0}, .vertexCount = 3},
    {.vertices = {0, 3, 2, 0}, .vertexCount = 3},
    {.vertices = {1, 2, 3, 0}, .vertexCount = 3},
}};
constexpr std::array<LocalFace, 6> kHexFaces = {{
    {.vertices = {0, 3, 2, 1}, .vertexCount = 4},
    {.vertices = {4, 5, 6, 7}, .vertexCount = 4},
    {.vertices = {0, 1, 5, 4}, .vertexCount = 4},
    {.vertices = {1, 2, 6, 5}, .vertexCount = 4},
    {.vertices = {2, 3, 7, 6}, .vertexCount = 4},
    {.vertices = {0, 4, 7, 3}, .vertexCount = 4},
}};
constexpr std::array<LocalFace, 5> kWedgeFaces = {{
    {.vertices = {0, 2, 1, 0}, .vertexCount = 3},
    {.vertices = {3, 4, 5, 0}, .vertexCount = 3},
    {.vertices = {0, 1, 4, 3}, .vertexCount = 4},
    {.vertices = {1, 2, 5, 4}, .vertexCount = 4},
    {.vertices = {0, 3, 5, 2}, .vertexCount = 4},
}};
constexpr std::array<LocalFace, 5> kPyramidFaces = {{
    {.vertices = {0, 3, 2, 1}, .vertexCount = 4},
    {.vertices = {0, 1, 4, 0}, .vertexCount = 3},
    {.vertices = {1, 2, 4, 0}, .vertexCount = 3},
    {.vertices = {2, 3, 4, 0}, .vertexCount = 3},
    {.vertices = {3, 0, 4, 0}, .vertexCount = 3},
}};

// A face of a 3D element, keyed by its sorted vertices so that the two sides
// of an internal face compare equal.
struct FaceRecord
{
    std::array<NodeIndex, 4> key{};
    ElementIndex element = 0;
    std::uint8_t localFace = 0;
};

SurfaceFace orientedFace(const Mesh& mesh, ElementIndex element, const LocalFace& local)
{
    const auto nodes = mesh.elementNodes(element);
    SurfaceFace face;
    face.element = element;
    face.nodeCount = local.vertexCount;
    for (std::size_t i = 0; i < local.vertexCount; ++i) {
        face.nodes.at(i) = nodes[local.vertices.at(i)];
    }
    return face;
}

std::vector<SurfaceEdge> uniqueEdges(const std::vector<SurfaceFace>& faces)
{
    std::vector<SurfaceEdge> edges;
    edges.reserve(faces.size() * 3);
    for (const SurfaceFace& face : faces) {
        for (std::size_t i = 0; i < face.nodeCount; ++i) {
            const NodeIndex a = face.nodes.at(i);
            const NodeIndex b = face.nodes.at((i + 1) % face.nodeCount);
            edges.push_back({.a = std::min(a, b), .b = std::max(a, b), .element = face.element});
        }
    }
    std::ranges::sort(edges, [](const SurfaceEdge& x, const SurfaceEdge& y) {
        return x.a != y.a ? x.a < y.a : x.b < y.b;
    });
    const auto duplicates = std::ranges::unique(
        edges, [](const SurfaceEdge& x, const SurfaceEdge& y) { return x.a == y.a && x.b == y.b; });
    edges.erase(duplicates.begin(), duplicates.end());
    edges.shrink_to_fit();
    return edges;
}

} // namespace

std::span<const LocalFace> elementFaces(ElementFamily family) noexcept
{
    switch (family) {
    case ElementFamily::Tetrahedron:
        return kTetFaces;
    case ElementFamily::Hexahedron:
        return kHexFaces;
    case ElementFamily::Wedge:
        return kWedgeFaces;
    case ElementFamily::Pyramid:
        return kPyramidFaces;
    default:
        return {};
    }
}

MeshSurface extractSurface(const Mesh& mesh)
{
    MeshSurface surface;

    // Faces of 2D elements and records of every face of every 3D element.
    std::vector<FaceRecord> records;
    for (const ElementBlock& block : mesh.blocks()) {
        const ElementTypeInfo& info = elementTypeInfo(block.type);
        const int count = block.elementCount();
        if (info.dimension == 2) {
            const LocalFace local{.vertices = {0, 1, 2, 3},
                                  .vertexCount = static_cast<std::uint8_t>(info.vertexCount)};
            for (int i = 0; i < count; ++i) {
                surface.faces2d.push_back(orientedFace(mesh, block.firstElement + i, local));
            }
        } else if (info.dimension == 3) {
            const auto faces = elementFaces(info.family);
            records.reserve(records.size() + faces.size() * static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i) {
                const ElementIndex element = block.firstElement + i;
                const auto nodes = mesh.elementNodes(element);
                for (std::size_t f = 0; f < faces.size(); ++f) {
                    FaceRecord record;
                    record.key.fill(kNoNode);
                    for (std::size_t v = 0; v < faces[f].vertexCount; ++v) {
                        record.key.at(v) = nodes[faces[f].vertices.at(v)];
                    }
                    std::ranges::sort(record.key);
                    record.element = element;
                    record.localFace = static_cast<std::uint8_t>(f);
                    records.push_back(record);
                }
            }
        }
    }

    // A face seen once belongs to the outer surface.
    std::ranges::sort(records, {}, &FaceRecord::key);
    for (std::size_t i = 0; i < records.size();) {
        std::size_t j = i + 1;
        while (j < records.size() && records[j].key == records[i].key) {
            ++j;
        }
        if (j - i == 1) {
            const FaceRecord& record = records[i];
            const ElementTypeInfo& info = elementTypeInfo(mesh.elementType(record.element));
            surface.faces3d.push_back(
                orientedFace(mesh, record.element, elementFaces(info.family)[record.localFace]));
        }
        i = j;
    }
    records.clear();
    records.shrink_to_fit();

    surface.faces2d.shrink_to_fit();
    surface.faces3d.shrink_to_fit();
    surface.edges2d = uniqueEdges(surface.faces2d);
    surface.edges3d = uniqueEdges(surface.faces3d);
    return surface;
}

} // namespace fem::core
