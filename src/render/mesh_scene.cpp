#include <fem/render/mesh_scene.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
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
        for (const core::SurfaceFace& face : faces) {
            const auto triangle = [&](std::size_t a, std::size_t b, std::size_t c) {
                scene->m_triangleVertices.push_back(at(face.nodes.at(a)));
                scene->m_triangleVertices.push_back(at(face.nodes.at(b)));
                scene->m_triangleVertices.push_back(at(face.nodes.at(c)));
                scene->m_triangleElements.push_back(face.element);
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
                scene->m_surfaceNodes[1].push_back(nodes[0]);
                scene->m_surfaceNodes[1].push_back(nodes[1]);
            } else if (info.dimension == 0) {
                scene->m_pointElements.push_back(element);
                scene->m_surfaceNodes[0].push_back(nodes[0]);
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

    for (const core::SurfaceFace& face : scene->m_surface.faces2d) {
        scene->m_surfaceNodes[2].insert(scene->m_surfaceNodes[2].end(), face.nodes.begin(),
                                        face.nodes.begin() + face.nodeCount);
    }
    for (const core::SurfaceFace& face : scene->m_surface.faces3d) {
        scene->m_surfaceNodes[3].insert(scene->m_surfaceNodes[3].end(), face.nodes.begin(),
                                        face.nodes.begin() + face.nodeCount);
    }
    for (std::size_t d = 0; d < 4; ++d) {
        sortUnique(scene->m_surfaceNodes.at(d));
        sortUnique(scene->m_allNodes.at(d));
    }

    scene->m_triangleVertices.shrink_to_fit();
    scene->m_triangleElements.shrink_to_fit();
    scene->m_lineVertices.shrink_to_fit();
    scene->m_lineElements.shrink_to_fit();
    scene->m_pointElements.shrink_to_fit();
    return scene;
}

} // namespace fem::render
