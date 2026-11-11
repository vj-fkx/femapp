#pragma once

#include <fem/core/detail/id_ranges.hpp>
#include <fem/core/element_type.hpp>
#include <fem/core/expected.hpp>
#include <fem/core/mesh.hpp>

#include <Eigen/Core>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fem::core {

// Element as known while building: its index inside the block of its type.
struct ElementHandle
{
    ElementType type = ElementType::Point1;
    std::int32_t local = 0;
};

// Builds a Mesh, checking every node, element and group on the way
// (REQ-003-09, 10, 14). Nodes come first: the first addElement() closes the
// node list.
class MeshBuilder
{
public:
    void reserveNodes(std::size_t count);
    void reserveElements(ElementType type, std::size_t count);

    [[nodiscard]] Expected<NodeIndex> addNode(ExternalId id, const Eigen::Vector3d& position);
    // Closes the node list and checks for repeated ids. Called by the first
    // addElement() if not called before.
    [[nodiscard]] Expected<void> finishNodes();

    [[nodiscard]] Expected<ElementHandle> addElement(ElementType type, ExternalId id,
                                                     std::span<const ExternalId> nodeIds);

    [[nodiscard]] Expected<void> addGroup(std::string name, int dimension,
                                          std::vector<ElementHandle> elements);

    // Checks repeated element ids and assembles the mesh. The builder is left
    // empty either way.
    [[nodiscard]] Expected<Mesh> build();

    static constexpr std::size_t kMaxCount = 2147483647; // 2^31 - 1

private:
    struct PendingBlock
    {
        std::vector<NodeIndex> connectivity;
        detail::IdRanges ids;
    };

    struct PendingGroup
    {
        std::string name;
        int dimension = 0;
        std::vector<ElementHandle> elements;
    };

    [[nodiscard]] ExternalId pendingElementId(ElementHandle handle) const;

    std::vector<double> m_x;
    std::vector<double> m_y;
    std::vector<double> m_z;
    detail::IdRanges m_nodeIds;
    bool m_nodesFinished = false;
    std::array<PendingBlock, kElementTypeCount> m_blocks;
    std::size_t m_elementCount = 0;
    std::vector<PendingGroup> m_groups;
    std::vector<NodeIndex> m_scratch;
};

} // namespace fem::core
