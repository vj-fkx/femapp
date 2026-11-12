#include <fem/core/mesh_builder.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace fem::core {

namespace {

// Relative tolerance for "the mesh lies in the XY plane".
constexpr double kPlanarTolerance = 1e-12;

std::size_t indexOf(ElementType type)
{
    return static_cast<std::size_t>(type);
}

std::string describe(ExternalId id, ElementType type)
{
    return "Element " + std::to_string(id) + " (" + std::string(elementTypeInfo(type).name) + ")";
}

bool liesInXYPlane(const Mesh& mesh)
{
    const BoundingBox box = mesh.boundingBox();
    if (box.isEmpty()) {
        return true;
    }
    const double size = (box.max - box.min).norm();
    const double limit = kPlanarTolerance * std::max(size, 1.0);
    return std::abs(box.min.z()) <= limit && std::abs(box.max.z()) <= limit;
}

} // namespace

void MeshBuilder::reserveNodes(std::size_t count)
{
    m_x.reserve(count);
    m_y.reserve(count);
    m_z.reserve(count);
}

void MeshBuilder::reserveElements(ElementType type, std::size_t count)
{
    const auto nodes = static_cast<std::size_t>(elementTypeInfo(type).nodeCount);
    m_blocks[indexOf(type)].connectivity.reserve(count * nodes);
}

Expected<NodeIndex> MeshBuilder::addNode(ExternalId id, const Eigen::Vector3d& position)
{
    if (m_nodesFinished) {
        return makeError("Node " + std::to_string(id) +
                         " was added after the first element; nodes must come first.");
    }
    if (m_x.size() >= kMaxCount) {
        return makeError("The mesh has more than " + std::to_string(kMaxCount) + " nodes.");
    }
    const auto index = static_cast<NodeIndex>(m_x.size());
    m_x.push_back(position.x());
    m_y.push_back(position.y());
    m_z.push_back(position.z());
    m_nodeIds.append(id);
    return index;
}

Expected<void> MeshBuilder::finishNodes()
{
    if (m_nodesFinished) {
        return {};
    }
    m_nodesFinished = true;
    if (const auto repeated = m_nodeIds.seal()) {
        return makeError("Node id " + std::to_string(*repeated) + " is used more than once.");
    }
    return {};
}

Expected<ElementHandle> MeshBuilder::addElement(ElementType type, ExternalId id,
                                                std::span<const ExternalId> nodeIds)
{
    if (const Expected<void> finished = finishNodes(); !finished) {
        return tl::unexpected(finished.error());
    }

    const ElementTypeInfo& info = elementTypeInfo(type);
    if (nodeIds.size() != static_cast<std::size_t>(info.nodeCount)) {
        return makeError(describe(id, type) + " has " + std::to_string(nodeIds.size()) +
                         " nodes; " + std::string(info.name) + " needs " +
                         std::to_string(info.nodeCount) + ".");
    }
    if (m_elementCount >= kMaxCount) {
        return makeError("The mesh has more than " + std::to_string(kMaxCount) + " elements.");
    }

    m_scratch.clear();
    for (const ExternalId nodeId : nodeIds) {
        const std::optional<NodeIndex> node = m_nodeIds.find(nodeId);
        if (!node) {
            return makeError(describe(id, type) + " references node " + std::to_string(nodeId) +
                             ", which does not exist.");
        }
        m_scratch.push_back(*node);
    }

    PendingBlock& block = m_blocks[indexOf(type)];
    block.connectivity.insert(block.connectivity.end(), m_scratch.begin(), m_scratch.end());

    // Repeated nodes: compare after sorting a copy.
    std::ranges::sort(m_scratch);
    const auto repeated = std::ranges::adjacent_find(m_scratch);
    if (repeated != m_scratch.end()) {
        block.connectivity.resize(block.connectivity.size() - nodeIds.size());
        return makeError(describe(id, type) + " uses node " +
                         std::to_string(m_nodeIds.idAt(*repeated)) + " more than once.");
    }

    const auto local = static_cast<std::int32_t>(block.ids.size());
    block.ids.append(id);
    ++m_elementCount;
    return ElementHandle{.type = type, .local = local};
}

ExternalId MeshBuilder::pendingElementId(ElementHandle handle) const
{
    return m_blocks[indexOf(handle.type)].ids.idAt(handle.local);
}

Expected<void> MeshBuilder::addGroup(std::string name, int dimension,
                                     std::vector<ElementHandle> elements)
{
    const std::string label = "Group '" + name + "' (" + std::to_string(dimension) + "D)";
    if (dimension < 0 || dimension > 3) {
        return makeError(label + " has an invalid dimension.");
    }
    const bool duplicate = std::ranges::any_of(m_groups, [&](const PendingGroup& g) {
        return g.dimension == dimension && g.name == name;
    });
    if (duplicate) {
        return makeError("There is already a " + std::to_string(dimension) + "D group named '" +
                         name + "'.");
    }
    for (const ElementHandle handle : elements) {
        const PendingBlock& block = m_blocks[indexOf(handle.type)];
        if (handle.local < 0 || static_cast<std::size_t>(handle.local) >= block.ids.size()) {
            return makeError(label + " contains an element that does not exist.");
        }
        const int elementDimension = elementTypeInfo(handle.type).dimension;
        if (elementDimension != dimension) {
            return makeError(label + " contains element " +
                             std::to_string(pendingElementId(handle)) + ", which is " +
                             std::to_string(elementDimension) + "D.");
        }
    }
    m_groups.push_back(PendingGroup{
        .name = std::move(name), .dimension = dimension, .elements = std::move(elements)});
    return {};
}

Expected<Mesh> MeshBuilder::build()
{
    MeshBuilder source = std::move(*this);
    *this = MeshBuilder();

    if (const Expected<void> finished = source.finishNodes(); !finished) {
        return tl::unexpected(finished.error());
    }

    Mesh mesh;
    mesh.m_x = std::move(source.m_x);
    mesh.m_y = std::move(source.m_y);
    mesh.m_z = std::move(source.m_z);
    mesh.m_x.shrink_to_fit();
    mesh.m_y.shrink_to_fit();
    mesh.m_z.shrink_to_fit();
    mesh.m_nodeIds = std::move(source.m_nodeIds);
    mesh.m_nodeIds.shrinkToFit();

    // Blocks in ElementType order; the global element index follows it.
    std::array<ElementIndex, kElementTypeCount> firstElement{};
    ElementIndex next = 0;
    for (const ElementType type : allElementTypes()) {
        PendingBlock& pending = source.m_blocks[indexOf(type)];
        firstElement[indexOf(type)] = next;
        if (pending.ids.size() == 0) {
            continue;
        }
        mesh.m_elementIds.appendAll(pending.ids);
        ElementBlock block;
        block.type = type;
        block.firstElement = next;
        block.connectivity = std::move(pending.connectivity);
        block.connectivity.shrink_to_fit();
        next += static_cast<ElementIndex>(pending.ids.size());
        mesh.m_blocks.push_back(std::move(block));
    }
    mesh.m_blocks.shrink_to_fit();
    mesh.m_elementCount = next;
    if (const auto repeated = mesh.m_elementIds.seal()) {
        return makeError("Element id " + std::to_string(*repeated) + " is used more than once.");
    }
    mesh.m_elementIds.shrinkToFit();

    for (PendingGroup& pending : source.m_groups) {
        Group group;
        group.name = std::move(pending.name);
        group.dimension = pending.dimension;
        group.elements.reserve(pending.elements.size());
        for (const ElementHandle handle : pending.elements) {
            group.elements.push_back(firstElement[indexOf(handle.type)] + handle.local);
        }
        std::ranges::sort(group.elements);
        const auto duplicates = std::ranges::unique(group.elements);
        group.elements.erase(duplicates.begin(), duplicates.end());
        group.elements.shrink_to_fit();
        mesh.m_groups.push_back(std::move(group));
    }

    mesh.m_liesInXYPlane = liesInXYPlane(mesh);
    return mesh;
}

} // namespace fem::core
