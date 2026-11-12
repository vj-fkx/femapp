#include <fem/render/mesh_scene.hpp>

#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <utility>

namespace fem::render {

namespace {

constexpr Rgba rgb(std::uint32_t r, std::uint32_t g, std::uint32_t b) noexcept
{
    return r | (g << 8U) | (b << 16U);
}

// Design section 5.2.
constexpr std::array<Rgba, 4> kDimensionColors = {
    rgb(20, 20, 20),    // 0D
    rgb(40, 90, 200),   // 1D
    rgb(60, 170, 160),  // 2D
    rgb(150, 165, 185), // 3D
};

// Twelve well separated colors, repeated after the twelfth group.
constexpr std::array<Rgba, 12> kGroupPalette = {
    rgb(31, 119, 180),  rgb(255, 127, 14),  rgb(44, 160, 44),   rgb(214, 39, 40),
    rgb(148, 103, 189), rgb(140, 86, 75),   rgb(227, 119, 194), rgb(188, 189, 34),
    rgb(23, 190, 207),  rgb(255, 187, 120), rgb(152, 223, 138), rgb(197, 176, 213),
};

void sortUnique(std::vector<core::NodeIndex>& nodes)
{
    std::ranges::sort(nodes);
    const auto duplicates = std::ranges::unique(nodes);
    nodes.erase(duplicates.begin(), duplicates.end());
    nodes.shrink_to_fit();
}

// Local nodes (vertices and higher order nodes) lying on each local face of a
// solid type, found in the reference element: a node is on a face when its
// reference coordinates are in the plane of the face vertices (design §13).
class FaceNodeTable
{
public:
    const std::vector<int>& nodesOn(core::ElementType type, std::size_t localFace)
    {
        const auto key = std::make_pair(static_cast<int>(type), localFace);
        const auto found = m_cache.find(key);
        if (found != m_cache.end()) {
            return found->second;
        }
        const core::ElementTypeInfo& info = core::elementTypeInfo(type);
        const core::LocalFace& face = core::elementFaces(info.family)[localFace];
        const auto reference = core::referenceNodeCoordinates(type);
        const auto point = [&](std::size_t node) {
            const std::array<double, 3>& p = reference[node];
            return Eigen::Vector3d(p[0], p[1], p[2]);
        };
        const Eigen::Vector3d a = point(face.vertices[0]);
        const Eigen::Vector3d normal =
            (point(face.vertices[1]) - a).cross(point(face.vertices[2]) - a);
        constexpr double kTolerance = 1e-9;
        std::vector<int> nodes;
        for (std::size_t j = 0; j < reference.size(); ++j) {
            if (std::abs(normal.dot(point(j) - a)) <= kTolerance * normal.norm()) {
                nodes.push_back(static_cast<int>(j));
            }
        }
        return m_cache.emplace(key, std::move(nodes)).first->second;
    }

private:
    std::map<std::pair<int, std::size_t>, std::vector<int>> m_cache;
};

// Local index of the face of `element` whose vertices are `face`, or -1.
int localFaceOf(const core::Mesh& mesh, const core::SurfaceFace& face)
{
    const core::ElementTypeInfo& info = core::elementTypeInfo(mesh.elementType(face.element));
    const auto nodes = mesh.elementNodes(face.element);
    std::array<int, 4> local{-1, -1, -1, -1};
    for (std::size_t v = 0; v < face.nodeCount; ++v) {
        for (int j = 0; j < info.vertexCount; ++j) {
            if (nodes[static_cast<std::size_t>(j)] == face.nodes.at(v)) {
                local.at(v) = j;
            }
        }
    }
    std::sort(local.begin(), local.begin() + face.nodeCount);
    const auto faces = core::elementFaces(info.family);
    for (std::size_t f = 0; f < faces.size(); ++f) {
        if (faces[f].vertexCount != face.nodeCount) {
            continue;
        }
        std::array<int, 4> vertices{-1, -1, -1, -1};
        for (std::size_t v = 0; v < faces[f].vertexCount; ++v) {
            vertices.at(v) = faces[f].vertices.at(v);
        }
        std::sort(vertices.begin(), vertices.begin() + face.nodeCount);
        if (vertices == local) {
            return static_cast<int>(f);
        }
    }
    return -1;
}

} // namespace

Rgba dimensionColor(int dimension) noexcept
{
    return kDimensionColors[static_cast<std::size_t>(std::clamp(dimension, 0, 3))];
}

Rgba groupColor(std::size_t groupIndex) noexcept
{
    return kGroupPalette[groupIndex % kGroupPalette.size()];
}

Range MeshScene::triangles(int dimension) const noexcept
{
    return dimension == 2 ? m_triangleRanges[0] : dimension == 3 ? m_triangleRanges[1] : Range{};
}

Range MeshScene::lines(LineSet set) const noexcept
{
    return m_lineRanges[static_cast<std::size_t>(set)];
}

int MeshScene::edgeCount() const noexcept
{
    return static_cast<int>(m_surface.edges2d.size() + m_surface.edges3d.size());
}

const core::SurfaceEdge& MeshScene::edge(int index) const
{
    const auto i = static_cast<std::size_t>(index);
    const std::size_t flat = m_surface.edges2d.size();
    return i < flat ? m_surface.edges2d.at(i) : m_surface.edges3d.at(i - flat);
}

int MeshScene::edgeDimension(int index) const noexcept
{
    return static_cast<std::size_t>(index) < m_surface.edges2d.size() ? 2 : 3;
}

std::span<const core::NodeIndex> MeshScene::surfaceNodes(int dimension) const noexcept
{
    if (dimension < 0 || dimension > 3) {
        return {};
    }
    return m_surfaceNodes[static_cast<std::size_t>(dimension)];
}

std::span<const core::NodeIndex> MeshScene::allNodes(int dimension) const noexcept
{
    if (dimension < 0 || dimension > 3) {
        return {};
    }
    return m_allNodes[static_cast<std::size_t>(dimension)];
}

std::vector<Rgba> MeshScene::elementColors(ColorMode mode) const
{
    const core::Mesh& mesh = *m_mesh;
    std::vector<Rgba> colors(static_cast<std::size_t>(mesh.elementCount()), kNoGroupColor);
    if (mode == ColorMode::ByDimension) {
        for (const core::ElementBlock& block : mesh.blocks()) {
            const Rgba color = dimensionColor(core::elementTypeInfo(block.type).dimension);
            const auto first = static_cast<std::size_t>(block.firstElement);
            std::fill_n(colors.begin() + static_cast<std::ptrdiff_t>(first), block.elementCount(),
                        color);
        }
        return colors;
    }

    // REQ-004-15: the first group (in mesh order) that contains the element.
    std::vector<bool> assigned(colors.size(), false);
    const auto groups = mesh.groups();
    for (std::size_t g = 0; g < groups.size(); ++g) {
        for (const core::ElementIndex element : groups[g].elements) {
            const auto index = static_cast<std::size_t>(element);
            if (!assigned[index]) {
                assigned[index] = true;
                colors[index] = groupColor(g);
            }
        }
    }
    return colors;
}

std::shared_ptr<const MeshScene> buildMeshScene(std::shared_ptr<const core::Mesh> mesh)
{
    // NOLINTNEXTLINE(modernize-make-shared): the constructor is private.
    auto scene = std::shared_ptr<MeshScene>(new MeshScene());
    scene->m_mesh = std::move(mesh);
    const core::Mesh& m = *scene->m_mesh;

    scene->m_surface = core::extractSurface(m);
    scene->m_bounds = m.boundingBox();
    if (!scene->m_bounds.isEmpty()) {
        scene->m_origin = 0.5 * (scene->m_bounds.min + scene->m_bounds.max);
    }

    // Node positions relative to the centre, in float (REQ-004-18).
    scene->m_nodePositions.reserve(static_cast<std::size_t>(m.nodeCount()));
    for (core::NodeIndex n = 0; n < m.nodeCount(); ++n) {
        scene->m_nodePositions.emplace_back((m.nodeCoordinates(n) - scene->m_origin).cast<float>());
    }
    const auto& positions = scene->m_nodePositions;
    const auto at = [&](core::NodeIndex node) { return positions[static_cast<std::size_t>(node)]; };

    // Triangles: 2D faces, then 3D outer faces.
    const auto addFaces = [&](const std::vector<core::SurfaceFace>& faces) {
        const auto first = static_cast<std::uint32_t>(scene->m_triangleElements.size());
        for (std::size_t f = 0; f < faces.size(); ++f) {
            const core::SurfaceFace& face = faces[f];
            const auto triangle = [&](std::size_t a, std::size_t b, std::size_t c) {
                scene->m_triangleVertices.push_back(at(face.nodes.at(a)));
                scene->m_triangleVertices.push_back(at(face.nodes.at(b)));
                scene->m_triangleVertices.push_back(at(face.nodes.at(c)));
                scene->m_triangleElements.push_back(face.element);
                scene->m_triangleFaces.push_back(static_cast<std::uint32_t>(f));
            };
            triangle(0, 1, 2);
            if (face.nodeCount == 4) {
                triangle(0, 2, 3);
            }
        }
        return Range{.first = first,
                     .count = static_cast<std::uint32_t>(scene->m_triangleElements.size()) - first};
    };
    scene->m_triangleRanges[0] = addFaces(scene->m_surface.faces2d);
    scene->m_triangleRanges[1] = addFaces(scene->m_surface.faces3d);

    // Segments: 1D elements (by their vertices), 2D edges, 3D edges.
    const auto addSegment = [&](core::NodeIndex a, core::NodeIndex b, core::ElementIndex element) {
        scene->m_lineVertices.push_back(at(a));
        scene->m_lineVertices.push_back(at(b));
        scene->m_lineElements.push_back(element);
    };
    const auto segmentCount = [&] {
        return static_cast<std::uint32_t>(scene->m_lineElements.size());
    };

    std::uint32_t first = segmentCount();
    for (const core::ElementBlock& block : m.blocks()) {
        const core::ElementTypeInfo& info = core::elementTypeInfo(block.type);
        for (int i = 0; i < block.elementCount(); ++i) {
            const core::ElementIndex element = block.firstElement + i;
            const auto nodes = m.elementNodes(element);
            auto& all = scene->m_allNodes[static_cast<std::size_t>(info.dimension)];
            all.insert(all.end(), nodes.begin(), nodes.end());
            if (info.dimension == 1) {
                addSegment(nodes[0], nodes[1], element);
            } else if (info.dimension == 0) {
                scene->m_pointElements.push_back(element);
            }
        }
    }
    scene->m_lineRanges[0] = {.first = first, .count = segmentCount() - first};

    first = segmentCount();
    for (const core::SurfaceEdge& edge : scene->m_surface.edges2d) {
        addSegment(edge.a, edge.b, edge.element);
    }
    scene->m_lineRanges[1] = {.first = first, .count = segmentCount() - first};

    first = segmentCount();
    for (const core::SurfaceEdge& edge : scene->m_surface.edges3d) {
        addSegment(edge.a, edge.b, edge.element);
    }
    scene->m_lineRanges[2] = {.first = first, .count = segmentCount() - first};

    // REQ-004-34: the nodes on the visible surface, higher order ones
    // included. Points, lines and 2D elements are drawn whole; of the solids,
    // the nodes lying on their outer faces.
    FaceNodeTable faceNodes;
    auto& solidSurface = scene->m_surfaceNodes[3];
    for (const core::SurfaceFace& face : scene->m_surface.faces3d) {
        solidSurface.insert(solidSurface.end(), face.nodes.begin(),
                            face.nodes.begin() + face.nodeCount);
        const int localFace = localFaceOf(m, face);
        if (localFace < 0) {
            continue;
        }
        const auto nodes = m.elementNodes(face.element);
        for (const int j :
             faceNodes.nodesOn(m.elementType(face.element), static_cast<std::size_t>(localFace))) {
            solidSurface.push_back(nodes[static_cast<std::size_t>(j)]);
        }
    }
    for (std::size_t d = 0; d < 4; ++d) {
        sortUnique(scene->m_allNodes.at(d));
    }
    for (std::size_t d = 0; d < 3; ++d) {
        scene->m_surfaceNodes.at(d) = scene->m_allNodes.at(d);
    }
    sortUnique(solidSurface);

    scene->m_triangleVertices.shrink_to_fit();
    scene->m_triangleElements.shrink_to_fit();
    scene->m_triangleFaces.shrink_to_fit();
    scene->m_lineVertices.shrink_to_fit();
    scene->m_lineElements.shrink_to_fit();
    scene->m_pointElements.shrink_to_fit();
    return scene;
}

} // namespace fem::render
