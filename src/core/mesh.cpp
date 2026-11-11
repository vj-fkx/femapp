#include <fem/core/mesh.hpp>

#include <algorithm>
#include <cassert>

namespace fem::core {

int ElementBlock::elementCount() const noexcept
{
    return static_cast<int>(connectivity.size()) / elementTypeInfo(type).nodeCount;
}

int Mesh::nodeCount() const noexcept
{
    return static_cast<int>(m_x.size());
}

int Mesh::elementCount() const noexcept
{
    return m_elementCount;
}

int Mesh::elementCount(ElementType type) const noexcept
{
    const auto it = std::find_if(m_blocks.begin(), m_blocks.end(),
                                 [type](const ElementBlock& block) { return block.type == type; });
    return it == m_blocks.end() ? 0 : it->elementCount();
}

int Mesh::dimension() const noexcept
{
    int result = 0;
    for (const ElementBlock& block : m_blocks) {
        result = std::max(result, elementTypeInfo(block.type).dimension);
    }
    return result;
}

Eigen::Vector3d Mesh::nodeCoordinates(NodeIndex node) const
{
    assert(node >= 0 && node < nodeCount());
    const auto i = static_cast<std::size_t>(node);
    return {m_x[i], m_y[i], m_z[i]};
}

ExternalId Mesh::nodeId(NodeIndex node) const
{
    assert(node >= 0 && node < nodeCount());
    return m_nodeIds.idAt(node);
}

std::optional<NodeIndex> Mesh::findNode(ExternalId id) const
{
    return m_nodeIds.find(id);
}

const ElementBlock& Mesh::blockOf(ElementIndex element) const
{
    assert(element >= 0 && element < m_elementCount);
    // Last block whose first element is <= element.
    const auto it = std::upper_bound(
        m_blocks.begin(), m_blocks.end(), element,
        [](ElementIndex value, const ElementBlock& block) { return value < block.firstElement; });
    return *(it - 1);
}

ElementType Mesh::elementType(ElementIndex element) const
{
    return blockOf(element).type;
}

std::span<const NodeIndex> Mesh::elementNodes(ElementIndex element) const
{
    const ElementBlock& block = blockOf(element);
    const auto nodeCount = static_cast<std::size_t>(elementTypeInfo(block.type).nodeCount);
    const auto local = static_cast<std::size_t>(element - block.firstElement);
    return std::span<const NodeIndex>(block.connectivity).subspan(local * nodeCount, nodeCount);
}

ExternalId Mesh::elementId(ElementIndex element) const
{
    assert(element >= 0 && element < m_elementCount);
    return m_elementIds.idAt(element);
}

std::optional<ElementIndex> Mesh::findElement(ExternalId id) const
{
    return m_elementIds.find(id);
}

BoundingBox Mesh::boundingBox() const
{
    BoundingBox box;
    if (m_x.empty()) {
        return box;
    }
    const auto [minX, maxX] = std::minmax_element(m_x.begin(), m_x.end());
    const auto [minY, maxY] = std::minmax_element(m_y.begin(), m_y.end());
    const auto [minZ, maxZ] = std::minmax_element(m_z.begin(), m_z.end());
    box.min = {*minX, *minY, *minZ};
    box.max = {*maxX, *maxY, *maxZ};
    box.empty = false;
    return box;
}

const Group* Mesh::findGroup(std::string_view name, int dimension) const
{
    const auto it = std::find_if(m_groups.begin(), m_groups.end(), [&](const Group& group) {
        return group.dimension == dimension && group.name == name;
    });
    return it == m_groups.end() ? nullptr : &*it;
}

std::vector<const Group*> Mesh::findGroups(std::string_view name) const
{
    std::vector<const Group*> result;
    for (const Group& group : m_groups) {
        if (group.name == name) {
            result.push_back(&group);
        }
    }
    return result;
}

std::vector<NodeIndex> Mesh::groupNodes(const Group& group) const
{
    std::vector<NodeIndex> nodes;
    for (const ElementIndex element : group.elements) {
        const auto elementNodes = this->elementNodes(element);
        nodes.insert(nodes.end(), elementNodes.begin(), elementNodes.end());
    }
    std::sort(nodes.begin(), nodes.end());
    nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
    return nodes;
}

std::size_t Mesh::memoryUsage() const noexcept
{
    std::size_t bytes = sizeof(Mesh);
    bytes += (m_x.capacity() + m_y.capacity() + m_z.capacity()) * sizeof(double);
    bytes += m_nodeIds.memoryUsage() + m_elementIds.memoryUsage();
    bytes += m_blocks.capacity() * sizeof(ElementBlock);
    for (const ElementBlock& block : m_blocks) {
        bytes += block.connectivity.capacity() * sizeof(NodeIndex);
    }
    bytes += m_groups.capacity() * sizeof(Group);
    for (const Group& group : m_groups) {
        bytes += group.name.capacity() + group.elements.capacity() * sizeof(ElementIndex);
    }
    return bytes;
}

} // namespace fem::core
