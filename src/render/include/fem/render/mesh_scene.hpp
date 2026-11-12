#pragma once

#include <fem/core/mesh.hpp>
#include <fem/core/mesh_surface.hpp>
#include <fem/render/display_options.hpp>

#include <Eigen/Core>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace fem::render {

// Contiguous primitives (triangles or segments) in a vertex stream.
struct Range
{
    std::uint32_t first = 0; // first primitive
    std::uint32_t count = 0; // number of primitives
};

enum class LineSet : std::uint8_t { Elements1d, Edges2d, Edges3d };

// RGBA8 color packed as 0xAABBGGRR, so that the bytes in memory are R, G, B, A.
using Rgba = std::uint32_t;

// Everything needed to draw and pick a mesh, built once per import and
// immutable afterwards. It does not touch the GPU, so it can be built on a
// worker thread and shared (REQ-004-04).
class MeshScene
{
public:
    [[nodiscard]] const core::Mesh& mesh() const noexcept { return *m_mesh; }
    [[nodiscard]] const core::MeshSurface& surface() const noexcept { return m_surface; }

    // Positions are stored relative to this point, the centre of the mesh,
    // in float (REQ-004-18).
    [[nodiscard]] const Eigen::Vector3d& origin() const noexcept { return m_origin; }
    [[nodiscard]] const core::BoundingBox& bounds() const noexcept { return m_bounds; }
    [[nodiscard]] std::span<const Eigen::Vector3f> nodePositions() const noexcept
    {
        return m_nodePositions;
    }

    // Triangles: 3 vertices each; faces of 2D elements first, then the outer
    // faces of 3D elements. Quadrilaterals are split along their 0-2 diagonal.
    [[nodiscard]] std::span<const Eigen::Vector3f> triangleVertices() const noexcept
    {
        return m_triangleVertices;
    }
    [[nodiscard]] std::span<const core::ElementIndex> triangleElements() const noexcept
    {
        return m_triangleElements;
    }
    // REQ-004-32: per triangle, the index of its face in surface().faces2d
    // (triangles(2)) or surface().faces3d (triangles(3)). Likewise, segment s
    // of lines(Edges3d) is surface().edges3d[s - lines(Edges3d).first].
    [[nodiscard]] std::span<const std::uint32_t> triangleFaces() const noexcept
    {
        return m_triangleFaces;
    }
    // `dimension` is 2 or 3.
    [[nodiscard]] Range triangles(int dimension) const noexcept;

    // Segments: 2 vertices each; 1D elements, then edges of 2D faces, then
    // edges of 3D outer faces.
    [[nodiscard]] std::span<const Eigen::Vector3f> lineVertices() const noexcept
    {
        return m_lineVertices;
    }
    [[nodiscard]] std::span<const core::ElementIndex> lineElements() const noexcept
    {
        return m_lineElements;
    }
    [[nodiscard]] Range lines(LineSet set) const noexcept;

    // 0D elements and their nodes.
    [[nodiscard]] std::span<const core::ElementIndex> pointElements() const noexcept
    {
        return m_pointElements;
    }

    // Nodes that can be clicked (drawn on the visible surface) and nodes that
    // a rectangle can reach (all nodes of the elements), per dimension.
    [[nodiscard]] std::span<const core::NodeIndex> surfaceNodes(int dimension) const noexcept;
    [[nodiscard]] std::span<const core::NodeIndex> allNodes(int dimension) const noexcept;

    // Color of every element for a color mode, alpha 0 (REQ-004-14, 15).
    [[nodiscard]] std::vector<Rgba> elementColors(ColorMode mode) const;

private:
    friend std::shared_ptr<const MeshScene> buildMeshScene(std::shared_ptr<const core::Mesh> mesh);

    std::shared_ptr<const core::Mesh> m_mesh;
    core::MeshSurface m_surface;
    Eigen::Vector3d m_origin = Eigen::Vector3d::Zero();
    core::BoundingBox m_bounds;
    std::vector<Eigen::Vector3f> m_nodePositions;
    std::vector<Eigen::Vector3f> m_triangleVertices;
    std::vector<core::ElementIndex> m_triangleElements;
    std::vector<std::uint32_t> m_triangleFaces;
    std::array<Range, 2> m_triangleRanges{};
    std::vector<Eigen::Vector3f> m_lineVertices;
    std::vector<core::ElementIndex> m_lineElements;
    std::array<Range, 3> m_lineRanges{};
    std::vector<core::ElementIndex> m_pointElements;
    std::array<std::vector<core::NodeIndex>, 4> m_surfaceNodes;
    std::array<std::vector<core::NodeIndex>, 4> m_allNodes;
};

// Builds the scene of a mesh (REQ-004-09, 10, 16, 18).
[[nodiscard]] std::shared_ptr<const MeshScene>
buildMeshScene(std::shared_ptr<const core::Mesh> mesh);

// Colors used by elementColors (exposed for tests and the legend of later
// features).
[[nodiscard]] Rgba dimensionColor(int dimension) noexcept;
[[nodiscard]] Rgba groupColor(std::size_t groupIndex) noexcept;
inline constexpr Rgba kNoGroupColor = 0x00C8C8C8U;

} // namespace fem::render
