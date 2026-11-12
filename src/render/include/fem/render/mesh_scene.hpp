#pragma once

#include <fem/core/mesh.hpp>
#include <fem/core/mesh_surface.hpp>
#include <fem/render/camera.hpp>
#include <fem/render/display_options.hpp>

#include <Eigen/Core>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
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
    [[nodiscard]] const std::shared_ptr<const core::Mesh>& sharedMesh() const noexcept
    {
        return m_mesh;
    }
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

    // REQ-004-32: selectable edges, those of the 2D faces (surface().edges2d)
    // then those of the outer solid faces (surface().edges3d), in the order
    // of the segments of lines(Edges2d) and lines(Edges3d), which follow each
    // other: edge i is segment lines(Edges2d).first + i.
    [[nodiscard]] int edgeCount() const noexcept;
    [[nodiscard]] const core::SurfaceEdge& edge(int index) const;
    [[nodiscard]] int edgeDimension(int index) const noexcept; // 2 or 3

    // Nodes shown and clickable on the visible surface, higher order ones
    // included (REQ-004-34), and nodes that a rectangle can reach (all nodes
    // of the elements), per dimension.
    [[nodiscard]] std::span<const core::NodeIndex> surfaceNodes(int dimension) const noexcept;
    [[nodiscard]] std::span<const core::NodeIndex> allNodes(int dimension) const noexcept;

    // Color of every element for a color mode, alpha 0 (REQ-004-14, 15).
    [[nodiscard]] std::vector<Rgba> elementColors(ColorMode mode) const;

private:
    friend std::shared_ptr<const MeshScene> buildMeshScene(std::shared_ptr<const core::Mesh> mesh,
                                                           const std::vector<bool>* include);

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
// With `include` (one flag per element), only the included elements are
// drawn, as a part of their own (REQ-004-37); indices stay those of the mesh.
[[nodiscard]] std::shared_ptr<const MeshScene>
buildMeshScene(std::shared_ptr<const core::Mesh> mesh, const std::vector<bool>* include = nullptr);

// REQ-004-36: the standard view looking straight at a mesh without solids
// that lies in a coordinate plane (Top for XY, Front for XZ, Right for YZ);
// nothing for other meshes.
[[nodiscard]] std::optional<StandardView> planarView(const MeshScene& scene);

// REQ-004-36: the view a freshly imported mesh opens in: planarView, else
// the isometric view.
[[nodiscard]] StandardView initialView(const MeshScene& scene);

// Colors used by elementColors (exposed for tests and the legend of later
// features).
[[nodiscard]] Rgba dimensionColor(int dimension) noexcept;
[[nodiscard]] Rgba groupColor(std::size_t groupIndex) noexcept;
inline constexpr Rgba kNoGroupColor = 0x00C8C8C8U;

} // namespace fem::render
