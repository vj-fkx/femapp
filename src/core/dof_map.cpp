#include <fem/core/dof_map.hpp>
#include <fem/core/element_formulation.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <string>

namespace fem::core {

std::string_view dofName(DofType type) noexcept
{
    constexpr std::array<std::string_view, kDofTypeCount> kNames = {"u",  "v",  "w", "rx",
                                                                    "ry", "rz", "T", "warp"};
    return kNames.at(static_cast<std::size_t>(type));
}

DofMap DofMap::build(const Mesh& mesh, std::span<const ElementSet> sets)
{
    DofMap map;
    map.m_mesh = &mesh;
    const auto nodes = static_cast<std::size_t>(mesh.nodeCount());
    map.m_masks.assign(nodes, 0);
    for (const ElementSet& set : sets) {
        for (const ElementIndex element : set.elements) {
            const ElementType type = mesh.elementType(element);
            const auto local = mesh.elementNodes(element);
            for (std::size_t k = 0; k < local.size(); ++k) {
                map.m_masks[static_cast<std::size_t>(local[k])] |=
                    set.formulation->dofsAt(type, static_cast<int>(k));
            }
        }
    }

    // REQ-006-09: node by node, and in DofType order within a node.
    map.m_first.resize(nodes + 1);
    DofIndex next = 0;
    for (std::size_t n = 0; n < nodes; ++n) {
        map.m_first[n] = next;
        for (int t = 0; t < kDofTypeCount; ++t) {
            if ((map.m_masks[n] & mask(static_cast<DofType>(t))) != 0) {
                map.m_dofNode.push_back(static_cast<NodeIndex>(n));
                map.m_dofType.push_back(static_cast<DofType>(t));
                ++next;
            }
        }
    }
    map.m_first[nodes] = next;
    map.m_fixed.assign(static_cast<std::size_t>(next), 0);
    map.m_values.assign(static_cast<std::size_t>(next), 0.0);
    return map;
}

DofMask DofMap::maskOf(NodeIndex node) const
{
    return m_masks.at(static_cast<std::size_t>(node));
}

DofIndex DofMap::firstDof(NodeIndex node) const
{
    return m_first.at(static_cast<std::size_t>(node));
}

std::optional<DofIndex> DofMap::find(NodeIndex node, DofType type) const
{
    if (node < 0 || static_cast<std::size_t>(node) >= m_masks.size()) {
        return std::nullopt;
    }
    const DofMask nodeMask = m_masks[static_cast<std::size_t>(node)];
    const DofMask bit = mask(type);
    if ((nodeMask & bit) == 0) {
        return std::nullopt;
    }
    const auto before = static_cast<DofIndex>(
        std::popcount(static_cast<unsigned>(nodeMask & static_cast<DofMask>(bit - 1U))));
    return m_first[static_cast<std::size_t>(node)] + before;
}

NodeIndex DofMap::nodeOf(DofIndex dof) const
{
    return m_dofNode.at(static_cast<std::size_t>(dof));
}

DofType DofMap::typeOf(DofIndex dof) const
{
    return m_dofType.at(static_cast<std::size_t>(dof));
}

Expected<void> DofMap::prescribe(NodeIndex node, DofType type, double value)
{
    const auto dof = find(node, type);
    if (!dof) {
        const std::string id = m_mesh != nullptr && node >= 0 && node < m_mesh->nodeCount()
                                   ? std::to_string(m_mesh->nodeId(node))
                                   : std::to_string(node);
        return makeError("Node " + id + " has no dof " + std::string(dofName(type)) + ".");
    }
    const auto index = static_cast<std::size_t>(*dof);
    m_fixed[index] = 1;
    m_values[index] = value;
    m_numbered = false;
    return {};
}

bool DofMap::isPrescribed(DofIndex dof) const
{
    return m_fixed.at(static_cast<std::size_t>(dof)) != 0;
}

double DofMap::prescribedValue(DofIndex dof) const
{
    return m_values.at(static_cast<std::size_t>(dof));
}

std::int32_t DofMap::equation(DofIndex dof) const
{
    numberEquations();
    return m_equation.at(static_cast<std::size_t>(dof));
}

std::int32_t DofMap::freeCount() const
{
    numberEquations();
    return m_freeCount;
}

void DofMap::numberEquations() const
{
    if (m_numbered) {
        return;
    }
    m_numbered = true;
    m_equation.resize(m_fixed.size());
    std::int32_t next = 0;
    for (std::size_t d = 0; d < m_fixed.size(); ++d) {
        m_equation[d] = m_fixed[d] != 0 ? -1 : next++;
    }
    m_freeCount = next;
}

} // namespace fem::core
