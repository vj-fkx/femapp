#pragma once

#include <fem/core/detail/id_ranges.hpp>
#include <fem/core/element_type.hpp>

#include <Eigen/Core>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fem::core {

using NodeIndex = std::int32_t;    // internal, contiguous from 0
using ElementIndex = std::int32_t; // internal, contiguous from 0
using ExternalId = std::int64_t;   // number in the source file

struct BoundingBox
{
    Eigen::Vector3d min = Eigen::Vector3d::Zero();
    Eigen::Vector3d max = Eigen::Vector3d::Zero();
    bool empty = true; // mesh without nodes

    [[nodiscard]] bool isEmpty() const noexcept { return empty; }
};

// Named set of elements of one dimension, e.g. a Gmsh physical group.
struct Group
{
    std::string name;
    int dimension = 0;
    std::vector<ElementIndex> elements; // sorted, unique
};

// All elements of one type, stored contiguously.
struct ElementBlock
{
    ElementType type = ElementType::Point1;
    ElementIndex firstElement = 0;       // global index of the first element
    std::vector<NodeIndex> connectivity; // nodeCount entries per element

    [[nodiscard]] int elementCount() const noexcept;
};

class MeshBuilder;

// Nodes, elements of any of the supported types and named groups
// (REQ-003-04 to 07, 11 to 13). Immutable; built by MeshBuilder.
class Mesh
{
public:
    Mesh() = default;

    [[nodiscard]] int nodeCount() const noexcept;
    [[nodiscard]] int elementCount() const noexcept;
    [[nodiscard]] int elementCount(ElementType type) const noexcept;
    // Highest element dimension; 0 when there are no elements.
    [[nodiscard]] int dimension() const noexcept;

    [[nodiscard]] Eigen::Vector3d nodeCoordinates(NodeIndex node) const;
    [[nodiscard]] ExternalId nodeId(NodeIndex node) const;
    [[nodiscard]] std::optional<NodeIndex> findNode(ExternalId id) const;

    [[nodiscard]] ElementType elementType(ElementIndex element) const;
    [[nodiscard]] std::span<const NodeIndex> elementNodes(ElementIndex element) const;
    [[nodiscard]] ExternalId elementId(ElementIndex element) const;
    [[nodiscard]] std::optional<ElementIndex> findElement(ExternalId id) const;

    // One block per type present, in ElementType order; the global element
    // index follows this order.
    [[nodiscard]] std::span<const ElementBlock> blocks() const noexcept { return m_blocks; }

    [[nodiscard]] BoundingBox boundingBox() const;
    // True when every node has Z = 0 within a tolerance relative to the size of
    // the mesh. Areas are signed only in that case (see elementMeasure).
    [[nodiscard]] bool liesInXYPlane() const noexcept { return m_liesInXYPlane; }

    [[nodiscard]] std::span<const Group> groups() const noexcept { return m_groups; }
    [[nodiscard]] const Group* findGroup(std::string_view name, int dimension) const;
    [[nodiscard]] std::vector<const Group*> findGroups(std::string_view name) const;
    // Nodes used by the elements of the group: sorted, unique.
    [[nodiscard]] std::vector<NodeIndex> groupNodes(const Group& group) const;

    // Bytes owned by the mesh (capacities of its containers). NFR-003-01.
    [[nodiscard]] std::size_t memoryUsage() const noexcept;

private:
    friend class MeshBuilder;

    [[nodiscard]] const ElementBlock& blockOf(ElementIndex element) const;

    std::vector<double> m_x;
    std::vector<double> m_y;
    std::vector<double> m_z;
    detail::IdRanges m_nodeIds;
    std::vector<ElementBlock> m_blocks;
    detail::IdRanges m_elementIds;
    std::vector<Group> m_groups;
    int m_elementCount = 0;
    bool m_liesInXYPlane = true;
};

} // namespace fem::core
